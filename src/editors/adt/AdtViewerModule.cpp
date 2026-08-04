// AdtViewerModule — see AdtViewerModule.h.

#include "editors/adt/AdtViewerModule.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>

#include "imgui.h"
#include <glm/glm.hpp>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"

namespace we
{
namespace
{
std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
} // namespace

void AdtViewerModule::OnClientDataLoaded()
{
    dbcLoaded_ = false;
    maps_.clear();
    mapList_.clear();
    selectedMap_ = -1;
    currentMapId_ = 0;
    loadedName_.clear();
    npcLayer_.Clear();   // client data (model paths) changed — drop cached NPC models
    goLayer_.Clear();
    npcStatus_.clear();
    goStatus_.clear();
}

void AdtViewerModule::OnConnected()
{
    LoadNpcSpawns();       // a DB is now available — populate NPCs + GameObjects for the open map
    LoadGameObjects();
}

void AdtViewerModule::OnDisconnected()
{
    // Keep any already-loaded NPCs/GOs rendering locally; they just can't be refreshed now.
    npcStatus_ = "Disconnected — NPCs frozen (reconnect to refresh).";
}

void AdtViewerModule::OnShutdown()
{
    npcLayer_.Clear();      // free NPC/GO models while the renderer is still alive
    goLayer_.Clear();
    streamer_.Shutdown();   // join workers + free GPU while the renderer/client data are still alive
}

void AdtViewerModule::LoadNpcSpawns()
{
    npcStatus_.clear();
    if (!streamerInit_ || !svc_)
        return;
    npcLayer_.Init(svc_->clientData, svc_->dbcStore, svc_->renderer);
    if (currentMapId_ == 0 && selectedMap_ < 0)
        return;
    if (!svc_->connected || !svc_->activeDb)
    {
        npcLayer_.SetSpawns({});
        npcStatus_ = "Connect a project's database to see NPCs.";
        return;
    }
    std::vector<MapSpawn> spawns;
    DbError e = spawnRepo_.LoadSpawnsForMap(*svc_->activeDb, currentMapId_, spawns);
    if (!e.ok)
    {
        npcLayer_.SetSpawns({});
        npcStatus_ = "NPC load failed: " + e.message;
        return;
    }
    const size_t n = spawns.size();
    npcLayer_.SetSpawns(std::move(spawns));
    npcStatus_ = std::to_string(n) + " NPC spawns on this map.";
}

void AdtViewerModule::LoadGameObjects()
{
    goStatus_.clear();
    if (!streamerInit_ || !svc_)
        return;
    goLayer_.Init(svc_->clientData, svc_->dbcStore, svc_->renderer);
    if (currentMapId_ == 0 && selectedMap_ < 0)
        return;
    if (!svc_->connected || !svc_->activeDb)
    {
        goLayer_.SetGameObjects({});
        goStatus_ = "Connect a project's database to see GameObjects.";
        return;
    }
    std::vector<MapGameObject> gos;
    DbError e = spawnRepo_.LoadGameObjectsForMap(*svc_->activeDb, currentMapId_, gos);
    if (!e.ok)
    {
        goLayer_.SetGameObjects({});
        goStatus_ = "GameObject load failed: " + e.message;
        return;
    }
    const size_t n = gos.size();
    goLayer_.SetGameObjects(std::move(gos));

    // MO_TRANSPORTs (boats/zeppelins) live in the `transports` table, not per-map; load them all
    // and let the layer render each only while its route is on the open map. Missing table is fine.
    std::vector<MoTransportDef> mos;
    if (spawnRepo_.LoadMoTransports(*svc_->activeDb, mos).ok)
        goLayer_.SetMoTransports(std::move(mos));

    // Game-event list for the Events filter dropdown (missing table -> empty, fine).
    gameEvents_.clear();
    spawnRepo_.LoadGameEvents(*svc_->activeDb, gameEvents_);
    if (eventSel_ > static_cast<int>(gameEvents_.size()) + 1)
        eventSel_ = 0;   // previous selection no longer valid

    goStatus_ = std::to_string(n) + " GameObject spawns on this map.";
}

void AdtViewerModule::DrawPanels()
{
    // Lazily start the streamer (needs client data + renderer) and load the map list.
    if (!streamerInit_ && svc_ && svc_->clientData && svc_->clientData->IsOpen() && svc_->renderer &&
        svc_->dbcStore)
    {
        streamer_.Init(svc_->clientData, svc_->dbcStore, svc_->renderer, 3);
        npcLayer_.Init(svc_->clientData, svc_->dbcStore, svc_->renderer);
        goLayer_.Init(svc_->clientData, svc_->dbcStore, svc_->renderer);
        streamerInit_ = true;
    }
    if (!dbcLoaded_ && svc_ && svc_->clientData && svc_->clientData->IsOpen() && svc_->dbcStore)
    {
        maps_ = svc_->dbcStore->LoadMaps(*svc_->clientData);
        mapList_.clear();
        for (const auto& kv : maps_)
            mapList_.emplace_back(kv.first, kv.second.directory + "  (" + kv.second.name + ")");
        std::sort(mapList_.begin(), mapList_.end(),
                  [](const auto& a, const auto& b) { return Lower(a.second) < Lower(b.second); });
        dbcLoaded_ = true;
    }

    DrawBrowserPanel();
    DrawViewportPanel();
}

void AdtViewerModule::DrawBrowserPanel()
{
    if (!ImGui::Begin("ADT Browser"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        ImGui::TextWrapped("Load WoW client data (File menu) to browse maps.");
        ImGui::End();
        return;
    }
    if (!svc_->renderer)
    {
        ImGui::TextWrapped("Renderer unavailable.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Map");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "filter maps", search_, sizeof(search_));
    ImGui::BeginChild("##maplist", ImVec2(0, 360), true);
    for (int i = 0; i < (int)mapList_.size(); ++i)
    {
        if (search_[0] && Lower(mapList_[i].second).find(Lower(search_)) == std::string::npos)
            continue;
        if (ImGui::Selectable(mapList_[i].second.c_str(), i == selectedMap_))
        {
            selectedMap_ = i;
            currentMapId_ = mapList_[i].first;
            selectedMapDir_ = maps_[mapList_[i].first].directory;
            OpenMapDir(selectedMapDir_, true);
            npcLayer_.Clear();   // drop the previous map's NPC + GO models
            goLayer_.Clear();
            LoadNpcSpawns();     // query this map's spawns (no-op if not connected)
            LoadGameObjects();
        }
    }
    ImGui::EndChild();

    if (!error_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());
    ImGui::End();
}

void AdtViewerModule::OpenMapDir(const std::string& dir, bool frameCamera)
{
    if (!streamerInit_)
        return;
    if (!streamer_.OpenMap(dir))
    {
        error_ = "WDT missing for " + dir;
        loadedName_.clear();
        return;
    }
    error_.clear();
    loadedName_ = dir + (streamer_.wmoOnly() ? "  (WMO)" : "");
    if (frameCamera)
        camera_.Frame(streamer_.startPos(), streamer_.startRadius());
    if (svc_ && svc_->setStatus)
        svc_->setStatus("Opened map: " + dir);
}

void AdtViewerModule::DrawViewportPanel()
{
    if (!ImGui::Begin("ADT Viewer"))
    {
        ImGui::End();
        return;
    }
    if (!streamerInit_ || loadedName_.empty())
    {
        ImGui::TextDisabled("No map loaded. Pick a map in the ADT Browser, then fly (Fly camera).");
        ImGui::End();
        return;
    }

    camera_.DrawControls();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    ImGui::Checkbox("Stats", &showStats_);
    ImGui::SameLine();
    { bool w = streamer_.showWdl(); if (ImGui::Checkbox("Distant", &w)) streamer_.setShowWdl(w); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Doodads", &opt_.doodads)) OpenMapDir(selectedMapDir_, false);
    ImGui::SameLine();
    if (ImGui::Checkbox("WMOs", &opt_.wmos)) OpenMapDir(selectedMapDir_, false);
    ImGui::SameLine();
    if (ImGui::Checkbox("Liquid", &opt_.liquid)) OpenMapDir(selectedMapDir_, false);
    if (!streamer_.wmoOnly())
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("Radius", &streamRadius_, 1, 5);   // pool sized for R<=5 (13x13 loaded)
    }

    // NPC controls: toggle + how many/how far to render creature spawns.
    ImGui::Checkbox("NPCs", &showNpcs_);
    if (showNpcs_)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("Max NPCs", &npcMaxDraw_, 25, 500);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("NPC range", &npcCullDist_, 100.0f, 800.0f, "%.0f yd");
        ImGui::SameLine();
        ImGui::TextDisabled("%d/%d NPCs%s", npcLayer_.drawnCount(), npcLayer_.spawnCount(),
                            npcLayer_.cappedLastFrame() ? " (capped)" : "");
    }

    ImGui::Checkbox("GameObjects", &showGos_);
    if (showGos_)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderInt("Max GOs", &goMaxDraw_, 25, 800);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::SliderFloat("GO range", &goCullDist_, 100.0f, 1000.0f, "%.0f yd");
        ImGui::SameLine();
        ImGui::TextDisabled("%d/%d GOs%s", goLayer_.drawnCount(), goLayer_.count(),
                            goLayer_.cappedLastFrame() ? " (capped)" : "");
    }

