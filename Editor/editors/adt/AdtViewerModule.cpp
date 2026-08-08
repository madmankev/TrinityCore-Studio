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

// Project a TrinityCore world point into the World Editor's render target. The camera projection
// already has Vulkan's Y flip, so its NDC Y maps directly to the top-left ImGui viewport convention
// used by MakePickRay (top = -1, bottom = +1).
bool ProjectWorldPoint(const glm::vec3& world, const glm::vec3& origin, const glm::mat4& view,
                       const glm::mat4& proj, const ImVec2& p0, int w, int h, ImVec2& screen)
{
    const glm::vec4 clip = proj * view * glm::vec4(world.x - origin.x, world.y - origin.y, world.z, 1.0f);
    if (clip.w <= 1e-5f)
        return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -0.01f || ndc.z > 1.01f)
        return false;
    screen.x = p0.x + (ndc.x * 0.5f + 0.5f) * static_cast<float>(w);
    screen.y = p0.y + (ndc.y * 0.5f + 0.5f) * static_cast<float>(h);
    return true;
}

const char* WaypointSourceLabel(WaypointPathSource source)
{
    switch (source)
    {
        case WaypointPathSource::SpawnAddon:    return "Spawn addon (local)";
        case WaypointPathSource::TemplateAddon: return "Template default (shared)";
        default:                                return "No route assigned";
    }
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
    formations_.clear();
    formationsAvailable_ = false;
    formationEdit_ = FormationState{};
    formationOrig_ = FormationState{};
    formationEditGuid_ = 0;
    formationDirty_ = false;
    brushPlacements_.clear();
    brushActive_ = false;
    outlinerDirty_ = true;
}

void AdtViewerModule::OnConnected()
{
    LoadNpcSpawns();       // a DB is now available — populate NPCs + GameObjects for the open map
    LoadGameObjects();
    LoadFormations();
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

void AdtViewerModule::HandleShortcuts()
{
    // Shell already excludes text input before dispatching module shortcuts. F mirrors the common
    // DCC/world-editor convention: center the streamed world around the current selection.
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) && selKind_ != SelKind::None)
        FrameSelection();
}

void AdtViewerModule::LoadSettings(const nlohmann::json& editorNode)
{
    bookmarks_.clear();
    try
    {
        if (!editorNode.contains("bookmarks") || !editorNode["bookmarks"].is_array())
            return;
        for (const nlohmann::json& j : editorNode["bookmarks"])
        {
            if (!j.is_object())
                continue;
            WorldBookmark b;
            b.name = j.value("name", std::string());
            b.mapId = j.value("mapId", 0u);
            b.mapDir = j.value("mapDir", std::string());
            b.world.x = j.value("x", 0.0f);
            b.world.y = j.value("y", 0.0f);
            b.world.z = j.value("z", 0.0f);
            b.radius = std::clamp(j.value("radius", 75.0f), 5.0f, 2000.0f);
            if (b.name.empty() || b.mapDir.empty())
                continue;
            bookmarks_.push_back(std::move(b));
            if (bookmarks_.size() >= 100)  // settings guard; plenty for a project without bloating config
                break;
        }
    }
    catch (...)
    {
        bookmarks_.clear();
    }
}

void AdtViewerModule::SaveSettings(nlohmann::json& editorNode) const
{
    nlohmann::json saved = nlohmann::json::array();
    for (const WorldBookmark& b : bookmarks_)
        saved.push_back({{"name", b.name}, {"mapId", b.mapId}, {"mapDir", b.mapDir},
                         {"x", b.world.x}, {"y", b.world.y}, {"z", b.world.z},
                         {"radius", b.radius}});
    editorNode["bookmarks"] = std::move(saved);
}

void AdtViewerModule::Undo()
{
    if (waypointDirty_ && waypointUndo_.CanUndo())
    {
        waypointEdit_ = waypointUndo_.Undo(waypointEdit_);
        waypointSelected_ = waypointEdit_.points.empty() ? -1 :
                             std::clamp(waypointSelected_, 0,
                                        static_cast<int>(waypointEdit_.points.size()) - 1);
        npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
        waypointStatus_ = "Undid waypoint edit (unsaved preview).";
        return;
    }
    undo_.Undo();
}

void AdtViewerModule::Redo()
{
    if (waypointDirty_ && waypointUndo_.CanRedo())
    {
        waypointEdit_ = waypointUndo_.Redo(waypointEdit_);
        waypointSelected_ = waypointEdit_.points.empty() ? -1 :
                             std::clamp(waypointSelected_, 0,
                                        static_cast<int>(waypointEdit_.points.size()) - 1);
        npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
        waypointStatus_ = "Redid waypoint edit (unsaved preview).";
        return;
    }
    undo_.Redo();
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
        outlinerDirty_ = true;
        return;
    }
    std::vector<MapSpawn> spawns;
    DbError e = spawnRepo_.LoadSpawnsForMap(*svc_->activeDb, currentMapId_, spawns);
    if (!e.ok)
    {
        npcLayer_.SetSpawns({});
        npcStatus_ = "NPC load failed: " + e.message;
        outlinerDirty_ = true;
        return;
    }
    const size_t n = spawns.size();
    npcLayer_.SetSpawns(std::move(spawns));
    npcStatus_ = std::to_string(n) + " NPC spawns on this map.";
    outlinerDirty_ = true;
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
        outlinerDirty_ = true;
        return;
    }
    std::vector<MapGameObject> gos;
    DbError e = spawnRepo_.LoadGameObjectsForMap(*svc_->activeDb, currentMapId_, gos);
    if (!e.ok)
    {
        goLayer_.SetGameObjects({});
        goStatus_ = "GameObject load failed: " + e.message;
        outlinerDirty_ = true;
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
    outlinerDirty_ = true;
}

void AdtViewerModule::LoadFormations()
{
    formations_.clear();
    formationsAvailable_ = false;
    formationEdit_ = FormationState{};
    formationOrig_ = FormationState{};
    formationEditGuid_ = 0;
    formationDirty_ = false;
    formationStatus_.clear();
    if (!svc_ || !svc_->connected || !svc_->activeDb || (currentMapId_ == 0 && selectedMap_ < 0))
        return;
    const DbError e = spawnRepo_.LoadCreatureFormationsForMap(*svc_->activeDb, currentMapId_, formations_);
    // A formation table is optional for older/custom projects. Keep the rest of the World Editor
    // usable and surface the detail only in the Formation panel instead of failing map load.
    if (!e.ok)
        formationStatus_ = "Formation data unavailable: " + e.message;
    else
        formationsAvailable_ = true;
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
    DrawOutlinerPanel();
    DrawSpawnPalettePanel();
    DrawLocationsPanel();
    DrawTransformPanel();
    DrawFormationPanel();
    DrawNpcInstancePanel();
    DrawGoInstancePanel();
    DrawWaypointPathPanel();
    DrawViewportPanel();
}

void AdtViewerModule::DrawBrowserPanel()
{
    if (!ImGui::Begin("World Browser###ADT Browser"))
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
            brushPlacements_.clear();
            outlinerDirty_ = true;
            LoadNpcSpawns();     // query this map's spawns (no-op if not connected)
            LoadGameObjects();
            LoadFormations();
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
    if (pendingLocationFocus_ && pendingLocationMapDir_ == dir)
    {
        FrameWorldPosition(pendingLocationWorld_, pendingLocationRadius_);
        pendingLocationFocus_ = false;
        pendingLocationMapDir_.clear();
    }
    else if (frameCamera)
        camera_.Frame(streamer_.startPos(), streamer_.startRadius());
    if (svc_ && svc_->setStatus)
        svc_->setStatus("Opened map: " + dir);
}


SpawnFilter AdtViewerModule::CurrentSpawnFilter() const
{
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
    return filter;
}

void AdtViewerModule::FrameWorldPosition(const glm::vec3& world, float radius)
{
    if (!streamerInit_)
        return;
    const glm::vec3 origin = streamer_.origin();
    camera_.Frame(glm::vec3(world.x - origin.x, world.y - origin.y, world.z),
                  std::max(radius, 5.0f));
}

void AdtViewerModule::FrameSelection()
{
    if (selKind_ == SelKind::None)
        return;
    const glm::vec3 origin = streamer_.origin();
    if (selKind_ == SelKind::Npc)
    {
        if (const MapSpawn* s = npcLayer_.FindSpawn(selGuid_))
            FrameWorldPosition(glm::vec3(s->x, s->y, s->z), std::max(25.0f, s->scale * 18.0f));
    }
    else if (selKind_ == SelKind::GameObject)
    {
        if (const MapGameObject* g = goLayer_.FindSpawn(selGuid_))
            FrameWorldPosition(glm::vec3(g->x, g->y, g->z), std::max(30.0f, g->size * 25.0f));
    }
    else
    {
        glm::mat4 transform(1.0f);
        if (streamer_.ObjectTransform(selUid_, transform))
        {
            const glm::vec3 local(transform[3]);
            FrameWorldPosition(glm::vec3(local.x + origin.x, local.y + origin.y, local.z), 40.0f);
        }
    }
}

bool AdtViewerModule::CanBrushPlace(const glm::vec3& world) const
{
    if (brushMinSpacing_ <= 0.01f)
        return true;
    const float min2 = brushMinSpacing_ * brushMinSpacing_;
    for (const BrushPlacement& p : brushPlacements_)
    {
        if (p.kind != brushKind_ || p.entry != brushEntry_)
            continue;
        const glm::vec3 delta = p.world - world;
        if (glm::dot(delta, delta) < min2)
            return false;
    }
    return true;
}

void AdtViewerModule::RebuildOutliner()
{
    outlinerEntries_.clear();
    std::vector<MapSpawn> npcs;
    std::vector<MapGameObject> gos;
    npcLayer_.SnapshotSpawns(npcs);
    goLayer_.SnapshotGameObjects(gos);
    outlinerEntries_.reserve(npcs.size() + gos.size());

    for (const MapSpawn& s : npcs)
    {
        OutlinerEntry e;
        e.kind = SelKind::Npc;
        e.guid = s.guid;
        e.entry = s.entry;
        e.world = glm::vec3(s.x, s.y, s.z);
        e.phaseMask = s.phaseMask;
        e.spawnMask = s.spawnMask;
        e.eventEntry = s.eventEntry;
        e.poolHidden = s.poolHidden;
        e.groupManual = s.groupManual;
        e.label = svc_ && svc_->lookups ? svc_->lookups->LabelCreature(s.entry)
                                        : ("entry " + std::to_string(s.entry));
        e.searchKey = Lower(std::to_string(e.guid) + " " + std::to_string(e.entry) + " " + e.label);
        outlinerEntries_.push_back(std::move(e));
    }
    for (const MapGameObject& g : gos)
    {
        OutlinerEntry e;
        e.kind = SelKind::GameObject;
        e.guid = g.guid;
        e.entry = g.entry;
        e.world = glm::vec3(g.x, g.y, g.z);
        e.phaseMask = g.phaseMask;
        e.spawnMask = g.spawnMask;
        e.eventEntry = g.eventEntry;
        e.poolHidden = g.poolHidden;
        e.groupManual = g.groupManual;
        e.label = svc_ && svc_->lookups ? svc_->lookups->LabelGameObject(g.entry)
                                        : ("entry " + std::to_string(g.entry));
        e.searchKey = Lower(std::to_string(e.guid) + " " + std::to_string(e.entry) + " " + e.label);
        outlinerEntries_.push_back(std::move(e));
    }
    outlinerDirty_ = false;
}

