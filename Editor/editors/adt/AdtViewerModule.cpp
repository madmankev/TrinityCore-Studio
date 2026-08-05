// AdtViewerModule — see AdtViewerModule.h.

#include "editors/adt/AdtViewerModule.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/EditorServices.h"
#include "adt/AdtWriter.h"
#include "clientdata/ClientData.h"
#include "data/LookupCache.h"
#include "ui/Widgets.h"
#include "ui/Enums.h"
#include "viewer/Picking.h"

namespace we
{
namespace
{
std::string Lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Decompose an assumed T*R*S matrix (no shear) into translation, rotation quaternion, and scale.
void DecomposeTRS(const glm::mat4& m, glm::vec3& t, glm::quat& r, glm::vec3& s)
{
    t = glm::vec3(m[3]);
    s.x = glm::length(glm::vec3(m[0]));
    s.y = glm::length(glm::vec3(m[1]));
    s.z = glm::length(glm::vec3(m[2]));
    const glm::mat3 rot(glm::vec3(m[0]) / (s.x > 1e-6f ? s.x : 1.0f),
                        glm::vec3(m[1]) / (s.y > 1e-6f ? s.y : 1.0f),
                        glm::vec3(m[2]) / (s.z > 1e-6f ? s.z : 1.0f));
    r = glm::normalize(glm::quat_cast(rot));
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
    DrawNpcInstancePanel();
    DrawGoInstancePanel();
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
    adtEdits_.SetMap(dir);   // switching maps discards pending (unsaved) edits; same map keeps them
    if (dir != undoMapDir_)   // an actual map change (not an option-toggle reload) invalidates undo
    {
        undo_.Clear();
        undoMapDir_ = dir;
    }
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
    ImGui::Checkbox("Edit", &editMode_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Click a model to select it, then drag the gizmo to move/rotate/scale it.");
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

    if (editMode_)
        DrawSelectionToolbar();

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
    const float aspect = (float)w / (float)h;

    // Single camera matrices for this frame, from the camera's CURRENT state (the end of last frame's
    // update). The gizmo, picking, and the 3D render ALL use this same view/proj, so the gizmo stays
    // locked to the model. camera_.Update() runs at the END of the frame (this frame's input -> next
    // frame's view), gated by the current-frame gizmo state — see the bottom of this function.
    const glm::vec3 eye = camera_.Eye();
    const glm::mat4 view = camera_.View();
    const glm::mat4 proj = camera_.Proj(aspect);

    // Stream around the camera's focus: the eye when flying, the orbit pivot otherwise.
    const glm::vec3 focus = (camera_.mode() == ViewportCamera::Mode::Fly) ? eye : camera_.center();

    // Assemble the live spawn-visibility filter from the toolbar state (used by picking + the layers).
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

    // Selection: hover-highlight + click-to-select, then seed the gizmo from the live object.
    // Highlights must be set BEFORE BuildFrame / the layers' Build consume them below. Picking is
    // suppressed while the gizmo is busy so grabbing a handle doesn't reselect — using LAST frame's
    // gizmo state, since this frame's RunGizmo runs after the scene render below.
    const bool gizmoBusyPrev = editMode_ && (gizmoUsingPrev_ || gizmoHoveredPrev_);
    if (editMode_)
    {
        UpdateHoverAndSelection(view, proj, p0, w, h, hovered, gizmoBusyPrev, focus, filter);
        ApplySelectionOutline();
        RefreshGizmoFromSelection();
    }
    else
    {
        streamer_.SetObjectHighlight(0, glm::vec4(0.0f));
        goLayer_.SetHighlight(0, glm::vec4(0.0f));
        npcLayer_.SetHighlight(0, glm::vec4(0.0f));
        streamer_.SetObjectOutline(0, glm::vec4(0.0f));
        goLayer_.SetOutline(0, glm::vec4(0.0f));
        npcLayer_.SetOutline(0, glm::vec4(0.0f));
    }

    // Right-click on terrain (no drag) opens the "add object here" popup.
    if (editMode_)
        HandleRightClickAdd(view, proj, p0, w, h, hovered, focus, filter);
    // Delete key removes the selected object (undoable).
    if (editMode_ && hovered && selKind_ != SelKind::None && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_Delete))
        DeleteSelection();

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
    // loading; harmless when disconnected (existing NPCs keep simulating locally). The
    // spawn-visibility filter was assembled above (before picking).
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

    // Transform gizmo: drawn ON TOP of the blitted image, on THIS window's draw list, with the SAME
    // view/proj the scene was rendered with (so it stays locked to the model). Runs before the camera
    // update below so its IsOver()/IsUsing() can veto the camera on the CURRENT frame.
    if (editMode_)
        RunGizmo(view, proj, p0, w, h);
    else
    {
        gizmoUsingPrev_ = false;
        gizmoHoveredPrev_ = false;
    }

    // Integrate the camera LAST (this frame's input -> next frame's view). Suppressed while the gizmo
    // is hovered or dragged, so grabbing a handle moves the object instead of orbiting the camera.
    const bool gizmoBusy = editMode_ && selKind_ != SelKind::None &&
                           (ImGuizmo::IsUsing() || ImGuizmo::IsOver());
    camera_.Update(hovered && !gizmoBusy, active && !gizmoBusy, io.DeltaTime);

    if (editMode_)
    {
        DrawAddObjectPopup();
        DrawObjectContextPopup();
    }

    if (showStats_)
        DrawStatsOverlay(p0, io);
    ImGui::End();
}

void AdtViewerModule::ClearSelection()
{
    selKind_ = SelKind::None;
    selUid_ = 0;
    selGuid_ = 0;
    selEntry_ = 0;
    selLabel_.clear();
    gizmoUsingPrev_ = false;
}

// Docked panel that reflects the selected NPC's full `creature` row into an editable working copy.
// The row is loaded lazily whenever the selection changes; edits are held locally and persisted only
// on Save (one UPDATE + one undo step), so a click-through doesn't hammer the DB.
void AdtViewerModule::DrawNpcInstancePanel()
{
    if (!ImGui::Begin("NPC Instance"))
    {
        ImGui::End();
        return;
    }
    if (selKind_ != SelKind::Npc)
    {
        npcEditGuid_ = 0;   // force a reload for the next NPC selected
        npcEditDirty_ = false;
        ImGui::TextWrapped("Select an NPC in the viewer to edit its spawn instance.");
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project DB to edit spawn instances.");
        ImGui::End();
        return;
    }

    // Lazy-load the full row when the selection changes.
    if (selGuid_ != npcEditGuid_)
    {
        CreatureSpawn loaded;
        const DbError e = spawnRepo_.LoadCreatureSpawn(*svc_->activeDb, selGuid_, loaded);
        if (!e.ok)
        {
            ImGui::TextWrapped("Failed to load spawn %u: %s", selGuid_, e.message.c_str());
            ImGui::End();
            return;
        }
        npcEdit_ = loaded;
        npcEditOrig_ = loaded;
        npcEditGuid_ = selGuid_;
        npcEditDirty_ = false;
        npcEditStatus_.clear();
    }

    const std::string title =
        svc_->lookups ? svc_->lookups->LabelCreature(selEntry_) : ("entry " + std::to_string(selEntry_));
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextDisabled("guid %u  (map %u)", npcEdit_.guid, static_cast<unsigned>(npcEdit_.map));
    ImGui::Separator();

    ImGui::BeginDisabled(!npcEditDirty_);
    if (ImGui::Button("Save"))
        SaveNpcEdit();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        npcEdit_ = npcEditOrig_;
        npcEditDirty_ = false;
    }
    ImGui::EndDisabled();
    if (npcEditDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "unsaved");
    }
    if (!npcEditStatus_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", npcEditStatus_.c_str());
    }

