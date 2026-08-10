#pragma once

// AdtViewerModule — a streamed whole-map world viewer. Pick a map (Map.dbc); terrain maps stream
// tiles in around the fly camera (loaded async, evicted behind you, objects deduped by uniqueId),
// WMO-only maps load their single global WMO. All the heavy lifting lives in AdtStreamer; this
// module is the browser + per-frame camera→streamer→RenderWorld glue.

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "imgui.h"      // ImGuizmo.h uses ImGui types but does not include imgui itself
#include "ImGuizmo.h"

#include "app/IEditorModule.h"
#include "gfx/IRenderer.h"
#include "ui/ViewportCamera.h"
#include "adt/AdtStreamer.h"
#include "adt/AdtLoader.h"
#include "clientdata/DbcStore.h"
#include "editors/adt/MapSpawnRepository.h"
#include "editors/adt/NpcLayer.h"
#include "editors/adt/GameObjectLayer.h"
#include "editors/adt/AdtEditStore.h"
#include "editors/common/CommandStack.h"
#include "editors/common/SnapshotStack.h"

namespace we
{
class AdtViewerModule final : public IEditorModule
{
public:
    const char* Id() const override { return "adt"; }  // stable settings/layout key from earlier releases
    const char* DisplayName() const override { return "World Editor"; }
    const char* RailGlyph() const override { return "W"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        // Keep the pre-v0.5 ImGui IDs after ### so existing users retain their saved docking layout
        // while the visible product language moves from "ADT Viewer" to "World Editor".
        return {{"World Browser###ADT Browser", DockSlot::Left, true},
                {"World Outliner", DockSlot::Left, true},
                {"Spawn Palette", DockSlot::Left, true},
                {"Locations", DockSlot::Left, true},
                {"Transform", DockSlot::Left, true},
                {"Formation", DockSlot::Left, true},
                {"AI Behavior", DockSlot::Left, true},
                {"Script Triggers", DockSlot::Left, true},
                {"NPC Instance", DockSlot::Left, true},
                {"GameObject Instance", DockSlot::Left, true},
                {"Waypoint Path", DockSlot::Bottom, true},
                {"Terrain Sculpt", DockSlot::Bottom, true},
                {"Light Editor", DockSlot::Left, true},
                {"Realtime Preview", DockSlot::Left, true},
                {"World Editor###ADT Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}
    void OnClientDataLoaded() override;
    void OnConnected() override;
    void OnDisconnected() override;
    void OnShutdown() override;
    void HandleShortcuts() override;
    void LoadSettings(const nlohmann::json& editorNode) override;
    void SaveSettings(nlohmann::json& editorNode) const override;

    // Undo/redo of object moves and the currently-authored waypoint route. While a route has
    // unsaved edits, Ctrl+Z/Ctrl+Y naturally targets its local snapshots; otherwise it targets the
    // persistent object command stack.
    bool CanUndo() const override { return (waypointDirty_ && waypointUndo_.CanUndo()) || undo_.CanUndo(); }
    bool CanRedo() const override { return (waypointDirty_ && waypointUndo_.CanRedo()) || undo_.CanRedo(); }
    void Undo() override;
    void Redo() override;

    bool HasRecord() const override { return !loadedName_.empty(); }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawOutlinerPanel();
    void DrawSpawnPalettePanel();
    void DrawLocationsPanel();
    void DrawTransformPanel();
    void DrawLightEditorPanel();
    void DrawRealtimePreviewPanel();
    void UpdateRealtimePreview(float dtSeconds);
    void DrawViewportPanel();
    void DrawStatsOverlay(const ImVec2& p0, const ImGuiIO& io);
    void OpenMapDir(const std::string& dir, bool frameCamera);
    void LoadNpcSpawns();       // query the current map's creature spawns into the NPC layer
    void LoadGameObjects();     // query the current map's gameobject spawns into the GO layer
    void LoadFormations();      // query creature_formations members for the open map (fail-soft)
    SpawnFilter CurrentSpawnFilter() const;
    void FrameWorldPosition(const glm::vec3& world, float radius = 75.0f);
    void FrameSelection();
    bool CanBrushPlace(const glm::vec3& world) const;

    // --- object selection + transform gizmo (see AdtViewerModule.cpp) ---
    enum class SelKind { None, Doodad, GameObject, Npc };
    struct TransformEdit
    {
        SelKind kind = SelKind::None;
        uint64_t uid = 0;
        uint32_t guid = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float yaw = 0.0f;              // radians, shared by NPC/GO/doodad placement controls
        float scale = 1.0f;
        bool initialized = false;
        bool dirty = false;
    };
    struct FormationState;
    void SyncTransformEdit();
    void ApplyTransformEdit();
    void RevertTransformEdit();
    void DrawFormationPanel();
    void SyncFormationEdit();
    void SaveFormationEdit();
    void DrawAiBehaviorPanel();
    void SyncAiBehaviorEdit();
    void ApplyAiBehaviorEdit(bool persistServerColumns);
    void ApplyAiBehaviorProfiles();
    void DrawAiBehaviorOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                               int w, int h);
    bool TryPlaceAiPreviewTarget(const glm::vec3& world);
    void DrawScriptTriggersPanel();
    void LoadScriptTriggersForMap(const std::string& mapDir);
    void SaveScriptTriggers();
    void RevertScriptTriggers();
    void MarkScriptTriggersDirty(const char* status = nullptr);
    void EvaluateScriptTriggers(float dtMs);
    void FireScriptTrigger(uint64_t triggerId, const char* reason);
    void FireInteractionTriggers(int objectKind, uint32_t guid);
    void DrawScriptTriggerOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                                  int w, int h);
    bool TryPlaceScriptPreviewPlayer(const glm::vec3& world);
    bool TryPlaceScriptTriggerCenter(const glm::vec3& world);
    void DeleteFormationEdit();
    void ApplyFormationState(uint32_t memberGuid, const FormationState& state);
    void DrawFormationOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                              int w, int h);
    struct OutlinerEntry
    {
        SelKind kind = SelKind::None;
        uint32_t guid = 0, entry = 0;
        glm::vec3 world{0.0f};
        uint32_t phaseMask = 1, spawnMask = 1;
        int32_t eventEntry = 0;
        bool poolHidden = false, groupManual = false;
        std::string label;
        std::string searchKey;
    };
    struct WorldBookmark
    {
        std::string name;
        uint32_t mapId = 0;
        std::string mapDir;
        glm::vec3 world{0.0f};
        float radius = 75.0f;
    };