void AdtViewerModule::DrawOutlinerPanel()
{
    if (!ImGui::Begin("World Outliner"))
    {
        ImGui::End();
        return;
    }
    if (loadedName_.empty())
    {
        ImGui::TextWrapped("Open a map to browse all of its database spawns. The outliner is not limited by the current draw distance.");
        ImGui::End();
        return;
    }
    if (outlinerDirty_)
        RebuildOutliner();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##outlinersearch", "filter by name, entry, or guid", outlinerSearch_,
                             sizeof(outlinerSearch_));
    static const char* kKinds[] = {"All", "NPCs", "GameObjects"};
    ImGui::SetNextItemWidth(130.0f);
    ImGui::Combo("Type", &outlinerKind_, kKinds, IM_ARRAYSIZE(kKinds));
    ImGui::SameLine();
    ImGui::Checkbox("Visible only", &outlinerVisibleOnly_);
    ImGui::SameLine();
    ImGui::Checkbox("Nearest first", &outlinerSortByDistance_);

    const SpawnFilter filter = CurrentSpawnFilter();
    const std::string query = Lower(outlinerSearch_);
    outlinerFiltered_.clear();
    outlinerFiltered_.reserve(outlinerEntries_.size());
    for (int i = 0; i < static_cast<int>(outlinerEntries_.size()); ++i)
    {
        const OutlinerEntry& e = outlinerEntries_[i];
        if (outlinerKind_ == 1 && e.kind != SelKind::Npc)
            continue;
        if (outlinerKind_ == 2 && e.kind != SelKind::GameObject)
            continue;
        if (outlinerVisibleOnly_ && !filter.Visible(e.phaseMask, e.spawnMask, e.eventEntry,
                                                    e.poolHidden, e.groupManual))
            continue;
        if (!query.empty() && e.searchKey.find(query) == std::string::npos)
            continue;
        outlinerFiltered_.push_back(i);
    }

    const glm::vec3 localFocus = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye()
                                                                               : camera_.center();
    const glm::vec3 worldFocus(localFocus.x + streamer_.origin().x,
                               localFocus.y + streamer_.origin().y, localFocus.z);
    if (outlinerSortByDistance_)
        std::sort(outlinerFiltered_.begin(), outlinerFiltered_.end(), [&](int a, int b) {
            const glm::vec3 da = outlinerEntries_[a].world - worldFocus;
            const glm::vec3 db = outlinerEntries_[b].world - worldFocus;
            const float aa = glm::dot(da, da);
            const float bb = glm::dot(db, db);
            if (aa != bb)
                return aa < bb;
            return outlinerEntries_[a].guid < outlinerEntries_[b].guid;
        });
    ImGui::TextDisabled("%d shown / %d total spawns", static_cast<int>(outlinerFiltered_.size()),
                        static_cast<int>(outlinerEntries_.size()));

    ImGui::BeginChild("##outlinerrows", ImVec2(0, 0), true);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(outlinerFiltered_.size()));
    while (clipper.Step())
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const OutlinerEntry& e = outlinerEntries_[outlinerFiltered_[row]];
            const bool selected = (selKind_ == e.kind && selGuid_ == e.guid);
            const char* type = e.kind == SelKind::Npc ? "NPC" : "GO";
            const std::string line = std::string(type) + "  #" + std::to_string(e.guid) +
                                     "  " + e.label;
            const std::string rowId = std::to_string(static_cast<int>(e.kind)) + ":" +
                                      std::to_string(e.guid);
            ImGui::PushID(rowId.c_str());
            if (ImGui::Selectable(line.c_str(), selected))
            {
                if (e.kind == SelKind::Npc)
                    SelectObject(SelKind::Npc, 0, 0, e.guid);
                else
                    SelectObject(SelKind::GameObject, 0, e.guid, 0);
                FrameWorldPosition(e.world, e.kind == SelKind::Npc ? 40.0f : 55.0f);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::Text("guid %u  entry %u", e.guid, e.entry);
                ImGui::Text("X %.2f  Y %.2f  Z %.2f", e.world.x, e.world.y, e.world.z);
                ImGui::TextDisabled("phase 0x%X  spawn 0x%X", e.phaseMask, e.spawnMask);
                if (e.eventEntry)
                    ImGui::TextDisabled("game event %d", e.eventEntry);
                if (e.poolHidden)
                    ImGui::TextDisabled("hidden by current pool simulation");
                if (e.groupManual)
                    ImGui::TextDisabled("manual spawn group");
                ImGui::EndTooltip();
            }
            ImGui::PopID();
        }
    ImGui::EndChild();
    ImGui::End();
}