    CreatureSpawn& s = npcEdit_;
    bool ch = false;

    if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("npcplace"))
        {
            FieldRow("Position X"); ch |= InputFloatField("##px", s.x);
            FieldRow("Position Y"); ch |= InputFloatField("##py", s.y);
            FieldRow("Position Z"); ch |= InputFloatField("##pz", s.z);
            FieldRow("Orientation"); ch |= InputFloatField("##po", s.o);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Movement", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("npcmove"))
        {
            uint32_t mt = s.movementType;
            FieldRow("Movement type");
            if (EnumCombo("##mt", mt, MovementTypeValues())) { s.movementType = static_cast<uint8_t>(mt); ch = true; }
            FieldRow("Wander distance"); ch |= InputFloatField("##wd", s.wanderDistance);
            FieldRow("Current waypoint"); ch |= InputU32("##cwp", s.currentWaypoint);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("npcspawn"))
        {
            FieldRow("Respawn (secs)"); ch |= InputU32("##sts", s.spawnTimeSecs);
            uint32_t sm = s.spawnMask;
            FieldRow("Spawn mask");
            if (InputU32("##sm", sm)) { s.spawnMask = static_cast<uint8_t>(sm); ch = true; }
            FieldRow("Phase mask"); ch |= InputU32("##pm", s.phaseMask);
            FieldRow("Cur health"); ch |= InputU32("##chp", s.curHealth);
            FieldRow("Cur mana"); ch |= InputU32("##cmp", s.curMana);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Appearance / Equipment"))
        if (BeginFieldTable("npcapp"))
        {
            FieldRow("Model id (0 = template)"); ch |= InputU32("##mid", s.modelId);
            int32_t eq = s.equipmentId;
            FieldRow("Equipment id");
            if (InputI32("##eq", eq)) { s.equipmentId = static_cast<int8_t>(eq); ch = true; }
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Flag overrides"))
    {
        ch |= FlagCheckboxGrid("NPC flags (npcflag)", s.npcflag, CreatureNpcFlagBits(), 2);
        ch |= FlagCheckboxGrid("Unit flags (unit_flags)", s.unitFlags, UnitFlagBits(), 2);
        if (BeginFieldTable("npcdyn"))
        {
            FieldRow("Dynamic flags"); ch |= InputU32("##dyn", s.dynamicFlags);
            EndFieldTable();
        }
    }

    if (ImGui::CollapsingHeader("Script"))
        if (BeginFieldTable("npcscript"))
        {
            FieldRow("ScriptName"); ch |= InputTextString("##sn", s.scriptName);
            FieldRow("StringId"); ch |= InputTextString("##sid", s.stringId);
            EndFieldTable();
        }

    if (ch)
        npcEditDirty_ = true;

    ImGui::End();
}

// Persist the working copy (one UPDATE), sync the viewer's live render/sim state, and record one
// undo step. Only reachable while npcEditDirty_ (the Save button is disabled otherwise).
void AdtViewerModule::SaveNpcEdit()
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || npcEditGuid_ == 0)
        return;
    const CreatureSpawn before = npcEditOrig_;
    const CreatureSpawn after = npcEdit_;
    const DbError e = spawnRepo_.UpdateCreatureSpawn(*svc_->activeDb, after);
    if (!e.ok)
    {
        npcEditStatus_ = "Save failed: " + e.message;
        return;
    }
    ApplyRenderFromSpawn(after);
    npcEditOrig_ = after;
    npcEditDirty_ = false;
    npcEditStatus_ = "Saved guid " + std::to_string(after.guid);
    if (svc_->setStatus)
        svc_->setStatus(npcEditStatus_);
    undo_.Push(MakeCommand([this, before]() { ApplyAndPersistNpcSpawn(before); },
                           [this, after]() { ApplyAndPersistNpcSpawn(after); }, "Edit NPC spawn"));
}

// Shared undo/redo body: re-persist a captured spawn row + re-sync the viewer, and refresh the panel
// copies if that guid is the one on screen. No-ops safely if the guid is gone (map switched).
void AdtViewerModule::ApplyAndPersistNpcSpawn(const CreatureSpawn& s)
{
    if (svc_ && svc_->connected && svc_->activeDb)
    {
        const DbError e = spawnRepo_.UpdateCreatureSpawn(*svc_->activeDb, s);
        npcEditStatus_ = e.ok ? ("Saved guid " + std::to_string(s.guid))
                              : ("Save failed: " + e.message);
        if (e.ok && svc_->setStatus)
            svc_->setStatus(npcEditStatus_);
    }
    ApplyRenderFromSpawn(s);
    if (npcEditGuid_ == s.guid)
    {
        npcEdit_ = s;
        npcEditOrig_ = s;
        npcEditDirty_ = false;
    }
}

// Push the edit-affected fields into the NPC layer's in-memory spawn so the viewer updates live.
void AdtViewerModule::ApplyRenderFromSpawn(const CreatureSpawn& s)
{
    const MapSpawn* cur = npcLayer_.FindSpawn(s.guid);
    if (!cur)
        return;
    MapSpawn f = *cur;   // preserve entry/scale/speed/pathId/event/pool fields the panel doesn't edit
    f.x = s.x; f.y = s.y; f.z = s.z; f.o = s.o;
    f.movementType = s.movementType;
    f.wanderDistance = s.wanderDistance;
    f.phaseMask = s.phaseMask;
    f.spawnMask = s.spawnMask;
    // Resolved model: a nonzero modelid override wins. Reverting an override to 0 keeps the current
    // display until the map reloads (recomputing the template fallback would need a requery).
    if (s.modelId != 0)
        f.displayId = s.modelId;
    npcLayer_.UpdateSpawnEditable(s.guid, f);
}

// Keep the panel's working copy consistent when the gizmo moves the same NPC (so a drag doesn't leave
// stale coordinates in the panel, and isn't mistaken for an unsaved panel edit).
void AdtViewerModule::SyncNpcPanelTransform(uint32_t guid, float x, float y, float z, float o)
{
    if (npcEditGuid_ != guid)
        return;
    npcEdit_.x = x; npcEdit_.y = y; npcEdit_.z = z; npcEdit_.o = o;
    npcEditOrig_.x = x; npcEditOrig_.y = y; npcEditOrig_.z = z; npcEditOrig_.o = o;
}