    // Visual Script Event Triggers are a map-scoped Studio authoring layer. Classic 3.3.5 cores
    // have fixed client AreaTrigger.dbc volumes and SmartAI sources, but no universal DB table for
    // arbitrary editor-authored circles/boxes; the panel therefore previews and serializes precise
    // runtime hook metadata instead of pretending an unsupported schema write will execute server-side.
    enum class ScriptTriggerType : uint8_t { Area, Interaction, Proximity, Timer };
    enum class ScriptTriggerObjectKind : uint8_t { None, Npc, GameObject };
    enum class ScriptTriggerAreaShape : uint8_t { Circle, Box };
    struct ScriptEventTrigger
    {
        uint64_t id = 0;
        std::string name;
        bool enabled = true;
        ScriptTriggerType type = ScriptTriggerType::Area;
        ScriptTriggerAreaShape areaShape = ScriptTriggerAreaShape::Circle;
        glm::vec3 center{0.0f};
        float radius = 8.0f;               // circle / proximity radius (yards)
        glm::vec3 boxExtents{8.0f, 8.0f, 4.0f}; // half-extents for an area box
        float height = 0.0f;               // circle vertical half-height; 0 = unlimited
        ScriptTriggerObjectKind targetKind = ScriptTriggerObjectKind::None;
        uint32_t targetGuid = 0;           // NPC/GO guid for interaction/proximity
        float timerDelaySeconds = 5.0f;
        float timerRepeatSeconds = 0.0f;   // 0 = fire once
        std::string scriptHook;            // custom server/Studio hook name
        uint32_t scriptEventId = 0;        // optional script event id / message key
        uint32_t smartActionListId = 0;    // optional SmartAI timed action-list reference
        std::string note;
    };
    struct ScriptTriggerRuntime
    {
        bool areaInside = false;
        bool proximityInside = false;
        bool timerFired = false;
        float timerElapsed = 0.0f;
    };
    struct ScriptTriggerLogEntry
    {
        uint64_t triggerId = 0;
        float timeSeconds = 0.0f;
        std::string text;
    };
    // WoWEdit-style Studio light authoring. WotLK ADTs do not carry standalone editable
    // point/spot-light records, so these are deliberately a Studio settings layer: non-destructive,
    // map-scoped, saved in Studio settings, and fed live to the World Editor renderer.
    enum class WorldLightType : uint8_t { Point, Spot };
    struct WorldLight
    {
        uint64_t id = 0;
        std::string name;
        WorldLightType type = WorldLightType::Point;
        bool enabled = true;
        glm::vec3 position{0.0f};      // TrinityCore world coordinates
        glm::vec3 direction{0.0f, 0.0f, -1.0f}; // spot beam direction (outward from emitter)
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        float range = 12.0f;
        float falloff = 2.0f;
        float innerAngle = 20.0f;      // degrees, spot only
        float outerAngle = 35.0f;      // degrees, spot only
    };
    struct WorldLightingProfile
    {
        bool previewEnabled = true;
        bool sunEnabled = true;
        bool fogEnabled = false;
        // Optional real-time day/night preview. The stored clock is the reset/captured time;
        // runtimeTimeOfDay_ advances independently while playback is active.
        bool dayNightCycle = false;
        bool dayNightPlaying = true;
        float timeOfDayMinutes = 720.0f;       // 00:00..1440, 720 = midday
        float dayMinutesPerSecond = 30.0f;     // preview clock speed
        bool showMarkers = true;
        bool showVolumes = true;
        glm::vec3 ambientColor{1.0f};
        float ambientIntensity = 0.45f;
        glm::vec3 sunColor{1.0f};
        float sunIntensity = 0.55f;
        float sunAzimuth = 48.8f;      // degrees; matches the historical fixed sun approximately
        float sunElevation = 58.2f;
        glm::vec3 fogColor{0.12f, 0.12f, 0.14f};
        float fogStart = 500.0f;
        float fogEnd = 1000.0f;
        std::vector<WorldLight> lights;
    };
    struct LightMarker
    {
        uint64_t id = 0;
        ImVec2 screen{};
        float dist2 = 0.0f;
    };
    static WorldLightingProfile DefaultLightingProfile();
    static const char* WorldLightTypeName(WorldLightType type);
    void LoadLightingForMap(const std::string& mapDir);
    void SaveLightingProfile();
    void RevertLightingProfile();
    void MarkLightingDirty(const char* status = nullptr);
    WorldLight* FindWorldLight(uint64_t id);
    const WorldLight* FindWorldLight(uint64_t id) const;
    void AddWorldLight(WorldLightType type, const glm::vec3& world);
    void DuplicateSelectedWorldLight();
    void DeleteSelectedWorldLight();
    void NormalizeWorldLight(WorldLight& light);
    void FrameSelectedWorldLight();
    void ApplyWorldLightingToRenderer(const glm::vec3& origin, const glm::vec3& worldFocus);
    void BuildLightMarkerCache(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                               int w, int h, const glm::vec3& focus);
    bool TrySelectLightMarkerOverlay(bool viewportHovered);
    void DrawLightOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                          int w, int h);