void AdtViewerModule::DrawSpawnPalettePanel()
{
    if (!ImGui::Begin("Spawn Palette"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project database to use rapid NPC/GameObject placement.");
        ImGui::End();
        return;
    }
    if (loadedName_.empty())
    {
        ImGui::TextWrapped("Open a map before arming the placement palette.");
        ImGui::End();
        return;
    }

    if (ImGui::RadioButton("NPC", &brushKind_, 0))
    {
        brushEntry_ = 0;
        brushActive_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("GameObject", &brushKind_, 1))
    {
        brushEntry_ = 0;
        brushActive_ = false;
    }
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##brushsearch", "search by name or entry", brushSearch_, sizeof(brushSearch_));
    ImGui::BeginChild("##brushresults", ImVec2(0, 180), true);
    if (!svc_->lookups)
        ImGui::TextDisabled("Template name lookup unavailable.");
    else
    {
        const std::vector<NameEntry> results = brushKind_ == 0
            ? svc_->lookups->SearchCreatures(brushSearch_, 150)
            : svc_->lookups->SearchGameObjects(brushSearch_, 150);
        for (const NameEntry& e : results)
        {
            const std::string label = std::to_string(e.id) + "  " + e.name;
            if (ImGui::Selectable(label.c_str(), brushEntry_ == e.id))
            {
                brushEntry_ = e.id;
                brushStatus_.clear();
            }
        }
        if (results.empty())
            ImGui::TextDisabled("No matching templates.");
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(145.0f);
    InputU32("Entry", brushEntry_);
    float yawDegrees = glm::degrees(brushYaw_);
    ImGui::SetNextItemWidth(145.0f);
    if (ImGui::InputFloat("Yaw (degrees)", &yawDegrees, 1.0f, 15.0f, "%.1f"))
        brushYaw_ = glm::radians(yawDegrees);
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat("Minimum spacing", &brushMinSpacing_, 0.1f, 0.0f, 100.0f, "%.1f yd");
    if (ImGui::Button(brushActive_ ? "Stop placement brush" : "Arm placement brush"))
    {
        if (brushEntry_ == 0)
            brushStatus_ = "Choose a template entry before arming the brush.";
        else
        {
            brushActive_ = !brushActive_;
            brushStatus_ = brushActive_ ? "Brush armed — right-click terrain to place repeatedly. Escape cancels."
                                        : "Brush stopped.";
        }
    }
    if (brushActive_)
        ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.42f, 1.0f),
                           "%s brush active: entry %u", brushKind_ == 0 ? "NPC" : "GameObject", brushEntry_);
    if (!brushStatus_.empty())
        ImGui::TextDisabled("%s", brushStatus_.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("This is a rapid version of the right-click Add menu. Every stamp is a normal database spawn, gets its own undo command, and can be moved precisely afterward. The spacing guard applies only to stamps made in this session; set it to 0 to allow overlap.");
    ImGui::End();
}

void AdtViewerModule::DrawLocationsPanel()
{
    if (!ImGui::Begin("Locations"))
    {
        ImGui::End();
        return;
    }
    const bool mapOpen = streamerInit_ && !loadedName_.empty();
    ImGui::SeparatorText("Coordinate navigator");
    ImGui::BeginDisabled(!mapOpen);
    ImGui::SetNextItemWidth(125.0f); ImGui::InputFloat("X", &locationX_, 0.0f, 0.0f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(125.0f); ImGui::InputFloat("Y", &locationY_, 0.0f, 0.0f, "%.3f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f); ImGui::InputFloat("Z", &locationZ_, 0.0f, 0.0f, "%.3f");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderFloat("Frame radius", &locationRadius_, 10.0f, 500.0f, "%.0f yd", ImGuiSliderFlags_Logarithmic);
    if (ImGui::Button("Go to coordinates"))
        FrameWorldPosition(glm::vec3(locationX_, locationY_, locationZ_), locationRadius_);
    ImGui::SameLine();
    if (ImGui::Button("Use camera focus"))
    {
        const glm::vec3 local = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye()
                                                                              : camera_.center();
        const glm::vec3 origin = streamer_.origin();
        locationX_ = local.x + origin.x;
        locationY_ = local.y + origin.y;
        locationZ_ = local.z;
    }
    ImGui::EndDisabled();
    if (!mapOpen)
        ImGui::TextDisabled("Open a map before navigating to coordinates.");

    if (mapOpen && !streamer_.wmoOnly())
    {
        ImGui::SeparatorText("Map overview");
        const float side = std::max(128.0f, std::min(ImGui::GetContentRegionAvail().x, 300.0f));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##tileoverview", ImVec2(side, side));
        const bool mapHovered = ImGui::IsItemHovered();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(13, 18, 24, 255));
        const float cell = side / 64.0f;
        std::vector<uint8_t> exists(64 * 64, 0);
        for (const auto& tile : streamer_.world().tiles)
            if (tile.first >= 0 && tile.first < 64 && tile.second >= 0 && tile.second < 64)
                exists[tile.second * 64 + tile.first] = 1;
        for (int ty = 0; ty < 64; ++ty)
            for (int tx = 0; tx < 64; ++tx)
            {
                if (!exists[ty * 64 + tx])
                    continue;
                const ImVec2 a(p0.x + tx * cell, p0.y + ty * cell);
                const ImVec2 b(a.x + std::max(cell - 0.25f, 1.0f), a.y + std::max(cell - 0.25f, 1.0f));
                const ImU32 color = streamer_.IsTileLoaded(tx, ty) ? IM_COL32(67, 177, 113, 255)
                                   : streamer_.IsTilePending(tx, ty) ? IM_COL32(240, 184, 68, 255)
                                                                     : IM_COL32(69, 89, 110, 255);
                draw->AddRectFilled(a, b, color);
            }
        const glm::vec3 local = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye()
                                                                              : camera_.center();
        const glm::vec3 origin = streamer_.origin();
        const float worldX = local.x + origin.x;
        const float worldY = local.y + origin.y;
        const int cx = std::clamp(static_cast<int>(std::floor(32.0f - worldY / adt::kTileSize)), 0, 63);
        const int cy = std::clamp(static_cast<int>(std::floor(32.0f - worldX / adt::kTileSize)), 0, 63);
        const ImVec2 cameraAt(p0.x + (cx + 0.5f) * cell, p0.y + (cy + 0.5f) * cell);
        draw->AddCircleFilled(cameraAt, std::max(2.5f, cell * 1.2f), IM_COL32(255, 87, 87, 255), 12);
        draw->AddRect(p0, ImVec2(p0.x + side, p0.y + side), IM_COL32(160, 180, 205, 255));
        if (mapHovered)
        {
            const int tx = std::clamp(static_cast<int>((mouse.x - p0.x) / cell), 0, 63);
            const int ty = std::clamp(static_cast<int>((mouse.y - p0.y) / cell), 0, 63);
            if (exists[ty * 64 + tx])
            {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const glm::vec3 target((31.5f - ty) * adt::kTileSize,
                                           (31.5f - tx) * adt::kTileSize, local.z);
                    locationX_ = target.x; locationY_ = target.y; locationZ_ = target.z;
                    FrameWorldPosition(target, adt::kTileSize * 1.8f);
                }
                ImGui::SetTooltip("Tile %d, %d%s", tx, ty,
                                  streamer_.IsTileLoaded(tx, ty) ? " (loaded)"
                                                                    : streamer_.IsTilePending(tx, ty) ? " (loading)" : "");
            }
        }
        ImGui::TextDisabled("Green = streamed terrain, gold = loading, grey = map tile. Click a tile to fly there.");
    }

    ImGui::SeparatorText("Bookmarks");
    ImGui::BeginDisabled(!mapOpen);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##bookmarkname", "bookmark name", bookmarkName_, sizeof(bookmarkName_));
    if (ImGui::Button("Save current location"))
    {
        WorldBookmark b;
        b.name = bookmarkName_[0] ? bookmarkName_
                                  : ("Location " + std::to_string(bookmarks_.size() + 1));
        b.mapId = currentMapId_;
        b.mapDir = selectedMapDir_;
        b.world = glm::vec3(locationX_, locationY_, locationZ_);
        b.radius = locationRadius_;
        bookmarks_.push_back(std::move(b));
        bookmarkName_[0] = 0;
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::EndDisabled();

    int erase = -1;
    ImGui::BeginChild("##bookmarklist", ImVec2(0, 0), true);
    for (int i = 0; i < static_cast<int>(bookmarks_.size()); ++i)
    {
        const WorldBookmark& b = bookmarks_[i];
        ImGui::PushID(i);
        const std::string label = b.name + "  [" + b.mapDir + "]";
        if (ImGui::Selectable(label.c_str(), false))
        {
            locationX_ = b.world.x;
            locationY_ = b.world.y;
            locationZ_ = b.world.z;
            locationRadius_ = b.radius;
            if (b.mapDir == selectedMapDir_ && mapOpen)
                FrameWorldPosition(b.world, b.radius);
            else if (streamerInit_)
            {
                pendingLocationFocus_ = true;
                pendingLocationMapDir_ = b.mapDir;
                pendingLocationWorld_ = b.world;
                pendingLocationRadius_ = b.radius;
                currentMapId_ = b.mapId;
                for (int mi = 0; mi < static_cast<int>(mapList_.size()); ++mi)
                    if (mapList_[mi].first == b.mapId || maps_[mapList_[mi].first].directory == b.mapDir)
                    {
                        selectedMap_ = mi;
                        currentMapId_ = mapList_[mi].first;
                        break;
                    }
                selectedMapDir_ = b.mapDir;
                OpenMapDir(b.mapDir, false);
                npcLayer_.Clear();
                goLayer_.Clear();
                brushPlacements_.clear();
                brushActive_ = false;
                outlinerDirty_ = true;
                LoadNpcSpawns();
                LoadGameObjects();
                LoadFormations();
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete"))
            erase = i;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("X %.2f  Y %.2f  Z %.2f", b.world.x, b.world.y, b.world.z);
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (erase >= 0)
    {
        bookmarks_.erase(bookmarks_.begin() + erase);
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::End();
}

void AdtViewerModule::SyncTransformEdit()
{
    if (selKind_ == SelKind::None)
    {
        transformEdit_ = TransformEdit{};
        return;
    }
    const uint64_t uid = selKind_ == SelKind::Doodad ? selUid_ : 0;
    const uint32_t guid = selKind_ == SelKind::Doodad ? 0 : selGuid_;
    if (transformEdit_.initialized && transformEdit_.kind == selKind_ &&
        transformEdit_.uid == uid && transformEdit_.guid == guid)
        return;

    const AdtXform source = CaptureSelection();
    if (source.kind == SelKind::None)
    {
        transformEdit_ = TransformEdit{};
        return;
    }
    TransformEdit fresh;
    fresh.kind = source.kind;
    fresh.uid = source.uid;
    fresh.guid = source.guid;
    if (source.kind == SelKind::Doodad)
    {
        glm::vec3 local, scale;
        glm::quat rotation;
        DecomposeTRS(source.local, local, rotation, scale);
        const glm::vec3 origin = streamer_.origin();
        fresh.x = local.x + origin.x;
        fresh.y = local.y + origin.y;
        fresh.z = local.z;
        fresh.scale = std::max(scale.x, 0.001f);
        fresh.yaw = std::atan2(2.0f * (rotation.w * rotation.z + rotation.x * rotation.y),
                               1.0f - 2.0f * (rotation.y * rotation.y + rotation.z * rotation.z));
    }
    else if (source.kind == SelKind::GameObject)
    {
        fresh.x = source.x; fresh.y = source.y; fresh.z = source.z;
        fresh.scale = source.size;
        fresh.yaw = source.o;
    }
    else
    {
        fresh.x = source.x; fresh.y = source.y; fresh.z = source.z;
        fresh.yaw = source.o;
        if (const MapSpawn* n = npcLayer_.FindSpawn(source.guid))
            fresh.scale = n->scale;
    }
    fresh.initialized = true;
    transformEdit_ = fresh;
}

void AdtViewerModule::RevertTransformEdit()
{
    transformEdit_ = TransformEdit{};
    transformStatus_ = "Transform edits discarded.";
    SyncTransformEdit();
}

void AdtViewerModule::ApplyTransformEdit()
{
    if (!transformEdit_.initialized || !transformEdit_.dirty || selKind_ == SelKind::None ||
        transformEdit_.kind != selKind_ ||
        (selKind_ == SelKind::Doodad ? transformEdit_.uid != selUid_
                                     : transformEdit_.guid != selGuid_))
    {
        transformStatus_ = "Select the object whose staged transform you want to apply.";
        return;
    }

    const AdtXform before = CaptureSelection();
    if (before.kind == SelKind::None)
    {
        transformStatus_ = "The selected object is no longer loaded.";
        return;
    }
    const TransformEdit edit = transformEdit_;
    if (before.kind == SelKind::Doodad)
    {
        glm::vec3 oldLocal, oldScale;
        glm::quat oldRotation;
        DecomposeTRS(before.local, oldLocal, oldRotation, oldScale);
        const float oldYaw = std::atan2(2.0f * (oldRotation.w * oldRotation.z + oldRotation.x * oldRotation.y),
                                        1.0f - 2.0f * (oldRotation.y * oldRotation.y + oldRotation.z * oldRotation.z));
        const glm::quat rotation = glm::normalize(glm::angleAxis(edit.yaw - oldYaw,
                                                                   glm::vec3(0.0f, 0.0f, 1.0f)) * oldRotation);
        const glm::vec3 origin = streamer_.origin();
        glm::mat4 matrix(1.0f);
        matrix = glm::translate(matrix, glm::vec3(edit.x - origin.x, edit.y - origin.y, edit.z));
        matrix = matrix * glm::mat4_cast(rotation);
        matrix = glm::scale(matrix, glm::vec3(std::clamp(edit.scale, 0.001f, 64.0f)));
        if (!streamer_.SetObjectTransform(before.uid, matrix))
        {
            transformStatus_ = "Could not apply the doodad transform.";
            return;
        }
    }
    else if (before.kind == SelKind::GameObject)
    {
        if (MapGameObject* g = goLayer_.FindSpawn(before.guid))
        {
            glm::quat oldRotation(g->rot[3], g->rot[0], g->rot[1], g->rot[2]);
            if (glm::length(oldRotation) < 1e-6f)
                oldRotation = glm::angleAxis(g->o, glm::vec3(0.0f, 0.0f, 1.0f));
            else
                oldRotation = glm::normalize(oldRotation);
            const float oldYaw = std::atan2(2.0f * (oldRotation.w * oldRotation.z + oldRotation.x * oldRotation.y),
                                            1.0f - 2.0f * (oldRotation.y * oldRotation.y + oldRotation.z * oldRotation.z));
            const glm::quat rotation = glm::normalize(glm::angleAxis(edit.yaw - oldYaw,
                                                                       glm::vec3(0.0f, 0.0f, 1.0f)) * oldRotation);
            g->x = edit.x; g->y = edit.y; g->z = edit.z; g->o = edit.yaw;
            g->rot[0] = rotation.x; g->rot[1] = rotation.y;
            g->rot[2] = rotation.z; g->rot[3] = rotation.w;
        }
        else
        {
            transformStatus_ = "The selected GameObject is no longer loaded.";
            return;
        }
    }
    else if (!npcLayer_.SetSpawnHome(before.guid, edit.x, edit.y, edit.z, edit.yaw))
    {
        transformStatus_ = "The selected NPC is no longer loaded.";
        return;
    }

    const AdtXform after = CaptureSelection();
    if (after.kind == SelKind::None || SameXform(before, after))
    {
        transformStatus_ = "No transform change to apply.";
        transformEdit_.dirty = false;
        return;
    }
    CommitSelectionToDb();
    undo_.Push(MakeCommand([this, before]() { ApplyAndPersist(before); },
                           [this, after]() { ApplyAndPersist(after); }, "Set precise transform"));
    transformStatus_ = saveStatus_.empty() ? "Transform applied." : saveStatus_;
    transformEdit_.initialized = false;
    SyncTransformEdit();
}

void AdtViewerModule::DrawTransformPanel()
{
    if (!ImGui::Begin("Transform"))
    {
        ImGui::End();
        return;
    }
    if (selKind_ == SelKind::None)
    {
        ImGui::TextWrapped("Select an NPC, GameObject, doodad, or WMO in the World Editor for exact placement controls.");
        ImGui::End();
        return;
    }
    const uint64_t activeUid = selKind_ == SelKind::Doodad ? selUid_ : 0;
    const uint32_t activeGuid = selKind_ == SelKind::Doodad ? 0 : selGuid_;
    if (transformEdit_.dirty && (!transformEdit_.initialized || transformEdit_.kind != selKind_ ||
        transformEdit_.uid != activeUid || transformEdit_.guid != activeGuid))
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                           "A different object has an unapplied staged transform.");
        if (ImGui::Button("Discard staged transform"))
            RevertTransformEdit();
        ImGui::End();
        return;
    }
    SyncTransformEdit();
    if (!transformEdit_.initialized)
    {
        ImGui::TextDisabled("Selection is no longer available.");
        ImGui::End();
        return;
    }

    const char* kind = selKind_ == SelKind::Npc ? "NPC" : selKind_ == SelKind::GameObject ? "GameObject" : "ADT placement";
    ImGui::Text("%s  —  %s", kind, selLabel_.c_str());
    ImGui::Separator();
    bool changed = false;
    if (BeginFieldTable("precisetransform", 120.0f))
    {
        FieldRow("World X"); changed |= InputFloatField("##tx", transformEdit_.x);
        FieldRow("World Y"); changed |= InputFloatField("##ty", transformEdit_.y);
        FieldRow("World Z"); changed |= InputFloatField("##tz", transformEdit_.z);
        float yawDegrees = glm::degrees(transformEdit_.yaw);
        FieldRow("Yaw (degrees)");
        if (ImGui::InputFloat("##tyaw", &yawDegrees, 1.0f, 15.0f, "%.2f"))
        {
            transformEdit_.yaw = glm::radians(yawDegrees);
            changed = true;
        }
        if (selKind_ == SelKind::Doodad)
        {
            FieldRow("Scale", "ADT doodad scale is per-placement and persists to the client-data overlay.");
            changed |= InputFloatField("##tscale", transformEdit_.scale);
            transformEdit_.scale = std::clamp(transformEdit_.scale, 0.001f, 64.0f);
        }
        else
        {
            FieldRow("Scale");
            ImGui::TextDisabled("Template-owned (edit the template, not this spawn).");
        }
        EndFieldTable();
    }
    if (changed)
        transformEdit_.dirty = true;

    ImGui::SeparatorText("Placement tools");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Nudge", &transformNudge_, 0.1f, 0.05f, 500.0f, "%.2f yd", ImGuiSliderFlags_Logarithmic);
    auto nudge = [&](const char* label, float dx, float dy, float dz) {
        if (ImGui::SmallButton(label))
        {
            transformEdit_.x += dx * transformNudge_;
            transformEdit_.y += dy * transformNudge_;
            transformEdit_.z += dz * transformNudge_;
            transformEdit_.dirty = true;
        }
    };
    nudge("X-", -1, 0, 0); ImGui::SameLine(); nudge("X+", 1, 0, 0); ImGui::SameLine();
    nudge("Y-", 0, -1, 0); ImGui::SameLine(); nudge("Y+", 0, 1, 0); ImGui::SameLine();
    nudge("Z-", 0, 0, -1); ImGui::SameLine(); nudge("Z+", 0, 0, 1);

    if (ImGui::Button("Copy placement"))
    {
        transformClipboard_ = transformEdit_;
        transformClipboard_.dirty = false;
        transformClipboard_.initialized = true;
        transformStatus_ = "Placement copied.";
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!transformClipboard_.initialized);
    if (ImGui::Button("Paste placement"))
    {
        transformEdit_.x = transformClipboard_.x;
        transformEdit_.y = transformClipboard_.y;
        transformEdit_.z = transformClipboard_.z;
        transformEdit_.yaw = transformClipboard_.yaw;
        if (selKind_ == SelKind::Doodad)
            transformEdit_.scale = transformClipboard_.scale;
        transformEdit_.dirty = true;
        transformStatus_ = "Placement pasted — click Apply to save.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Frame selection"))
        FrameSelection();
    ImGui::SameLine();
    if (ImGui::Button("Snap selection"))
    {
        SnapSelectionToGround();
        transformEdit_ = TransformEdit{};
        SyncTransformEdit();
    }
    if (selKind_ == SelKind::Npc || selKind_ == SelKind::GameObject)
    {
        if (ImGui::Button("Use selected template as brush"))
        {
            brushKind_ = selKind_ == SelKind::Npc ? 0 : 1;
            brushEntry_ = selEntry_;
            brushActive_ = brushEntry_ != 0;
            brushStatus_ = brushActive_ ? "Selected template loaded into the placement brush."
                                        : "Selection has no template entry.";
        }
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!transformEdit_.dirty);
    if (ImGui::Button("Apply transform"))
        ApplyTransformEdit();
    ImGui::SameLine();
    if (ImGui::Button("Revert staged"))
        RevertTransformEdit();
    ImGui::EndDisabled();
    if (transformEdit_.dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f), "staged");
    }
    if (!transformStatus_.empty())
        ImGui::TextDisabled("%s", transformStatus_.c_str());
    ImGui::End();
}

void AdtViewerModule::SyncFormationEdit()
{
    if (selKind_ != SelKind::Npc)
    {
        if (!formationDirty_)
        {
            formationEdit_ = FormationState{};
            formationOrig_ = FormationState{};
            formationEditGuid_ = 0;
        }
        return;
    }
    if (formationDirty_ || formationEditGuid_ == selGuid_)
        return;

    FormationState state;
    state.row.memberGuid = selGuid_;
    state.row.leaderGuid = selGuid_;
    for (const CreatureFormationMember& f : formations_)
        if (f.memberGuid == selGuid_)
        {
            state.present = true;
            state.row = f;
            break;
        }
    formationEdit_ = state;
    formationOrig_ = state;
    formationEditGuid_ = selGuid_;
    formationStatus_.clear();
}

void AdtViewerModule::ApplyFormationState(uint32_t memberGuid, const FormationState& state)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || memberGuid == 0)
        return;
    FormationState normalized = state;
    normalized.row.memberGuid = memberGuid;
    DbError e = normalized.present ? spawnRepo_.SaveCreatureFormation(*svc_->activeDb, normalized.row)
                                   : spawnRepo_.DeleteCreatureFormation(*svc_->activeDb, memberGuid);
    if (!e.ok)
    {
        formationStatus_ = "Formation save failed: " + e.message;
        return;
    }
    formations_.erase(std::remove_if(formations_.begin(), formations_.end(),
                                     [memberGuid](const CreatureFormationMember& f) {
                                         return f.memberGuid == memberGuid;
                                     }), formations_.end());
    if (normalized.present)
        formations_.push_back(normalized.row);
    std::sort(formations_.begin(), formations_.end(), [](const CreatureFormationMember& a,
                                                          const CreatureFormationMember& b) {
        if (a.leaderGuid != b.leaderGuid)
            return a.leaderGuid < b.leaderGuid;
        return a.memberGuid < b.memberGuid;
    });
    if (formationEditGuid_ == memberGuid)
    {
        formationEdit_ = normalized;
        formationOrig_ = normalized;
        formationDirty_ = false;
    }
    formationStatus_ = normalized.present ? "Formation saved." : "Formation membership removed.";
    if (svc_->setStatus)
        svc_->setStatus(formationStatus_);
}