// Docked panel that reflects the selected GameObject's full `gameobject` row (same working-copy +
// Save/Revert + undo model as the NPC panel).
void AdtViewerModule::DrawGoInstancePanel()
{
    if (!ImGui::Begin("GameObject Instance"))
    {
        ImGui::End();
        return;
    }
    if (selKind_ != SelKind::GameObject)
    {
        goEditGuid_ = 0;
        goEditDirty_ = false;
        ImGui::TextWrapped("Select a GameObject in the viewer to edit its spawn instance.");
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project DB to edit spawn instances.");
        ImGui::End();
        return;
    }

    if (selGuid_ != goEditGuid_)
    {
        GameObjectSpawn loaded;
        const DbError e = spawnRepo_.LoadGameObjectSpawn(*svc_->activeDb, selGuid_, loaded);
        if (!e.ok)
        {
            ImGui::TextWrapped("Failed to load spawn %u: %s", selGuid_, e.message.c_str());
            ImGui::End();
            return;
        }
        goEdit_ = loaded;
        goEditOrig_ = loaded;
        goEditGuid_ = selGuid_;
        goEditDirty_ = false;
        goEditStatus_.clear();
    }

    const std::string title = svc_->lookups ? svc_->lookups->LabelGameObject(selEntry_)
                                            : ("entry " + std::to_string(selEntry_));
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextDisabled("guid %u  (map %u)", goEdit_.guid, static_cast<unsigned>(goEdit_.map));
    ImGui::Separator();

    ImGui::BeginDisabled(!goEditDirty_);
    if (ImGui::Button("Save"))
        SaveGoEdit();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        goEdit_ = goEditOrig_;
        goEditDirty_ = false;
    }
    ImGui::EndDisabled();
    if (goEditDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "unsaved");
    }
    if (!goEditStatus_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", goEditStatus_.c_str());
    }

    GameObjectSpawn& s = goEdit_;
    bool ch = false;

    if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("goplace"))
        {
            FieldRow("Position X"); ch |= InputFloatField("##gpx", s.x);
            FieldRow("Position Y"); ch |= InputFloatField("##gpy", s.y);
            FieldRow("Position Z"); ch |= InputFloatField("##gpz", s.z);
            FieldRow("Orientation (yaw)",
                     "Editing yaw re-derives rotation0..3 on save (upright). Use the gizmo for a full "
                     "3D tilt.");
            ch |= InputFloatField("##gpo", s.o);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Spawn", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("gospawn"))
        {
            FieldRow("Respawn (secs)", "Negative = despawned by default / manual spawn.");
            ch |= InputI32("##gsts", s.spawnTimeSecs);
            FieldRow("Spawn mask"); ch |= InputU8("##gsm", s.spawnMask);
            FieldRow("Phase mask"); ch |= InputU32("##gpm", s.phaseMask);
            FieldRow("Anim progress"); ch |= InputU8("##gap", s.animProgress);
            FieldRow("State", "GOState: 0 active, 1 ready (closed/idle).");
            ch |= InputU8("##gst", s.state);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Script"))
        if (BeginFieldTable("goscript"))
        {
            FieldRow("ScriptName"); ch |= InputTextString("##gsn", s.scriptName);
            FieldRow("StringId"); ch |= InputTextString("##gsid", s.stringId);
            EndFieldTable();
        }

    if (ch)
        goEditDirty_ = true;

    ImGui::End();
}

void AdtViewerModule::SaveGoEdit()
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || goEditGuid_ == 0)
        return;
    // If the yaw was edited, re-derive the rotation0..3 quaternion (upright, Z axis) so the stored
    // rotation the client actually reads stays consistent. Untouched yaw preserves the exact loaded
    // quaternion (so a 3D tilt set via the gizmo survives a non-transform edit).
    if (goEdit_.o != goEditOrig_.o)
    {
        const float h = goEdit_.o * 0.5f;
        goEdit_.rotation = {0.0f, 0.0f, std::sin(h), std::cos(h)};
    }
    const GameObjectSpawn before = goEditOrig_;
    const GameObjectSpawn after = goEdit_;
    const DbError e = spawnRepo_.UpdateGameObjectSpawn(*svc_->activeDb, after);
    if (!e.ok)
    {
        goEditStatus_ = "Save failed: " + e.message;
        return;
    }
    ApplyRenderFromGoSpawn(after);
    goEditOrig_ = after;
    goEditDirty_ = false;
    goEditStatus_ = "Saved guid " + std::to_string(after.guid);
    if (svc_->setStatus)
        svc_->setStatus(goEditStatus_);
    undo_.Push(MakeCommand([this, before]() { ApplyAndPersistGoSpawn(before); },
                           [this, after]() { ApplyAndPersistGoSpawn(after); }, "Edit GameObject spawn"));
}

void AdtViewerModule::ApplyAndPersistGoSpawn(const GameObjectSpawn& s)
{
    if (svc_ && svc_->connected && svc_->activeDb)
    {
        const DbError e = spawnRepo_.UpdateGameObjectSpawn(*svc_->activeDb, s);
        goEditStatus_ = e.ok ? ("Saved guid " + std::to_string(s.guid))
                             : ("Save failed: " + e.message);
        if (e.ok && svc_->setStatus)
            svc_->setStatus(goEditStatus_);
    }
    ApplyRenderFromGoSpawn(s);
    if (goEditGuid_ == s.guid)
    {
        goEdit_ = s;
        goEditOrig_ = s;
        goEditDirty_ = false;
    }
}

// Push the edit-affected fields into the GO layer's in-memory spawn so the viewer updates live.
// GameObjects are static (no simulation) and carry no per-spawn model override, so this is a direct
// field copy — no re-seeding or model re-resolution needed.
void AdtViewerModule::ApplyRenderFromGoSpawn(const GameObjectSpawn& s)
{
    MapGameObject* g = goLayer_.FindSpawn(s.guid);
    if (!g)
        return;
    g->x = s.x; g->y = s.y; g->z = s.z; g->o = s.o;
    g->rot[0] = s.rotation[0]; g->rot[1] = s.rotation[1];
    g->rot[2] = s.rotation[2]; g->rot[3] = s.rotation[3];
    g->phaseMask = s.phaseMask;
    g->spawnMask = s.spawnMask;
    g->state = s.state;
}

// Keep the panel's working copy consistent when the gizmo moves the same GameObject.
void AdtViewerModule::SyncGoPanelTransform(uint32_t guid)
{
    if (goEditGuid_ != guid)
        return;
    const MapGameObject* g = goLayer_.FindSpawn(guid);
    if (!g)
        return;
    auto set = [&](GameObjectSpawn& s) {
        s.x = g->x; s.y = g->y; s.z = g->z; s.o = g->o;
        s.rotation = {g->rot[0], g->rot[1], g->rot[2], g->rot[3]};
    };
    set(goEdit_);
    set(goEditOrig_);
}

