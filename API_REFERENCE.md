# WowEdit plug-in and scripting API reference

## ABI guidance

The C++ API targets C++17 and the same compiler/runtime as the host. For a
cross-compiler marketplace, wrap `IWowEditPlugin` behind a small C ABI and pass
serialized JSON/value handles rather than STL types. Do not retain model pointers
after unload, map close, or a document-generation change.

## Core commands

```cpp
class ICommand {
public:
    virtual ~ICommand() = default;
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual std::string getDescription() const = 0;
};
```

`CommandManager::executeCommand()` invokes `execute`, records history, invalidates
redo, and caps history to 1000 by default. `beginMacro(name)` / `endMacro()`
combine already-executed child commands into one reversible history entry.

Required built-ins:

- `TerrainHeightModifyCommand`, `TexturePaintCommand`, `VertexPaintCommand`
- `DoodadPlaceCommand`, `DoodadDeleteCommand`, `DoodadMoveCommand`,
  `DoodadTransformCommand`
- `CreatureSpawnerPlaceCommand`, `CreatureSpawnerModifyCommand`
- `WorldChunkCopyCommand`, `WorldChunkPasteCommand`
- `PropertyChangeCommand::Make(property, before, after, description)`

Never mutate a document from a plugin without submitting an undoable command.

## Events

Subscribe through `EventBus`:

```cpp
auto id = events.subscribe(wowedit::EventType::TerrainModified,
    [](const wowedit::Event& event) { /* update plugin UI/cache */ });
// ... events.unsubscribe(id);
```

Supported event families include `TerrainModified`, `TexturePainted`,
`WaterLevelChanged`; doodad placed/selected/moved/deleted/transformed; creature
placed/modified/waypoint added; selection; map opened/saved/closed; camera,
viewport, display; tool/brush; and undo/redo/settings events. `publish()` invokes
a handler snapshot immediately; worker threads should use `enqueue()` and let the
main/UI thread call `dispatchQueued()`.

## Plugin interface

```cpp
class IWowEditPlugin {
public:
  virtual std::string id() const = 0;
  virtual std::string displayName() const = 0;
  virtual std::string version() const = 0;
  virtual bool onLoad(PluginContext&, std::string& error) = 0;
  virtual void onUnload() = 0;
};
```

`PluginContext` has `EventBus`, `CommandManager`, `GameDatabase`, and
`ScriptEngine` pointers. Register a plugin with `PluginManager::registerPlugin`.
The dynamic entry point type is `CreateWowEditPlugin`.

### Example: log terrain edits

```cpp
class TerrainAudit final : public wowedit::IWowEditPlugin {
  wowedit::EventBus::SubscriptionId subscription_ = 0;
public:
  std::string id() const override { return "example.terrain-audit"; }
  std::string displayName() const override { return "Terrain Audit"; }
  std::string version() const override { return "1.0"; }
  bool onLoad(wowedit::PluginContext& c, std::string&) override {
    subscription_ = c.events->subscribe(wowedit::EventType::TerrainModified,
      [](const wowedit::Event&) { /* append review record */ });
    return subscription_ != 0;
  }
  void onUnload() override { /* unsubscribe before context disappears */ }
};
```

## Terrain / object API

- `Heightmap` supports direct samples, bilinear world lookup, normals, import/
  export, named brush curves, flatten target, and deterministic noise.
- `TerrainEditor` commits brushes, ramp, stamp, thermal and hydraulic erosion as
  deltas. Use it instead of bulk mutating `Heightmap` in a tool.
- `TextureSplatmap` exposes four active RGBA channels, up to 13 zone texture
  registrations, paint/fill/smear/clone/height blend/vertex tint.
- `DoodadManager` owns placement, selection, scatter, grouping, and `Octree`
  broad-phase data. `TransformGizmo` holds snap/pivot policy independent of UI.
- `CreatureSpawnerRegistry`, `WaypointSystem`, and `AiBehaviorSimulator` provide
  editable spawn, path, and preview state. Server persistence is an adapter task.

## Scripting API

`ScriptEngine` provides named JSON bindings and a tiny deterministic command
format useful in tests/automation:

```text
# one binding per line
editor.undo {}
database.find_spell {"id":133}
```

```cpp
scripts.bind("my_plugin.scatter", [](const nlohmann::json& args) {
  return nlohmann::json{{"created", args.value("count", 0)}};
});
```

Lua and Python are opt-in runtime bridges:
`setLuaExecutor()` / `setPythonExecutor()` attach a host-provided interpreter.
`executeLua()` / `executePython()` return a clear error when no plugin/runtime is
installed; this avoids silently pretending a scripting language is available.

## SQL schema conversion import

`SqlImportConverter` converts SQL against the **live target schema**, rather than
assuming source and target use the same TrinityCore/AzerothCore revision:

```cpp
we::SqlImportConverter converter;
we::SqlImportOptions options;
options.targetFlavor = we::CoreFlavor::Auto;
options.useUpsert = true;
options.includeDeletes = false; // opt in only after review

we::SqlImportPlan plan = converter.Convert(sqlText, targetDatabase, options);
if (plan.ok && !plan.HasErrors()) {
    // Show plan.PreviewSql() plus plan.issues to the user first.
    we::DbError result = converter.Apply(targetDatabase, plan);
}
```

The converter supports explicit-column `INSERT`/`REPLACE` and simple `UPDATE` SQL,
with opt-in predicate-based `DELETE`. It maps known aliases such as spawn `id ↔ id1`,
`Title ↔ LogTitle`, selected table aliases, and legacy `modelid1..4` versus
`creature_template_model` visual rows. Unsupported semantic fields are warnings and
are omitted rather than guessed. Never apply a plan with errors or hide its issues
from the operator.

## Serialization and import/export

`MapSerializer::save/load` reads `.wowedit` manifests plus binary attachments.
`FileIo` provides atomic write functions. `ModelImporter` validates `.m2`, `.obj`,
`.fbx`; `TextureImporter` inspects `.blp`, `.png`, `.tga`; `ExportManager` emits
object CSV, collision OBJ, and heightmaps. Validate all external paths and report
errors through your UI rather than ignoring failed imports.