void AdtViewerModule::SaveFormationEdit()
{
    if (!formationDirty_ || formationEditGuid_ == 0)
        return;
    const FormationState before = formationOrig_;
    const FormationState after = formationEdit_;
    ApplyFormationState(formationEditGuid_, after);
    if (formationDirty_)  // Apply failed and intentionally left the staged edit untouched
        return;
    const uint32_t guid = formationEditGuid_;
    undo_.Push(MakeCommand([this, guid, before]() { ApplyFormationState(guid, before); },
                           [this, guid, after]() { ApplyFormationState(guid, after); },
                           "Edit creature formation"));
}

void AdtViewerModule::DeleteFormationEdit()
{
    if (formationEditGuid_ == 0 || !formationOrig_.present)
        return;
    const FormationState before = formationOrig_;
    FormationState after;
    after.row.memberGuid = formationEditGuid_;
    ApplyFormationState(formationEditGuid_, after);
    if (formationOrig_.present)  // delete failed
        return;
    const uint32_t guid = formationEditGuid_;
    undo_.Push(MakeCommand([this, guid, before]() { ApplyFormationState(guid, before); },
                           [this, guid, after]() { ApplyFormationState(guid, after); },
                           "Remove creature formation"));
}

void AdtViewerModule::DrawFormationPanel()
{
    if (!ImGui::Begin("Formation"))
    {
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project DB to edit creature formations.");
        ImGui::End();
        return;
    }
    if (!formationsAvailable_)
    {
        ImGui::TextWrapped("%s", formationStatus_.empty() ? "Formation data is unavailable for this map/database."
                                                             : formationStatus_.c_str());
        ImGui::End();
        return;
    }
    if (selKind_ != SelKind::Npc)
    {
        if (!formationDirty_)
            SyncFormationEdit();
        ImGui::TextWrapped("Select an NPC to inspect or edit its creature_formations membership.");
        if (formationDirty_)
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                               "A different NPC has an unsaved formation edit. Re-select it to save or revert.");
        if (!formationStatus_.empty())
            ImGui::TextDisabled("%s", formationStatus_.c_str());
        ImGui::End();
        return;
    }
    if (formationDirty_ && formationEditGuid_ != selGuid_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                           "Guid %u has an unsaved formation edit.", formationEditGuid_);
        if (ImGui::Button("Save current formation"))
            SaveFormationEdit();
        ImGui::SameLine();
        if (ImGui::Button("Discard and load selected"))
        {
            formationDirty_ = false;
            formationEditGuid_ = 0;
            SyncFormationEdit();
        }
        ImGui::End();
        return;
    }
    SyncFormationEdit();
    if (formationEditGuid_ == 0)
    {
        ImGui::TextDisabled("Formation data is unavailable for this selection.");
        ImGui::End();
        return;
    }

    const std::string title = svc_->lookups ? svc_->lookups->LabelCreature(selEntry_)
                                            : ("entry " + std::to_string(selEntry_));
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("guid %u", formationEditGuid_);
    ImGui::Checkbox("Show formation links in world", &showFormations_);

    bool enabled = formationEdit_.present;
    if (ImGui::Checkbox("Member of a formation", &enabled))
    {
        formationEdit_.present = enabled;
        if (enabled && formationEdit_.row.leaderGuid == 0)
            formationEdit_.row.leaderGuid = formationEditGuid_;
        formationDirty_ = true;
    }

    if (formationEdit_.present)
    {
        if (BeginFieldTable("formationfields", 165.0f))
        {
            FieldRow("Leader guid", "Set this NPC's own guid for a formation leader/self row.");
            if (InputU32("##flead", formationEdit_.row.leaderGuid)) formationDirty_ = true;
            FieldRow("Distance", "Formation offset distance in yards.");
            if (InputFloatField("##fdist", formationEdit_.row.distance)) formationDirty_ = true;
            FieldRow("Angle (degrees)", "0..360; TrinityCore formation angles are degrees.");
            if (InputFloatField("##fangle", formationEdit_.row.angle)) formationDirty_ = true;
            FieldRow("Group AI", "When enabled, members share combat/evade behavior.");
            bool ai = formationEdit_.row.groupAi != 0;
            if (ImGui::Checkbox("##fgai", &ai)) { formationEdit_.row.groupAi = ai ? 1 : 0; formationDirty_ = true; }
            FieldRow("Path point 1", "Optional angle-swap start point (newer schema revisions).");
            if (InputU32("##fp1", formationEdit_.row.point1)) formationDirty_ = true;
            FieldRow("Path point 2", "Optional angle-swap end point (newer schema revisions).");
            if (InputU32("##fp2", formationEdit_.row.point2)) formationDirty_ = true;
            EndFieldTable();
        }
        if (ImGui::Button("Make selected NPC the leader"))
        {
            formationEdit_.row.leaderGuid = formationEditGuid_;
            formationEdit_.row.distance = 0.0f;
            formationEdit_.row.angle = 0.0f;
            formationDirty_ = true;
        }
    }
    else
        ImGui::TextDisabled("Enable membership to create a self-leader row or join an existing leader guid.");

    ImGui::BeginDisabled(!formationDirty_);
    if (ImGui::Button(formationEdit_.present ? "Save formation" : "Remove membership"))
        SaveFormationEdit();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        formationEdit_ = formationOrig_;
        formationDirty_ = false;
    }
    ImGui::EndDisabled();
    if (formationOrig_.present)
    {
        ImGui::SameLine();
        if (ImGui::Button("Delete membership"))
            DeleteFormationEdit();
    }
    if (formationDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f), "unsaved");
    }
    if (!formationStatus_.empty())
        ImGui::TextDisabled("%s", formationStatus_.c_str());

    if (formationEdit_.present)
    {
        ImGui::SeparatorText("Members of this leader");
        int shown = 0;
        for (const CreatureFormationMember& f : formations_)
        {
            if (f.leaderGuid != formationEdit_.row.leaderGuid)
                continue;
            const MapSpawn* member = npcLayer_.FindSpawn(f.memberGuid);
            const std::string label = "guid " + std::to_string(f.memberGuid) +
                                      (member && svc_->lookups ? ("  " + svc_->lookups->LabelCreature(member->entry)) : "");
            ImGui::PushID(static_cast<int>(f.memberGuid));
            if (ImGui::Selectable(label.c_str(), f.memberGuid == formationEditGuid_))
            {
                SelectObject(SelKind::Npc, 0, 0, f.memberGuid);
                if (member)
                    FrameWorldPosition(glm::vec3(member->x, member->y, member->z), 40.0f);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("dist %.2f  angle %.1f°", f.distance, f.angle);
            ImGui::PopID();
            if (++shown >= 100)
            {
                ImGui::TextDisabled("... more members omitted");
                break;
            }
        }
        if (shown == 0)
            ImGui::TextDisabled("No saved members yet — save this row to create the group.");
    }
    ImGui::End();
}