// A one-line toolbar shown while editing: gizmo op buttons, a Local/World toggle, the selection
// label, Deselect, and the last save status.
void AdtViewerModule::DrawSelectionToolbar()
{
    // Batched save of queued ADT placement edits — shown whenever there are unsaved edits, with or
    // without a current selection.
    if (!adtEdits_.empty())
    {
        const std::string label = "Save ADT edits (" + std::to_string(adtEdits_.pendingCount()) + ")";
        if (ImGui::Button(label.c_str()) && svc_ && svc_->clientData)
        {
            std::string status;
            adtEdits_.Flush(*svc_->clientData, svc_->editRoot, status);
            saveStatus_ = status;
            if (svc_->setStatus)
                svc_->setStatus(status);
        }
        ImGui::SameLine();
    }

    if (selKind_ == SelKind::None)
    {
        ImGui::TextDisabled("Edit: click a model to select it, then drag the gizmo.");
        return;
    }
    auto opButton = [&](const char* label, ImGuizmo::OPERATION op) {
        const bool on = (gizmoOp_ == op);
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) gizmoOp_ = op;
        if (on) ImGui::PopStyleColor();
    };
    opButton("Move", ImGuizmo::TRANSLATE);
    ImGui::SameLine();
    opButton("Rotate", ImGuizmo::ROTATE);
    if (selKind_ != SelKind::Npc)   // creature scale is per-template, not per-spawn
    {
        ImGui::SameLine();
        opButton("Scale", ImGuizmo::SCALE);
    }
    ImGui::SameLine();
    bool world = (gizmoMode_ == ImGuizmo::WORLD);
    if (ImGui::Checkbox("World", &world)) gizmoMode_ = world ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    ImGui::TextUnformatted(selLabel_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Deselect")) ClearSelection();
    ImGui::SameLine();
    if (ImGui::Button("Delete")) DeleteSelection();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Delete the selected object (Del). Undo with Ctrl+Z.");
    if (!saveStatus_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", saveStatus_.c_str());
    }
}

// Seed the gizmo matrix + outline bounds from the currently-selected live object each frame (so it
// tracks NPC movement / a reloaded tile). Skips the matrix while a drag is in progress.
void AdtViewerModule::RefreshGizmoFromSelection()
{
    if (selKind_ == SelKind::None)
        return;
    const glm::vec3 origin = streamer_.origin();
    if (!gizmoUsingPrev_)
    {
        bool ok = false;
        if (selKind_ == SelKind::Doodad)
            ok = streamer_.ObjectTransform(selUid_, gizmoMatrix_);
        else if (selKind_ == SelKind::GameObject)
        {
            if (MapGameObject* g = goLayer_.FindSpawn(selGuid_))
            {
                gizmoMatrix_ = goLayer_.SpawnMatrix(*g, origin);
                ok = true;
            }
        }
        else if (selKind_ == SelKind::Npc)
            ok = npcLayer_.HomeMatrix(selGuid_, origin, gizmoMatrix_);
        if (!ok)
        {
            ClearSelection();
            return;
        }
    }
}

// Ray-pick under the mouse: tint the hovered object (all three sources) and, on a click, select it.
void AdtViewerModule::UpdateHoverAndSelection(const glm::mat4& view, const glm::mat4& proj,
                                              const ImVec2& p0, int w, int h, bool viewportHovered,
                                              bool gizmoBusy, const glm::vec3& focus,
                                              const SpawnFilter& filter)
{
    // Clear last frame's hover tint on every source.
    streamer_.SetObjectHighlight(0, glm::vec4(0.0f));
    goLayer_.SetHighlight(0, glm::vec4(0.0f));
    npcLayer_.SetHighlight(0, glm::vec4(0.0f));

    if (!viewportHovered || gizmoBusy)
        return;

    ImGuiIO& io = ImGui::GetIO();
    const float px = io.MousePos.x - p0.x;
    const float py = io.MousePos.y - p0.y;
    if (px < 0.0f || py < 0.0f || px >= (float)w || py >= (float)h)
        return;

    const PickRay ray = MakePickRay(view, proj, px, py, (float)w, (float)h);
    const glm::vec3 origin = streamer_.origin();

    float td = -1.0f, tg = -1.0f, tn = -1.0f;
    const uint64_t duid = streamer_.PickObject(ray.origin, ray.dir, td);
    const uint32_t gg = showGos_ ? goLayer_.Pick(ray.origin, ray.dir, focus, origin, goCullDist_, filter, tg) : 0;
    const uint32_t ng = showNpcs_ ? npcLayer_.Pick(ray.origin, ray.dir, focus, origin, npcCullDist_, filter, tn) : 0;

    SelKind hitKind = SelKind::None;
    float bestT = 1e30f;
    if (duid && td >= 0.0f && td < bestT) { bestT = td; hitKind = SelKind::Doodad; }
    if (gg && tg >= 0.0f && tg < bestT)   { bestT = tg; hitKind = SelKind::GameObject; }
    if (ng && tn >= 0.0f && tn < bestT)   { bestT = tn; hitKind = SelKind::Npc; }

    const glm::vec4 tint(0.30f, 0.55f, 1.0f, 0.22f);   // subtle additive blue glow
    if (hitKind == SelKind::Doodad)          streamer_.SetObjectHighlight(duid, tint);
    else if (hitKind == SelKind::GameObject) goLayer_.SetHighlight(gg, tint);
    else if (hitKind == SelKind::Npc)        npcLayer_.SetHighlight(ng, tint);

    // A left click with negligible drag selects (so camera left-drag still orbits/looks).
    const ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
    const bool click = ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                       (drag.x * drag.x + drag.y * drag.y < 36.0f);
    if (!click)
        return;

    if (hitKind == SelKind::None)
    {
        ClearSelection();
        return;
    }
    SelectObject(hitKind, duid, gg, ng);
}

// Make (kind, ids) the current selection: clears any prior selection and fills the sel* fields +
// label. Shared by left-click select and right-click context. Only the id matching `kind` is used.
void AdtViewerModule::SelectObject(SelKind kind, uint64_t duid, uint32_t gg, uint32_t ng)
{
    ClearSelection();
    selKind_ = kind;
    saveStatus_.clear();
    if (kind == SelKind::Doodad)
    {
        selUid_ = duid;
        std::string path = streamer_.ObjectModelPath(duid);
        const size_t slash = path.find_last_of("/\\");
        selLabel_ = "Doodad: " + (slash == std::string::npos ? path : path.substr(slash + 1));
    }
    else if (kind == SelKind::GameObject)
    {
        selGuid_ = gg;
        if (MapGameObject* g = goLayer_.FindSpawn(gg)) selEntry_ = g->entry;
        selLabel_ = "GameObject guid " + std::to_string(gg) + " (entry " + std::to_string(selEntry_) + ")";
    }
    else
    {
        selGuid_ = ng;
        if (const MapSpawn* s = npcLayer_.FindSpawn(ng)) selEntry_ = s->entry;
        selLabel_ = "Creature guid " + std::to_string(ng) + " (entry " + std::to_string(selEntry_) + ")";
    }
}