    struct BrushPlacement
    {
        int kind = 0;  // 0 NPC, 1 GameObject
        uint32_t entry = 0;
        glm::vec3 world{0.0f};
    };
    struct FormationState
    {
        bool present = false;
        CreatureFormationMember row;
    };
    struct AiBehaviorProfileEntry
    {
        NpcAiBehavior behavior;
        bool templateScope = true;  // DB save target when a recognized schema field is available
    };
    void RebuildOutliner();
    void DrawSelectionToolbar();
    void RefreshGizmoFromSelection();   // seed gizmoMatrix_ + outline bounds from the live object
    void UpdateHoverAndSelection(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                                 int w, int h, bool viewportHovered, bool gizmoBusy,
                                 const glm::vec3& focus, const SpawnFilter& filter);
    void RunGizmo(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0, int w, int h);
    void ApplyGizmoEdit();              // push gizmoMatrix_ back into the selected object
    void SnapSelectionToGround();       // terrain raycast + persist/undo for NPC/GO/doodad placement
    void CommitSelectionToDb();         // save the moved DB spawn (called on gizmo release)
    void ApplySelectionOutline();       // drive the selected object's inverted-hull outline (per source)
    void ClearSelection();
    // Make (kind, ids) the current selection (shared by left-click select + right-click context).
    void SelectObject(SelKind kind, uint64_t duid, uint32_t gg, uint32_t ng);
    // Right-click on terrain -> "Add object here" popup (NPC / GameObject / M2 / WMO); right-click on
    // an object -> select it + "ObjectContext" menu (see DrawObjectContextPopup).
    void HandleRightClickAdd(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0, int w,
                             int h, bool viewportHovered, const glm::vec3& focus,
                             const SpawnFilter& filter);
    void DrawAddObjectPopup();
    void DrawObjectContextPopup();   // context menu for a right-clicked object (move hint / delete)
    void PerformAddNpc(uint32_t entry);
    void PerformAddGameObject(uint32_t entry);
    bool PerformAddNpcAt(uint32_t entry, const glm::vec3& world, float yaw);
    bool PerformAddGameObjectAt(uint32_t entry, const glm::vec3& world, float yaw);
    void PerformAddModel(const std::string& path, bool isWmo);