void AdtViewerModule::DrawFormationOverlay(const glm::mat4& view, const glm::mat4& proj,
                                           const ImVec2& p0, int w, int h)
{
    if (!showFormations_ || formations_.empty())
        return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const glm::vec3 origin = streamer_.origin();
    int links = 0;
    for (const CreatureFormationMember& f : formations_)
    {
        if (f.memberGuid == f.leaderGuid)
            continue;  // leader self-row is useful in DB but has no visible link
        const MapSpawn* leader = npcLayer_.FindSpawn(f.leaderGuid);
        const MapSpawn* member = npcLayer_.FindSpawn(f.memberGuid);
        if (!leader || !member)
            continue;
        ImVec2 a, b;
        if (!ProjectWorldPoint(glm::vec3(leader->x, leader->y, leader->z), origin, view, proj, p0, w, h, a) ||
            !ProjectWorldPoint(glm::vec3(member->x, member->y, member->z), origin, view, proj, p0, w, h, b))
            continue;
        const bool selected = f.memberGuid == selGuid_ || f.leaderGuid == selGuid_;
        const ImU32 color = selected ? IM_COL32(255, 126, 221, 235) : IM_COL32(181, 102, 236, 175);
        draw->AddLine(a, b, color, selected ? 2.5f : 1.25f);
        draw->AddCircleFilled(b, selected ? 4.5f : 3.0f, color, 10);
        if (++links >= 750)
            break;  // keep the overlay cheap on intentionally huge formations
    }
}

void AdtViewerModule::DrawViewportPanel()
{
    if (!ImGui::Begin("World Editor###ADT Viewer"))
    {
        ImGui::End();
        return;
    }
    if (!streamerInit_ || loadedName_.empty())
    {
        ImGui::TextDisabled("No map loaded. Pick a map in the World Browser, then fly (Fly camera).");
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

    ImGui::SameLine();
    ImGui::Checkbox("Formations", &showFormations_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw creature_formations leader/member links for the open map.");

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
    if (brushActive_)
        ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.42f, 1.0f),
                           "Placement brush active: %s entry %u — right-click terrain; Esc stops.",
                           brushKind_ == 0 ? "NPC" : "GameObject", brushEntry_);

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

    // Assemble the live spawn-visibility filter once; picking, rendering, and the World Outliner
    // share this exact interpretation of phases/difficulty/events/pools/groups.
    const SpawnFilter filter = CurrentSpawnFilter();

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

    // Right-click on terrain (no drag) opens the "add object here" popup, or places/moves an armed
    // waypoint. Escape cancels the active terrain waypoint tool without changing the working route.
    if (editMode_)
        HandleRightClickAdd(view, proj, p0, w, h, hovered, focus, filter);
    if (editMode_ && hovered && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        if (waypointPlacementMode_ != WaypointPlacementMode::None)
        {
            waypointPlacementMode_ = WaypointPlacementMode::None;
            waypointStatus_ = "Terrain waypoint tool cancelled.";
        }
        else if (brushActive_)
        {
            brushActive_ = false;
            brushStatus_ = "Placement brush cancelled.";
        }
    }
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

    // The route overlay is editor UI, not an engine primitive: it remains crisp at every zoom level,
    // labels the ordered points, and is drawn above the final 3D image but below the transform gizmo.
    // It stays visible in read-only view mode; point picking/terrain tools still require Edit.
    DrawWaypointOverlay(view, proj, p0, w, h);
    DrawFormationOverlay(view, proj, p0, w, h);

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
    outlinerDirty_ = true;
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

// ---------------------------------------------------------------------------
// Waypoint Path editor
// ---------------------------------------------------------------------------

void AdtViewerModule::ResetWaypointPathEditor()
{
    waypointEdit_ = WaypointPath{};
    waypointEditOrig_ = WaypointPath{};
    waypointUndo_.Clear();
    waypointEditGuid_ = 0;
    waypointEditEntry_ = 0;
    waypointBindId_ = 0;
    waypointSelected_ = -1;
    waypointSource_ = WaypointPathSource::None;
    waypointPlacementMode_ = WaypointPlacementMode::None;
    waypointLoaded_ = false;
    waypointDirty_ = false;
    waypointStatus_.clear();
}

// Bring the route working copy in sync with the selected NPC. Deliberately do not replace a dirty
// route just because the user clicked another actor: the panel instead offers an explicit
// Save/Discard choice, which keeps a terrain-placement click from silently losing authored work.
void AdtViewerModule::SyncWaypointPathToSelection(bool discardCurrent)
{
    if (discardCurrent)
    {
        waypointDirty_ = false;
        waypointPlacementMode_ = WaypointPlacementMode::None;
    }
    if (waypointDirty_)
        return;
    if (selKind_ != SelKind::Npc)
    {
        ResetWaypointPathEditor();
        return;
    }
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn)
    {
        ResetWaypointPathEditor();
        return;
    }
    if (!discardCurrent && waypointEditGuid_ == spawn->guid &&
        ((waypointLoaded_ && waypointEdit_.id == spawn->pathId) ||
         (!waypointLoaded_ && spawn->pathId == 0)))
        return;

    waypointEdit_ = WaypointPath{};
    waypointEditOrig_ = WaypointPath{};
    waypointUndo_.Clear();
    waypointEditGuid_ = spawn->guid;
    waypointEditEntry_ = spawn->entry;
    waypointBindId_ = spawn->pathId;
    waypointSelected_ = -1;
    waypointSource_ = spawn->PathSource();
    waypointPlacementMode_ = WaypointPlacementMode::None;
    waypointLoaded_ = false;
    waypointDirty_ = false;
    waypointStatus_.clear();

    if (spawn->pathId == 0)
    {
        waypointStatus_ = "No waypoint path is assigned to this spawn.";
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        waypointStatus_ = "Connect a project database to load the waypoint path.";
        return;
    }
    WaypointPath loaded;
    const DbError e = spawnRepo_.LoadWaypointPath(*svc_->activeDb, spawn->pathId, loaded);
    if (!e.ok)
    {
        waypointStatus_ = "Path load failed: " + e.message;
        return;
    }
    waypointEdit_ = loaded;
    waypointEditOrig_ = loaded;
    waypointUndo_.Reset(loaded);
    waypointLoaded_ = true;
    if (!loaded.points.empty())
        waypointSelected_ = 0;
}

void AdtViewerModule::MarkWaypointPathDirty()
{
    if (!waypointLoaded_ || waypointEditGuid_ == 0)
        return;
    waypointDirty_ = true;
    // The NPC layer owns a separate, per-instance simulation cache. Updating it here gives the
    // artist immediate visual feedback for table edits, terrain placement, and route reordering.
    npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
}

void AdtViewerModule::ReindexWaypointPoints()
{
    for (size_t i = 0; i < waypointEdit_.points.size(); ++i)
        waypointEdit_.points[i].point = static_cast<uint32_t>(i + 1);  // TC convention: 1-based route points
    if (waypointSelected_ >= static_cast<int>(waypointEdit_.points.size()))
        waypointSelected_ = static_cast<int>(waypointEdit_.points.size()) - 1;
}

void AdtViewerModule::AddWaypointAt(const glm::vec3& world)
{
    if (!waypointLoaded_ || waypointEditGuid_ == 0)
        return;
    const WaypointPath before = waypointEdit_;
    WaypointPoint p;
    p.x = world.x;
    p.y = world.y;
    p.z = world.z;
    p.actionChance = 100;
    p.sourceExists = false;  // inserted row, even if it was duplicated from another point
    const int after = std::clamp(waypointSelected_ + 1, 0, static_cast<int>(waypointEdit_.points.size()));
    waypointEdit_.points.insert(waypointEdit_.points.begin() + after, p);
    waypointSelected_ = after;
    ReindexWaypointPoints();
    waypointUndo_.Push(before);
    MarkWaypointPathDirty();
}

void AdtViewerModule::MoveSelectedWaypointTo(const glm::vec3& world)
{
    if (!waypointLoaded_ || waypointSelected_ < 0 ||
        waypointSelected_ >= static_cast<int>(waypointEdit_.points.size()))
        return;
    const WaypointPath before = waypointEdit_;
    WaypointPoint& p = waypointEdit_.points[waypointSelected_];
    p.x = world.x;
    p.y = world.y;
    p.z = world.z;
    waypointUndo_.Push(before);
    MarkWaypointPathDirty();
}

void AdtViewerModule::SaveWaypointPathEdit()
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || !waypointLoaded_ || waypointEdit_.id == 0)
        return;
    const DbError e = spawnRepo_.SaveWaypointPath(*svc_->activeDb, waypointEdit_);
    if (!e.ok)
    {
        waypointStatus_ = "Save failed: " + e.message;
        return;
    }
    // The saved route has a fresh identity baseline. On a later reorder the repository will update
    // these exact rows in place, retaining custom waypoint_data columns.
    for (WaypointPoint& p : waypointEdit_.points)
    {
        p.sourcePoint = p.point;
        p.sourceExists = true;
    }
    waypointEditOrig_ = waypointEdit_;
    waypointUndo_.Reset(waypointEdit_);
    waypointDirty_ = false;
    npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
    waypointStatus_ = "Saved path " + std::to_string(waypointEdit_.id) + " (" +
                      std::to_string(waypointEdit_.points.size()) + " points).";
    saveStatus_ = waypointStatus_;
    if (svc_->setStatus)
        svc_->setStatus(waypointStatus_);
}