// Run ImGuizmo over the viewport for the selected object; apply the edit live and save DB spawns on
// release.
void AdtViewerModule::RunGizmo(const glm::mat4& view, const glm::mat4& proj, const ImVec2& p0,
                               int w, int h)
{
    if (selKind_ == SelKind::None)
    {
        gizmoUsingPrev_ = false;
        gizmoHoveredPrev_ = false;
        return;
    }
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    // Draw on THIS window's draw list (not the foreground list). ImGuizmo gates all handle
    // hit-testing on IsHoveringWindow(), which resolves the draw list's owner window by name — the
    // foreground list ("##Foreground") matches no window, so the gate fails and the gizmo can never
    // be grabbed. The window list's owner is "ADT Viewer", so hover works. RunGizmo is called AFTER
    // the 3D image is blitted (same window draw list), so the gizmo still renders on top.
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(p0.x, p0.y, (float)w, (float)h);

    // ImGuizmo assumes a GL-style (y-up) projection; undo the camera's Vulkan Y-flip.
    glm::mat4 projG = proj;
    projG[1][1] *= -1.0f;

    ImGuizmo::OPERATION op = gizmoOp_;
    if (selKind_ == SelKind::Npc && op == ImGuizmo::SCALE)
        op = ImGuizmo::TRANSLATE;   // creature scale isn't a per-spawn column

    ImGuizmo::Manipulate(&view[0][0], &projG[0][0], op, gizmoMode_, &gizmoMatrix_[0][0]);

    const bool using_ = ImGuizmo::IsUsing();
    if (using_ && !gizmoUsingPrev_)   // drag just started -> snapshot the pre-edit state
        dragBefore_ = CaptureSelection();
    if (using_)
        ApplyGizmoEdit();
    if (gizmoUsingPrev_ && !using_)   // released this frame -> persist + record an undo step
    {
        CommitSelectionToDb();
        const AdtXform after = CaptureSelection();
        if (!SameXform(dragBefore_, after))
        {
            const AdtXform before = dragBefore_;
            undo_.Push(MakeCommand([this, before]() { ApplyAndPersist(before); },
                                   [this, after]() { ApplyAndPersist(after); }, "Move object"));
        }
    }
    gizmoUsingPrev_ = using_;
    gizmoHoveredPrev_ = ImGuizmo::IsOver();
}

// Push the gizmo's edited matrix back into the selected object (live, every drag frame).
void AdtViewerModule::ApplyGizmoEdit()
{
    const glm::vec3 origin = streamer_.origin();
    if (selKind_ == SelKind::Doodad)
    {
        if (!streamer_.SetObjectTransform(selUid_, gizmoMatrix_))
            ClearSelection();
        return;
    }
    glm::vec3 t, s;
    glm::quat q;
    DecomposeTRS(gizmoMatrix_, t, q, s);
    const float yaw = std::atan2(2.0f * (q.w * q.z + q.x * q.y), 1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    if (selKind_ == SelKind::GameObject)
    {
        if (MapGameObject* g = goLayer_.FindSpawn(selGuid_))
        {
            g->x = t.x + origin.x;
            g->y = t.y + origin.y;
            g->z = t.z;
            g->rot[0] = q.x; g->rot[1] = q.y; g->rot[2] = q.z; g->rot[3] = q.w;
            g->o = yaw;
            if (s.x > 0.0f) g->size = s.x;
        }
        else
            ClearSelection();
    }
    else if (selKind_ == SelKind::Npc)
    {
        if (!npcLayer_.SetSpawnHome(selGuid_, t.x + origin.x, t.y + origin.y, t.z, yaw))
            ClearSelection();
    }
}

// Persist the moved DB spawn (called once, on gizmo release). Doodads have no DB row.
void AdtViewerModule::CommitSelectionToDb()
{
    if (selKind_ == SelKind::Doodad)
    {
        // ADT placements aren't DB rows — queue the move for the batched "Save ADT edits" flush
        // (which writes the edited tile to the project's client-data overlay).
        glm::mat4 xform(1.0f);
        bool isWmo = false;
        std::vector<std::pair<int, int>> tiles;
        if (streamer_.ObjectSaveInfo(selUid_, xform, isWmo, tiles) && !tiles.empty())
        {
            const adt::RawPlacement raw = adt::PlacementToRaw(xform, streamer_.origin());
            adtEdits_.RecordUpsert(selUid_, isWmo, raw, streamer_.ObjectModelPath(selUid_), tiles);
            saveStatus_ = "Move queued (" + std::to_string(adtEdits_.pendingCount()) +
                          " pending) — click 'Save ADT edits'.";
        }
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Moved — connect a project DB to save.";
        return;
    }
    DbError e;
    if (selKind_ == SelKind::GameObject)
    {
        MapGameObject* g = goLayer_.FindSpawn(selGuid_);
        if (!g)
            return;
        e = spawnRepo_.UpdateGameObjectTransform(*svc_->activeDb, g->guid, g->x, g->y, g->z, g->o, g->rot);
        saveStatus_ = e.ok ? ("Saved gameobject guid " + std::to_string(g->guid))
                           : ("Save failed: " + e.message);
        if (e.ok)
            SyncGoPanelTransform(g->guid);
    }
    else if (selKind_ == SelKind::Npc)
    {
        const MapSpawn* s = npcLayer_.FindSpawn(selGuid_);
        if (!s)
            return;
        e = spawnRepo_.UpdateCreatureTransform(*svc_->activeDb, s->guid, s->x, s->y, s->z, s->o);
        saveStatus_ = e.ok ? ("Saved creature guid " + std::to_string(s->guid))
                           : ("Save failed: " + e.message);
        if (e.ok)
            SyncNpcPanelTransform(s->guid, s->x, s->y, s->z, s->o);
    }
    if (e.ok && svc_->setStatus)
        svc_->setStatus(saveStatus_);
}

// Read the currently-selected object's live transform into an AdtXform (the undo/redo unit).
// Returns kind==None if nothing is selected or the object is gone.
AdtViewerModule::AdtXform AdtViewerModule::CaptureSelection()
{
    AdtXform s;
    s.kind = selKind_;
    switch (selKind_)
    {
    case SelKind::Doodad:
        s.uid = selUid_;
        if (!streamer_.ObjectSaveInfo(selUid_, s.local, s.isWmo, s.tiles))
            s.kind = SelKind::None;
        break;
    case SelKind::GameObject:
        s.guid = selGuid_;
        if (const MapGameObject* g = goLayer_.FindSpawn(selGuid_))
        {
            s.x = g->x; s.y = g->y; s.z = g->z; s.o = g->o;
            s.rot[0] = g->rot[0]; s.rot[1] = g->rot[1]; s.rot[2] = g->rot[2]; s.rot[3] = g->rot[3];
            s.size = g->size;
        }
        else
            s.kind = SelKind::None;
        break;
    case SelKind::Npc:
        s.guid = selGuid_;
        if (const MapSpawn* n = npcLayer_.FindSpawn(selGuid_))
        {
            s.x = n->x; s.y = n->y; s.z = n->z; s.o = n->o;
        }
        else
            s.kind = SelKind::None;
        break;
    default:
        break;
    }
    return s;
}

// Restore a captured transform to its object (in-memory) AND re-persist it. This is the shared body
// of both undo and redo: undo applies the pre-edit state, redo the post-edit state. No-ops safely if
// the object is gone (evicted / map switched — though the stack is cleared on map change).
void AdtViewerModule::ApplyAndPersist(const AdtXform& s)
{
    if (s.kind == SelKind::Doodad)
    {
        // Restore the streamer transform, then re-queue the placement so a later "Save ADT edits"
        // flush writes this state to the overlay (matches CommitSelectionToDb's doodad path).
        if (streamer_.SetObjectTransform(s.uid, s.local) && !s.tiles.empty())
        {
            const adt::RawPlacement raw = adt::PlacementToRaw(s.local, streamer_.origin());
            adtEdits_.RecordUpsert(s.uid, s.isWmo, raw, streamer_.ObjectModelPath(s.uid), s.tiles);
            saveStatus_ = "Move queued (" + std::to_string(adtEdits_.pendingCount()) +
                          " pending) — click 'Save ADT edits'.";
        }
        return;
    }
    if (s.kind == SelKind::GameObject)
    {
        if (MapGameObject* g = goLayer_.FindSpawn(s.guid))
        {
            g->x = s.x; g->y = s.y; g->z = s.z; g->o = s.o;
            g->rot[0] = s.rot[0]; g->rot[1] = s.rot[1]; g->rot[2] = s.rot[2]; g->rot[3] = s.rot[3];
            if (s.size > 0.0f)
                g->size = s.size;
        }
        else
            return;
        if (svc_ && svc_->connected && svc_->activeDb)
        {
            const DbError e = spawnRepo_.UpdateGameObjectTransform(*svc_->activeDb, s.guid, s.x, s.y,
                                                                   s.z, s.o, s.rot);
            saveStatus_ = e.ok ? ("Saved gameobject guid " + std::to_string(s.guid))
                               : ("Save failed: " + e.message);
            if (e.ok && svc_->setStatus)
                svc_->setStatus(saveStatus_);
        }
        SyncGoPanelTransform(s.guid);
        return;
    }
    if (s.kind == SelKind::Npc)
    {
        if (!npcLayer_.SetSpawnHome(s.guid, s.x, s.y, s.z, s.o))
            return;
        if (svc_ && svc_->connected && svc_->activeDb)
        {
            const DbError e = spawnRepo_.UpdateCreatureTransform(*svc_->activeDb, s.guid, s.x, s.y,
                                                                 s.z, s.o);
            saveStatus_ = e.ok ? ("Saved creature guid " + std::to_string(s.guid))
                               : ("Save failed: " + e.message);
            if (e.ok && svc_->setStatus)
                svc_->setStatus(saveStatus_);
        }
        SyncNpcPanelTransform(s.guid, s.x, s.y, s.z, s.o);
        return;
    }
}

// True if two captures represent the same transform (so a drag that ended where it started records
// no undo step). Compares only the fields relevant to the kind.
bool AdtViewerModule::SameXform(const AdtXform& a, const AdtXform& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind)
    {
    case SelKind::Doodad:
        return a.uid == b.uid && a.local == b.local;
    case SelKind::GameObject:
        return a.guid == b.guid && a.x == b.x && a.y == b.y && a.z == b.z && a.o == b.o &&
               a.rot[0] == b.rot[0] && a.rot[1] == b.rot[1] && a.rot[2] == b.rot[2] &&
               a.rot[3] == b.rot[3] && a.size == b.size;
    case SelKind::Npc:
        return a.guid == b.guid && a.x == b.x && a.y == b.y && a.z == b.z && a.o == b.o;
    default:
        return true;   // None == None: nothing to record
    }
}