    // --- undo/redo of object moves ---
    // A captured object transform (the undo/redo unit). Holds everything
    // ApplyAndPersist needs to both update the in-memory object AND re-persist it
    // (DB UPDATE for GO/NPC spawns; AdtEditStore queue for doodad/WMO placements).
    struct AdtXform
    {
        SelKind  kind = SelKind::None;
        uint64_t uid = 0;                            // doodad uniqueId
        uint32_t guid = 0;                           // GameObject/NPC guid
        float x = 0.f, y = 0.f, z = 0.f, o = 0.f;    // world position + yaw (GO/NPC)
        float rot[4] = {0.f, 0.f, 0.f, 1.f};         // GO quaternion
        float size = 0.f;                            // GO scale (0 = leave unchanged)
        glm::mat4 local{1.0f};                       // doodad local-frame transform
        bool isWmo = false;                          // doodad: WMO vs M2
        std::vector<std::pair<int, int>> tiles;      // doodad: covering tiles
    };
    AdtXform CaptureSelection();               // read the selected object's live transform
    void ApplyAndPersist(const AdtXform& s);   // restore a transform + re-persist it
    static bool SameXform(const AdtXform& a, const AdtXform& b);

    // --- add / delete of whole objects (create/destroy, the undoable unit) ---
    // A full object descriptor: everything CreateObject needs to (re)materialize an object and
    // DestroyObject needs to remove it. Identity is stable (guid / uniqueId) so undo/redo reuse it.
    struct AdtObjectDesc
    {
        SelKind  kind = SelKind::None;
        uint32_t guid = 0, entry = 0, displayId = 0;   // DB spawn (GameObject/NPC)
        float x = 0, y = 0, z = 0, o = 0;
        float rot[4] = {0, 0, 0, 1};
        float size = 1.0f;
        uint64_t  uid = 0;                              // ADT placement (doodad/WMO)
        bool      isWmo = false;
        std::string path;
        glm::mat4 local{1.0f};
        int       tileX = 0, tileY = 0;
        std::vector<std::pair<int, int>> tiles;         // covering tiles (spanning placement)
    };
    AdtObjectDesc CaptureSelectedDesc();       // full descriptor of the current selection
    void CreateObject(const AdtObjectDesc& d); // materialize (DB insert / ADT add) + render live
    void DestroyObject(const AdtObjectDesc& d);// remove (DB delete / ADT remove) + from the viewport
    void PushCreateUndo(const AdtObjectDesc& d); // record "just created d" as an undo step
    void DeleteSelection();                    // delete the selected object (+ undo step)

    // --- NPC spawn-instance editor (docked "NPC Instance" panel) ---
    // Reflects the selected NPC's full `creature` row (loaded lazily on selection change) into an
    // editable working copy; Save writes one UPDATE + pushes one undo step, Revert discards.
    void DrawNpcInstancePanel();
    void SaveNpcEdit();                             // persist npcEdit_ + sync render + push undo
    void ApplyAndPersistNpcSpawn(const CreatureSpawn& s);  // shared undo/redo body (persist + sync)
    void ApplyRenderFromSpawn(const CreatureSpawn& s);     // push edit-affected fields into NpcLayer
    void SyncNpcPanelTransform(uint32_t guid, float x, float y, float z, float o);  // gizmo -> panel