// Allocate a path and bind it to this one creature. A new empty route starts with a point at the
// spawn home; that makes it immediately visible and ensures the allocated id has a waypoint_data row.
void AdtViewerModule::CreateOrCloneLocalWaypointPath(bool cloneCurrent)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || selKind_ != SelKind::Npc)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn)
        return;
    uint32_t id = 0;
    const DbError idResult = spawnRepo_.NextWaypointPathId(*svc_->activeDb, id);
    if (!idResult.ok)
    {
        waypointStatus_ = "Could not allocate path: " + idResult.message;
        return;
    }

    WaypointPath created;
    if (cloneCurrent && waypointLoaded_)
        created = waypointEdit_;
    else
    {
        WaypointPoint home;
        home.point = 1;
        home.x = spawn->x;
        home.y = spawn->y;
        home.z = spawn->z;
        home.actionChance = 100;
        created.points.push_back(home);
    }
    created.id = id;
    if (created.points.empty())  // defensive: routes must have at least one point to reserve their id
    {
        WaypointPoint home;
        home.point = 1;
        home.x = spawn->x;
        home.y = spawn->y;
        home.z = spawn->z;
        home.actionChance = 100;
        created.points.push_back(home);
    }
    for (WaypointPoint& p : created.points)
    {
        // `sourcePoint` is meaningful only for rows under created.id. The repository sees no rows
        // for a new id and inserts them, then we establish a new source baseline after success.
        p.sourceExists = false;
        p.sourcePoint = 0;
    }
    for (size_t i = 0; i < created.points.size(); ++i)
        created.points[i].point = static_cast<uint32_t>(i + 1);

    const DbError e = spawnRepo_.SaveWaypointPathAndBindCreature(*svc_->activeDb, created, spawn->guid,
                                                                   spawn->entry, enableWaypointMotionOnBind_);
    if (!e.ok)
    {
        waypointStatus_ = "Create local path failed: " + e.message;
        return;
    }
    for (WaypointPoint& p : created.points)
    {
        p.sourcePoint = p.point;
        p.sourceExists = true;
    }
    const uint8_t movement = enableWaypointMotionOnBind_ ? 2 : spawn->movementType;
    npcLayer_.SetSpawnPathBinding(spawn->guid, created.id, created.id, spawn->templatePathId, true, movement);
    npcLayer_.SetWaypointPath(spawn->guid, created);
    if (npcEditGuid_ == spawn->guid && enableWaypointMotionOnBind_)
    {
        npcEdit_.movementType = 2;
        npcEdit_.wanderDistance = 0.0f;
        npcEditOrig_ = npcEdit_;
        npcEditDirty_ = false;
    }
    waypointEdit_ = created;
    waypointEditOrig_ = created;
    waypointUndo_.Reset(created);
    waypointEditGuid_ = spawn->guid;
    waypointEditEntry_ = spawn->entry;
    waypointBindId_ = created.id;
    waypointSource_ = WaypointPathSource::SpawnAddon;
    waypointLoaded_ = true;
    waypointDirty_ = false;
    waypointSelected_ = created.points.empty() ? -1 : 0;
    waypointPlacementMode_ = WaypointPlacementMode::None;
    waypointStatus_ = "Created local path " + std::to_string(created.id) + ".";
    saveStatus_ = waypointStatus_;
    if (svc_->setStatus)
        svc_->setStatus(waypointStatus_);
}

void AdtViewerModule::BindExistingWaypointPath(uint32_t pathId)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || selKind_ != SelKind::Npc || pathId == 0)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn)
        return;
    const DbError e = spawnRepo_.BindCreatureWaypointPath(*svc_->activeDb, spawn->guid, spawn->entry,
                                                            pathId, enableWaypointMotionOnBind_);
    if (!e.ok)
    {
        waypointStatus_ = "Assign path failed: " + e.message;
        return;
    }
    const uint8_t movement = enableWaypointMotionOnBind_ ? 2 : spawn->movementType;
    npcLayer_.SetSpawnPathBinding(spawn->guid, pathId, pathId, spawn->templatePathId, true, movement);
    if (npcEditGuid_ == spawn->guid && enableWaypointMotionOnBind_)
    {
        npcEdit_.movementType = 2;
        npcEdit_.wanderDistance = 0.0f;
        npcEditOrig_ = npcEdit_;
        npcEditDirty_ = false;
    }
    waypointDirty_ = false;
    waypointEditGuid_ = 0;  // force a fresh read of the assigned path below
    SyncWaypointPathToSelection(true);
    waypointStatus_ = "Assigned local path " + std::to_string(pathId) + ".";
    saveStatus_ = waypointStatus_;
    if (svc_->setStatus)
        svc_->setStatus(waypointStatus_);
}

void AdtViewerModule::ClearLocalWaypointPath()
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || selKind_ != SelKind::Npc)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn || !spawn->hasSpawnAddon || spawn->spawnPathId == 0)
        return;
    const uint32_t templatePath = spawn->templatePathId;
    const uint8_t movement = spawn->movementType;
    const DbError e = spawnRepo_.ClearCreatureWaypointPath(*svc_->activeDb, spawn->guid);
    if (!e.ok)
    {
        waypointStatus_ = "Clear local path failed: " + e.message;
        return;
    }
    // Preserve the creature_addon row (it may carry mounts/auras/emotes). TrinityCore chooses that
    // row as a whole, so path_id=0 means this spawn has no route rather than inheriting the template.
    npcLayer_.SetSpawnPathBinding(spawn->guid, 0, 0, templatePath, true, movement);
    waypointDirty_ = false;
    waypointEditGuid_ = 0;
    SyncWaypointPathToSelection(true);
    waypointStatus_ = templatePath ? "Cleared the local path (the existing spawn addon prevents template-path fallback)."
                                   : "Cleared the local waypoint path.";
    saveStatus_ = waypointStatus_;
    if (svc_->setStatus)
        svc_->setStatus(waypointStatus_);
}

void AdtViewerModule::EnableSelectedNpcWaypointMotion()
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || selKind_ != SelKind::Npc)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn || spawn->pathId == 0)
        return;
    const DbError e = spawnRepo_.EnableCreatureWaypointMotion(*svc_->activeDb, spawn->guid);
    if (!e.ok)
    {
        waypointStatus_ = "Enable waypoint motion failed: " + e.message;
        return;
    }
    // Preserve the live cached route while changing only the movement generator.
    npcLayer_.SetSpawnPathBinding(spawn->guid, spawn->pathId, spawn->spawnPathId,
                                  spawn->templatePathId, spawn->hasSpawnAddon, 2);
    if (waypointLoaded_ && waypointEditGuid_ == spawn->guid)
        npcLayer_.SetWaypointPath(spawn->guid, waypointEdit_);
    if (npcEditGuid_ == spawn->guid)
    {
        npcEdit_.movementType = 2;
        npcEdit_.wanderDistance = 0.0f;
        npcEditOrig_ = npcEdit_;
        npcEditDirty_ = false;
    }
    waypointStatus_ = "Waypoint movement enabled for guid " + std::to_string(spawn->guid) + ".";
    saveStatus_ = waypointStatus_;
    if (svc_->setStatus)
        svc_->setStatus(waypointStatus_);
}

bool AdtViewerModule::TrySelectWaypointOverlay(const glm::mat4& view, const glm::mat4& proj,
                                                const ImVec2& p0, int w, int h,
                                                bool viewportHovered)
{
    if (!viewportHovered || !showWaypointOverlay_ || !waypointLoaded_ ||
        waypointEditGuid_ == 0 || waypointEditGuid_ != selGuid_ ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec3 origin = streamer_.origin();
    int best = -1;
    float bestD2 = 13.0f * 13.0f;
    for (int i = 0; i < static_cast<int>(waypointEdit_.points.size()); ++i)
    {
        const WaypointPoint& p = waypointEdit_.points[i];
        ImVec2 at;
        if (!ProjectWorldPoint(glm::vec3(p.x, p.y, p.z), origin, view, proj, p0, w, h, at))
            continue;
        const float dx = at.x - mouse.x;
        const float dy = at.y - mouse.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = i;
        }
    }
    if (best < 0)
        return false;
    waypointSelected_ = best;
    return true;
}

void AdtViewerModule::DrawWaypointOverlay(const glm::mat4& view, const glm::mat4& proj,
                                          const ImVec2& p0, int w, int h)
{
    if (!showWaypointOverlay_ || !waypointLoaded_ || waypointEditGuid_ == 0 ||
        waypointEditGuid_ != selGuid_ || waypointEdit_.points.empty())
        return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const glm::vec3 origin = streamer_.origin();
    std::vector<ImVec2> projected(waypointEdit_.points.size());
    std::vector<uint8_t> visible(waypointEdit_.points.size(), 0);
    for (size_t i = 0; i < waypointEdit_.points.size(); ++i)
        visible[i] = ProjectWorldPoint(glm::vec3(waypointEdit_.points[i].x, waypointEdit_.points[i].y,
                                                  waypointEdit_.points[i].z),
                                       origin, view, proj, p0, w, h, projected[i]) ? 1u : 0u;

    const ImU32 line = IM_COL32(84, 207, 255, 220);
    for (size_t i = 1; i < projected.size(); ++i)
        if (visible[i - 1] && visible[i])
            draw->AddLine(projected[i - 1], projected[i], line, 2.0f);
    // Waypoint motion loops by default. A dim return segment makes that behavior clear without
    // confusing the ordered forward path.
    if (projected.size() > 2 && visible.front() && visible.back())
        draw->AddLine(projected.back(), projected.front(), IM_COL32(84, 207, 255, 105), 1.0f);

    for (size_t i = 0; i < projected.size(); ++i)
    {
        if (!visible[i])
            continue;
        const bool selected = static_cast<int>(i) == waypointSelected_;
        const ImU32 fill = selected ? IM_COL32(255, 193, 68, 255) : IM_COL32(35, 141, 228, 245);
        const ImU32 outline = selected ? IM_COL32(255, 244, 205, 255) : IM_COL32(220, 246, 255, 230);
        draw->AddCircleFilled(projected[i], selected ? 7.0f : 5.5f, fill, 12);
        draw->AddCircle(projected[i], selected ? 7.0f : 5.5f, outline, 12, 1.5f);
        const std::string label = std::to_string(waypointEdit_.points[i].point);
        draw->AddText(ImVec2(projected[i].x + 8.0f, projected[i].y - 8.0f), IM_COL32(255, 255, 255, 245),
                      label.c_str());
    }
}