// Full descriptor of the current selection — the create/destroy unit for delete + its undo.
AdtViewerModule::AdtObjectDesc AdtViewerModule::CaptureSelectedDesc()
{
    AdtObjectDesc d;
    d.kind = selKind_;
    if (selKind_ == SelKind::Doodad)
    {
        d.uid = selUid_;
        d.path = streamer_.ObjectModelPath(selUid_);
        if (!streamer_.ObjectSaveInfo(selUid_, d.local, d.isWmo, d.tiles))
            d.kind = SelKind::None;
        else if (!d.tiles.empty()) { d.tileX = d.tiles.front().first; d.tileY = d.tiles.front().second; }
    }
    else if (selKind_ == SelKind::GameObject)
    {
        if (const MapGameObject* g = goLayer_.FindSpawn(selGuid_))
        {
            d.guid = g->guid; d.entry = g->entry; d.displayId = g->displayId;
            d.x = g->x; d.y = g->y; d.z = g->z; d.o = g->o;
            d.rot[0] = g->rot[0]; d.rot[1] = g->rot[1]; d.rot[2] = g->rot[2]; d.rot[3] = g->rot[3];
            d.size = g->size;
        }
        else d.kind = SelKind::None;
    }
    else if (selKind_ == SelKind::Npc)
    {
        if (const MapSpawn* n = npcLayer_.FindSpawn(selGuid_))
        {
            d.guid = n->guid; d.entry = n->entry; d.displayId = n->displayId;
            d.x = n->x; d.y = n->y; d.z = n->z; d.o = n->o; d.size = n->scale;
        }
        else d.kind = SelKind::None;
    }
    return d;
}

// Materialize an object (DB insert for spawns with the ORIGINAL guid; ADT add for placements) and
// render it live. The inverse of DestroyObject.
void AdtViewerModule::CreateObject(const AdtObjectDesc& d)
{
    const std::vector<std::pair<int, int>> tiles =
        d.tiles.empty() ? std::vector<std::pair<int, int>>{{d.tileX, d.tileY}} : d.tiles;
    if (d.kind == SelKind::Doodad)
    {
        glm::mat4 local = d.local;
        if (streamer_.AddObject(d.path, d.isWmo, local, d.tileX, d.tileY, d.uid))
            adtEdits_.RecordUpsert(d.uid, d.isWmo, adt::PlacementToRaw(local, streamer_.origin()),
                                   d.path, tiles);
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Connect a DB to re-create the spawn.";
        return;
    }
    if (d.kind == SelKind::GameObject)
    {
        uint32_t guid = d.guid, disp = 0;
        spawnRepo_.InsertGameObjectSpawn(*svc_->activeDb, currentMapId_, d.entry, d.x, d.y, d.z, d.o,
                                         d.rot, guid, disp);
        MapGameObject g;
        g.guid = d.guid; g.entry = d.entry; g.x = d.x; g.y = d.y; g.z = d.z; g.o = d.o;
        g.rot[0] = d.rot[0]; g.rot[1] = d.rot[1]; g.rot[2] = d.rot[2]; g.rot[3] = d.rot[3];
        g.displayId = d.displayId ? d.displayId : disp;
        g.size = d.size > 0.0f ? d.size : 1.0f; g.phaseMask = 1; g.spawnMask = 1;
        goLayer_.AddGameObject(g);
    }
    else if (d.kind == SelKind::Npc)
    {
        uint32_t guid = d.guid, disp = 0;
        spawnRepo_.InsertCreatureSpawn(*svc_->activeDb, currentMapId_, d.entry, d.x, d.y, d.z, d.o,
                                       guid, disp);
        MapSpawn s;
        s.guid = d.guid; s.entry = d.entry; s.x = d.x; s.y = d.y; s.z = d.z; s.o = d.o;
        s.displayId = d.displayId ? d.displayId : disp;
        s.scale = d.size > 0.0f ? d.size : 1.0f; s.phaseMask = 1; s.spawnMask = 1;
        npcLayer_.AddSpawn(s);
    }
}