    // --- GameObject spawn-instance editor (docked "GameObject Instance" panel) ---
    // Same working-copy + Save/Revert + undo model as the NPC panel, over the `gameobject` row.
    void DrawGoInstancePanel();
    void SaveGoEdit();
    void ApplyAndPersistGoSpawn(const GameObjectSpawn& s);
    void ApplyRenderFromGoSpawn(const GameObjectSpawn& s);  // push edit-affected fields into GameObjectLayer
    void SyncGoPanelTransform(uint32_t guid);               // gizmo -> panel (reads live spawn)

    // --- visual waypoint-path editor (docked "Waypoint Path" panel + viewport overlay) ---
    // Keeps an editable working copy for the selected NPC. Paths can be shared by a creature template
    // or overridden per spawn; the UI makes the source explicit and can clone a shared route locally.
    enum class WaypointPlacementMode { None, Add, MoveSelected };
    void DrawWaypointPathPanel();
    void DrawTerrainSculptPanel();
    void SyncWaypointPathToSelection(bool discardCurrent = false);
    void ResetWaypointPathEditor();
    void MarkWaypointPathDirty();
    void ReindexWaypointPoints();
    void AddWaypointAt(const glm::vec3& world);
    void MoveSelectedWaypointTo(const glm::vec3& world);
    void SaveWaypointPathEdit();
    void CreateOrCloneLocalWaypointPath(bool cloneCurrent);
    void BindExistingWaypointPath(uint32_t pathId);
    void ClearLocalWaypointPath();
    void EnableSelectedNpcWaypointMotion();
    bool TrySelectWaypointOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                                  int w, int h, bool viewportHovered);
    void DrawWaypointOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                             int w, int h);
    void DrawTerrainBrushOverlay(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                                 int w, int h, bool viewportHovered);

    // NPC model fallbacks: map markers remain visible/selectable when a custom display ID cannot be
    // resolved to an M2, and make a missing WoW client-model pack immediately diagnosable.
    struct NpcMarker { uint32_t guid = 0, entry = 0; ImVec2 screen{}; float dist2 = 0.0f; };
    void BuildNpcMarkerCache(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                             int w, int h, const glm::vec3& focus, const SpawnFilter& filter);
    bool TrySelectNpcMarkerOverlay(bool viewportHovered);
    void DrawNpcMarkerOverlay();

    EditorServices* svc_ = nullptr;
    AdtStreamer streamer_;
    bool streamerInit_ = false;

    std::unordered_map<uint32_t, DbcStore::MapInfo> maps_;
    std::vector<std::pair<uint32_t, std::string>>   mapList_;   // (id, "dir (name)") sorted
    int selectedMap_ = -1;
    uint32_t currentMapId_ = 0;   // Map.dbc id of the open map (for creature.map queries)
    std::string selectedMapDir_;
    std::string loadedName_;
    std::string error_;

    // Coordinate navigation + persisted bookmarks. Coordinates are TrinityCore world values,
    // never streamer-local values, so they stay useful across tile-origin changes.
    std::vector<WorldBookmark> bookmarks_;
    char  bookmarkName_[80] = {0};
    float locationX_ = 0.0f, locationY_ = 0.0f, locationZ_ = 0.0f;
    float locationRadius_ = 75.0f;
    bool  pendingLocationFocus_ = false;
    std::string pendingLocationMapDir_;
    glm::vec3 pendingLocationWorld_{0.0f};
    float pendingLocationRadius_ = 75.0f;

    // Map-scoped Light Editor state. `lightingProfiles_` is the persisted baseline; a profile
    // being actively edited lives in lightingEdit_ and can remain as an in-memory draft while the
    // user moves between maps. This mirrors the World Editor's explicit Save/Revert discipline.
    std::unordered_map<std::string, WorldLightingProfile> lightingProfiles_;
    std::unordered_map<std::string, WorldLightingProfile> lightingDrafts_;
    WorldLightingProfile lightingEdit_;
    std::string lightingMapDir_;
    uint64_t nextWorldLightId_ = 1;
    uint64_t selectedWorldLightId_ = 0;
    bool lightingDirty_ = false;
    float runtimeTimeOfDay_ = 720.0f;  // advances when the day/night preview clock plays
    bool worldSimulationPaused_ = false;
    float worldSimulationSpeed_ = 1.0f;
    bool inGameViewMode_ = false;      // hides editor-only helpers without disabling real-time simulation
    bool lightPlacementActive_ = false;
    bool lightPlacementContinuous_ = false;
    WorldLightType lightPlacementType_ = WorldLightType::Point;
    char lightSearch_[128] = {0};
    std::string lightingStatus_;
    std::vector<LightMarker> lightMarkers_;

    // NPC layer: DB creature spawns rendered + movement-simulated on the terrain.
    MapSpawnRepository spawnRepo_;
    NpcLayer npcLayer_;
    bool  showNpcs_ = true;
    int   npcMaxDraw_ = 200;       // cap on animated NPCs (nearest first)
    float npcCullDist_ = 300.0f;   // yards: simulate/draw NPCs within this of the camera
    std::string npcStatus_;
    bool  showNpcMarkers_ = true;  // selectable overlay fallback when a creature M2 is unavailable
    bool  showNpcMarkerLabels_ = false;
    int   npcMarkerMax_ = 500;
    std::vector<NpcMarker> npcMarkers_;

    // GameObject layer: DB gameobject spawns rendered (M2 + WMO) on the terrain.
    GameObjectLayer goLayer_;
    bool  showGos_ = true;
    int   goMaxDraw_ = 300;        // cap on drawn GameObjects (nearest first)
    float goCullDist_ = 400.0f;    // yards: draw GameObjects within this of the camera
    std::string goStatus_;

    // World Outliner snapshots: all DB spawns on the open map, independent of the draw-distance
    // cap. Rebuilt only after map/spawn changes or client/DB labels refresh, then filtered/sorted
    // cheaply while the panel is visible.
    std::vector<OutlinerEntry> outlinerEntries_;
    std::vector<int> outlinerFiltered_;
    char outlinerSearch_[128] = {0};
    int  outlinerKind_ = 0;       // 0 all, 1 NPCs, 2 GameObjects
    bool outlinerVisibleOnly_ = false;
    bool outlinerSortByDistance_ = true;
    bool outlinerDirty_ = true;

    // Formation data is map-scoped and visualized as leader/member links in the viewport. The
    // selected member gets an editable working state with the same save/revert/undo discipline as
    // spawn instance panels.
    std::vector<CreatureFormationMember> formations_;
    bool formationsAvailable_ = false;
    bool showFormations_ = true;
    FormationState formationEdit_;
    FormationState formationOrig_;
    uint32_t formationEditGuid_ = 0;
    bool formationDirty_ = false;
    std::string formationStatus_;

    // AI Behavior Configuration is map-scoped Studio state layered over any recognized server
    // detection/leash columns. It drives the optional real-time preview target in NpcLayer.
    std::unordered_map<std::string, std::unordered_map<uint32_t, AiBehaviorProfileEntry>> aiBehaviorProfiles_;
    AiBehaviorProfileEntry aiBehaviorEdit_;
    AiBehaviorProfileEntry aiBehaviorOrig_;
    uint32_t aiBehaviorEditGuid_ = 0;
    bool aiBehaviorDirty_ = false;
    bool showAiBehaviorOverlay_ = true;
    bool aiPreviewTargetEnabled_ = false;
    bool aiPreviewTargetPlacementActive_ = false;
    glm::vec3 aiPreviewTargetWorld_{0.0f};
    std::string aiBehaviorStatus_;

    // Script Trigger authoring + live preview state. Stored profiles contain only authored trigger
    // metadata; runtime edges/timers/logs reset on map switch so a preview is deterministic.
    std::unordered_map<std::string, std::vector<ScriptEventTrigger>> scriptTriggerProfiles_;
    std::unordered_map<std::string, std::vector<ScriptEventTrigger>> scriptTriggerDrafts_;
    std::vector<ScriptEventTrigger> scriptTriggersEdit_;
    std::unordered_map<uint64_t, ScriptTriggerRuntime> scriptTriggerRuntime_;
    std::vector<ScriptTriggerLogEntry> scriptTriggerLog_;
    std::string scriptTriggerMapDir_;
    uint64_t nextScriptTriggerId_ = 1;
    uint64_t selectedScriptTriggerId_ = 0;
    bool scriptTriggersDirty_ = false;
    bool showScriptTriggerOverlay_ = true;
    bool scriptPreviewPlayerEnabled_ = false;
    bool scriptPreviewPlayerFollowCamera_ = false;
    bool scriptPreviewPlayerPlacementActive_ = false;
    bool scriptTriggerCenterPlacementActive_ = false;
    bool scriptInteractionMode_ = false;
    glm::vec3 scriptPreviewPlayerWorld_{0.0f};
    float scriptPreviewTimeSeconds_ = 0.0f;
    char scriptTriggerSearch_[128] = {0};
    std::string scriptTriggerStatus_;

    // Reusable terrain-click placement palette. Unlike the one-shot context menu, a palette entry
    // stays armed for rapid map dressing and keeps an inexpensive spacing guard for the session.
    int brushKind_ = 0;          // 0 NPC, 1 GameObject
    uint32_t brushEntry_ = 0;
    char brushSearch_[128] = {0};
    bool brushActive_ = false;
    float brushYaw_ = 0.0f;
    float brushMinSpacing_ = 2.0f;
    std::vector<BrushPlacement> brushPlacements_;
    std::string brushStatus_;

    // Spawn-visibility filters (live; assembled into a SpawnFilter each frame).
    // Phase: phaseSel_ 0 = All phases (0xFFFFFFFF); N>0 = single phase bit (1 << (N-1)).
    int      phaseSel_ = 1;               // default: Phase 1
    uint32_t viewPhaseMask_ = 1;
    int      diffSel_ = 0;                // spawnMask: 0 = All; N>0 = mode bit (1 << (N-1))
    int      eventSel_ = 0;               // events: 0 = None, 1 = All, 2+ = gameEvents_[i-2]
    bool     respectPools_ = true;        // hide pooled spawns beyond max_limit
    bool     showManualGroups_ = false;   // show MANUAL_SPAWN group members
    std::vector<GameEventInfo> gameEvents_;   // game_event list for the Events dropdown

    adt::AdtLoadOptions opt_;
    int  streamRadius_ = 4;   // tiles kept in each direction (user-chosen "large" default)
    ViewportCamera camera_;
    bool showGrid_ = false;   // a world grid at ±17k isn't useful; off by default
    bool showStats_ = true;   // perf HUD overlay
    bool dbcLoaded_ = false;
    char search_[128] = {0};
    float cpuBuildMs_ = 0.0f;   // last BuildFrame CPU time

    std::vector<TerrainHandle>    frameTerrains_;   // scratch, reused per frame
    std::vector<InstancedGroup>   frameGroups_;
    std::vector<SceneInstanceGpu> frameScene_;

    // Object selection + gizmo state. The gizmo edits `gizmoMatrix_` in the streamer-local frame;
    // moves for DB spawns are saved on release, doodad moves are visual-only (reset on tile reload).
    bool      editMode_ = true;               // click-to-select / gizmo enabled
    SelKind   selKind_ = SelKind::None;
    uint64_t  selUid_ = 0;                     // selected doodad uniqueId
    uint32_t  selGuid_ = 0;                    // selected GameObject/NPC guid
    uint32_t  selEntry_ = 0;                   // its template entry (for the label)
    std::string selLabel_;
    glm::mat4 gizmoMatrix_{1.0f};              // the matrix ImGuizmo manipulates (local frame)
    ImGuizmo::OPERATION gizmoOp_ = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      gizmoMode_ = ImGuizmo::WORLD;
    bool      gizmoUsingPrev_ = false;         // edge-detect release (to commit the DB save)
    bool      gizmoHoveredPrev_ = false;       // suppress camera when hovering the gizmo
    std::string saveStatus_;

    // Precise transform panel + copy/paste clipboard. The edit is staged so a multi-field change
    // produces one DB/ADT save and one undo command rather than a write for every keystroke.
    TransformEdit transformEdit_;
    TransformEdit transformClipboard_;
    float transformNudge_ = 1.0f;
    std::string transformStatus_;

    // NPC spawn-instance editor state. npcEdit_ is the working copy the panel widgets edit;
    // npcEditOrig_ is the last-saved baseline (Revert target + the undo "before" state).
    CreatureSpawn npcEdit_;
    CreatureSpawn npcEditOrig_;
    uint32_t      npcEditGuid_ = 0;    // guid currently loaded into the panel (0 = none)
    bool          npcEditDirty_ = false;
    std::string   npcEditStatus_;

    // GameObject spawn-instance editor state (mirrors the NPC editor above).
    GameObjectSpawn goEdit_;
    GameObjectSpawn goEditOrig_;
    uint32_t        goEditGuid_ = 0;
    bool            goEditDirty_ = false;
    std::string     goEditStatus_;

    // Waypoint path working copy. `waypointEditGuid_` is the NPC this route is previewed on;
    // it intentionally remains pinned while dirty even if the user selects another NPC, preventing
    // accidental loss of unsaved route work. source describes whether the loaded route is shared.
    WaypointPath         waypointEdit_;
    WaypointPath         waypointEditOrig_;
    SnapshotStack<WaypointPath> waypointUndo_;  // in-memory route edit history (cleared on load/save)
    uint32_t             waypointEditGuid_ = 0;
    uint32_t             waypointEditEntry_ = 0;
    uint32_t             waypointBindId_ = 0;
    int                  waypointSelected_ = -1;
    WaypointPathSource   waypointSource_ = WaypointPathSource::None;
    WaypointPlacementMode waypointPlacementMode_ = WaypointPlacementMode::None;
    bool                 waypointLoaded_ = false;
    bool                 waypointDirty_ = false;
    bool                 showWaypointOverlay_ = true;
    bool                 enableWaypointMotionOnBind_ = true;
    std::string          waypointStatus_;

    // Pending ADT placement edits (moved doodads/WMOs), flushed to the overlay by "Save ADT edits".
    AdtEditStore adtEdits_;

    // Terrain height sculpting is deliberately staged alongside placement edits: right-clicking
    // ground queues a lossless MCVT/MCNR patch in the project's loose overlay, and a Save reloads
    // streamed tiles from that overlay. Undo/redo applies only while the stroke remains pending.
    bool terrainSculptActive_ = false;
    // Draw staged strokes immediately in the GPU terrain shader. Save ADT edits still performs the
    // authoritative MCVT/MCNR patch + reload; this toggle is a safe real-time preview only.
    bool liveTerrainPreview_ = true;
    std::vector<AdtEditStore::TerrainStrokeRef> terrainPreviewStrokes_;
    int terrainSculptMode_ = 0;       // adt::TerrainBrushMode (0 raise, 1 lower, 2 flatten)
    float terrainBrushRadius_ = 10.0f;
    float terrainBrushStrength_ = 2.0f;
    float terrainFlattenZ_ = 0.0f;
    bool terrainSampleFlattenZ_ = true;
    uint64_t terrainHistoryGeneration_ = 1;
    std::string terrainStatus_;

    // Right-click "add object here" flow.
    ImVec2    rightPressPos_{0, 0};
    bool      rightDown_ = false;
    bool      rightMoved_ = false;
    int       addType_ = 0;                 // 0 NPC, 1 GameObject, 2 M2, 3 WMO
    char      addSearch_[128] = {0};
    glm::vec3 addWorldPos_{0.0f};           // DB coords of the ground hit (creature/gameobject.position_*)
    glm::vec3 addLocal_{0.0f};              // streamer-local ground hit (for AddObject)
    int       addTileX_ = 0, addTileY_ = 0; // owning tile of the ground hit
    uint32_t  addUidCounter_ = 0;           // allocates ADT insert uniqueIds from a high base
    std::vector<std::string> m2List_, wmoList_;   // cached listfile paths (lazy)
    bool      m2Listed_ = false, wmoListed_ = false;

    // Undo/redo of object moves — this editor's OWN stack (never shared). Each gizmo
    // drag records one command; undo reverts both the viewport and the DB/overlay.
    // Cleared on map change so stale guids/uniqueIds never resurface.
    CommandStack undo_;
    AdtXform dragBefore_;      // pre-edit snapshot captured at gizmo drag start
    std::string undoMapDir_;   // map the undo stack belongs to (cleared when the map actually changes)
};
} // namespace we