void AdtViewerModule::DrawWaypointPathPanel()
{
    if (!ImGui::Begin("Waypoint Path"))
    {
        ImGui::End();
        return;
    }
    if (selKind_ != SelKind::Npc)
    {
        if (waypointDirty_)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                               "Path %u for guid %u still has unsaved edits.", waypointEdit_.id, waypointEditGuid_);
            if (ImGui::Button("Save current path"))
                SaveWaypointPathEdit();
            ImGui::SameLine();
            if (ImGui::Button("Discard route edits"))
            {
                waypointEdit_ = waypointEditOrig_;
                waypointUndo_.Reset(waypointEdit_);
                waypointDirty_ = false;
                waypointPlacementMode_ = WaypointPlacementMode::None;
                npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
                waypointStatus_ = "Discarded unsaved route edits.";
            }
            if (!waypointStatus_.empty())
                ImGui::TextDisabled("%s", waypointStatus_.c_str());
        }
        else
        {
            ResetWaypointPathEditor();
            ImGui::TextWrapped("Select an NPC in the World Editor to view or author its waypoint route.");
        }
        ImGui::End();
        return;
    }
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        ImGui::TextWrapped("Connect a project DB to load and edit waypoint paths.");
        ImGui::End();
        return;
    }

    // A dirty route remains pinned to its original NPC. Give the user a conscious choice rather
    // than silently replacing it when a different NPC is selected in the viewport.
    if (waypointDirty_ && waypointEditGuid_ != selGuid_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                           "Path %u for guid %u has unsaved edits.", waypointEdit_.id, waypointEditGuid_);
        ImGui::TextWrapped("Save it, or discard it before loading the newly selected NPC's route.");
        if (ImGui::Button("Save current path"))
            SaveWaypointPathEdit();
        ImGui::SameLine();
        if (ImGui::Button("Discard and load selected"))
            SyncWaypointPathToSelection(true);
        if (!waypointStatus_.empty())
            ImGui::TextDisabled("%s", waypointStatus_.c_str());
        ImGui::End();
        return;
    }

    SyncWaypointPathToSelection();
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn)
    {
        ImGui::TextDisabled("The selected NPC is no longer available on this map.");
        ImGui::End();
        return;
    }

    const std::string creature = svc_->lookups ? svc_->lookups->LabelCreature(spawn->entry)
                                                : ("entry " + std::to_string(spawn->entry));
    ImGui::TextUnformatted(creature.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("guid %u", spawn->guid);
    ImGui::Checkbox("Show route in world", &showWaypointOverlay_);
    ImGui::SameLine();
    ImGui::Checkbox("Enable motion when assigning", &enableWaypointMotionOnBind_);

    ImGui::Separator();
    ImGui::Text("Source: %s", WaypointSourceLabel(waypointSource_));
    if (waypointSource_ == WaypointPathSource::TemplateAddon)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.77f, 0.24f, 1.0f),
                           "This is shared by the template; saving edits affects every spawn using it.");
        ImGui::BeginDisabled(!waypointLoaded_);
        if (ImGui::Button("Make local copy"))
            CreateOrCloneLocalWaypointPath(true);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("recommended before map-specific edits");
    }
    else if (waypointSource_ == WaypointPathSource::SpawnAddon)
    {
        if (spawn->spawnPathId != 0)
        {
            if (ImGui::Button("Clear local path"))
                ClearLocalWaypointPath();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Keep this spawn's addon fields but set path_id to 0. In TrinityCore an existing creature_addon row does not fall back to the template route.");
        }
        else
            ImGui::TextDisabled("This spawn addon has path_id 0, so it currently suppresses the template route.");
        if (spawn->templatePathId != 0)
        {
            ImGui::SameLine();
            if (ImGui::Button("Use template path ID locally"))
                BindExistingWaypointPath(spawn->templatePathId);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Copies the template route id into this spawn's existing addon without removing its other addon fields.");
        }
    }

    ImGui::SetNextItemWidth(150.0f);
    InputU32("Path ID", waypointBindId_);
    ImGui::SameLine();
    if (ImGui::Button("Assign as local route") && waypointBindId_ != 0)
        BindExistingWaypointPath(waypointBindId_);
    ImGui::SameLine();
    ImGui::BeginDisabled(waypointDirty_);
    if (ImGui::Button(waypointLoaded_ ? "New local route" : "Create local route"))
        CreateOrCloneLocalWaypointPath(false);
    ImGui::EndDisabled();
    if (waypointDirty_ && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Save or revert the current route first. Use 'Make local copy' to preserve its edits.");

    if (!waypointLoaded_)
    {
        if (!waypointStatus_.empty())
            ImGui::TextDisabled("%s", waypointStatus_.c_str());
        ImGui::TextWrapped("Assign an existing Path ID, or create a local route. New routes start at the NPC's home position; use \"Place on terrain\" to add more points from the 3D world.");
        ImGui::End();
        return;
    }

    if (spawn->movementType != 2)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.77f, 0.24f, 1.0f),
                           "This spawn is not using waypoint movement yet.");
        ImGui::SameLine();
        if (ImGui::Button("Enable waypoint movement"))
            EnableSelectedNpcWaypointMotion();
    }

    ImGui::SeparatorText(("Path " + std::to_string(waypointEdit_.id) + " — " +
                          std::to_string(waypointEdit_.points.size()) + " points").c_str());
    ImGui::BeginDisabled(!waypointDirty_);
    if (ImGui::Button("Save route"))
        SaveWaypointPathEdit();
    ImGui::SameLine();
    if (ImGui::Button("Revert route"))
    {
        waypointEdit_ = waypointEditOrig_;
        waypointUndo_.Reset(waypointEdit_);
        waypointDirty_ = false;
        waypointPlacementMode_ = WaypointPlacementMode::None;
        waypointSelected_ = waypointEdit_.points.empty() ? -1 :
                             std::min(waypointSelected_, static_cast<int>(waypointEdit_.points.size()) - 1);
        npcLayer_.SetWaypointPath(waypointEditGuid_, waypointEdit_);
        waypointStatus_ = "Reverted unsaved route edits.";
    }
    ImGui::EndDisabled();
    if (waypointDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f), "unsaved preview");
    }
    if (!waypointStatus_.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", waypointStatus_.c_str());
    }

    if (ImGui::Button("Add at NPC home"))
        AddWaypointAt(glm::vec3(spawn->x, spawn->y, spawn->z));
    ImGui::SameLine();
    const bool addMode = waypointPlacementMode_ == WaypointPlacementMode::Add;
    if (ImGui::Button(addMode ? "Stop placing" : "Place on terrain"))
        waypointPlacementMode_ = addMode ? WaypointPlacementMode::None : WaypointPlacementMode::Add;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("While active, right-click terrain in the World Editor to append a waypoint after the selected point.");
    ImGui::SameLine();
    const bool canMove = waypointSelected_ >= 0 && waypointSelected_ < static_cast<int>(waypointEdit_.points.size());
    ImGui::BeginDisabled(!canMove);
    const bool moveMode = waypointPlacementMode_ == WaypointPlacementMode::MoveSelected;
    if (ImGui::Button(moveMode ? "Stop moving point" : "Move selected on terrain"))
        waypointPlacementMode_ = moveMode ? WaypointPlacementMode::None : WaypointPlacementMode::MoveSelected;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Renumber 1..N"))
    {
        const WaypointPath before = waypointEdit_;
        ReindexWaypointPoints();
        waypointUndo_.Push(before);
        MarkWaypointPathDirty();
    }
    if (waypointPlacementMode_ != WaypointPlacementMode::None)
        ImGui::TextColored(ImVec4(0.35f, 0.78f, 1.0f, 1.0f), "Terrain tool active — right-click the ground in the World Editor.");

    const WaypointPath tableBefore = waypointEdit_;
    int moveUp = -1, moveDown = -1, erase = -1, duplicate = -1;
    bool changed = false;
    ImGui::BeginChild("##waypoint_rows", ImVec2(0, 270), true);
    if (ImGui::BeginTable("##waypoints", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                               ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Position (X, Y, Z)", ImGuiTableColumnFlags_WidthFixed, 250.0f);
        ImGui::TableSetupColumn("Facing");
        ImGui::TableSetupColumn("Delay");
        ImGui::TableSetupColumn("Move");
        ImGui::TableSetupColumn("Event");
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("%");
        ImGui::TableSetupColumn("Tools");
        ImGui::TableHeadersRow();
        static const char* kMoveTypes[] = {"Walk", "Run", "Land", "Take off"};
        for (int i = 0; i < static_cast<int>(waypointEdit_.points.size()); ++i)
        {
            WaypointPoint& p = waypointEdit_.points[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(std::to_string(p.point).c_str(), i == waypointSelected_))
                waypointSelected_ = i;
            ImGui::TableSetColumnIndex(1);
            float pos[3] = {p.x, p.y, p.z};
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::InputFloat3("##pos", pos, "%.3f"))
            {
                p.x = pos[0]; p.y = pos[1]; p.z = pos[2];
                changed = true;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(85.0f);
            changed |= InputFloatField("##face", p.o);
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(80.0f);
            changed |= InputU32("##delay", p.delay);
            ImGui::TableSetColumnIndex(4);
            int move = std::min<int>(p.moveType, IM_ARRAYSIZE(kMoveTypes) - 1);
            ImGui::SetNextItemWidth(82.0f);
            if (ImGui::Combo("##move", &move, kMoveTypes, IM_ARRAYSIZE(kMoveTypes)))
            {
                p.moveType = static_cast<uint8_t>(move);
                changed = true;
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::SetNextItemWidth(60.0f);
            changed |= InputU8("##event", p.moveEvent);
            ImGui::TableSetColumnIndex(6);
            ImGui::SetNextItemWidth(86.0f);
            changed |= InputU32("##action", p.action);
            ImGui::TableSetColumnIndex(7);
            ImGui::SetNextItemWidth(52.0f);
            if (InputU8("##chance", p.actionChance))
            {
                p.actionChance = std::min<uint8_t>(100, p.actionChance);
                changed = true;
            }
            ImGui::TableSetColumnIndex(8);
            if (ImGui::SmallButton("^")) moveUp = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("v")) moveDown = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) duplicate = i;
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) erase = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    bool structural = false;
    if (moveUp > 0)
    {
        std::swap(waypointEdit_.points[moveUp], waypointEdit_.points[moveUp - 1]);
        waypointSelected_ = moveUp - 1;
        structural = true;
    }
    else if (moveDown >= 0 && moveDown + 1 < static_cast<int>(waypointEdit_.points.size()))
    {
        std::swap(waypointEdit_.points[moveDown], waypointEdit_.points[moveDown + 1]);
        waypointSelected_ = moveDown + 1;
        structural = true;
    }
    else if (duplicate >= 0)
    {
        WaypointPoint copy = waypointEdit_.points[duplicate];
        copy.sourceExists = false;
        copy.sourcePoint = 0;
        waypointEdit_.points.insert(waypointEdit_.points.begin() + duplicate + 1, copy);
        waypointSelected_ = duplicate + 1;
        structural = true;
    }
    else if (erase >= 0)
    {
        if (waypointEdit_.points.size() <= 1)
            waypointStatus_ = "A route must keep at least one point. Clear the local path or assign another route instead.";
        else
        {
            waypointEdit_.points.erase(waypointEdit_.points.begin() + erase);
            waypointSelected_ = std::min(erase, static_cast<int>(waypointEdit_.points.size()) - 1);
            structural = true;
        }
    }
    if (structural)
    {
        ReindexWaypointPoints();
        changed = true;
    }
    if (changed)
    {
        waypointUndo_.Push(tableBefore);
        MarkWaypointPathDirty();
    }

    if (waypointSelected_ >= 0 && waypointSelected_ < static_cast<int>(waypointEdit_.points.size()) &&
        ImGui::CollapsingHeader("Selected point details"))
    {
        WaypointPoint& point = waypointEdit_.points[waypointSelected_];
        if (BeginFieldTable("waypointdetail", 170.0f))
        {
            FieldRow("Waypoint GUID (wpguid)", "Optional script-facing waypoint identifier. Leave 0 unless a script references it.");
            const WaypointPath beforeDetail = waypointEdit_;
            if (InputU32("##wpguid", point.waypointGuid))
            {
                waypointUndo_.Push(beforeDetail);
                MarkWaypointPathDirty();
            }
            FieldRow("Source row", "The original point number used to update this row in place. New points have no source row until saved.");
            ImGui::TextDisabled(point.sourceExists ? ("point " + std::to_string(point.sourcePoint)).c_str()
                                                    : "new point");
            EndFieldTable();
        }
        if (ImGui::Button("Frame selected point"))
        {
            const glm::vec3 origin = streamer_.origin();
            camera_.Frame(glm::vec3(point.x - origin.x, point.y - origin.y, point.z), 25.0f);
        }
    }

    ImGui::TextDisabled("Click a blue route marker in the world to select it. Orientation 0 keeps the creature's movement-facing; non-zero facing is applied on arrival.");
    ImGui::End();
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
    outlinerDirty_ = true;
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
    if (ImGui::Button("Snap to ground"))
        SnapSelectionToGround();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Raycast terrain below the selected placement, then save it as one undoable transform.");
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

    // Route markers have priority over world-model picking. Otherwise a click on a waypoint sitting
    // inside an NPC's model would deselect the route instead of selecting its point for terrain moves.
    if (TrySelectWaypointOverlay(view, proj, p0, w, h, viewportHovered))
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
    // be grabbed. The window list belongs to the World Editor (stable ImGui ID "ADT Viewer"), so hover works. RunGizmo is called AFTER
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

// Snap the selected placement's origin to the terrain directly below it. Unlike simply changing
// Z from a field, this uses the same loaded ADT triangles as right-click placement, works in the
// streamer's local frame, persists through the normal save path, and records one undoable transform.
void AdtViewerModule::SnapSelectionToGround()
{
    if (selKind_ == SelKind::None)
        return;

    glm::vec3 local;
    const glm::vec3 origin = streamer_.origin();
    if (selKind_ == SelKind::Doodad)
    {
        glm::mat4 transform(1.0f);
        if (!streamer_.ObjectTransform(selUid_, transform))
        {
            ClearSelection();
            return;
        }
        local = glm::vec3(transform[3]);
    }
    else if (selKind_ == SelKind::GameObject)
    {
        const MapGameObject* g = goLayer_.FindSpawn(selGuid_);
        if (!g)
        {
            ClearSelection();
            return;
        }
        local = glm::vec3(g->x - origin.x, g->y - origin.y, g->z);
    }
    else
    {
        const MapSpawn* n = npcLayer_.FindSpawn(selGuid_);
        if (!n)
        {
            ClearSelection();
            return;
        }
        local = glm::vec3(n->x - origin.x, n->y - origin.y, n->z);
    }

    glm::vec3 ground;
    float hitT = -1.0f;
    int tileX = 0, tileY = 0;
    // Client terrain is far below this conservative top-of-world start. WMO-only maps have no ADT
    // ground hit and safely report the explanatory status below.
    const glm::vec3 rayOrigin(local.x, local.y, 10000.0f);
    if (!streamer_.GroundHit(rayOrigin, glm::vec3(0.0f, 0.0f, -1.0f), ground, hitT, tileX, tileY))
    {
        saveStatus_ = "No loaded terrain below this selection — fly closer to an ADT tile and try again.";
        return;
    }

    const AdtXform before = CaptureSelection();
    if (selKind_ == SelKind::Doodad)
    {
        glm::mat4 transform(1.0f);
        if (!streamer_.ObjectTransform(selUid_, transform))
            return;
        transform[3].z = ground.z;
        streamer_.SetObjectTransform(selUid_, transform);
    }
    else if (selKind_ == SelKind::GameObject)
    {
        if (MapGameObject* g = goLayer_.FindSpawn(selGuid_))
            g->z = ground.z;
    }
    else if (const MapSpawn* n = npcLayer_.FindSpawn(selGuid_))
    {
        npcLayer_.SetSpawnHome(selGuid_, n->x, n->y, ground.z, n->o);
    }

    const AdtXform after = CaptureSelection();
    if (after.kind == SelKind::None || SameXform(before, after))
    {
        saveStatus_ = "Selection is already on the terrain.";
        return;
    }
    CommitSelectionToDb();
    undo_.Push(MakeCommand([this, before]() { ApplyAndPersist(before); },
                           [this, after]() { ApplyAndPersist(after); }, "Snap object to terrain"));
    const std::string snapped = "Snapped to terrain (tile " + std::to_string(tileX) + ", " +
                                std::to_string(tileY) + ").";
    if (saveStatus_.rfind("Save failed:", 0) != 0)
        saveStatus_ = snapped + " " + saveStatus_;
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
    if (selKind_ == SelKind::GameObject || selKind_ == SelKind::Npc)
        outlinerDirty_ = true;
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
        outlinerDirty_ = true;
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
        outlinerDirty_ = true;
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
        outlinerDirty_ = true;
    }
    else if (d.kind == SelKind::Npc)
    {
        npcLayer_.RemoveSpawn(d.guid);
        if (svc_ && svc_->connected && svc_->activeDb)
            spawnRepo_.DeleteCreatureSpawn(*svc_->activeDb, d.guid);
        outlinerDirty_ = true;
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

    // Waypoint terrain tools intentionally take precedence over object context menus: a route often
    // runs through its owning NPC/model, and the user explicitly armed this mode in the Waypoint
    // Path panel. The world hit is converted back to TrinityCore coordinates before it is stored.
    if (waypointPlacementMode_ != WaypointPlacementMode::None && waypointLoaded_ &&
        waypointEditGuid_ != 0 && waypointEditGuid_ == selGuid_ && selKind_ == SelKind::Npc)
    {
        const glm::vec3 world(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z);
        if (waypointPlacementMode_ == WaypointPlacementMode::Add)
            AddWaypointAt(world);
        else
            MoveSelectedWaypointTo(world);
        return;
    }

    // A palette brush has the next priority: an author deliberately armed it for rapid terrain
    // stamping, so don't turn the same click into an object context menu or one-shot add popup.
    if (brushActive_ && brushEntry_ != 0)
    {
        const glm::vec3 world(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z);
        if (!CanBrushPlace(world))
        {
            brushStatus_ = "Skipped placement: closer than the configured session spacing.";
            return;
        }
        const bool added = brushKind_ == 0 ? PerformAddNpcAt(brushEntry_, world, brushYaw_)
                                            : PerformAddGameObjectAt(brushEntry_, world, brushYaw_);
        if (added)
        {
            brushPlacements_.push_back({brushKind_, brushEntry_, world});
            brushStatus_ = "Placed stamp " + std::to_string(brushPlacements_.size()) +
                           " — right-click terrain to continue.";
        }
        return;
    }

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
    PerformAddNpcAt(entry, addWorldPos_, 0.0f);
}

bool AdtViewerModule::PerformAddNpcAt(uint32_t entry, const glm::vec3& world, float yaw)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Connect a project DB to add NPCs.";
        return false;
    }
    uint32_t guid = 0, displayId = 0;
    DbError e = spawnRepo_.InsertCreatureSpawn(*svc_->activeDb, currentMapId_, entry, world.x, world.y,
                                               world.z, yaw, guid, displayId);
    if (!e.ok || guid == 0)
    {
        saveStatus_ = "Add NPC failed: " + e.message;
        return false;
    }
    MapSpawn s;
    s.guid = guid; s.entry = entry;
    s.x = world.x; s.y = world.y; s.z = world.z; s.o = yaw;
    s.displayId = displayId; s.scale = 1.0f; s.phaseMask = 1; s.spawnMask = 1;
    npcLayer_.AddSpawn(s);
    outlinerDirty_ = true;
    saveStatus_ = "Added creature guid " + std::to_string(guid) + " (entry " + std::to_string(entry) + ").";
    if (svc_->setStatus) svc_->setStatus(saveStatus_);
    AdtObjectDesc d;
    d.kind = SelKind::Npc; d.guid = guid; d.entry = entry; d.displayId = displayId;
    d.x = s.x; d.y = s.y; d.z = s.z; d.o = s.o; d.size = s.scale;
    PushCreateUndo(d);
    return true;
}