// Remove an object (DB delete for spawns; ADT remove for placements) and from the viewport. The
// inverse of CreateObject.
void AdtViewerModule::DestroyObject(const AdtObjectDesc& d)
{
    if (d.kind == SelKind::Doodad)
    {
        streamer_.RemoveObject(d.uid);
        const std::vector<std::pair<int, int>> tiles =
            d.tiles.empty() ? std::vector<std::pair<int, int>>{{d.tileX, d.tileY}} : d.tiles;
        adtEdits_.RecordRemove(d.uid, d.isWmo, tiles);
    }
    else if (d.kind == SelKind::GameObject)
    {
        goLayer_.RemoveGameObject(d.guid);
        if (svc_ && svc_->connected && svc_->activeDb)
            spawnRepo_.DeleteGameObjectSpawn(*svc_->activeDb, d.guid);
    }
    else if (d.kind == SelKind::Npc)
    {
        npcLayer_.RemoveSpawn(d.guid);
        if (svc_ && svc_->connected && svc_->activeDb)
            spawnRepo_.DeleteCreatureSpawn(*svc_->activeDb, d.guid);
    }
    // Drop the selection if it referenced the destroyed object.
    const bool wasDoodad = d.kind == SelKind::Doodad && selKind_ == SelKind::Doodad && selUid_ == d.uid;
    const bool wasSpawn = (d.kind == SelKind::GameObject || d.kind == SelKind::Npc) &&
                          selKind_ == d.kind && selGuid_ == d.guid;
    if (wasDoodad || wasSpawn)
        ClearSelection();
}

void AdtViewerModule::PushCreateUndo(const AdtObjectDesc& d)
{
    if (d.kind == SelKind::None)
        return;
    undo_.Push(MakeCommand([this, d]() { DestroyObject(d); }, [this, d]() { CreateObject(d); },
                           "Add object"));
}

void AdtViewerModule::DeleteSelection()
{
    const AdtObjectDesc d = CaptureSelectedDesc();
    if (d.kind == SelKind::None)
        return;
    DestroyObject(d);   // clears the selection too
    undo_.Push(MakeCommand([this, d]() { CreateObject(d); }, [this, d]() { DestroyObject(d); },
                           "Delete object"));
    saveStatus_ = std::string("Deleted ") +
                  (d.kind == SelKind::Doodad ? (d.isWmo ? "WMO" : "doodad")
                                             : d.kind == SelKind::GameObject ? "gameobject" : "creature");
    if (d.kind == SelKind::Doodad)
        saveStatus_ += " — click 'Save ADT edits' to persist.";
}

// Drive the selected object's inverted-hull outline: clear it on every source, then set a yellow
// outline (rgb + view-space width) on the source that owns the selection. Consumed by BuildFrame /
// the layers' Build, then drawn by the renderer's outline pass in RenderWorld.
void AdtViewerModule::ApplySelectionOutline()
{
    const glm::vec4 none(0.0f);
    streamer_.SetObjectOutline(0, none);
    goLayer_.SetOutline(0, none);
    npcLayer_.SetOutline(0, none);
    if (selKind_ == SelKind::None)
        return;
    const glm::vec4 col(1.0f, 0.82f, 0.15f, 0.005f);   // yellow; .a = outline width (view-space fraction)
    if (selKind_ == SelKind::Doodad)          streamer_.SetObjectOutline(selUid_, col);
    else if (selKind_ == SelKind::GameObject) goLayer_.SetOutline(selGuid_, col);
    else if (selKind_ == SelKind::Npc)        npcLayer_.SetOutline(selGuid_, col);
}

// Right-click (no drag) on terrain opens the add popup at the ground point. An object nearer than
// the ground means the click was on an object, so no add is offered.
void AdtViewerModule::HandleRightClickAdd(const glm::mat4& view, const glm::mat4& proj,
                                          const ImVec2& p0, int w, int h, bool viewportHovered,
                                          const glm::vec3& focus, const SpawnFilter& filter)
{
    ImGuiIO& io = ImGui::GetIO();
    if (viewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        rightDown_ = true;
        rightMoved_ = false;
        rightPressPos_ = io.MousePos;
    }
    if (rightDown_ && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        const float dx = io.MousePos.x - rightPressPos_.x, dy = io.MousePos.y - rightPressPos_.y;
        if (dx * dx + dy * dy > 16.0f)
            rightMoved_ = true;   // it's a camera-look drag
    }
    if (!(rightDown_ && ImGui::IsMouseReleased(ImGuiMouseButton_Right)))
        return;
    rightDown_ = false;
    if (rightMoved_ || !viewportHovered)
        return;

    const float px = rightPressPos_.x - p0.x, py = rightPressPos_.y - p0.y;
    if (px < 0.0f || py < 0.0f || px >= (float)w || py >= (float)h)
        return;
    const PickRay ray = MakePickRay(view, proj, px, py, (float)w, (float)h);
    const glm::vec3 origin = streamer_.origin();
    glm::vec3 gLocal;
    float gT = -1.0f;
    int gtx = 0, gty = 0;
    if (!streamer_.GroundHit(ray.origin, ray.dir, gLocal, gT, gtx, gty))
        return;   // no ground under the cursor
    // If an object is closer than the ground hit, the right-click was on that object: select it and
    // open a context menu (move/delete) rather than the add-here popup.
    float td = -1.0f, tg = -1.0f, tn = -1.0f;
    const uint64_t duid = streamer_.PickObject(ray.origin, ray.dir, td);
    const uint32_t gg = showGos_ ? goLayer_.Pick(ray.origin, ray.dir, focus, origin, goCullDist_, filter, tg) : 0;
    const uint32_t ng = showNpcs_ ? npcLayer_.Pick(ray.origin, ray.dir, focus, origin, npcCullDist_, filter, tn) : 0;
    {
        SelKind ok = SelKind::None;
        float best = gT;
        if (duid && td >= 0.0f && td < best) { best = td; ok = SelKind::Doodad; }
        if (gg && tg >= 0.0f && tg < best)   { best = tg; ok = SelKind::GameObject; }
        if (ng && tn >= 0.0f && tn < best)   { best = tn; ok = SelKind::Npc; }
        if (ok != SelKind::None)
        {
            SelectObject(ok, duid, gg, ng);
            ImGui::OpenPopup("ObjectContext");
            return;
        }
    }

    addLocal_ = gLocal;
    addWorldPos_ = glm::vec3(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z);
    addTileX_ = gtx;
    addTileY_ = gty;
    addSearch_[0] = 0;
    ImGui::OpenPopup("AddObjectHere");
}

namespace
{
// Group WMO files (…_000.wmo) aren't placeable on their own — only root WMOs are.
bool IsWmoGroupFile(const std::string& path)
{
    if (path.size() < 8)
        return false;
    const size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot < 4)
        return false;
    return path[dot - 4] == '_' && std::isdigit((unsigned char)path[dot - 3]) &&
           std::isdigit((unsigned char)path[dot - 2]) && std::isdigit((unsigned char)path[dot - 1]);
}
} // namespace