    // Spawn-visibility filters: hide NPCs/GameObjects the DB wouldn't actually spawn given the
    // selected phase / difficulty / active events, and (approximately) respect pools + spawn groups.
    {
        static const char* kPhaseItems[] = {
            "All phases", "Phase 1", "Phase 2",  "Phase 3",  "Phase 4",  "Phase 5",  "Phase 6",
            "Phase 7",    "Phase 8", "Phase 9",  "Phase 10", "Phase 11", "Phase 12", "Phase 13",
            "Phase 14",   "Phase 15", "Phase 16"};
        ImGui::SetNextItemWidth(120);
        if (ImGui::Combo("Phase", &phaseSel_, kPhaseItems, IM_ARRAYSIZE(kPhaseItems)))
            viewPhaseMask_ = (phaseSel_ == 0) ? 0xFFFFFFFFu : (1u << (phaseSel_ - 1));

        ImGui::SameLine();
        static const char* kDiffItems[] = {"All modes", "Normal", "Heroic", "10-Heroic", "25-Heroic"};
        ImGui::SetNextItemWidth(120);
        ImGui::Combo("Difficulty", &diffSel_, kDiffItems, IM_ARRAYSIZE(kDiffItems));

        // Events combo: None / All / each game_event (dynamic list).
        ImGui::SameLine();
        const char* evPreview = (eventSel_ == 0) ? "None"
                                : (eventSel_ == 1) ? "All events"
                                : (eventSel_ - 2 < static_cast<int>(gameEvents_.size())
                                       ? gameEvents_[eventSel_ - 2].description.c_str()
                                       : "None");
        ImGui::SetNextItemWidth(180);
        if (ImGui::BeginCombo("Events", evPreview))
        {
            if (ImGui::Selectable("None", eventSel_ == 0)) eventSel_ = 0;
            if (ImGui::Selectable("All events", eventSel_ == 1)) eventSel_ = 1;
            for (int i = 0; i < static_cast<int>(gameEvents_.size()); ++i)
            {
                std::string label = "#" + std::to_string(gameEvents_[i].id) + "  " +
                                    gameEvents_[i].description;
                if (ImGui::Selectable(label.c_str(), eventSel_ == i + 2)) eventSel_ = i + 2;
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::Checkbox("Respect pools", &respectPools_);
        ImGui::SameLine();
        ImGui::Checkbox("Manual groups", &showManualGroups_);
    }

    if (!npcStatus_.empty())
        ImGui::TextDisabled("%s", npcStatus_.c_str());
    if (!goStatus_.empty())
        ImGui::TextDisabled("%s", goStatus_.c_str());

    ImGui::TextDisabled("loaded %d tiles (%d pending), %d objects, %d models", streamer_.loadedTiles(),
                        streamer_.pendingTiles(), streamer_.objectCount(), streamer_.modelCount());

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const int w = std::max(16, (int)avail.x);
    const int h = std::max(16, (int)avail.y);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##viewport", ImVec2((float)w, (float)h),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();

    camera_.Update(hovered, active, io.DeltaTime);
    const glm::vec3 eye = camera_.Eye();
    const glm::mat4 view = camera_.View();
    const glm::mat4 proj = camera_.Proj((float)w / (float)h);

    // Stream around the camera's focus: the eye when flying, the orbit pivot otherwise.
    const glm::vec3 focus = (camera_.mode() == ViewportCamera::Mode::Fly) ? eye : camera_.center();
    streamer_.Update(focus, streamRadius_, opt_);

    if (showGrid_)
    {
        const float gc[3] = {focus.x, focus.y, focus.z - 50.0f};
        const float ext = adt::kTileSize * (streamRadius_ + 1);
        svc_->renderer->SetGrid(true, gc, ext, std::max(ext / 20.0f, 1e-4f));
    }
    else
    {
        const float z[3] = {0, 0, 0};
        svc_->renderer->SetGrid(false, z, 0, 1);
    }

    const auto tBuild = std::chrono::steady_clock::now();
    streamer_.BuildFrame(view, proj, eye, io.DeltaTime * 1000.0f, frameTerrains_, frameGroups_, frameScene_);

    // NPC layer: simulate + animate the near creature spawns and append them to the same
    // scene list (world = focus + streamer origin). Uses the connected DB for lazy waypoint
    // loading; harmless when disconnected (existing NPCs keep simulating locally).
    // Assemble the live spawn-visibility filter from the toolbar state.
    SpawnFilter filter;
    filter.phaseMask = viewPhaseMask_;
    filter.spawnMask = (diffSel_ == 0) ? 0xFFFFFFFFu : (1u << (diffSel_ - 1));
    filter.activeEvent = (eventSel_ == 0) ? 0
                         : (eventSel_ == 1) ? -1
                         : (eventSel_ - 2 < static_cast<int>(gameEvents_.size())
                                ? gameEvents_[eventSel_ - 2].id
                                : 0);
    filter.respectPools = respectPools_;
    filter.showManualGroups = showManualGroups_;

    if (showNpcs_)
    {
        IDatabase* db = (svc_->connected) ? svc_->activeDb : nullptr;
        npcLayer_.Build(focus, streamer_.origin(), view, io.DeltaTime * 1000.0f, db, frameScene_,
                        npcMaxDraw_, npcCullDist_, filter);
    }
    if (showGos_)
    {
        goLayer_.Build(focus, streamer_.origin(), view, io.DeltaTime * 1000.0f, currentMapId_,
                       filter, frameScene_, goMaxDraw_, goCullDist_);
    }
    cpuBuildMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - tBuild).count();

    ImTextureID tex = svc_->renderer->RenderWorld(frameTerrains_.data(), (int)frameTerrains_.size(),
                                                  frameGroups_.data(), (int)frameGroups_.size(),
                                                  frameScene_.data(), (int)frameScene_.size(),
                                                  &view[0][0], &proj[0][0], w, h);
    if (tex)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + w, p0.y + h));

    if (showStats_)
        DrawStatsOverlay(p0, io);
    ImGui::End();
}

// A compact perf HUD in the top-left of the viewport (frame/GPU/CPU ms + draw & scene counts).
void AdtViewerModule::DrawStatsOverlay(const ImVec2& p0, const ImGuiIO& io)
{
    const RenderStats& rs = svc_->renderer->renderStats();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(p0.x + 6, p0.y + 6), ImVec2(p0.x + 250, p0.y + 108),
                      IM_COL32(0, 0, 0, 150), 4.0f);
    ImGui::SetCursorScreenPos(ImVec2(p0.x + 12, p0.y + 10));
    ImGui::BeginGroup();
    ImGui::Text("%.0f FPS  (%.2f ms)", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
    ImGui::Text("GPU %.2f ms   BuildFrame %.2f ms", rs.gpuMs, cpuBuildMs_);
    ImGui::Text("draws %d   tiles %d", rs.drawCalls, rs.terrainTiles);
    ImGui::Text("groups %d  inst %d  other %d", rs.instancedGroups, rs.instances, rs.nonInstanced);
    ImGui::Text("objects %d  models %d  loaded %d", streamer_.objectCount(), streamer_.modelCount(),
                streamer_.loadedTiles());
    ImGui::EndGroup();
}
} // namespace we
