# TrinityCore Studio

A desktop GUI for browsing and editing World of Warcraft world data in a
**TrinityCore 3.3.5a (build 12340)** *world* database. It hosts four editors -
**Quests, Items, Creatures, and GameObjects** - selectable from a left rail, edits
each record across every related table, resolves item / creature / gameobject /
faction / spell IDs to names, and can either write changes **live** (transactional)
or **export a reviewable `.sql` file** - selectable per session.

Built with Dear ImGui (docking) + GLFW + Vulkan 1.4. Optional World of Warcraft client
data (MPQ/DBC) adds icons, zone maps, name resolution, and a Blizzard-styled theme.

## Editors

Pick an editor from the left rail. Each one shares the same shape: a searchable
**browser** on the left, a tabbed **editor** in the center, and a **validation**
panel + log across the bottom.

- **Quests** - the full quest picture across `quest_template` and every related
  table (addon, offer_reward, request_items, details, mail_sender, greeting,
  questgivers, conditions, all `*_locale`), plus an interactive **POI map** rendered
  over the real zone map (drag/add/delete points).
- **Items** - every `item_template` field across General, Flags, Requirements,
  Stats, Weapon/Armor, Spells, Sockets, Text & Set, Locales, with a live icon
  preview.
- **Creatures** - `creature_template` and its child tables (addon, movement,
  resistances, spells, equipment, locales) **plus** the systems keyed by the
  creature: **vendor** inventory, **trainer** spells, **loot** tables, and **world
  spawns** (edited in place). Faction resolves to a name via a FactionTemplate.dbc
  lookup.
- **GameObjects** - `gameobject_template` with a **type-aware Data tab**: the 24
  generic `Data0..23` columns are labeled per the selected type (a Door's `Data1` is
  a Lock id; a Chest's `Data1` is a loot id) with id-name pickers where a field
  references another table - plus addon, locales, quest items, loot, and spawns.

### Shared features

- Browse & search by ID or name, with type/quality/rank filters, server-side sort,
  and paging.
- Create (blank or from an **archetype template**), **Clone**, **Delete**, **Revert**,
  and full **Undo/Redo**.
- Live **validation** with severity levels; click an issue to jump to the field.
- Tools per editor: **Where-Used** (reverse references), **New-from-Template**, and
  column-level **Batch-Edit** across the current browser list.
- Two write modes: **Live** (direct, transactional, with a confirmation) or **SQL
  Export** (reads live, writes an ordered `.sql` you can review/commit to git).
  **Preview SQL** shows exactly what a save will run.
- Optional SOAP `.reload` after a live save (if a worldserver account is configured).
- Saved connection profiles (`config/connections.json`; passwords stored only if you
  opt in) and per-editor preferences.

### Client data (optional)

Point the app at a WoW 3.3.5a `Data` folder (MPQ archives) to unlock item/spell
**icons**, quest **POI zone maps**, name resolution for factions / spells / areas /
skills / titles / faction templates, and the **Blizzard parchment theme** + UI font.
Everything works without it - you just get IDs instead of names and the dark theme.

## Build

Requirements: **Visual Studio 2026** (MSVC v145 toolset) and **git**. All other
dependencies (Dear ImGui, GLFW, the MySQL C client, nlohmann/json, StormLib,
premake5) are **vendored** in `third_party/` and `tools/` - nothing else to install.

```powershell
.\build.ps1 Debug      # or: .\build.ps1 Release
```

This runs `tools\premake5.exe vs2026` to generate `build\TrinityCoreStudio.slnx`, then
builds it. Output: `bin\Debug\TrinityCoreStudio.exe`. You can also open
`build\TrinityCoreStudio.slnx` in Visual Studio 2026 directly.

## Run

```powershell
.\bin\Debug\TrinityCoreStudio.exe
```

In the app: open the **Connection** panel, enter your world DB host/user/password/db,
pick **Live** or **SQL Export** mode, and Connect. Choose an editor from the rail, use
the **Browser** to find a record, edit it in the tabbed editor, and **Save** (live) or
export to `.sql`. On first run you can point it at your client `Data` folder for icons,
maps, and names.

## Notes / status

- Targets the 3.3.5a build-12340 world schema shipped with this TrinityCore tree.
  Works with **MySQL 8.0** (reserved-word columns like `rank` are quoted).
- Schema-adaptive writes: only columns the live DB actually has are written, so minor
  TrinityCore schema drift is tolerated. `VerifiedBuild` is written as `0`; empty text
  is written as `''` (not SQL `NULL`).
- Editing shared child data (loot templates, trainers) affects every record that
  references it; spawn edits apply per-`guid`.
- Windows-only for now (GLFW + Vulkan backend, Win32 surface; the code is otherwise
  portable). Requires the LunarG Vulkan SDK to build and a Vulkan-1.4 driver to run.
