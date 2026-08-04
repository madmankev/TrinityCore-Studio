#pragma once

// AdtViewerModule — a streamed whole-map world viewer. Pick a map (Map.dbc); terrain maps stream
// tiles in around the fly camera (loaded async, evicted behind you, objects deduped by uniqueId),
// WMO-only maps load their single global WMO. All the heavy lifting lives in AdtStreamer; this
// module is the browser + per-frame camera→streamer→RenderWorld glue.

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "app/IEditorModule.h"
#include "gfx/IRenderer.h"
#include "viewer/ViewportCamera.h"
#include "adt/AdtStreamer.h"
#include "adt/AdtLoader.h"
#include "clientdata/DbcStore.h"
#include "editors/adt/MapSpawnRepository.h"
#include "editors/adt/NpcLayer.h"
#include "editors/adt/GameObjectLayer.h"

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
        return {{"ADT Browser", DockSlot::Left, true}, {"ADT Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}
    void OnClientDataLoaded() override;
    void OnConnected() override;
    void OnDisconnected() override;
    void OnShutdown() override;

    bool HasRecord() const override { return !loadedName_.empty(); }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawViewportPanel();
    void DrawStatsOverlay(const ImVec2& p0, const ImGuiIO& io);
    void OpenMapDir(const std::string& dir, bool frameCamera);
    void LoadNpcSpawns();       // query the current map's creature spawns into the NPC layer
    void LoadGameObjects();     // query the current map's gameobject spawns into the GO layer

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
};
} // namespace we
