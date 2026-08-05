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

namespace we
{
class AdtViewerModule final : public IEditorModule
{
public:
    const char* Id() const override { return "adt"; }
    const char* DisplayName() const override { return "ADT Viewer"; }
    const char* RailGlyph() const override { return "5"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        return {{"ADT Browser", DockSlot::Left, true},
                {"NPC Instance", DockSlot::Left, true},
                {"GameObject Instance", DockSlot::Left, true},
                {"ADT Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}
    void OnClientDataLoaded() override;
    void OnConnected() override;
    void OnDisconnected() override;
    void OnShutdown() override;

    // Undo/redo of object moves — this editor's own stack (see Undo/Redo section below).
    bool CanUndo() const override { return undo_.CanUndo(); }
    bool CanRedo() const override { return undo_.CanRedo(); }
    void Undo() override { undo_.Undo(); }
    void Redo() override { undo_.Redo(); }

    bool HasRecord() const override { return !loadedName_.empty(); }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawViewportPanel();
    void DrawStatsOverlay(const ImVec2& p0, const ImGuiIO& io);
    void OpenMapDir(const std::string& dir, bool frameCamera);
    void LoadNpcSpawns();       // query the current map's creature spawns into the NPC layer
    void LoadGameObjects();     // query the current map's gameobject spawns into the GO layer

    // --- object selection + transform gizmo (see AdtViewerModule.cpp) ---
    enum class SelKind { None, Doodad, GameObject, Npc };
    void DrawSelectionToolbar();
    void RefreshGizmoFromSelection();   // seed gizmoMatrix_ + outline bounds from the live object
    void UpdateHoverAndSelection(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                                 int w, int h, bool viewportHovered, bool gizmoBusy,
                                 const glm::vec3& focus, const SpawnFilter& filter);
    void RunGizmo(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0, int w, int h);
    void ApplyGizmoEdit();              // push gizmoMatrix_ back into the selected object
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

    // NPC layer: DB creature spawns rendered + movement-simulated on the terrain.
    MapSpawnRepository spawnRepo_;
    NpcLayer npcLayer_;
    bool  showNpcs_ = true;
    int   npcMaxDraw_ = 200;       // cap on animated NPCs (nearest first)
    float npcCullDist_ = 300.0f;   // yards: simulate/draw NPCs within this of the camera
    std::string npcStatus_;

    // GameObject layer: DB gameobject spawns rendered (M2 + WMO) on the terrain.
    GameObjectLayer goLayer_;
    bool  showGos_ = true;
    int   goMaxDraw_ = 300;        // cap on drawn GameObjects (nearest first)
    float goCullDist_ = 400.0f;    // yards: draw GameObjects within this of the camera
    std::string goStatus_;

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

    // Pending ADT placement edits (moved doodads/WMOs), flushed to the overlay by "Save ADT edits".
    AdtEditStore adtEdits_;

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