void AdtViewerModule::PerformAddGameObject(uint32_t entry)
{
    PerformAddGameObjectAt(entry, addWorldPos_, 0.0f);
}

bool AdtViewerModule::PerformAddGameObjectAt(uint32_t entry, const glm::vec3& world, float yaw)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb)
    {
        saveStatus_ = "Connect a project DB to add GameObjects.";
        return false;
    }
    const float half = yaw * 0.5f;
    const float rot[4] = {0.0f, 0.0f, std::sin(half), std::cos(half)};
    uint32_t guid = 0, displayId = 0;
    DbError e = spawnRepo_.InsertGameObjectSpawn(*svc_->activeDb, currentMapId_, entry, world.x,
                                                 world.y, world.z, yaw, rot, guid, displayId);
    if (!e.ok || guid == 0)
    {
        saveStatus_ = "Add GameObject failed: " + e.message;
        return false;
    }
    MapGameObject g;
    g.guid = guid; g.entry = entry;
    g.x = world.x; g.y = world.y; g.z = world.z; g.o = yaw;
    g.rot[0] = rot[0]; g.rot[1] = rot[1]; g.rot[2] = rot[2]; g.rot[3] = rot[3];
    g.displayId = displayId; g.size = 1.0f; g.phaseMask = 1; g.spawnMask = 1;
    goLayer_.AddGameObject(g);
    outlinerDirty_ = true;
    saveStatus_ = "Added gameobject guid " + std::to_string(guid) + " (entry " + std::to_string(entry) + ").";
    if (svc_->setStatus) svc_->setStatus(saveStatus_);
    AdtObjectDesc d;
    d.kind = SelKind::GameObject; d.guid = guid; d.entry = entry; d.displayId = displayId;
    d.x = g.x; d.y = g.y; d.z = g.z; d.o = g.o; d.size = g.size;
    d.rot[0] = rot[0]; d.rot[1] = rot[1]; d.rot[2] = rot[2]; d.rot[3] = rot[3];
    PushCreateUndo(d);
    return true;
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