void AdtViewerModule::DrawAddObjectPopup()
{
    if (!ImGui::BeginPopup("AddObjectHere"))
        return;
    ImGui::Text("Add at  X %.1f  Y %.1f  Z %.1f   (tile %d, %d)", addWorldPos_.x, addWorldPos_.y,
                addWorldPos_.z, addTileX_, addTileY_);
    ImGui::Separator();
    ImGui::RadioButton("NPC", &addType_, 0); ImGui::SameLine();
    ImGui::RadioButton("GameObject", &addType_, 1); ImGui::SameLine();
    ImGui::RadioButton("M2", &addType_, 2); ImGui::SameLine();
    ImGui::RadioButton("WMO", &addType_, 3);

    const bool dbType = (addType_ == 0 || addType_ == 1);
    ImGui::SetNextItemWidth(380);
    ImGui::InputTextWithHint("##addsearch", dbType ? "search by name / id" : "search by path",
                             addSearch_, sizeof(addSearch_));
    const std::string q = addSearch_;

    ImGui::BeginChild("##addresults", ImVec2(380, 260), true);
    if (dbType)
    {
        if (!svc_ || !svc_->lookups)
            ImGui::TextDisabled("Name lookup unavailable (connect a DB).");
        else
        {
            const std::vector<NameEntry> res = (addType_ == 0) ? svc_->lookups->SearchCreatures(q, 200)
                                                               : svc_->lookups->SearchGameObjects(q, 200);
            for (const NameEntry& e : res)
            {
                const std::string label = std::to_string(e.id) + "  " + e.name;
                if (ImGui::Selectable(label.c_str()))
                {
                    if (addType_ == 0) PerformAddNpc(e.id); else PerformAddGameObject(e.id);
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    else
    {
        const bool wmo = (addType_ == 3);
        if (wmo && !wmoListed_ && svc_ && svc_->clientData)
        {
            wmoList_ = svc_->clientData->ListFiles(".wmo");
            std::sort(wmoList_.begin(), wmoList_.end());
            wmoListed_ = true;
        }
        if (!wmo && !m2Listed_ && svc_ && svc_->clientData)
        {
            m2List_ = svc_->clientData->ListFiles(".m2");
            std::sort(m2List_.begin(), m2List_.end());
            m2Listed_ = true;
        }
        const std::vector<std::string>& list = wmo ? wmoList_ : m2List_;
        const std::string ql = Lower(q);
        int shown = 0;
        for (const std::string& path : list)
        {
            if (wmo && IsWmoGroupFile(path))
                continue;
            if (!ql.empty() && Lower(path).find(ql) == std::string::npos)
                continue;
            if (ImGui::Selectable(path.c_str()))
            {
                PerformAddModel(path, wmo);
                ImGui::CloseCurrentPopup();
            }
            if (++shown >= 400)
            {
                ImGui::TextDisabled("... refine the search");
                break;
            }
        }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
}

// Context menu shown when the user right-clicks an object (opened from HandleRightClickAdd). The
// object is already selected, so the gizmo is live; this exposes the discrete actions.
void AdtViewerModule::DrawObjectContextPopup()
{
    if (!ImGui::BeginPopup("ObjectContext"))
        return;
    if (selKind_ == SelKind::None)
    {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    ImGui::TextDisabled("%s", selLabel_.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("Drag the gizmo to move. Or:");
    if (ImGui::MenuItem("Delete"))
    {
        DeleteSelection();
        ImGui::CloseCurrentPopup();
    }
    if (ImGui::MenuItem("Deselect"))
    {
        ClearSelection();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void AdtViewerModule::PerformAddNpc(uint32_t entry)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Connect a project DB to add NPCs.";
        return;
    }
    uint32_t guid = 0, displayId = 0;
    DbError e = spawnRepo_.InsertCreatureSpawn(*svc_->activeDb, currentMapId_, entry, addWorldPos_.x,
                                               addWorldPos_.y, addWorldPos_.z, 0.0f, guid, displayId);
    if (!e.ok || guid == 0)
    {
        saveStatus_ = "Add NPC failed: " + e.message;
        return;
    }
    MapSpawn s;
    s.guid = guid; s.entry = entry;
    s.x = addWorldPos_.x; s.y = addWorldPos_.y; s.z = addWorldPos_.z; s.o = 0.0f;
    s.displayId = displayId; s.scale = 1.0f; s.phaseMask = 1; s.spawnMask = 1;
    npcLayer_.AddSpawn(s);
    saveStatus_ = "Added creature guid " + std::to_string(guid) + " (entry " + std::to_string(entry) + ").";
    if (svc_->setStatus) svc_->setStatus(saveStatus_);
    AdtObjectDesc d;
    d.kind = SelKind::Npc; d.guid = guid; d.entry = entry; d.displayId = displayId;
    d.x = s.x; d.y = s.y; d.z = s.z; d.o = s.o; d.size = s.scale;
    PushCreateUndo(d);
}

void AdtViewerModule::PerformAddGameObject(uint32_t entry)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Connect a project DB to add GameObjects.";
        return;
    }
    const float rot[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // identity (facing yaw 0)
    uint32_t guid = 0, displayId = 0;
    DbError e = spawnRepo_.InsertGameObjectSpawn(*svc_->activeDb, currentMapId_, entry, addWorldPos_.x,
                                                 addWorldPos_.y, addWorldPos_.z, 0.0f, rot, guid, displayId);
    if (!e.ok || guid == 0)
    {
        saveStatus_ = "Add GameObject failed: " + e.message;
        return;
    }
    MapGameObject g;
    g.guid = guid; g.entry = entry;
    g.x = addWorldPos_.x; g.y = addWorldPos_.y; g.z = addWorldPos_.z; g.o = 0.0f;
    g.rot[0] = rot[0]; g.rot[1] = rot[1]; g.rot[2] = rot[2]; g.rot[3] = rot[3];
    g.displayId = displayId; g.size = 1.0f; g.phaseMask = 1; g.spawnMask = 1;
    goLayer_.AddGameObject(g);
    saveStatus_ = "Added gameobject guid " + std::to_string(guid) + " (entry " + std::to_string(entry) + ").";
    if (svc_->setStatus) svc_->setStatus(saveStatus_);
    AdtObjectDesc d;
    d.kind = SelKind::GameObject; d.guid = guid; d.entry = entry; d.displayId = displayId;
    d.x = g.x; d.y = g.y; d.z = g.z; d.o = g.o; d.size = g.size;
    d.rot[0] = rot[0]; d.rot[1] = rot[1]; d.rot[2] = rot[2]; d.rot[3] = rot[3];
    PushCreateUndo(d);
}

void AdtViewerModule::PerformAddModel(const std::string& path, bool isWmo)
{
    const uint64_t uid = 0xF0000000ull + (addUidCounter_++);
    const glm::mat4 xform = glm::translate(glm::mat4(1.0f), addLocal_);
    if (!streamer_.AddObject(path, isWmo, xform, addTileX_, addTileY_, uid))
    {
        saveStatus_ = "Failed to load model: " + path;
        return;
    }
    const adt::RawPlacement raw = adt::PlacementToRaw(xform, streamer_.origin());
    adtEdits_.RecordUpsert(uid, isWmo, raw, path, {{addTileX_, addTileY_}});
    saveStatus_ = std::string(isWmo ? "Added WMO" : "Added M2") + " — queued (" +
                  std::to_string(adtEdits_.pendingCount()) + " pending); click 'Save ADT edits'.";
    // Record an undo step: undo removes the added placement, redo re-adds it.
    AdtObjectDesc d;
    d.kind = SelKind::Doodad;
    d.uid = uid; d.isWmo = isWmo; d.path = path; d.local = xform;
    d.tileX = addTileX_; d.tileY = addTileY_;
    d.tiles = {{addTileX_, addTileY_}};
    PushCreateUndo(d);
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
