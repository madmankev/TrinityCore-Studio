// AdtViewerModule — see AdtViewerModule.h.

#include "editors/adt/AdtViewerModule.h"
#include "editors/spell/SpellSchema.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <unordered_set>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "app/EditorServices.h"
#include "adt/AdtWriter.h"
#include "clientdata/ClientData.h"
#include "data/LookupCache.h"
#include "data/CoreSupport.h"
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

// Deterministic value/fractal noise for Terrainify. The resulting field is expanded
// into ordinary Raise/Lower ADT strokes, so its exact sequence—not an opaque random
// generator—is persisted, undoable, and reproduced by the staged GPU brush preview.
float NoiseHash(float x, float y, uint32_t seed)
{
    const float value = std::sin(x * 127.1f + y * 311.7f + static_cast<float>(seed) * 74.7f) * 43758.5453123f;
    return value - std::floor(value);
}

float NoiseFade(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

float ValueNoise(float x, float y, uint32_t seed)
{
    const float ix = std::floor(x);
    const float iy = std::floor(y);
    const float tx = NoiseFade(x - ix);
    const float ty = NoiseFade(y - iy);
    const float a = NoiseHash(ix, iy, seed);
    const float b = NoiseHash(ix + 1.0f, iy, seed);
    const float c = NoiseHash(ix, iy + 1.0f, seed);
    const float d = NoiseHash(ix + 1.0f, iy + 1.0f, seed);
    const float ab = a + (b - a) * tx;
    const float cd = c + (d - c) * tx;
    return (ab + (cd - ab) * ty) * 2.0f - 1.0f;
}

float FractalNoise(float x, float y, uint32_t seed, int octaves)
{
    float value = 0.0f;
    float amplitude = 1.0f;
    float totalAmplitude = 0.0f;
    for (int octave = 0; octave < std::clamp(octaves, 1, 6); ++octave)
    {
        value += ValueNoise(x, y, seed + static_cast<uint32_t>(octave) * 101u) * amplitude;
        totalAmplitude += amplitude;
        amplitude *= 0.5f;
        x *= 2.0f;
        y *= 2.0f;
    }
    return totalAmplitude > 1e-6f ? value / totalAmplitude : 0.0f;
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
    adtEdits_.SetMap("");
    terrainSculptActive_ = false;
    terrainRampHasStart_ = false;
    ++terrainHistoryGeneration_;
    terrainStatus_.clear();
    // Client/project data changed: do not carry an unsaved map-light draft into a different
    // world that happens to reuse the same map directory name.
    lightingDrafts_.clear();
    lightingMapDir_.clear();
    lightingDirty_ = false;
    runtimeTimeOfDay_ = 720.0f;
    selectedWorldLightId_ = 0;
    lightPlacementActive_ = false;
    aiBehaviorEditGuid_ = 0;
    aiBehaviorDirty_ = false;
    aiPreviewTargetEnabled_ = false;
    aiPreviewTargetPlacementActive_ = false;
    aiPreviewTargetWorld_ = glm::vec3(0.0f);
    scriptTriggerDrafts_.clear();
    scriptTriggersEdit_.clear();
    scriptTriggerRuntime_.clear();
    pendingScriptTriggerActions_.clear();
    scriptTriggerLog_.clear();
    scriptTriggerMapDir_.clear();
    scriptTriggersDirty_ = false;
    selectedScriptTriggerId_ = 0;
    scriptPreviewPlayerEnabled_ = false;
    scriptPreviewPlayerFollowCamera_ = false;
    scriptPreviewPlayerPlacementActive_ = false;
    scriptTriggerCenterPlacementActive_ = false;
    scriptInteractionMode_ = false;
    scriptPreviewPlayerWorld_ = glm::vec3(0.0f);
    lightMarkers_.clear();
    worldValidationIssues_.clear();
    worldValidationHasRun_ = false;
    worldValidationStatus_.clear();
    // Re-resolve the selected spell after the new client DBC set is open. This
    // preserves the user's preview choice while replacing safe defaults with the
    // actual WotLK cast/range/duration fields.
    LoadSpellPreviewDefinition(spellPreview_.definition.id);
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
    if (ImGui::IsKeyPressed(ImGuiKey_F, false) &&
        (selKind_ != SelKind::None || selectedWorldLightId_ != 0))
        FrameSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
    {
        // Preserve DCC-style R = scale while an object is selected. With no
        // transform target, R becomes the terrain Ramp / Stairs shortcut.
        if (editMode_ && selKind_ != SelKind::None)
        {
            if (selKind_ == SelKind::Npc)
                transformStatus_ = "NPC scale is template-owned; use the NPC Instance panel rather than a per-spawn scale gizmo.";
            else
                gizmoOp_ = ImGuizmo::SCALE;
        }
        else if (streamerInit_ && !loadedName_.empty() && !streamer_.wmoOnly())
        {
            terrainSculptMode_ = 3;
            terrainRampHasStart_ = false;
            terrainSculptActive_ = true;
            inGameViewMode_ = false;
            editMode_ = true;
            terrainStatus_ = "Ramp tool armed (R) — right-click a terrain start, then end.";
            if (svc_ && svc_->focusWindow)
                svc_->focusWindow("Terrain Sculpt");
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_N, false) && streamerInit_ && !loadedName_.empty() && !streamer_.wmoOnly())
    {
        terrainSculptMode_ = 4;
        terrainRampHasStart_ = false;
        terrainSculptActive_ = true;
        inGameViewMode_ = false;
        editMode_ = true;
        terrainStatus_ = "Terrainify noise armed (N) — right-click terrain to queue a deterministic noise stamp.";
        if (svc_ && svc_->focusWindow)
            svc_->focusWindow("Terrain Sculpt");
    }
    if (ImGui::IsKeyPressed(ImGuiKey_S, false) && streamerInit_ && !loadedName_.empty() && !streamer_.wmoOnly())
    {
        terrainSculptMode_ = 6;
        terrainRampHasStart_ = false;
        terrainSculptActive_ = true;
        inGameViewMode_ = false;
        editMode_ = true;
        terrainStatus_ = "Terrain smooth armed (S) — right-click terrain to queue a staged Laplacian-style blend.";
        if (svc_ && svc_->focusWindow)
            svc_->focusWindow("Terrain Sculpt");
    }
}

void AdtViewerModule::DrawMainMenuExtensions()
{
    // These are genuine world-authoring commands, not a second set of decorative
    // menu labels: each action arms an existing tool, switches an existing editor,
    // or raises the docked panel that owns the workflow.
    const auto focus = [this](const char* title) {
        if (svc_ && svc_->focusWindow)
            svc_->focusWindow(title);
    };
    const auto activate = [this](const char* id) {
        if (svc_ && svc_->activateModule)
            svc_->activateModule(id);
    };
    const auto status = [this](const char* text) {
        if (svc_ && svc_->setStatus)
            svc_->setStatus(text);
    };
    const auto armTerrain = [this, &focus](int mode) {
        terrainSculptMode_ = mode;
        terrainRampHasStart_ = false;
        terrainSculptActive_ = true;
        inGameViewMode_ = false;
        editMode_ = true;
        focus("Terrain Sculpt");
    };

    if (ImGui::BeginMenu("Terrain"))
    {
        if (ImGui::BeginMenu("Terrain Tools"))
        {
            if (ImGui::MenuItem("Raise", "T")) armTerrain(static_cast<int>(adt::TerrainBrushMode::Raise));
            if (ImGui::MenuItem("Lower")) armTerrain(static_cast<int>(adt::TerrainBrushMode::Lower));
            if (ImGui::MenuItem("Flatten", "L")) armTerrain(static_cast<int>(adt::TerrainBrushMode::Flatten));
            if (ImGui::MenuItem("Ramp / Stairs", "R")) armTerrain(3);
            if (ImGui::MenuItem("Noise / Terrainify", "N")) armTerrain(4);
            if (ImGui::MenuItem("Terrain Stamp / Preset")) armTerrain(5);
            if (ImGui::MenuItem("Smooth / Preserve Edges", "S")) armTerrain(6);
            ImGui::Separator();
            if (ImGui::MenuItem("Terrain Sculpt Panel...")) focus("Terrain Sculpt");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Water Tools"))
        {
            bool liquid = opt_.liquid;
            if (ImGui::MenuItem("Show Water", nullptr, &liquid))
            {
                opt_.liquid = liquid;
                if (!selectedMapDir_.empty())
                    OpenMapDir(selectedMapDir_, false);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save Pending ADT Edits", "Ctrl+S", false, !adtEdits_.empty()))
        {
            SavePendingAdtEdits();
            focus("World Editor###ADT Viewer");
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Light Editor...")) focus("Light Editor");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Objects"))
    {
        if (ImGui::BeginMenu("Transform Tools"))
        {
            if (ImGui::MenuItem("Select", "Q")) { editMode_ = true; focus("World Editor###ADT Viewer"); }
            if (ImGui::MenuItem("Move", "W")) { editMode_ = true; gizmoOp_ = ImGuizmo::TRANSLATE; focus("World Editor###ADT Viewer"); }
            if (ImGui::MenuItem("Rotate", "E")) { editMode_ = true; gizmoOp_ = ImGuizmo::ROTATE; focus("World Editor###ADT Viewer"); }
            if (ImGui::MenuItem("Scale", "R", false, selKind_ != SelKind::Npc)) { editMode_ = true; gizmoOp_ = ImGuizmo::SCALE; focus("World Editor###ADT Viewer"); }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Snap Selected to Ground", nullptr, false, selKind_ != SelKind::None))
            SnapSelectionToGround();
        if (ImGui::MenuItem("Exact Transform...", nullptr, false, selKind_ != SelKind::None)) focus("Transform");
        if (ImGui::MenuItem("Delete Selected", "Delete", false, selKind_ != SelKind::None)) DeleteSelection();
        ImGui::Separator();
        if (ImGui::MenuItem("Placement Palette...")) focus("Spawn Palette");
        if (ImGui::MenuItem("World Outliner...")) focus("World Outliner");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Creatures"))
    {
        if (ImGui::MenuItem("Place Creature Spawner...")) focus("Spawn Palette");
        if (ImGui::MenuItem("Edit Selected Spawner...", nullptr, false, selKind_ == SelKind::Npc)) focus("NPC Instance");
        ImGui::Separator();
        if (ImGui::MenuItem("Waypoint Editor...")) focus("Waypoint Path");
        if (ImGui::MenuItem("AI Behavior Configuration...")) focus("AI Behavior");
        if (ImGui::MenuItem("Creature Formation...")) focus("Formation");
        ImGui::Separator();
        ImGui::MenuItem("Show Creatures", "C", &showNpcs_);
        ImGui::MenuItem("Show Waypoints", nullptr, &showWaypointOverlay_);
        ImGui::MenuItem("Show Formations", nullptr, &showFormations_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Quest"))
    {
        if (ImGui::MenuItem("Quest Browser...")) activate("quest");
        if (ImGui::MenuItem("Script Trigger Editor...")) focus("Script Triggers");
        if (ImGui::MenuItem("Place / Move Preview Player..."))
        {
            scriptPreviewPlayerPlacementActive_ = true;
            scriptTriggerCenterPlacementActive_ = false;
            inGameViewMode_ = false;
            focus("World Editor###ADT Viewer");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Spells"))
    {
        if (ImGui::MenuItem("Spell Effect Previewer..."))
            focus("Spell Effect Previewer");
        if (ImGui::MenuItem("Open Spell Editor..."))
        {
            // The spell module owns live Spell.dbc/world DB definitions; the World
            // Editor's Script Trigger action sequence can then reference its spell ID.
            activate("spell");
            status("Opened Spell Editor. Configure a spell, then reference its ID from Script Triggers for world preview dispatch.");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Realtime Preview...")) focus("Realtime Preview");
        if (ImGui::MenuItem("Light Editor...")) focus("Light Editor");
        if (ImGui::MenuItem("Script Trigger Log...")) focus("Script Triggers");
        if (ImGui::MenuItem("World Validation...")) focus("World Validation");
        if (ImGui::MenuItem("World Statistics")) { showStats_ = true; focus("World Editor###ADT Viewer"); }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window"))
    {
        if (ImGui::MenuItem("World Browser")) focus("World Browser###ADT Browser");
        if (ImGui::MenuItem("Content / Spawn Palette")) focus("Spawn Palette");
        if (ImGui::MenuItem("Properties / Transform")) focus("Transform");
        if (ImGui::MenuItem("Hierarchy / Outliner")) focus("World Outliner");
        if (ImGui::MenuItem("Terrain Tools")) focus("Terrain Sculpt");
        if (ImGui::MenuItem("World Validation")) focus("World Validation");
        if (ImGui::MenuItem("Creature Editor")) focus("NPC Instance");
        if (ImGui::MenuItem("Quest Trigger Panel")) focus("Script Triggers");
        if (ImGui::MenuItem("Chunk / World Canvas")) focus("World Editor###ADT Viewer");
        ImGui::EndMenu();
    }
}

AdtViewerModule::WorldLightingProfile AdtViewerModule::DefaultLightingProfile()
{
    // Mirrors the fixed world lighting used before the Light Editor existed: white ambient 0.45
    // plus a slightly blue 0.55 directional sun. This makes the feature opt-in visually while
    // still giving every newly authored profile predictable game-style daylight.
    WorldLightingProfile profile;
    profile.ambientColor = glm::vec3(1.0f);
    profile.ambientIntensity = 0.45f;
    profile.sunColor = glm::vec3(1.0f);
    profile.sunIntensity = 0.55f;
    profile.sunAzimuth = 48.8f;
    profile.sunElevation = 58.2f;
    profile.fogColor = glm::vec3(0.12f, 0.12f, 0.14f);
    profile.fogStart = 500.0f;
    profile.fogEnd = 1000.0f;
    profile.dayNightCycle = false;
    profile.dayNightPlaying = true;
    profile.timeOfDayMinutes = 720.0f;
    profile.dayMinutesPerSecond = 30.0f;
    return profile;
}

const char* AdtViewerModule::WorldLightTypeName(WorldLightType type)
{
    return type == WorldLightType::Spot ? "Spot" : "Point";
}

void AdtViewerModule::UpdateRealtimePreview(float dtSeconds)
{
    // Spell effects share the same simulation pause/speed discipline as NPCs,
    // transports, particles and liquid frames, even when the day/night clock is off.
    UpdateSpellEffectPreview(dtSeconds);

    if (!lightingEdit_.dayNightCycle || !lightingEdit_.dayNightPlaying)
    {
        runtimeTimeOfDay_ = lightingEdit_.timeOfDayMinutes;
        return;
    }
    if (worldSimulationPaused_)
        return; // one Play/Pause control freezes actors, transports, effects and the environment clock
    // Clamp frame hitches so a debugger/minimize pause cannot jump an entire day on resume.
    const float step = std::clamp(dtSeconds, 0.0f, 0.10f) *
                       std::clamp(lightingEdit_.dayMinutesPerSecond, 0.01f, 3600.0f);
    runtimeTimeOfDay_ = std::fmod(runtimeTimeOfDay_ + step, 1440.0f);
    if (runtimeTimeOfDay_ < 0.0f)
        runtimeTimeOfDay_ += 1440.0f;
}

float AdtViewerModule::SpellPreviewDuration() const
{
    const SpellPreviewDefinition& spell = spellPreview_.definition;
    const float cast = std::max(0.0f, spell.castTimeSeconds);
    const float travel = std::clamp(spell.rangeYards / std::max(spell.projectileSpeed, 1.0f), 0.15f, 8.0f);
    // A duration aura/channel is represented by a sustained impact ring; instant
    // casts still retain a short visible impact tail for practical scrubbing.
    return std::max(0.4f, cast + travel + std::max(0.75f, spell.durationSeconds));
}

void AdtViewerModule::UpdateSpellEffectPreview(float dtSeconds)
{
    if (!spellPreview_.playing || worldSimulationPaused_)
        return;
    const float dt = std::clamp(dtSeconds, 0.0f, 0.10f) *
                     std::clamp(worldSimulationSpeed_ * spellPreview_.playbackSpeed, 0.05f, 16.0f);
    spellPreview_.timelineSeconds += dt;
    const float duration = SpellPreviewDuration();
    if (spellPreview_.timelineSeconds < duration)
        return;
    if (spellPreview_.loop && duration > 1e-4f)
        spellPreview_.timelineSeconds = std::fmod(spellPreview_.timelineSeconds, duration);
    else
    {
        spellPreview_.timelineSeconds = duration;
        spellPreview_.playing = false;
    }
}

void AdtViewerModule::LoadSpellPreviewDefinition(uint32_t spellId)
{
    SpellPreviewDefinition definition;
    definition.id = spellId;
    definition.name = svc_ && svc_->lookups ? svc_->lookups->NameOfSpell(spellId) : std::string();
    if (definition.name.empty())
        definition.name = "Spell " + std::to_string(spellId);

    if (svc_ && svc_->clientData && svc_->clientData->IsOpen())
    {
        Dbc spellDbc;
        if (spellDbc.Load(svc_->clientData->ReadFile("DBFilesClient\\Spell.dbc")))
        {
            for (uint32_t row = 0; row < spellDbc.RecordCount(); ++row)
            {
                if (spellDbc.GetUInt(row, spell::Id) != spellId)
                    continue;
                const std::string name = spellDbc.GetString(row, spell::SpellName);
                if (!name.empty())
                    definition.name = name;
                definition.manaCost = static_cast<float>(spellDbc.GetUInt(row, spell::ManaCost));
                definition.cooldownSeconds = static_cast<float>(spellDbc.GetUInt(row, spell::RecoveryTime)) / 1000.0f;
                const float dbcSpeed = spellDbc.GetFloat(row, spell::Speed);
                // Instant/no-missile spells store zero speed. Keep the generated
                // preview responsive with a sensible default rather than making a
                // 35-yard test trajectory take tens of seconds.
                definition.projectileSpeed = dbcSpeed > 0.1f ? dbcSpeed : 30.0f;
                definition.visualId = spellDbc.GetUInt(row, spell::SpellVisual);
                definition.iconId = spellDbc.GetUInt(row, spell::SpellIconID);
                for (int effect = 0; effect < 3; ++effect)
                    definition.effectIds[effect] = spellDbc.GetUInt(row, spell::Effect + static_cast<uint32_t>(effect));

                const uint32_t castIndex = spellDbc.GetUInt(row, spell::CastingTimeIndex);
                const uint32_t durationIndex = spellDbc.GetUInt(row, spell::DurationIndex);
                const uint32_t rangeIndex = spellDbc.GetUInt(row, spell::RangeIndex);
                auto lookupMilliseconds = [&](const char* path, uint32_t index) {
                    if (index == 0)
                        return 0.0f;
                    Dbc dbc;
                    if (!dbc.Load(svc_->clientData->ReadFile(path)))
                        return 0.0f;
                    for (uint32_t i = 0; i < dbc.RecordCount(); ++i)
                        if (dbc.GetUInt(i, 0) == index)
                        {
                            // SpellDuration uses signed milliseconds; -1 means an
                            // indefinite aura and must not become a 49-day preview.
                            const int32_t milliseconds = static_cast<int32_t>(dbc.GetUInt(i, 1));
                            return milliseconds > 0
                                ? std::clamp(static_cast<float>(milliseconds) / 1000.0f, 0.0f, 3600.0f)
                                : 0.0f;
                        }
                    return 0.0f;
                };
                definition.castTimeSeconds = lookupMilliseconds("DBFilesClient\\SpellCastTimes.dbc", castIndex);
                definition.durationSeconds = lookupMilliseconds("DBFilesClient\\SpellDuration.dbc", durationIndex);
                if (rangeIndex != 0)
                {
                    Dbc rangeDbc;
                    if (rangeDbc.Load(svc_->clientData->ReadFile("DBFilesClient\\SpellRange.dbc")))
                        for (uint32_t i = 0; i < rangeDbc.RecordCount(); ++i)
                            if (rangeDbc.GetUInt(i, 0) == rangeIndex)
                            {
                                definition.rangeYards = std::max(1.0f, rangeDbc.GetFloat(i, 3));
                                break;
                            }
                }
                break;
            }
        }
    }

    spellPreview_.definition = std::move(definition);
    spellPreview_.timelineSeconds = 0.0f;
    spellPreview_.playing = false;
    spellPreview_.status = "Loaded " + spellPreview_.definition.name + " (#" +
                           std::to_string(spellPreview_.definition.id) + ").";
}

glm::vec3 AdtViewerModule::ResolveSpellPreviewTarget(const glm::vec3& cameraWorld,
                                                      const glm::vec3& cameraForward)
{
    if (spellPreview_.targetSource == SpellPreviewTargetSource::ScriptPreviewPlayer &&
        scriptPreviewPlayerEnabled_)
        return scriptPreviewPlayerWorld_;

    if (spellPreview_.targetSource == SpellPreviewTargetSource::SelectedObject)
    {
        if (selKind_ == SelKind::Npc)
        {
            if (const MapSpawn* npc = npcLayer_.FindSpawn(selGuid_))
                return glm::vec3(npc->x, npc->y, npc->z);
        }
        else if (selKind_ == SelKind::GameObject)
        {
            if (MapGameObject* gameObject = goLayer_.FindSpawn(selGuid_))
                return glm::vec3(gameObject->x, gameObject->y, gameObject->z);
        }
        else if (selKind_ == SelKind::Doodad)
        {
            const AdtXform selected = CaptureSelection();
            if (selected.kind == SelKind::Doodad)
                return glm::vec3(selected.local[3]) + streamer_.origin();
        }
    }

    return cameraWorld + cameraForward * std::max(1.0f, spellPreview_.definition.rangeYards);
}

void AdtViewerModule::DrawSpellEffectOverlay(const glm::mat4& view, const glm::mat4& proj,
                                             const ImVec2& p0, int w, int h,
                                             const glm::vec3& cameraWorld)
{
    if (!spellPreview_.showOverlay || (!spellPreview_.playing && spellPreview_.timelineSeconds <= 0.0f))
        return;

    const glm::mat4 inverseView = glm::inverse(view);
    glm::vec3 cameraForward = -glm::vec3(inverseView[2]);
    if (glm::length(cameraForward) < 1e-5f)
        cameraForward = glm::vec3(1.0f, 0.0f, 0.0f);
    else
        cameraForward = glm::normalize(cameraForward);
    const glm::vec3 caster = cameraWorld;
    const glm::vec3 target = ResolveSpellPreviewTarget(cameraWorld, cameraForward);
    const float distance = glm::length(target - caster);
    const float cast = std::max(0.0f, spellPreview_.definition.castTimeSeconds);
    const float travel = std::clamp(distance / std::max(spellPreview_.definition.projectileSpeed, 1.0f), 0.15f, 8.0f);
    const float impactAt = cast + travel;
    const float t = std::clamp(spellPreview_.timelineSeconds, 0.0f, SpellPreviewDuration());

    ImVec4 baseColor(1.0f, 0.34f, 0.08f, 1.0f); // warm fire-like neutral preview color
    switch (spellPreview_.weather)
    {
    case 1: baseColor = ImVec4(0.34f, 0.62f, 1.0f, 1.0f); break; // Rain / frost-blue readability
    case 2: baseColor = ImVec4(0.78f, 0.90f, 1.0f, 1.0f); break;
    case 3: baseColor = ImVec4(0.72f, 0.62f, 0.94f, 1.0f); break;
    case 4: baseColor = ImVec4(0.96f, 0.72f, 0.18f, 1.0f); break;
    default: break;
    }
    const auto color = [&](float alpha, float brightness = 1.0f) {
        return IM_COL32(std::clamp(static_cast<int>(baseColor.x * brightness * 255.0f), 0, 255),
                        std::clamp(static_cast<int>(baseColor.y * brightness * 255.0f), 0, 255),
                        std::clamp(static_cast<int>(baseColor.z * brightness * 255.0f), 0, 255),
                        std::clamp(static_cast<int>(alpha * 255.0f), 0, 255));
    };
    const glm::vec3 origin = streamer_.origin();
    const auto project = [&](const glm::vec3& world, ImVec2& screen) {
        return ProjectWorldPoint(world, origin, view, proj, p0, w, h, screen);
    };
    const auto ring = [&](const glm::vec3& center, float radius, ImU32 ringColor, float thickness) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 previous;
        bool havePrevious = false;
        constexpr int segments = 40;
        for (int i = 0; i <= segments; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>(segments) * glm::two_pi<float>();
            ImVec2 current;
            if (project(center + glm::vec3(std::cos(angle) * radius, std::sin(angle) * radius, 0.0f), current))
            {
                if (havePrevious)
                    draw->AddLine(previous, current, ringColor, thickness);
                previous = current;
                havePrevious = true;
            }
            else
                havePrevious = false;
        }
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (t < cast)
    {
        const float phase = cast > 1e-5f ? t / cast : 1.0f;
        const float radius = 0.6f + phase * 1.7f;
        ring(caster, radius, color(0.75f, 0.85f + 0.15f * std::sin(t * 12.0f)), 2.0f);
        ImVec2 casterScreen;
        if (project(caster, casterScreen))
            draw->AddCircleFilled(casterScreen, 5.0f + phase * 7.0f, color(0.32f), 20);
    }
    else if (t < impactAt)
    {
        const float phase = travel > 1e-5f ? (t - cast) / travel : 1.0f;
        glm::vec3 projectile = glm::mix(caster, target, phase);
        projectile.z += std::sin(phase * glm::pi<float>()) * std::min(8.0f, distance * 0.12f);
        ImVec2 current;
        if (project(projectile, current))
        {
            for (int trail = 1; trail <= 7; ++trail)
            {
                const float earlier = std::max(0.0f, phase - static_cast<float>(trail) * 0.05f);
                glm::vec3 trailing = glm::mix(caster, target, earlier);
                trailing.z += std::sin(earlier * glm::pi<float>()) * std::min(8.0f, distance * 0.12f);
                ImVec2 prior;
                if (project(trailing, prior))
                    draw->AddLine(prior, current, color(0.54f / static_cast<float>(trail), 0.9f),
                                  std::max(1.0f, 4.0f - static_cast<float>(trail) * 0.4f));
            }
            draw->AddCircleFilled(current, 7.0f, color(0.96f, 1.3f), 20);
            draw->AddCircle(current, 11.0f, color(0.72f), 20, 2.0f);
        }
    }
    else
    {
        const float age = t - impactAt;
        const float sustain = std::max(0.75f, spellPreview_.definition.durationSeconds);
        const float fade = 1.0f - std::clamp(age / sustain, 0.0f, 1.0f);
        ring(target, 1.0f + age * 7.0f, color(0.85f * fade, 1.2f), 2.6f);
        ring(target, 2.0f + age * 3.0f, color(0.35f * fade, 0.8f), 1.2f);
        ImVec2 targetScreen;
        if (project(target, targetScreen))
        {
            draw->AddCircleFilled(targetScreen, 14.0f * fade + 3.0f, color(0.28f * fade, 1.4f), 24);
            for (int ray = 0; ray < 10; ++ray)
            {
                const float angle = static_cast<float>(ray) * glm::two_pi<float>() / 10.0f + age * 2.0f;
                draw->AddLine(targetScreen,
                              ImVec2(targetScreen.x + std::cos(angle) * (16.0f + age * 18.0f),
                                     targetScreen.y + std::sin(angle) * (16.0f + age * 18.0f)),
                              color(0.72f * fade, 1.3f), 1.5f);
            }
        }
    }
}


void AdtViewerModule::LoadSettings(const nlohmann::json& editorNode)
{
    bookmarks_.clear();
    lightingProfiles_.clear();
    lightingDrafts_.clear();
    aiBehaviorProfiles_.clear();
    scriptTriggerProfiles_.clear();
    scriptTriggerDrafts_.clear();
    scriptTriggersEdit_.clear();
    scriptTriggerRuntime_.clear();
    pendingScriptTriggerActions_.clear();
    scriptTriggerLog_.clear();
    scriptTriggerMapDir_.clear();
    scriptTriggersDirty_ = false;
    selectedScriptTriggerId_ = 0;
    nextScriptTriggerId_ = 1;
    nextScriptTriggerActionId_ = 1;
    showScriptTriggerOverlay_ = true;
    scriptPreviewPlayerEnabled_ = false;
    scriptPreviewPlayerFollowCamera_ = false;
    scriptPreviewPlayerPlacementActive_ = false;
    scriptTriggerCenterPlacementActive_ = false;
    scriptInteractionMode_ = false;
    scriptPreviewPlayerWorld_ = glm::vec3(0.0f);
    scriptPreviewTimeSeconds_ = 0.0f;
    aiBehaviorEdit_ = AiBehaviorProfileEntry{};
    aiBehaviorOrig_ = AiBehaviorProfileEntry{};
    aiBehaviorEditGuid_ = 0;
    aiBehaviorDirty_ = false;
    lightingEdit_ = DefaultLightingProfile();
    lightingMapDir_.clear();
    lightingDirty_ = false;
    selectedWorldLightId_ = 0;
    nextWorldLightId_ = 1;
    runtimeTimeOfDay_ = 720.0f;
    worldSimulationPaused_ = false;
    worldSimulationSpeed_ = 1.0f;
    inGameViewMode_ = false;
    liveTerrainPreview_ = true;
    spellPreview_ = SpellPreviewState{};

    auto finite = [](float value, float fallback) {
        return std::isfinite(value) ? value : fallback;
    };
    try
    {
        if (editorNode.contains("bookmarks") && editorNode["bookmarks"].is_array())
            for (const nlohmann::json& j : editorNode["bookmarks"])
            {
                if (!j.is_object())
                    continue;
                WorldBookmark b;
                b.name = j.value("name", std::string());
                b.mapId = j.value("mapId", 0u);
                b.mapDir = j.value("mapDir", std::string());
                b.world.x = finite(j.value("x", 0.0f), 0.0f);
                b.world.y = finite(j.value("y", 0.0f), 0.0f);
                b.world.z = finite(j.value("z", 0.0f), 0.0f);
                b.radius = std::clamp(finite(j.value("radius", 75.0f), 75.0f), 5.0f, 2000.0f);
                if (b.name.empty() || b.mapDir.empty())
                    continue;
                bookmarks_.push_back(std::move(b));
                if (bookmarks_.size() >= 100)  // settings guard; plenty for a project without bloating config
                    break;
            }

        // Lighting profiles are deliberately Studio-side data rather than an unsafe rewrite of
        // client Light*.dbc tables. Each map keeps its own editable point/spot collection and
        // atmosphere, which the renderer consumes live while the World Editor is open.
        const nlohmann::json* profiles = nullptr;
        if (editorNode.contains("worldLighting") && editorNode["worldLighting"].is_object())
        {
            const nlohmann::json& lighting = editorNode["worldLighting"];
            if (lighting.contains("profiles") && lighting["profiles"].is_array())
                profiles = &lighting["profiles"];
        }
        if (editorNode.contains("worldPreview") && editorNode["worldPreview"].is_object())
        {
            const nlohmann::json& preview = editorNode["worldPreview"];
            worldSimulationPaused_ = preview.value("simulationPaused", false);
            worldSimulationSpeed_ = std::clamp(finite(preview.value("simulationSpeed", 1.0f), 1.0f),
                                                0.05f, 8.0f);
            inGameViewMode_ = preview.value("inGameView", false);
            liveTerrainPreview_ = preview.value("liveTerrainPreview", true);
        }
        if (editorNode.contains("terrainTools") && editorNode["terrainTools"].is_object())
        {
            const nlohmann::json& terrain = editorNode["terrainTools"];
            terrainBrushRadius_ = std::clamp(finite(terrain.value("brushRadius", terrainBrushRadius_), terrainBrushRadius_), 1.0f, 100.0f);
            terrainBrushStrength_ = std::clamp(finite(terrain.value("brushStrength", terrainBrushStrength_), terrainBrushStrength_), 0.05f, 30.0f);
            terrainRampWidth_ = std::clamp(finite(terrain.value("rampWidth", terrainRampWidth_), terrainRampWidth_), 2.0f, 100.0f);
            terrainRampMaxSlopeDegrees_ = std::clamp(finite(terrain.value("rampMaxSlope", terrainRampMaxSlopeDegrees_), terrainRampMaxSlopeDegrees_), 1.0f, 60.0f);
            terrainRampStepCount_ = std::clamp(terrain.value("rampSteps", terrainRampStepCount_), 0, 32);
            terrainNoiseAmplitude_ = std::clamp(finite(terrain.value("noiseAmplitude", terrainNoiseAmplitude_), terrainNoiseAmplitude_), 0.05f, 30.0f);
            terrainNoiseFrequency_ = std::clamp(finite(terrain.value("noiseFrequency", terrainNoiseFrequency_), terrainNoiseFrequency_), 0.01f, 1.0f);
            terrainNoiseOctaves_ = std::clamp(terrain.value("noiseOctaves", terrainNoiseOctaves_), 1, 6);
            terrainNoiseSeed_ = terrain.value("noiseSeed", terrainNoiseSeed_);
        terrainStampPreset_ = std::clamp(terrain.value("stampPreset", terrainStampPreset_), 0, 3);
        terrainStampYawDegrees_ = finite(terrain.value("stampYawDegrees", terrainStampYawDegrees_), terrainStampYawDegrees_);
        terrainSmoothIterations_ = std::clamp(terrain.value("smoothIterations", terrainSmoothIterations_), 1, 8);
        terrainSmoothBlend_ = std::clamp(finite(terrain.value("smoothBlend", terrainSmoothBlend_), terrainSmoothBlend_), 0.05f, 1.0f);
        terrainSmoothPreserveEdges_ = terrain.value("smoothPreserveEdges", terrainSmoothPreserveEdges_);
        terrainSmoothEdgeThreshold_ = std::clamp(finite(terrain.value("smoothEdgeThreshold", terrainSmoothEdgeThreshold_), terrainSmoothEdgeThreshold_), 0.01f, 100.0f);
        }
        if (editorNode.contains("spawnPalette") && editorNode["spawnPalette"].is_object())
        {
            const nlohmann::json& palette = editorNode["spawnPalette"];
            brushKind_ = std::clamp(palette.value("kind", brushKind_), 0, 1);
            brushPlacementMode_ = std::clamp(palette.value("placementMode", brushPlacementMode_), 0, 1);
            brushYaw_ = finite(palette.value("yaw", brushYaw_), brushYaw_);
            brushMinSpacing_ = std::clamp(finite(palette.value("minimumSpacing", brushMinSpacing_), brushMinSpacing_), 0.0f, 100.0f);
            brushGridRows_ = std::clamp(palette.value("gridRows", brushGridRows_), 1, 16);
            brushGridColumns_ = std::clamp(palette.value("gridColumns", brushGridColumns_), 1, 16);
            brushGridSpacingX_ = std::clamp(finite(palette.value("gridSpacingX", brushGridSpacingX_), brushGridSpacingX_), 0.1f, 100.0f);
            brushGridSpacingY_ = std::clamp(finite(palette.value("gridSpacingY", brushGridSpacingY_), brushGridSpacingY_), 0.1f, 100.0f);
            brushGridCenterOnClick_ = palette.value("gridCenterOnClick", brushGridCenterOnClick_);
        }
        if (editorNode.contains("spellEffectPreview") && editorNode["spellEffectPreview"].is_object())
        {
            const nlohmann::json& preview = editorNode["spellEffectPreview"];
            const uint32_t id = preview.value("spellId", spellPreview_.definition.id);
            LoadSpellPreviewDefinition(id);
            spellPreview_.loop = preview.value("loop", false);
            spellPreview_.showOverlay = preview.value("showOverlay", true);
            spellPreview_.playbackSpeed = std::clamp(finite(preview.value("playbackSpeed", 1.0f), 1.0f), 0.05f, 8.0f);
            spellPreview_.targetSource = static_cast<SpellPreviewTargetSource>(
                std::clamp(preview.value("targetSource", 0), 0, 2));
            spellPreview_.weather = std::clamp(preview.value("weather", 0), 0, 4);
        }
        if (editorNode.contains("aiBehavior") && editorNode["aiBehavior"].is_object())
        {
            const nlohmann::json& ai = editorNode["aiBehavior"];
            showAiBehaviorOverlay_ = ai.value("showOverlay", true);
            if (ai.contains("profiles") && ai["profiles"].is_array())
                for (const nlohmann::json& p : ai["profiles"])
                {
                    if (!p.is_object())
                        continue;
                    const std::string mapDir = p.value("mapDir", std::string());
                    if (mapDir.empty() || !p.contains("spawns") || !p["spawns"].is_array())
                        continue;
                    auto& map = aiBehaviorProfiles_[mapDir];
                    for (const nlohmann::json& row : p["spawns"])
                    {
                        if (!row.is_object())
                            continue;
                        const uint32_t guid = row.value("guid", 0u);
                        if (guid == 0)
                            continue;
                        AiBehaviorProfileEntry behavior;
                        behavior.templateScope = row.value("templateScope", true);
                        behavior.behavior.aggroEnabled = row.value("aggroEnabled", true);
                        behavior.behavior.aggroRadius = std::clamp(finite(row.value("aggroRadius", 20.0f), 20.0f),
                                                                    0.0f, 10000.0f);
                        behavior.behavior.leashDistance = std::clamp(finite(row.value("leashDistance", 50.0f), 50.0f),
                                                                       0.0f, 10000.0f);
                        const int pattern = std::clamp(row.value("patrolPattern", 0), 0, 2);
                        behavior.behavior.patrolPattern = static_cast<PatrolRoutePattern>(pattern);
                        map[guid] = behavior;
                    }
                }
        }
        if (editorNode.contains("scriptTriggers") && editorNode["scriptTriggers"].is_object())
        {
            const nlohmann::json& triggers = editorNode["scriptTriggers"];
            showScriptTriggerOverlay_ = triggers.value("showOverlay", true);
            if (triggers.contains("profiles") && triggers["profiles"].is_array())
                for (const nlohmann::json& p : triggers["profiles"])
                {
                    if (!p.is_object())
                        continue;
                    const std::string mapDir = p.value("mapDir", std::string());
                    if (mapDir.empty() || !p.contains("triggers") || !p["triggers"].is_array())
                        continue;
                    auto& list = scriptTriggerProfiles_[mapDir];
                    for (const nlohmann::json& row : p["triggers"])
                    {
                        if (!row.is_object() || list.size() >= 256)
                            continue;
                        ScriptEventTrigger trigger;
                        trigger.id = row.value("id", uint64_t(0));
                        if (trigger.id == 0)
                            trigger.id = nextScriptTriggerId_++;
                        trigger.name = row.value("name", std::string());
                        trigger.enabled = row.value("enabled", true);
                        trigger.type = static_cast<ScriptTriggerType>(std::clamp(row.value("type", 0), 0, 3));
                        trigger.areaShape = static_cast<ScriptTriggerAreaShape>(std::clamp(row.value("areaShape", 0), 0, 1));
                        trigger.center.x = finite(row.value("x", 0.0f), 0.0f);
                        trigger.center.y = finite(row.value("y", 0.0f), 0.0f);
                        trigger.center.z = finite(row.value("z", 0.0f), 0.0f);
                        trigger.radius = std::clamp(finite(row.value("radius", 8.0f), 8.0f), 0.1f, 10000.0f);
                        trigger.boxExtents.x = std::clamp(finite(row.value("extentX", 8.0f), 8.0f), 0.1f, 10000.0f);
                        trigger.boxExtents.y = std::clamp(finite(row.value("extentY", 8.0f), 8.0f), 0.1f, 10000.0f);
                        trigger.boxExtents.z = std::clamp(finite(row.value("extentZ", 4.0f), 4.0f), 0.1f, 10000.0f);
                        trigger.height = std::max(0.0f, finite(row.value("height", 0.0f), 0.0f));
                        trigger.targetKind = static_cast<ScriptTriggerObjectKind>(std::clamp(row.value("targetKind", 0), 0, 2));
                        trigger.targetGuid = row.value("targetGuid", 0u);
                        trigger.timerDelaySeconds = std::clamp(finite(row.value("timerDelay", 5.0f), 5.0f), 0.0f, 86400.0f);
                        trigger.timerRepeatSeconds = std::clamp(finite(row.value("timerRepeat", 0.0f), 0.0f), 0.0f, 86400.0f);
                        trigger.scriptHook = row.value("scriptHook", std::string());
                        trigger.scriptEventId = row.value("scriptEventId", 0u);
                        trigger.smartActionListId = row.value("smartActionListId", 0u);
                        trigger.note = row.value("note", std::string());
                        if (row.contains("actions") && row["actions"].is_array())
                            for (const nlohmann::json& actionRow : row["actions"])
                            {
                                if (!actionRow.is_object() || trigger.actions.size() >= 32)
                                    continue;
                                ScriptTriggerAction action;
                                action.id = actionRow.value("id", uint64_t(0));
                                if (action.id == 0)
                                    action.id = nextScriptTriggerActionId_++;
                                action.enabled = actionRow.value("enabled", true);
                                action.type = static_cast<ScriptTriggerActionType>(
                                    std::clamp(actionRow.value("type", 0), 0, 4));
                                action.delaySeconds = std::clamp(finite(actionRow.value("delay", 0.0f), 0.0f),
                                                                 0.0f, 86400.0f);
                                action.hook = actionRow.value("hook", std::string());
                                action.value = actionRow.value("value", 0u);
                                action.text = actionRow.value("text", std::string());
                                nextScriptTriggerActionId_ = std::max(nextScriptTriggerActionId_, action.id + 1);
                                trigger.actions.push_back(std::move(action));
                            }
                        if (trigger.name.empty())
                            trigger.name = "Trigger " + std::to_string(trigger.id);
                        nextScriptTriggerId_ = std::max(nextScriptTriggerId_, trigger.id + 1);
                        list.push_back(std::move(trigger));
                    }
                }
        }
        if (profiles)
            for (const nlohmann::json& j : *profiles)
            {
                if (!j.is_object())
                    continue;
                const std::string mapDir = j.value("mapDir", std::string());
                if (mapDir.empty())
                    continue;
                WorldLightingProfile profile = DefaultLightingProfile();
                profile.previewEnabled = j.value("previewEnabled", true);
                profile.sunEnabled = j.value("sunEnabled", true);
                profile.fogEnabled = j.value("fogEnabled", false);
                profile.dayNightCycle = j.value("dayNightCycle", false);
                profile.dayNightPlaying = j.value("dayNightPlaying", true);
                profile.timeOfDayMinutes = std::clamp(finite(j.value("timeOfDayMinutes", 720.0f), 720.0f),
                                                       0.0f, 1440.0f);
                profile.dayMinutesPerSecond = std::clamp(finite(j.value("dayMinutesPerSecond", 30.0f), 30.0f),
                                                          0.01f, 3600.0f);
                profile.showMarkers = j.value("showMarkers", true);
                profile.showVolumes = j.value("showVolumes", true);
                profile.ambientColor.x = std::clamp(finite(j.value("ambientR", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.ambientColor.y = std::clamp(finite(j.value("ambientG", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.ambientColor.z = std::clamp(finite(j.value("ambientB", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.ambientIntensity = std::clamp(finite(j.value("ambientIntensity", 0.45f), 0.45f), 0.0f, 8.0f);
                profile.sunColor.x = std::clamp(finite(j.value("sunR", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.sunColor.y = std::clamp(finite(j.value("sunG", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.sunColor.z = std::clamp(finite(j.value("sunB", 1.0f), 1.0f), 0.0f, 8.0f);
                profile.sunIntensity = std::clamp(finite(j.value("sunIntensity", 0.55f), 0.55f), 0.0f, 16.0f);
                profile.sunAzimuth = finite(j.value("sunAzimuth", profile.sunAzimuth), profile.sunAzimuth);
                profile.sunElevation = std::clamp(finite(j.value("sunElevation", profile.sunElevation), profile.sunElevation),
                                                   -89.9f, 89.9f);
                profile.fogColor.x = std::clamp(finite(j.value("fogR", profile.fogColor.x), profile.fogColor.x), 0.0f, 8.0f);
                profile.fogColor.y = std::clamp(finite(j.value("fogG", profile.fogColor.y), profile.fogColor.y), 0.0f, 8.0f);
                profile.fogColor.z = std::clamp(finite(j.value("fogB", profile.fogColor.z), profile.fogColor.z), 0.0f, 8.0f);
                profile.fogStart = std::max(0.0f, finite(j.value("fogStart", profile.fogStart), profile.fogStart));
                profile.fogEnd = std::max(profile.fogStart + 0.01f,
                                          finite(j.value("fogEnd", profile.fogEnd), profile.fogEnd));

                if (j.contains("lights") && j["lights"].is_array())
                    for (const nlohmann::json& lj : j["lights"])
                    {
                        if (!lj.is_object() || profile.lights.size() >= 256)
                            continue;
                        WorldLight light;
                        light.id = lj.value("id", uint64_t(0));
                        if (light.id == 0)
                            light.id = nextWorldLightId_++;
                        light.name = lj.value("name", std::string());
                        light.type = lj.value("type", std::string("point")) == "spot"
                                         ? WorldLightType::Spot : WorldLightType::Point;
                        light.enabled = lj.value("enabled", true);
                        light.position.x = finite(lj.value("x", 0.0f), 0.0f);
                        light.position.y = finite(lj.value("y", 0.0f), 0.0f);
                        light.position.z = finite(lj.value("z", 0.0f), 0.0f);
                        light.direction.x = finite(lj.value("dirX", 0.0f), 0.0f);
                        light.direction.y = finite(lj.value("dirY", 0.0f), 0.0f);
                        light.direction.z = finite(lj.value("dirZ", -1.0f), -1.0f);
                        light.color.x = finite(lj.value("r", 1.0f), 1.0f);
                        light.color.y = finite(lj.value("g", 1.0f), 1.0f);
                        light.color.z = finite(lj.value("b", 1.0f), 1.0f);
                        light.intensity = finite(lj.value("intensity", 1.0f), 1.0f);
                        light.range = finite(lj.value("range", 12.0f), 12.0f);
                        light.falloff = finite(lj.value("falloff", 2.0f), 2.0f);
                        light.innerAngle = finite(lj.value("innerAngle", 20.0f), 20.0f);
                        light.outerAngle = finite(lj.value("outerAngle", 35.0f), 35.0f);
                        NormalizeWorldLight(light);
                        nextWorldLightId_ = std::max(nextWorldLightId_, light.id + 1);
                        profile.lights.push_back(std::move(light));
                    }
                lightingProfiles_[mapDir] = std::move(profile);
            }
    }
    catch (...)
    {
        // Keep valid bookmarks/profiles from a previous project out of a malformed settings node.
        bookmarks_.clear();
        lightingProfiles_.clear();
        aiBehaviorProfiles_.clear();
        scriptTriggerProfiles_.clear();
        nextWorldLightId_ = 1;
        nextScriptTriggerId_ = 1;
        nextScriptTriggerActionId_ = 1;
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

    nlohmann::json profiles = nlohmann::json::array();
    std::vector<std::string> mapDirs;
    mapDirs.reserve(lightingProfiles_.size());
    for (const auto& pair : lightingProfiles_)
        mapDirs.push_back(pair.first);
    std::sort(mapDirs.begin(), mapDirs.end());
    for (const std::string& mapDir : mapDirs)
    {
        const WorldLightingProfile& profile = lightingProfiles_.at(mapDir);
        nlohmann::json p = {
            {"mapDir", mapDir},
            {"previewEnabled", profile.previewEnabled}, {"sunEnabled", profile.sunEnabled},
            {"fogEnabled", profile.fogEnabled}, {"dayNightCycle", profile.dayNightCycle},
            {"dayNightPlaying", profile.dayNightPlaying},
            {"timeOfDayMinutes", profile.timeOfDayMinutes},
            {"dayMinutesPerSecond", profile.dayMinutesPerSecond},
            {"showMarkers", profile.showMarkers},
            {"showVolumes", profile.showVolumes},
            {"ambientR", profile.ambientColor.r}, {"ambientG", profile.ambientColor.g}, {"ambientB", profile.ambientColor.b},
            {"ambientIntensity", profile.ambientIntensity},
            {"sunR", profile.sunColor.r}, {"sunG", profile.sunColor.g}, {"sunB", profile.sunColor.b},
            {"sunIntensity", profile.sunIntensity}, {"sunAzimuth", profile.sunAzimuth},
            {"sunElevation", profile.sunElevation},
            {"fogR", profile.fogColor.r}, {"fogG", profile.fogColor.g}, {"fogB", profile.fogColor.b},
            {"fogStart", profile.fogStart}, {"fogEnd", profile.fogEnd},
            {"lights", nlohmann::json::array()}
        };
        for (const WorldLight& light : profile.lights)
            p["lights"].push_back({
                {"id", light.id}, {"name", light.name},
                {"type", light.type == WorldLightType::Spot ? "spot" : "point"}, {"enabled", light.enabled},
                {"x", light.position.x}, {"y", light.position.y}, {"z", light.position.z},
                {"dirX", light.direction.x}, {"dirY", light.direction.y}, {"dirZ", light.direction.z},
                {"r", light.color.r}, {"g", light.color.g}, {"b", light.color.b},
                {"intensity", light.intensity}, {"range", light.range}, {"falloff", light.falloff},
                {"innerAngle", light.innerAngle}, {"outerAngle", light.outerAngle}
            });
        profiles.push_back(std::move(p));
    }
    editorNode["worldLighting"] = {{"profiles", std::move(profiles)}};
    editorNode["worldPreview"] = {{"simulationPaused", worldSimulationPaused_},
                                   {"simulationSpeed", worldSimulationSpeed_},
                                   {"inGameView", inGameViewMode_},
                                   {"liveTerrainPreview", liveTerrainPreview_}};

    editorNode["terrainTools"] = {{"brushRadius", terrainBrushRadius_},
                                  {"brushStrength", terrainBrushStrength_},
                                  {"rampWidth", terrainRampWidth_},
                                  {"rampMaxSlope", terrainRampMaxSlopeDegrees_},
                                  {"rampSteps", terrainRampStepCount_},
                                  {"noiseAmplitude", terrainNoiseAmplitude_},
                                  {"noiseFrequency", terrainNoiseFrequency_},
                                  {"noiseOctaves", terrainNoiseOctaves_},
                                  {"noiseSeed", terrainNoiseSeed_},
                                  {"stampPreset", terrainStampPreset_},
                                  {"stampYawDegrees", terrainStampYawDegrees_},
                                  {"smoothIterations", terrainSmoothIterations_},
                                  {"smoothBlend", terrainSmoothBlend_},
                                  {"smoothPreserveEdges", terrainSmoothPreserveEdges_},
                                  {"smoothEdgeThreshold", terrainSmoothEdgeThreshold_}};

    editorNode["spawnPalette"] = {{"kind", brushKind_},
                                   {"placementMode", brushPlacementMode_},
                                   {"yaw", brushYaw_},
                                   {"minimumSpacing", brushMinSpacing_},
                                   {"gridRows", brushGridRows_},
                                   {"gridColumns", brushGridColumns_},
                                   {"gridSpacingX", brushGridSpacingX_},
                                   {"gridSpacingY", brushGridSpacingY_},
                                   {"gridCenterOnClick", brushGridCenterOnClick_}};

    editorNode["spellEffectPreview"] = {{"spellId", spellPreview_.definition.id},
                                          {"loop", spellPreview_.loop},
                                          {"showOverlay", spellPreview_.showOverlay},
                                          {"playbackSpeed", spellPreview_.playbackSpeed},
                                          {"targetSource", static_cast<int>(spellPreview_.targetSource)},
                                          {"weather", spellPreview_.weather}};

    nlohmann::json behaviorProfiles = nlohmann::json::array();
    std::vector<std::string> behaviorMaps;
    behaviorMaps.reserve(aiBehaviorProfiles_.size());
    for (const auto& pair : aiBehaviorProfiles_)
        behaviorMaps.push_back(pair.first);
    std::sort(behaviorMaps.begin(), behaviorMaps.end());
    for (const std::string& mapDir : behaviorMaps)
    {
        nlohmann::json profile = {{"mapDir", mapDir}, {"spawns", nlohmann::json::array()}};
        std::vector<uint32_t> guids;
        const auto& entries = aiBehaviorProfiles_.at(mapDir);
        guids.reserve(entries.size());
        for (const auto& pair : entries)
            guids.push_back(pair.first);
        std::sort(guids.begin(), guids.end());
        for (uint32_t guid : guids)
        {
            const AiBehaviorProfileEntry& behavior = entries.at(guid);
            profile["spawns"].push_back({
                {"guid", guid}, {"templateScope", behavior.templateScope},
                {"aggroEnabled", behavior.behavior.aggroEnabled},
                {"aggroRadius", behavior.behavior.aggroRadius},
                {"leashDistance", behavior.behavior.leashDistance},
                {"patrolPattern", static_cast<int>(behavior.behavior.patrolPattern)}
            });
        }
        behaviorProfiles.push_back(std::move(profile));
    }
    editorNode["aiBehavior"] = {{"showOverlay", showAiBehaviorOverlay_},
                                 {"profiles", std::move(behaviorProfiles)}};

    nlohmann::json triggerProfiles = nlohmann::json::array();
    std::vector<std::string> triggerMaps;
    triggerMaps.reserve(scriptTriggerProfiles_.size());
    for (const auto& pair : scriptTriggerProfiles_)
        triggerMaps.push_back(pair.first);
    std::sort(triggerMaps.begin(), triggerMaps.end());
    for (const std::string& mapDir : triggerMaps)
    {
        nlohmann::json profile = {{"mapDir", mapDir}, {"triggers", nlohmann::json::array()}};
        std::vector<ScriptEventTrigger> ordered = scriptTriggerProfiles_.at(mapDir);
        std::sort(ordered.begin(), ordered.end(), [](const ScriptEventTrigger& a, const ScriptEventTrigger& b) {
            return a.id < b.id;
        });
        for (const ScriptEventTrigger& trigger : ordered)
        {
            nlohmann::json row = {
                {"id", trigger.id}, {"name", trigger.name}, {"enabled", trigger.enabled},
                {"type", static_cast<int>(trigger.type)}, {"areaShape", static_cast<int>(trigger.areaShape)},
                {"x", trigger.center.x}, {"y", trigger.center.y}, {"z", trigger.center.z},
                {"radius", trigger.radius}, {"extentX", trigger.boxExtents.x},
                {"extentY", trigger.boxExtents.y}, {"extentZ", trigger.boxExtents.z},
                {"height", trigger.height}, {"targetKind", static_cast<int>(trigger.targetKind)},
                {"targetGuid", trigger.targetGuid}, {"timerDelay", trigger.timerDelaySeconds},
                {"timerRepeat", trigger.timerRepeatSeconds}, {"scriptHook", trigger.scriptHook},
                {"scriptEventId", trigger.scriptEventId}, {"smartActionListId", trigger.smartActionListId},
                {"note", trigger.note}, {"actions", nlohmann::json::array()}
            };
            for (const ScriptTriggerAction& action : trigger.actions)
                row["actions"].push_back({
                    {"id", action.id}, {"enabled", action.enabled}, {"type", static_cast<int>(action.type)},
                    {"delay", action.delaySeconds}, {"hook", action.hook}, {"value", action.value},
                    {"text", action.text}
                });
            profile["triggers"].push_back(std::move(row));
        }
        triggerProfiles.push_back(std::move(profile));
    }
    editorNode["scriptTriggers"] = {{"showOverlay", showScriptTriggerOverlay_},
                                     {"profiles", std::move(triggerProfiles)}};
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
    size_t serverOverrides = 0, templateModelRows = 0, legacyModels = 0, unresolvedModels = 0,
           eventOverrides = 0;
    for (const MapSpawn& spawn : spawns)
    {
        switch (spawn.displaySource)
        {
            case CreatureDisplaySource::SpawnOverride: ++serverOverrides; break;
            case CreatureDisplaySource::TemplateModel: ++templateModelRows; break;
            case CreatureDisplaySource::LegacyTemplate: ++legacyModels; break;
            default: break;
        }
        if (spawn.displayId == 0)
            ++unresolvedModels;
        if (!spawn.eventDisplayOverrides.empty())
            ++eventOverrides;
    }
    npcLayer_.SetSpawns(std::move(spawns));
    ApplyAiBehaviorProfiles();
    npcStatus_ = std::to_string(n) + " NPC spawns on this map (" +
                 std::to_string(serverOverrides) + " server overrides, " +
                 std::to_string(templateModelRows) + " template-model, " +
                 std::to_string(legacyModels) + " legacy" +
                 (eventOverrides ? (", " + std::to_string(eventOverrides) + " event-model") : "") + ").";
    if (unresolvedModels)
        npcStatus_ += " " + std::to_string(unresolvedModels) + " have no resolved display ID.";
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
    DrawAiBehaviorPanel();
    DrawScriptTriggersPanel();
    DrawNpcInstancePanel();
    DrawGoInstancePanel();
    DrawWaypointPathPanel();
    DrawTerrainSculptPanel();
    DrawWorldValidationPanel();
    DrawLightEditorPanel();
    DrawRealtimePreviewPanel();
    DrawSpellEffectPreviewerPanel();
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

    if (svc_->connected)
    {
        ImGui::TextDisabled("Core profile: %s", CoreFlavorName(svc_->coreFlavor));
        if (!svc_->coreSchemaSummary.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", svc_->coreSchemaSummary.c_str());
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
    worldValidationIssues_.clear();
    worldValidationHasRun_ = false;
    worldValidationStatus_.clear();
    if (dir != undoMapDir_)   // an actual map change (not an option-toggle reload) invalidates undo
    {
        terrainRampHasStart_ = false;
        undo_.Clear();
        undoMapDir_ = dir;
        aiBehaviorEditGuid_ = 0;
        aiBehaviorDirty_ = false;
        aiPreviewTargetEnabled_ = false;
        aiPreviewTargetPlacementActive_ = false;
        aiBehaviorStatus_.clear();
        if (!scriptTriggerMapDir_.empty() && scriptTriggersDirty_)
            scriptTriggerDrafts_[scriptTriggerMapDir_] = scriptTriggersEdit_;
        scriptTriggersDirty_ = false;
        selectedScriptTriggerId_ = 0;
        scriptPreviewPlayerEnabled_ = false;
        scriptPreviewPlayerFollowCamera_ = false;
        scriptPreviewPlayerPlacementActive_ = false;
        scriptTriggerCenterPlacementActive_ = false;
        scriptInteractionMode_ = false;
        scriptTriggerRuntime_.clear();
        pendingScriptTriggerActions_.clear();
        scriptTriggerLog_.clear();
        scriptPreviewTimeSeconds_ = 0.0f;
        scriptTriggerStatus_.clear();
    }
    if (!streamer_.OpenMap(dir))
    {
        error_ = "WDT missing for " + dir;
        loadedName_.clear();
        return;
    }
    error_.clear();
    LoadLightingForMap(dir);
    LoadScriptTriggersForMap(dir);
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
    {
        FrameSelectedWorldLight();
        return;
    }
    const glm::vec3 origin = streamer_.origin();
    if (selKind_ == SelKind::Npc)
    {
        if (const MapSpawn* s = npcLayer_.FindSpawn(selGuid_))
            FrameWorldPosition(glm::vec3(s->x, s->y, s->z),
                               std::max(25.0f, s->scale * s->displayScale * 18.0f));
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

void AdtViewerModule::LoadLightingForMap(const std::string& mapDir)
{
    if (mapDir.empty() || lightingMapDir_ == mapDir)
        return;

    // Preserve a dirty map as a session-only draft when moving to another map. Explicit Save is
    // what writes a profile into Studio settings, matching terrain/path editor expectations.
    if (!lightingMapDir_.empty() && lightingDirty_)
        lightingDrafts_[lightingMapDir_] = lightingEdit_;

    lightingMapDir_ = mapDir;
    const auto draft = lightingDrafts_.find(mapDir);
    if (draft != lightingDrafts_.end())
    {
        lightingEdit_ = draft->second;
        lightingDirty_ = true;
        lightingStatus_ = "Restored an unsaved lighting draft for " + mapDir + ".";
    }
    else
    {
        const auto saved = lightingProfiles_.find(mapDir);
        lightingEdit_ = saved != lightingProfiles_.end() ? saved->second : DefaultLightingProfile();
        lightingDirty_ = false;
        lightingStatus_ = saved != lightingProfiles_.end()
            ? "Loaded saved Studio lighting for " + mapDir + "."
            : "Using the neutral game-style lighting baseline for " + mapDir + ".";
    }
    selectedWorldLightId_ = lightingEdit_.lights.empty() ? 0 : lightingEdit_.lights.front().id;
    runtimeTimeOfDay_ = lightingEdit_.timeOfDayMinutes;
    lightPlacementActive_ = false;
    lightMarkers_.clear();
}

void AdtViewerModule::SaveLightingProfile()
{
    if (lightingMapDir_.empty())
        return;
    lightingProfiles_[lightingMapDir_] = lightingEdit_;
    lightingDrafts_.erase(lightingMapDir_);
    lightingDirty_ = false;
    lightingStatus_ = "Saved " + std::to_string(lightingEdit_.lights.size()) +
                      " Studio light(s) for " + lightingMapDir_ + ".";
    if (svc_ && svc_->requestSaveSettings)
        svc_->requestSaveSettings();
    if (svc_ && svc_->setStatus)
        svc_->setStatus(lightingStatus_);
}

void AdtViewerModule::RevertLightingProfile()
{
    if (lightingMapDir_.empty())
        return;
    const auto saved = lightingProfiles_.find(lightingMapDir_);
    lightingEdit_ = saved != lightingProfiles_.end() ? saved->second : DefaultLightingProfile();
    lightingDrafts_.erase(lightingMapDir_);
    lightingDirty_ = false;
    selectedWorldLightId_ = lightingEdit_.lights.empty() ? 0 : lightingEdit_.lights.front().id;
    runtimeTimeOfDay_ = lightingEdit_.timeOfDayMinutes;
    lightPlacementActive_ = false;
    lightingStatus_ = saved != lightingProfiles_.end()
        ? "Reverted to saved Studio lighting."
        : "Reverted to the neutral game-style lighting baseline.";
}

void AdtViewerModule::MarkLightingDirty(const char* status)
{
    if (lightingMapDir_.empty())
        return;
    lightingDirty_ = true;
    lightingDrafts_[lightingMapDir_] = lightingEdit_;
    if (status)
        lightingStatus_ = status;
}

AdtViewerModule::WorldLight* AdtViewerModule::FindWorldLight(uint64_t id)
{
    for (WorldLight& light : lightingEdit_.lights)
        if (light.id == id)
            return &light;
    return nullptr;
}

const AdtViewerModule::WorldLight* AdtViewerModule::FindWorldLight(uint64_t id) const
{
    for (const WorldLight& light : lightingEdit_.lights)
        if (light.id == id)
            return &light;
    return nullptr;
}

void AdtViewerModule::NormalizeWorldLight(WorldLight& light)
{
    auto finite = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
    light.position.x = finite(light.position.x, 0.0f);
    light.position.y = finite(light.position.y, 0.0f);
    light.position.z = finite(light.position.z, 0.0f);
    light.direction.x = finite(light.direction.x, 0.0f);
    light.direction.y = finite(light.direction.y, 0.0f);
    light.direction.z = finite(light.direction.z, -1.0f);
    if (glm::length(light.direction) < 1e-5f)
        light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    else
        light.direction = glm::normalize(light.direction);
    light.color.x = std::clamp(finite(light.color.x, 1.0f), 0.0f, 8.0f);
    light.color.y = std::clamp(finite(light.color.y, 1.0f), 0.0f, 8.0f);
    light.color.z = std::clamp(finite(light.color.z, 1.0f), 0.0f, 8.0f);
    light.intensity = std::clamp(finite(light.intensity, 1.0f), 0.0f, 64.0f);
    light.range = std::clamp(finite(light.range, 12.0f), 0.1f, 5000.0f);
    light.falloff = std::clamp(finite(light.falloff, 2.0f), 0.05f, 16.0f);
    light.innerAngle = std::clamp(finite(light.innerAngle, 20.0f), 0.1f, 89.0f);
    light.outerAngle = std::clamp(finite(light.outerAngle, 35.0f), light.innerAngle + 0.1f, 89.9f);
    if (light.name.empty())
        light.name = std::string(WorldLightTypeName(light.type)) + " Light " + std::to_string(light.id);
}

void AdtViewerModule::AddWorldLight(WorldLightType type, const glm::vec3& world)
{
    if (lightingMapDir_.empty() || lightingEdit_.lights.size() >= 256)
    {
        lightingStatus_ = lightingMapDir_.empty() ? "Open a map before placing a light."
                                                   : "This map already has the 256-light project safety limit.";
        return;
    }
    WorldLight light;
    light.id = nextWorldLightId_++;
    light.type = type;
    light.name = std::string(WorldLightTypeName(type)) + " Light " +
                 std::to_string(lightingEdit_.lights.size() + 1);
    light.position = world;
    light.direction = glm::vec3(0.0f, 0.0f, -1.0f);
    light.color = type == WorldLightType::Spot ? glm::vec3(1.0f, 0.88f, 0.62f)
                                               : glm::vec3(1.0f, 0.94f, 0.78f);
    light.intensity = type == WorldLightType::Spot ? 2.0f : 1.35f;
    light.range = type == WorldLightType::Spot ? 24.0f : 14.0f;
    light.falloff = 2.0f;
    light.innerAngle = 17.0f;
    light.outerAngle = 32.0f;
    NormalizeWorldLight(light);
    ClearSelection();
    selectedWorldLightId_ = light.id;
    lightingEdit_.lights.push_back(std::move(light));
    MarkLightingDirty("Added a preview light. Save Lighting to persist it in the project.");
}

void AdtViewerModule::DuplicateSelectedWorldLight()
{
    WorldLight* source = FindWorldLight(selectedWorldLightId_);
    if (!source || lightingEdit_.lights.size() >= 256)
        return;
    WorldLight duplicate = *source;
    duplicate.id = nextWorldLightId_++;
    duplicate.name += " Copy";
    duplicate.position += glm::vec3(1.0f, 1.0f, 0.5f);
    selectedWorldLightId_ = duplicate.id;
    lightingEdit_.lights.push_back(std::move(duplicate));
    MarkLightingDirty("Duplicated the selected light.");
}

void AdtViewerModule::DeleteSelectedWorldLight()
{
    if (selectedWorldLightId_ == 0)
        return;
    const uint64_t removed = selectedWorldLightId_;
    const size_t before = lightingEdit_.lights.size();
    lightingEdit_.lights.erase(std::remove_if(lightingEdit_.lights.begin(), lightingEdit_.lights.end(),
                                               [removed](const WorldLight& light) {
                                                   return light.id == removed;
                                               }), lightingEdit_.lights.end());
    if (lightingEdit_.lights.size() == before) // stale marker/selection; nothing was removed
        return;
    selectedWorldLightId_ = lightingEdit_.lights.empty() ? 0 : lightingEdit_.lights.front().id;
    MarkLightingDirty("Deleted the selected light.");
}

void AdtViewerModule::FrameSelectedWorldLight()
{
    if (const WorldLight* light = FindWorldLight(selectedWorldLightId_))
        FrameWorldPosition(light->position, std::max(18.0f, light->range * 1.25f));
}

void AdtViewerModule::ApplyWorldLightingToRenderer(const glm::vec3& origin,
                                                    const glm::vec3& worldFocus)
{
    if (!svc_ || !svc_->renderer)
        return;
    WorldLightingGpu gpu{}; // neutral historical sunlight by default
    if (lightingEdit_.previewEnabled)
    {
        glm::vec3 ambientColor = lightingEdit_.ambientColor;
        float ambientIntensity = lightingEdit_.ambientIntensity;
        glm::vec3 sunColor = lightingEdit_.sunColor;
        float sunIntensity = lightingEdit_.sunEnabled ? lightingEdit_.sunIntensity : 0.0f;
        glm::vec3 fogColor = lightingEdit_.fogColor;
        float fogStart = lightingEdit_.fogStart;
        float fogEnd = lightingEdit_.fogEnd;
        float sunAzimuth = lightingEdit_.sunAzimuth;
        float sunElevation = lightingEdit_.sunElevation;

        // A deterministic game-style day/night curve gives the viewport a genuinely live world
        // state without modifying a client Light*.dbc. Manual values remain the daytime art
        // direction; dawn/dusk warm the sun, and night keeps only blue ambient + authored lights.
        if (lightingEdit_.dayNightCycle)
        {
            const float phase = (runtimeTimeOfDay_ / 1440.0f) * 6.28318530718f;
            const float solarHeight = std::sin(phase - 1.57079632679f); // -1 midnight, +1 midday
            const float daylight = glm::smoothstep(-0.10f, 0.18f, solarHeight);
            const float twilight = 1.0f - glm::smoothstep(0.08f, 0.42f, std::fabs(solarHeight));
            sunAzimuth = glm::degrees(phase - 1.57079632679f);
            sunElevation = solarHeight * 70.0f;
            const glm::vec3 nightAmbient(0.12f, 0.18f, 0.42f);
            const glm::vec3 warmSun(1.0f, 0.40f, 0.17f);
            const glm::vec3 nightFog(0.025f, 0.045f, 0.11f);
            ambientColor = glm::mix(nightAmbient, ambientColor, daylight);
            ambientIntensity = glm::mix(0.24f, ambientIntensity, daylight);
            sunColor = glm::mix(warmSun, sunColor, glm::smoothstep(0.15f, 0.65f, daylight));
            sunIntensity *= daylight * (0.72f + 0.28f * twilight);
            fogColor = glm::mix(nightFog, fogColor, daylight);
            fogStart *= glm::mix(0.62f, 1.0f, daylight);
            fogEnd *= glm::mix(0.72f, 1.0f, daylight);
        }

        gpu.ambientColor[0] = ambientColor.r;
        gpu.ambientColor[1] = ambientColor.g;
        gpu.ambientColor[2] = ambientColor.b;
        gpu.ambientColor[3] = ambientIntensity;
        gpu.sunColor[0] = sunColor.r;
        gpu.sunColor[1] = sunColor.g;
        gpu.sunColor[2] = sunColor.b;
        gpu.sunColor[3] = 1.0f;
        const float azimuth = glm::radians(sunAzimuth);
        const float elevation = glm::radians(sunElevation);
        const glm::vec3 sunDirection(std::cos(elevation) * std::cos(azimuth),
                                     std::cos(elevation) * std::sin(azimuth), std::sin(elevation));
        gpu.sunDirectionIntensity[0] = sunDirection.x;
        gpu.sunDirectionIntensity[1] = sunDirection.y;
        gpu.sunDirectionIntensity[2] = sunDirection.z;
        gpu.sunDirectionIntensity[3] = sunIntensity;
        gpu.fogColor[0] = fogColor.r;
        gpu.fogColor[1] = fogColor.g;
        gpu.fogColor[2] = fogColor.b;
        gpu.fogColor[3] = 1.0f;
        gpu.fogParams[0] = fogStart;
        gpu.fogParams[1] = fogEnd;
        gpu.fogParams[2] = lightingEdit_.fogEnabled ? 1.0f : 0.0f;

        struct Candidate { float dist2 = 0.0f; const WorldLight* light = nullptr; };
        std::vector<Candidate> nearest;
        nearest.reserve(lightingEdit_.lights.size());
        for (const WorldLight& light : lightingEdit_.lights)
        {
            if (!light.enabled || light.intensity <= 0.0f || light.range <= 0.0f)
                continue;
            const glm::vec3 d = light.position - worldFocus;
            nearest.push_back({glm::dot(d, d), &light});
        }
        std::sort(nearest.begin(), nearest.end(), [](const Candidate& a, const Candidate& b) {
            return a.dist2 < b.dist2;
        });
        const int count = std::min<int>(static_cast<int>(nearest.size()), kMaxWorldLights);
        for (int i = 0; i < count; ++i)
        {
            const WorldLight& light = *nearest[i].light;
            WorldLightGpu& out = gpu.lights[i];
            out.positionRange[0] = light.position.x - origin.x;
            out.positionRange[1] = light.position.y - origin.y;
            out.positionRange[2] = light.position.z;
            out.positionRange[3] = light.range;
            out.colorIntensity[0] = light.color.r;
            out.colorIntensity[1] = light.color.g;
            out.colorIntensity[2] = light.color.b;
            out.colorIntensity[3] = light.intensity;
            out.directionInnerCos[0] = light.direction.x;
            out.directionInnerCos[1] = light.direction.y;
            out.directionInnerCos[2] = light.direction.z;
            out.directionInnerCos[3] = std::cos(glm::radians(light.innerAngle));
            out.outerType[0] = std::cos(glm::radians(light.outerAngle));
            out.outerType[1] = light.type == WorldLightType::Spot ? 1.0f : 0.0f;
            out.outerType[2] = light.falloff;
        }
        gpu.fogParams[3] = static_cast<float>(count);
    }

    // Staged terrain strokes get a shader-side real-time preview without writing client files or
    // rebuilding every streamed tile. Keep the newest brush history bounded for predictable GPU
    // work; Save ADT edits still applies the full chronological set to disk and rebuilds MCNR.
    terrainPreviewStrokes_.clear();
    if (liveTerrainPreview_)
    {
        adtEdits_.SnapshotTerrainStrokes(terrainPreviewStrokes_);
        const size_t first = terrainPreviewStrokes_.size() > static_cast<size_t>(kMaxTerrainPreviewStrokes)
                                 ? terrainPreviewStrokes_.size() - static_cast<size_t>(kMaxTerrainPreviewStrokes)
                                 : 0;
        int previewCount = 0;
        for (size_t i = first; i < terrainPreviewStrokes_.size() && previewCount < kMaxTerrainPreviewStrokes; ++i)
        {
            const adt::TerrainBrushStroke& stroke = terrainPreviewStrokes_[i].stroke;
            TerrainPreviewStrokeGpu& out = gpu.terrainPreview[previewCount++];
            out.centerRadius[0] = stroke.worldX - origin.x;
            out.centerRadius[1] = stroke.worldY - origin.y;
            out.centerRadius[2] = stroke.radius;
            out.params[0] = std::fabs(stroke.strength);
            out.params[1] = stroke.targetZ;
            out.params[2] = static_cast<float>(static_cast<int>(stroke.mode));
        }
        gpu.terrainPreviewParams[0] = static_cast<float>(previewCount);
    }
    svc_->renderer->SetWorldLighting(gpu);
}

void AdtViewerModule::BuildLightMarkerCache(const glm::mat4& view, const glm::mat4& proj,
                                             const ImVec2& p0, int w, int h,
                                             const glm::vec3& focus)
{
    lightMarkers_.clear();
    if (!lightingEdit_.showMarkers || lightingEdit_.lights.empty())
        return;
    const glm::vec3 origin = streamer_.origin();
    const glm::vec3 worldFocus = focus + origin;
    for (const WorldLight& light : lightingEdit_.lights)
    {
        ImVec2 screen;
        if (!ProjectWorldPoint(light.position, origin, view, proj, p0, w, h, screen))
            continue;
        const glm::vec3 d = light.position - worldFocus;
        lightMarkers_.push_back({light.id, screen, glm::dot(d, d)});
    }
    std::sort(lightMarkers_.begin(), lightMarkers_.end(), [](const LightMarker& a, const LightMarker& b) {
        return a.dist2 < b.dist2;
    });
    if (lightMarkers_.size() > 256)
        lightMarkers_.resize(256);
}

bool AdtViewerModule::TrySelectLightMarkerOverlay(bool viewportHovered)
{
    if (!viewportHovered || lightMarkers_.empty() || !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const LightMarker* best = nullptr;
    float bestD2 = 14.0f * 14.0f;
    for (const LightMarker& marker : lightMarkers_)
    {
        const float dx = marker.screen.x - mouse.x;
        const float dy = marker.screen.y - mouse.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = &marker;
        }
    }
    if (!best)
        return false;
    selectedWorldLightId_ = best->id;
    ClearSelection(); // a Light Editor selection is independent from DB/ADT object transforms
    lightingStatus_ = "Selected a light marker in the World Editor.";
    return true;
}

void AdtViewerModule::DrawLightOverlay(const glm::mat4& view, const glm::mat4& proj,
                                       const ImVec2& p0, int w, int h)
{
    if (!lightingEdit_.showMarkers || lightMarkers_.empty())
        return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const glm::vec3 origin = streamer_.origin();
    auto colorFor = [](const WorldLight& light, int alpha) {
        const int r = std::clamp(static_cast<int>(light.color.r * 255.0f), 0, 255);
        const int g = std::clamp(static_cast<int>(light.color.g * 255.0f), 0, 255);
        const int b = std::clamp(static_cast<int>(light.color.b * 255.0f), 0, 255);
        return IM_COL32(r, g, b, alpha);
    };
    auto project = [&](const glm::vec3& world, ImVec2& screen) {
        return ProjectWorldPoint(world, origin, view, proj, p0, w, h, screen);
    };

    for (const LightMarker& marker : lightMarkers_)
    {
        const WorldLight* light = FindWorldLight(marker.id);
        if (!light)
            continue;
        const bool selected = light->id == selectedWorldLightId_;
        const ImU32 color = colorFor(*light, light->enabled ? 240 : 110);
        const ImU32 dimColor = colorFor(*light, light->enabled ? 120 : 55);

        if (lightingEdit_.showVolumes && light->range > 0.1f)
        {
            if (light->type == WorldLightType::Point)
            {
                constexpr int segments = 28;
                ImVec2 previous{};
                bool havePrevious = false;
                for (int i = 0; i <= segments; ++i)
                {
                    const float angle = (static_cast<float>(i) / segments) * 6.28318530718f;
                    ImVec2 at;
                    const glm::vec3 rim = light->position + glm::vec3(std::cos(angle) * light->range,
                                                                         std::sin(angle) * light->range, 0.0f);
                    const bool visible = project(rim, at);
                    if (visible && havePrevious)
                        draw->AddLine(previous, at, dimColor, selected ? 2.0f : 1.0f);
                    previous = at;
                    havePrevious = visible;
                }
            }
            else
            {
                const glm::vec3 forward = glm::normalize(light->direction);
                const glm::vec3 reference = std::fabs(forward.z) < 0.9f ? glm::vec3(0, 0, 1)
                                                                          : glm::vec3(0, 1, 0);
                const glm::vec3 right = glm::normalize(glm::cross(forward, reference));
                const glm::vec3 up = glm::normalize(glm::cross(right, forward));
                const glm::vec3 end = light->position + forward * light->range;
                const float radius = std::tan(glm::radians(light->outerAngle)) * light->range;
                constexpr int segments = 16;
                ImVec2 source;
                const bool sourceVisible = project(light->position, source);
                ImVec2 previous{};
                bool havePrevious = false;
                for (int i = 0; i <= segments; ++i)
                {
                    const float angle = (static_cast<float>(i) / segments) * 6.28318530718f;
                    const glm::vec3 rim = end + (right * std::cos(angle) + up * std::sin(angle)) * radius;
                    ImVec2 at;
                    const bool visible = project(rim, at);
                    if (visible && havePrevious)
                        draw->AddLine(previous, at, dimColor, selected ? 2.0f : 1.0f);
                    if (visible && sourceVisible && (i % 4 == 0))
                        draw->AddLine(source, at, dimColor, selected ? 1.8f : 1.0f);
                    previous = at;
                    havePrevious = visible;
                }
            }
        }

        const float radius = selected ? 8.0f : 6.0f;
        draw->AddCircleFilled(marker.screen, radius, color, 16);
        draw->AddCircle(marker.screen, radius, selected ? IM_COL32(255, 244, 206, 255) : IM_COL32(235, 245, 255, 225),
                        16, selected ? 2.0f : 1.25f);
        draw->AddLine(ImVec2(marker.screen.x - radius * 0.62f, marker.screen.y),
                      ImVec2(marker.screen.x + radius * 0.62f, marker.screen.y), IM_COL32(22, 24, 28, 235), 1.3f);
        draw->AddLine(ImVec2(marker.screen.x, marker.screen.y - radius * 0.62f),
                      ImVec2(marker.screen.x, marker.screen.y + radius * 0.62f), IM_COL32(22, 24, 28, 235), 1.3f);
        if (selected)
        {
            const std::string label = light->name + "  [" + WorldLightTypeName(light->type) + "]";
            draw->AddText(ImVec2(marker.screen.x + radius + 4.0f, marker.screen.y - radius),
                          IM_COL32(255, 247, 220, 255), label.c_str());
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

void AdtViewerModule::PlaceSpawnGrid(const glm::vec3& anchor)
{
    if (!svc_ || !svc_->connected || !svc_->activeDb || brushEntry_ == 0)
    {
        brushStatus_ = "Connect a project database and select a template before placing a grid.";
        return;
    }
    const int rows = std::clamp(brushGridRows_, 1, 16);
    const int columns = std::clamp(brushGridColumns_, 1, 16);
    const float spacingX = std::max(0.1f, brushGridSpacingX_);
    const float spacingY = std::max(0.1f, brushGridSpacingY_);
    const float offsetX = brushGridCenterOnClick_ ? static_cast<float>(columns - 1) * spacingX * 0.5f : 0.0f;
    const float offsetY = brushGridCenterOnClick_ ? static_cast<float>(rows - 1) * spacingY * 0.5f : 0.0f;
    const glm::vec3 origin = streamer_.origin();
    int placed = 0;
    int skipped = 0;
    undo_.BeginMacro(brushKind_ == 0 ? "Place NPC grid" : "Place GameObject grid");
    for (int row = 0; row < rows; ++row)
        for (int column = 0; column < columns; ++column)
        {
            const glm::vec3 requested(anchor.x + static_cast<float>(column) * spacingX - offsetX,
                                      anchor.y + static_cast<float>(row) * spacingY - offsetY,
                                      anchor.z);
            glm::vec3 localGround;
            float hitDistance = -1.0f;
            int tileX = 0, tileY = 0;
            const glm::vec3 top(requested.x - origin.x, requested.y - origin.y, 10000.0f);
            if (!streamer_.GroundHit(top, glm::vec3(0.0f, 0.0f, -1.0f), localGround, hitDistance, tileX, tileY))
            {
                ++skipped;
                continue;
            }
            if (liveTerrainPreview_ && adtEdits_.terrainPendingCount() > 0)
                localGround.z = adtEdits_.PreviewTerrainZ(localGround.z, localGround.x + origin.x,
                                                          localGround.y + origin.y);
            const glm::vec3 snapped(localGround.x + origin.x, localGround.y + origin.y, localGround.z);
            const bool added = brushKind_ == 0 ? PerformAddNpcAt(brushEntry_, snapped, brushYaw_)
                                                : PerformAddGameObjectAt(brushEntry_, snapped, brushYaw_);
            if (added)
            {
                ++placed;
                brushPlacements_.push_back({brushKind_, brushEntry_, snapped});
            }
            else
                ++skipped;
        }
    undo_.EndMacro();
    if (placed == 0)
        brushStatus_ = "Grid placement found no writable loaded terrain cells.";
    else
        brushStatus_ = "Placed " + std::to_string(placed) + " " +
                       (brushKind_ == 0 ? "NPC" : "GameObject") + " grid cell(s)" +
                       (skipped ? "; skipped " + std::to_string(skipped) + " cell(s) without terrain or DB success." : ".") +
                       " Undo removes the whole grid as one operation.";
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
                if (e.kind == SelKind::Npc)
                    if (const MapSpawn* spawn = npcLayer_.FindSpawn(e.guid))
                    {
                        const ResolvedCreatureDisplay display = spawn->ResolveDisplay(CurrentSpawnFilter());
                        ImGui::TextDisabled("display %u  (%s)", display.displayId,
                                            CreatureDisplaySourceName(display.source));
                    }
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

    ImGui::SeparatorText("Placement mode");
    if (ImGui::RadioButton("Brush / scatter", &brushPlacementMode_, 0))
        brushActive_ = false;
    ImGui::SameLine();
    if (ImGui::RadioButton("Array / grid", &brushPlacementMode_, 1))
        brushActive_ = false;
    if (brushPlacementMode_ == 0)
    {
        ImGui::SetNextItemWidth(180.0f);
        ImGui::DragFloat("Minimum spacing", &brushMinSpacing_, 0.1f, 0.0f, 100.0f, "%.1f yd");
    }
    else
    {
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderInt("Rows", &brushGridRows_, 1, 16);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::SliderInt("Columns", &brushGridColumns_, 1, 16);
        ImGui::SetNextItemWidth(170.0f);
        ImGui::DragFloat("Spacing X", &brushGridSpacingX_, 0.1f, 0.1f, 100.0f, "%.1f yd");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(170.0f);
        ImGui::DragFloat("Spacing Y", &brushGridSpacingY_, 0.1f, 0.1f, 100.0f, "%.1f yd");
        ImGui::Checkbox("Center grid on terrain click", &brushGridCenterOnClick_);
        ImGui::TextDisabled("One terrain click will place up to %d snapped %s instances as one undo step.",
                            brushGridRows_ * brushGridColumns_, brushKind_ == 0 ? "NPC" : "GameObject");
    }

    const char* armLabel = brushPlacementMode_ == 0
        ? (brushActive_ ? "Stop placement brush" : "Arm placement brush")
        : (brushActive_ ? "Stop grid placement" : "Arm grid placement");
    if (ImGui::Button(armLabel))
    {
        if (brushEntry_ == 0)
            brushStatus_ = "Choose a template entry before arming placement.";
        else
        {
            brushActive_ = !brushActive_;
            brushStatus_ = brushActive_
                ? (brushPlacementMode_ == 0
                    ? "Brush armed — right-click terrain to place repeatedly. Escape cancels."
                    : "Grid armed — right-click terrain to place the configured array. Escape cancels.")
                : "Placement tool stopped.";
        }
    }
    if (brushActive_)
        ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.42f, 1.0f),
                           brushPlacementMode_ == 0 ? "%s brush active: entry %u" : "%s grid active: %dx%d entry %u",
                           brushKind_ == 0 ? "NPC" : "GameObject", brushGridRows_, brushGridColumns_, brushEntry_);
    if (!brushStatus_.empty())
        ImGui::TextDisabled("%s", brushStatus_.c_str());
    ImGui::Separator();
    ImGui::TextWrapped(brushPlacementMode_ == 0
        ? "This is a rapid version of the right-click Add menu. Every stamp is a normal database spawn, gets its own undo command, and can be moved precisely afterward. The spacing guard applies only to stamps made in this session; set it to 0 to allow overlap."
        : "Array/Grid placement samples terrain under every cell, then creates normal database spawns in one compound undo command. Cells without loaded terrain are skipped and reported; no off-map or floating placeholder is inserted.");
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

void AdtViewerModule::ApplyAiBehaviorProfiles()
{
    std::vector<MapSpawn> spawns;
    npcLayer_.SnapshotSpawns(spawns);
    const auto profile = aiBehaviorProfiles_.find(selectedMapDir_);
    for (const MapSpawn& spawn : spawns)
    {
        NpcAiBehavior behavior = spawn.aiBehavior;
        if (profile != aiBehaviorProfiles_.end())
        {
            const auto saved = profile->second.find(spawn.guid);
            if (saved != profile->second.end())
                behavior = saved->second.behavior;
        }
        npcLayer_.SetAiBehavior(spawn.guid, behavior);
    }
    npcLayer_.SetAiPreviewTarget(aiPreviewTargetEnabled_, aiPreviewTargetWorld_);
}

void AdtViewerModule::SyncAiBehaviorEdit()
{
    if (selKind_ != SelKind::Npc)
    {
        if (!aiBehaviorDirty_)
            aiBehaviorEditGuid_ = 0;
        return;
    }
    if (aiBehaviorDirty_ || aiBehaviorEditGuid_ == selGuid_)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn)
    {
        aiBehaviorEditGuid_ = 0;
        return;
    }
    AiBehaviorProfileEntry state;
    state.behavior = spawn->aiBehavior;
    state.templateScope = !(spawn->aggroRadiusFromSpawn || spawn->leashDistanceFromSpawn);
    const auto profile = aiBehaviorProfiles_.find(selectedMapDir_);
    if (profile != aiBehaviorProfiles_.end())
        if (const auto saved = profile->second.find(spawn->guid); saved != profile->second.end())
            state = saved->second;
    aiBehaviorEdit_ = state;
    aiBehaviorOrig_ = state;
    aiBehaviorEditGuid_ = spawn->guid;
    aiBehaviorStatus_.clear();
}

void AdtViewerModule::ApplyAiBehaviorEdit(bool persistServerColumns)
{
    if (aiBehaviorEditGuid_ == 0)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(aiBehaviorEditGuid_);
    if (!spawn)
        return;
    NpcAiBehavior& behavior = aiBehaviorEdit_.behavior;
    behavior.aggroRadius = std::clamp(behavior.aggroRadius, 0.0f, 10000.0f);
    behavior.leashDistance = std::clamp(behavior.leashDistance, 0.0f, 10000.0f);

    bool dbAggro = false, dbLeash = false;
    if (persistServerColumns)
    {
        if (!svc_ || !svc_->connected || !svc_->activeDb)
        {
            aiBehaviorStatus_ = "Connect a project database to save server behavior columns.";
            return;
        }
        const DbError result = spawnRepo_.UpdateCreatureAiBehavior(*svc_->activeDb, spawn->guid,
                                                                     spawn->entry, aiBehaviorEdit_.templateScope,
                                                                     behavior, dbAggro, dbLeash);
        if (!result.ok)
        {
            aiBehaviorStatus_ = "Server save unavailable: " + result.message +
                                " Studio preview values remain staged.";
            return;
        }
        aiBehaviorStatus_ = result.message.empty()
            ? "Saved recognized server behavior column(s)."
            : result.message;
    }
    else
        aiBehaviorStatus_ = "Saved Studio AI preview behavior.";

    aiBehaviorProfiles_[selectedMapDir_][spawn->guid] = aiBehaviorEdit_;
    npcLayer_.SetAiBehavior(spawn->guid, behavior);
    npcLayer_.SetAiPreviewTarget(aiPreviewTargetEnabled_, aiPreviewTargetWorld_);
    aiBehaviorOrig_ = aiBehaviorEdit_;
    aiBehaviorDirty_ = false;
    if (svc_ && svc_->requestSaveSettings)
        svc_->requestSaveSettings();
    if (svc_ && svc_->setStatus)
        svc_->setStatus(aiBehaviorStatus_);
}

bool AdtViewerModule::TryPlaceAiPreviewTarget(const glm::vec3& world)
{
    if (!aiPreviewTargetPlacementActive_)
        return false;
    aiPreviewTargetWorld_ = world;
    aiPreviewTargetEnabled_ = true;
    aiPreviewTargetPlacementActive_ = false;
    npcLayer_.SetAiPreviewTarget(true, world);
    aiBehaviorStatus_ = "Placed AI preview target. Move it to test aggro and leash behavior.";
    return true;
}

void AdtViewerModule::DrawAiBehaviorOverlay(const glm::mat4& view, const glm::mat4& proj,
                                             const ImVec2& p0, int w, int h)
{
    if (!showAiBehaviorOverlay_ || selKind_ != SelKind::Npc)
        return;
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    const NpcAiBehavior* behavior = npcLayer_.FindAiBehavior(selGuid_);
    if (!spawn || !behavior)
        return;
    const glm::vec3 origin = streamer_.origin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    auto ring = [&](float radius, ImU32 color, float thickness) {
        if (radius <= 0.01f)
            return;
        constexpr int kSegments = 48;
        ImVec2 previous{};
        bool havePrevious = false;
        for (int i = 0; i <= kSegments; ++i)
        {
            const float angle = (static_cast<float>(i) / kSegments) * 6.28318530718f;
            ImVec2 at;
            const glm::vec3 world(spawn->x + std::cos(angle) * radius,
                                  spawn->y + std::sin(angle) * radius, spawn->z);
            const bool visible = ProjectWorldPoint(world, origin, view, proj, p0, w, h, at);
            if (visible && havePrevious)
                draw->AddLine(previous, at, color, thickness);
            previous = at;
            havePrevious = visible;
        }
    };
    // Orange/red is detection; cool blue is the maximum chase-return envelope.
    if (behavior->aggroEnabled)
        ring(behavior->aggroRadius, IM_COL32(255, 121, 58, 180), 2.0f);
    ring(behavior->leashDistance, IM_COL32(95, 168, 255, 150), 1.25f);

    NpcLayer::AiPreviewState state;
    npcLayer_.GetAiPreviewState(spawn->guid, state);
    ImVec2 npcAt;
    if (ProjectWorldPoint(state.position, origin, view, proj, p0, w, h, npcAt))
    {
        const char* label = state.chasing ? "CHASING" : state.returning ? "RETURNING" :
                            behavior->patrolPattern == PatrolRoutePattern::PingPong ? "PATROL: PING-PONG" :
                            behavior->patrolPattern == PatrolRoutePattern::Once ? "PATROL: ONCE" : "PATROL: LOOP";
        const ImU32 stateColor = state.chasing ? IM_COL32(255, 90, 68, 255) :
                                 state.returning ? IM_COL32(111, 180, 255, 255) : IM_COL32(255, 220, 106, 240);
        draw->AddText(ImVec2(npcAt.x + 9.0f, npcAt.y - 20.0f), stateColor, label);
    }

    if (aiPreviewTargetEnabled_)
    {
        ImVec2 targetAt;
        if (ProjectWorldPoint(aiPreviewTargetWorld_, origin, view, proj, p0, w, h, targetAt))
        {
            draw->AddCircleFilled(targetAt, 7.0f, IM_COL32(236, 74, 216, 240), 14);
            draw->AddCircle(targetAt, 7.0f, IM_COL32(255, 230, 252, 255), 14, 1.5f);
            draw->AddLine(ImVec2(targetAt.x - 5.0f, targetAt.y), ImVec2(targetAt.x + 5.0f, targetAt.y),
                          IM_COL32(55, 16, 50, 255), 1.4f);
            draw->AddLine(ImVec2(targetAt.x, targetAt.y - 5.0f), ImVec2(targetAt.x, targetAt.y + 5.0f),
                          IM_COL32(55, 16, 50, 255), 1.4f);
            draw->AddText(ImVec2(targetAt.x + 9.0f, targetAt.y - 8.0f), IM_COL32(255, 222, 253, 255),
                          "AI target");
            if ((state.chasing || state.returning) && ProjectWorldPoint(state.position, origin, view, proj, p0, w, h, npcAt))
                draw->AddLine(npcAt, targetAt, state.chasing ? IM_COL32(255, 90, 68, 215)
                                                              : IM_COL32(111, 180, 255, 180), 1.8f);
        }
    }
}

void AdtViewerModule::DrawAiBehaviorPanel()
{
    if (!ImGui::Begin("AI Behavior"))
    {
        ImGui::End();
        return;
    }
    if (selKind_ != SelKind::Npc)
    {
        if (!aiBehaviorDirty_)
            SyncAiBehaviorEdit();
        ImGui::TextWrapped("Select an NPC in the World Editor to configure its visual waypoint patrol, aggro radius, leash return distance, and preview target.");
        ImGui::End();
        return;
    }
    if (aiBehaviorDirty_ && aiBehaviorEditGuid_ != selGuid_)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f),
                           "Guid %u has unsaved AI behavior edits.", aiBehaviorEditGuid_);
        if (ImGui::Button("Save Studio behavior"))
            ApplyAiBehaviorEdit(false);
        ImGui::SameLine();
        if (ImGui::Button("Discard and load selected"))
        {
            aiBehaviorDirty_ = false;
            aiBehaviorEditGuid_ = 0;
            SyncAiBehaviorEdit();
        }
        ImGui::End();
        return;
    }
    SyncAiBehaviorEdit();
    const MapSpawn* spawn = npcLayer_.FindSpawn(selGuid_);
    if (!spawn || aiBehaviorEditGuid_ == 0)
    {
        ImGui::TextDisabled("The selected NPC is no longer available.");
        ImGui::End();
        return;
    }

    const std::string title = svc_ && svc_->lookups ? svc_->lookups->LabelCreature(spawn->entry)
                                                      : ("entry " + std::to_string(spawn->entry));
    ImGui::TextUnformatted(title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("guid %u", spawn->guid);
    if (ImGui::Checkbox("Show AI overlay", &showAiBehaviorOverlay_))
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();

    bool changed = false;
    bool targetChanged = false;
    if (ImGui::CollapsingHeader("Detection and leash", ImGuiTreeNodeFlags_DefaultOpen))
        if (BeginFieldTable("aibehaviorcombat", 164.0f))
        {
            FieldRow("Enable aggro preview"); changed |= ImGui::Checkbox("##aggroen", &aiBehaviorEdit_.behavior.aggroEnabled);
            FieldRow("Aggro radius (yd)", "Preview target acquisition radius. Uses detection_range/aggro aliases when a live server column exists.");
            changed |= InputFloatField("##aggro", aiBehaviorEdit_.behavior.aggroRadius);
            FieldRow("Leash distance (yd)", "Maximum horizontal distance from home before the preview returns to patrol. A zero value disables the preview leash.");
            changed |= InputFloatField("##leash", aiBehaviorEdit_.behavior.leashDistance);
            FieldRow("Server save scope");
            changed |= ImGui::Checkbox("Save to template (all spawns)", &aiBehaviorEdit_.templateScope);
            EndFieldTable();
        }

    if (ImGui::CollapsingHeader("Patrol route", ImGuiTreeNodeFlags_DefaultOpen))
    {
        static const char* kPatterns[] = {"Loop (server standard)", "Ping-pong (preview)", "One-shot (preview)"};
        int pattern = static_cast<int>(aiBehaviorEdit_.behavior.patrolPattern);
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("Pattern", &pattern, kPatterns, IM_ARRAYSIZE(kPatterns)))
        {
            aiBehaviorEdit_.behavior.patrolPattern = static_cast<PatrolRoutePattern>(std::clamp(pattern, 0, 2));
            changed = true;
        }
        ImGui::TextDisabled("Waypoint Editor provides visual point creation, terrain placement, ordering and delays. Loop persists as normal core waypoint motion; Ping-pong/One-shot are Studio simulation patterns until backed by custom server scripting.");
        if (spawn->pathId == 0)
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.24f, 1.0f), "No waypoint path is currently bound to this NPC.");
        else
            ImGui::TextDisabled("Bound path %u (%s)", spawn->pathId, WaypointSourceLabel(spawn->PathSource()));
        if (spawn->pathId != 0 && spawn->movementType != 2)
        {
            ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.24f, 1.0f), "Waypoint movement is not enabled for this spawn.");
            ImGui::SameLine();
            if (ImGui::SmallButton("Enable waypoint movement"))
                EnableSelectedNpcWaypointMotion();
        }
        if (ImGui::Button("Open Waypoint Path panel") && svc_ && svc_->focusWindow)
            svc_->focusWindow("Waypoint Path");
    }

    ImGui::SeparatorText("Live aggro / leash preview");
    ImGui::Checkbox("Enable preview target", &aiPreviewTargetEnabled_);
    if (BeginFieldTable("aitarget", 164.0f))
    {
        FieldRow("Target X"); targetChanged |= InputFloatField("##aitx", aiPreviewTargetWorld_.x);
        FieldRow("Target Y"); targetChanged |= InputFloatField("##aity", aiPreviewTargetWorld_.y);
        FieldRow("Target Z"); targetChanged |= InputFloatField("##aitz", aiPreviewTargetWorld_.z);
        EndFieldTable();
    }
    if (ImGui::Button(aiPreviewTargetPlacementActive_ ? "Stop placing target" : "Place target on terrain"))
    {
        aiPreviewTargetPlacementActive_ = !aiPreviewTargetPlacementActive_;
        if (aiPreviewTargetPlacementActive_)
        {
            inGameViewMode_ = false;
            editMode_ = true;
            terrainSculptActive_ = false;
            lightPlacementActive_ = false;
            brushActive_ = false;
            waypointPlacementMode_ = WaypointPlacementMode::None;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Target at camera"))
    {
        const glm::vec3 local = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye() : camera_.center();
        const glm::vec3 origin = streamer_.origin();
        aiPreviewTargetWorld_ = glm::vec3(local.x + origin.x, local.y + origin.y, local.z);
        aiPreviewTargetEnabled_ = true;
    }
    if (changed)
    {
        aiBehaviorEdit_.behavior.aggroRadius = std::clamp(aiBehaviorEdit_.behavior.aggroRadius, 0.0f, 10000.0f);
        aiBehaviorEdit_.behavior.leashDistance = std::clamp(aiBehaviorEdit_.behavior.leashDistance, 0.0f, 10000.0f);
        aiBehaviorDirty_ = true;
    }
    if (targetChanged)
    {
        aiPreviewTargetEnabled_ = true;
        aiBehaviorStatus_ = "Moved AI preview target.";
    }
    npcLayer_.SetAiPreviewTarget(aiPreviewTargetEnabled_, aiPreviewTargetWorld_);
    if (aiBehaviorDirty_)
        npcLayer_.SetAiBehavior(spawn->guid, aiBehaviorEdit_.behavior); // immediate live feedback before Save

    ImGui::BeginDisabled(!aiBehaviorDirty_);
    if (ImGui::Button("Save Studio behavior"))
        ApplyAiBehaviorEdit(false);
    ImGui::SameLine();
    if (ImGui::Button("Save recognized server values"))
        ApplyAiBehaviorEdit(true);
    ImGui::SameLine();
    if (ImGui::Button("Revert behavior"))
    {
        aiBehaviorEdit_ = aiBehaviorOrig_;
        aiBehaviorDirty_ = false;
        npcLayer_.SetAiBehavior(spawn->guid, aiBehaviorEdit_.behavior);
    }
    ImGui::EndDisabled();

    ImGui::TextDisabled("Schema: aggro %s; leash %s. Missing server fields stay safely in the Studio preview profile.",
                        spawn->hasAggroRadiusColumn ? (spawn->aggroRadiusFromSpawn ? "spawn override" : "template/default") : "Studio fallback",
                        spawn->hasLeashDistanceColumn ? (spawn->leashDistanceFromSpawn ? "spawn override" : "template/default") : "Studio fallback");
    if (!aiBehaviorStatus_.empty())
        ImGui::TextDisabled("%s", aiBehaviorStatus_.c_str());
    ImGui::End();
}



void AdtViewerModule::LoadScriptTriggersForMap(const std::string& mapDir)
{
    if (mapDir.empty() || scriptTriggerMapDir_ == mapDir)
        return;
    if (!scriptTriggerMapDir_.empty() && scriptTriggersDirty_)
        scriptTriggerDrafts_[scriptTriggerMapDir_] = scriptTriggersEdit_;

    scriptTriggerMapDir_ = mapDir;
    const auto draft = scriptTriggerDrafts_.find(mapDir);
    if (draft != scriptTriggerDrafts_.end())
    {
        scriptTriggersEdit_ = draft->second;
        scriptTriggersDirty_ = true;
        scriptTriggerStatus_ = "Restored unsaved trigger definitions for " + mapDir + ".";
    }
    else
    {
        const auto saved = scriptTriggerProfiles_.find(mapDir);
        scriptTriggersEdit_ = saved != scriptTriggerProfiles_.end() ? saved->second
                                                                      : std::vector<ScriptEventTrigger>{};
        scriptTriggersDirty_ = false;
        scriptTriggerStatus_ = saved != scriptTriggerProfiles_.end()
            ? "Loaded saved script trigger definitions for " + mapDir + "."
            : "No script triggers authored for this map yet.";
    }
    selectedScriptTriggerId_ = scriptTriggersEdit_.empty() ? 0 : scriptTriggersEdit_.front().id;
    scriptTriggerRuntime_.clear();
    pendingScriptTriggerActions_.clear();
    scriptTriggerLog_.clear();
    scriptPreviewTimeSeconds_ = 0.0f;
    scriptPreviewPlayerEnabled_ = false;
    scriptPreviewPlayerFollowCamera_ = false;
    scriptPreviewPlayerPlacementActive_ = false;
    scriptTriggerCenterPlacementActive_ = false;
    scriptInteractionMode_ = false;
}

void AdtViewerModule::SaveScriptTriggers()
{
    if (scriptTriggerMapDir_.empty())
        return;
    scriptTriggerProfiles_[scriptTriggerMapDir_] = scriptTriggersEdit_;
    scriptTriggerDrafts_.erase(scriptTriggerMapDir_);
    scriptTriggersDirty_ = false;
    scriptTriggerStatus_ = "Saved " + std::to_string(scriptTriggersEdit_.size()) +
                           " Studio script trigger(s) for " + scriptTriggerMapDir_ + ".";
    if (svc_ && svc_->requestSaveSettings)
        svc_->requestSaveSettings();
    if (svc_ && svc_->setStatus)
        svc_->setStatus(scriptTriggerStatus_);
}

void AdtViewerModule::RevertScriptTriggers()
{
    if (scriptTriggerMapDir_.empty())
        return;
    const auto saved = scriptTriggerProfiles_.find(scriptTriggerMapDir_);
    scriptTriggersEdit_ = saved != scriptTriggerProfiles_.end() ? saved->second
                                                                   : std::vector<ScriptEventTrigger>{};
    scriptTriggerDrafts_.erase(scriptTriggerMapDir_);
    scriptTriggersDirty_ = false;
    selectedScriptTriggerId_ = scriptTriggersEdit_.empty() ? 0 : scriptTriggersEdit_.front().id;
    scriptTriggerRuntime_.clear();
    pendingScriptTriggerActions_.clear();
    scriptTriggerLog_.clear();
    scriptPreviewTimeSeconds_ = 0.0f;
    scriptTriggerStatus_ = "Reverted script triggers to the saved Studio profile.";
}

void AdtViewerModule::MarkScriptTriggersDirty(const char* status)
{
    if (scriptTriggerMapDir_.empty())
        return;
    scriptTriggersDirty_ = true;
    scriptTriggerDrafts_[scriptTriggerMapDir_] = scriptTriggersEdit_;
    if (status)
        scriptTriggerStatus_ = status;
}

bool AdtViewerModule::TryPlaceScriptPreviewPlayer(const glm::vec3& world)
{
    if (!scriptPreviewPlayerPlacementActive_)
        return false;
    scriptPreviewPlayerWorld_ = world;
    scriptPreviewPlayerEnabled_ = true;
    scriptPreviewPlayerPlacementActive_ = false;
    scriptTriggerStatus_ = "Placed script preview player. Cross area/proximity volumes to test events.";
    return true;
}

bool AdtViewerModule::TryPlaceScriptTriggerCenter(const glm::vec3& world)
{
    if (!scriptTriggerCenterPlacementActive_ || selectedScriptTriggerId_ == 0)
        return false;
    for (ScriptEventTrigger& trigger : scriptTriggersEdit_)
        if (trigger.id == selectedScriptTriggerId_)
        {
            trigger.center = world;
            scriptTriggerCenterPlacementActive_ = false;
            scriptTriggerRuntime_.erase(trigger.id);
            MarkScriptTriggersDirty("Placed trigger center on terrain.");
            return true;
        }
    scriptTriggerCenterPlacementActive_ = false;
    return false;
}

void AdtViewerModule::FireScriptTrigger(uint64_t triggerId, const char* reason)
{
    const auto found = std::find_if(scriptTriggersEdit_.begin(), scriptTriggersEdit_.end(),
                                    [triggerId](const ScriptEventTrigger& t) { return t.id == triggerId; });
    if (found == scriptTriggersEdit_.end() || !found->enabled)
        return;
    const ScriptEventTrigger& trigger = *found;
    std::string detail = reason ? reason : "triggered";
    detail += " — " + trigger.name;
    if (!trigger.scriptHook.empty())
        detail += " | legacy hook: " + trigger.scriptHook;
    if (trigger.scriptEventId != 0)
        detail += " | event: " + std::to_string(trigger.scriptEventId);
    if (trigger.smartActionListId != 0)
        detail += " | SmartAI list: " + std::to_string(trigger.smartActionListId);
    if (trigger.actions.empty() && trigger.scriptHook.empty() && trigger.scriptEventId == 0 &&
        trigger.smartActionListId == 0)
        detail += " | no server hook bound (preview event only)";
    if (!trigger.actions.empty())
        detail += " | queued actions: " + std::to_string(trigger.actions.size());
    scriptTriggerLog_.push_back({trigger.id, scriptPreviewTimeSeconds_, std::move(detail)});
    if (scriptTriggerLog_.size() > 100)
        scriptTriggerLog_.erase(scriptTriggerLog_.begin(), scriptTriggerLog_.begin() +
                                 static_cast<std::ptrdiff_t>(scriptTriggerLog_.size() - 100));
    scriptTriggerStatus_ = scriptTriggerLog_.back().text;

    // Preserve legacy single-hook fields, then append an ordered preview sequence. A delay of zero
    // executes in the current world tick; later actions remain queued against this trigger id.
    if (trigger.actions.empty())
    {
        if (!trigger.scriptHook.empty())
        {
            ScriptTriggerAction legacy;
            legacy.type = ScriptTriggerActionType::ScriptHook;
            legacy.hook = trigger.scriptHook;
            ExecuteScriptTriggerAction(trigger.id, legacy);
        }
        if (trigger.smartActionListId != 0)
        {
            ScriptTriggerAction legacy;
            legacy.type = ScriptTriggerActionType::SmartActionList;
            legacy.value = trigger.smartActionListId;
            ExecuteScriptTriggerAction(trigger.id, legacy);
        }
    }
    else
        for (const ScriptTriggerAction& action : trigger.actions)
            if (action.enabled)
                pendingScriptTriggerActions_.push_back({trigger.id, action,
                                                        std::max(action.delaySeconds, 0.0f)});
    if (svc_ && svc_->setStatus)
        svc_->setStatus(scriptTriggerStatus_);
}

void AdtViewerModule::ExecuteScriptTriggerAction(uint64_t triggerId, const ScriptTriggerAction& action)
{
    if (!action.enabled)
        return;
    const char* kind = action.type == ScriptTriggerActionType::ScriptHook ? "hook" :
                       action.type == ScriptTriggerActionType::SmartActionList ? "SmartAI action-list" :
                       action.type == ScriptTriggerActionType::CastSpell ? "cast spell" :
                       action.type == ScriptTriggerActionType::TalkText ? "talk text" : "toggle GameObject";
    std::string detail = std::string("action ") + kind;
    if (action.type == ScriptTriggerActionType::ScriptHook)
        detail += action.hook.empty() ? " (unnamed)" : (": " + action.hook);
    else if (action.type == ScriptTriggerActionType::TalkText)
        detail += action.text.empty() ? " (empty)" : (": " + action.text);
    else
        detail += " " + std::to_string(action.value);

    // A CastSpell sequence action now drives the same timing/trajectory preview as the Spell
    // Effect Previewer panel. This remains a Studio visualization; server execution still needs
    // the authored SmartAI/custom hook path represented by the trigger metadata.
    if (action.type == ScriptTriggerActionType::CastSpell && action.value != 0)
    {
        LoadSpellPreviewDefinition(action.value);
        spellPreview_.targetSource = scriptPreviewPlayerEnabled_
            ? SpellPreviewTargetSource::ScriptPreviewPlayer : SpellPreviewTargetSource::SelectedObject;
        spellPreview_.timelineSeconds = 0.0f;
        spellPreview_.playing = true;
        detail += " (World Editor VFX preview started)";
    }

    // A GameObject state flip is the one sequence action that has a useful immediate world-side
    // preview; it remains session-only until a normal GameObject Instance save is requested.
    if (action.type == ScriptTriggerActionType::ToggleGameObject && action.value != 0)
    {
        if (MapGameObject* go = goLayer_.FindSpawn(action.value))
        {
            go->state = go->state == 0 ? 1 : 0;
            detail += " (preview state toggled)";
        }
        else
            detail += " (guid not loaded)";
    }
    scriptTriggerLog_.push_back({triggerId, scriptPreviewTimeSeconds_, std::move(detail)});
    if (scriptTriggerLog_.size() > 100)
        scriptTriggerLog_.erase(scriptTriggerLog_.begin(), scriptTriggerLog_.begin() +
                                 static_cast<std::ptrdiff_t>(scriptTriggerLog_.size() - 100));
    scriptTriggerStatus_ = scriptTriggerLog_.back().text;
    if (svc_ && svc_->setStatus)
        svc_->setStatus(scriptTriggerStatus_);
}

void AdtViewerModule::ProcessPendingScriptTriggerActions(float dtSeconds)
{
    const float dt = std::max(dtSeconds, 0.0f);
    for (size_t i = 0; i < pendingScriptTriggerActions_.size(); )
    {
        PendingScriptTriggerAction& pending = pendingScriptTriggerActions_[i];
        const auto trigger = std::find_if(scriptTriggersEdit_.begin(), scriptTriggersEdit_.end(),
                                          [&](const ScriptEventTrigger& t) { return t.id == pending.triggerId; });
        if (trigger == scriptTriggersEdit_.end() || !trigger->enabled)
        {
            pendingScriptTriggerActions_.erase(pendingScriptTriggerActions_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        pending.remainingSeconds -= dt;
        if (pending.remainingSeconds > 1e-5f)
        {
            ++i;
            continue;
        }
        const uint64_t triggerId = pending.triggerId;
        const ScriptTriggerAction action = pending.action;
        pendingScriptTriggerActions_.erase(pendingScriptTriggerActions_.begin() + static_cast<std::ptrdiff_t>(i));
        ExecuteScriptTriggerAction(triggerId, action);
    }
}

void AdtViewerModule::FireInteractionTriggers(int objectKind, uint32_t guid)
{
    if (!scriptInteractionMode_ || guid == 0)
        return;
    for (const ScriptEventTrigger& trigger : scriptTriggersEdit_)
        if (trigger.enabled && trigger.type == ScriptTriggerType::Interaction &&
            (trigger.targetKind == ScriptTriggerObjectKind::None ||
             (static_cast<int>(trigger.targetKind) == objectKind && trigger.targetGuid == guid)))
            FireScriptTrigger(trigger.id, "interaction");
}

void AdtViewerModule::EvaluateScriptTriggers(float dtMs)
{
    const float dt = std::clamp(dtMs, 0.0f, 250.0f) / 1000.0f;
    scriptPreviewTimeSeconds_ += dt;
    auto targetPosition = [&](ScriptTriggerObjectKind kind, uint32_t guid, glm::vec3& out) {
        if (kind == ScriptTriggerObjectKind::Npc)
        {
            NpcLayer::AiPreviewState state;
            if (npcLayer_.GetAiPreviewState(guid, state))
            {
                out = state.position;
                return true;
            }
        }
        else if (kind == ScriptTriggerObjectKind::GameObject)
        {
            if (const MapGameObject* spawn = goLayer_.FindSpawn(guid))
            {
                out = glm::vec3(spawn->x, spawn->y, spawn->z);
                return true;
            }
        }
        return false;
    };

    for (const ScriptEventTrigger& trigger : scriptTriggersEdit_)
    {
        ScriptTriggerRuntime& runtime = scriptTriggerRuntime_[trigger.id];
        if (!trigger.enabled)
        {
            runtime = ScriptTriggerRuntime{};
            continue;
        }
        if (trigger.type == ScriptTriggerType::Area)
        {
            bool inside = false;
            if (scriptPreviewPlayerEnabled_)
            {
                const glm::vec3 d = scriptPreviewPlayerWorld_ - trigger.center;
                if (trigger.areaShape == ScriptTriggerAreaShape::Circle)
                    inside = d.x * d.x + d.y * d.y <= trigger.radius * trigger.radius &&
                             (trigger.height <= 0.0f || std::fabs(d.z) <= trigger.height);
                else
                    inside = std::fabs(d.x) <= trigger.boxExtents.x && std::fabs(d.y) <= trigger.boxExtents.y &&
                             std::fabs(d.z) <= trigger.boxExtents.z;
            }
            if (inside && !runtime.areaInside)
                FireScriptTrigger(trigger.id, "area enter");
            else if (!inside && runtime.areaInside)
                FireScriptTrigger(trigger.id, "area exit");
            runtime.areaInside = inside;
        }
        else if (trigger.type == ScriptTriggerType::Proximity)
        {
            bool inside = false;
            glm::vec3 object;
            if (scriptPreviewPlayerEnabled_ && targetPosition(trigger.targetKind, trigger.targetGuid, object))
            {
                const glm::vec3 d = scriptPreviewPlayerWorld_ - object;
                inside = glm::dot(d, d) <= trigger.radius * trigger.radius;
            }
            if (inside && !runtime.proximityInside)
                FireScriptTrigger(trigger.id, "proximity enter");
            else if (!inside && runtime.proximityInside)
                FireScriptTrigger(trigger.id, "proximity exit");
            runtime.proximityInside = inside;
        }
        else if (trigger.type == ScriptTriggerType::Timer)
        {
            runtime.timerElapsed += dt;
            const float delay = std::max(trigger.timerDelaySeconds, 0.0f);
            if (!runtime.timerFired)
            {
                if (runtime.timerElapsed >= delay)
                {
                    FireScriptTrigger(trigger.id, "timer elapsed");
                    runtime.timerFired = true;
                    runtime.timerElapsed = 0.0f;
                }
            }
            else if (trigger.timerRepeatSeconds > 0.0f && runtime.timerElapsed >= trigger.timerRepeatSeconds)
            {
                FireScriptTrigger(trigger.id, "timer repeat");
                runtime.timerElapsed = 0.0f;
            }
        }
    }
    ProcessPendingScriptTriggerActions(dt);
}

void AdtViewerModule::DrawScriptTriggerOverlay(const glm::mat4& view, const glm::mat4& proj,
                                                const ImVec2& p0, int w, int h)
{
    if (!showScriptTriggerOverlay_ || scriptTriggersEdit_.empty())
        return;
    const glm::vec3 origin = streamer_.origin();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    auto project = [&](const glm::vec3& world, ImVec2& screen) {
        return ProjectWorldPoint(world, origin, view, proj, p0, w, h, screen);
    };
    auto objectPosition = [&](ScriptTriggerObjectKind kind, uint32_t guid, glm::vec3& out) {
        if (kind == ScriptTriggerObjectKind::Npc)
        {
            NpcLayer::AiPreviewState state;
            if (npcLayer_.GetAiPreviewState(guid, state)) { out = state.position; return true; }
        }
        if (kind == ScriptTriggerObjectKind::GameObject)
            if (const MapGameObject* spawn = goLayer_.FindSpawn(guid)) { out = {spawn->x, spawn->y, spawn->z}; return true; }
        return false;
    };
    auto ring = [&](const glm::vec3& center, float radius, ImU32 color, float thickness) {
        constexpr int segments = 40;
        ImVec2 previous{};
        bool havePrevious = false;
        for (int i = 0; i <= segments; ++i)
        {
            const float angle = (static_cast<float>(i) / segments) * 6.28318530718f;
            ImVec2 at;
            const bool visible = project(center + glm::vec3(std::cos(angle) * radius,
                                                             std::sin(angle) * radius, 0.0f), at);
            if (visible && havePrevious)
                draw->AddLine(previous, at, color, thickness);
            previous = at;
            havePrevious = visible;
        }
    };

    for (const ScriptEventTrigger& trigger : scriptTriggersEdit_)
    {
        const bool selected = trigger.id == selectedScriptTriggerId_;
        const ImU32 color = trigger.type == ScriptTriggerType::Area ? IM_COL32(255, 183, 65, 180) :
                           trigger.type == ScriptTriggerType::Interaction ? IM_COL32(95, 206, 255, 210) :
                           trigger.type == ScriptTriggerType::Proximity ? IM_COL32(126, 240, 127, 190) :
                                                                           IM_COL32(214, 110, 255, 190);
        if (trigger.type == ScriptTriggerType::Area)
        {
            if (trigger.areaShape == ScriptTriggerAreaShape::Circle)
                ring(trigger.center, trigger.radius, color, selected ? 2.5f : 1.2f);
            else
            {
                const glm::vec3 e = trigger.boxExtents;
                glm::vec3 corners[5] = {
                    trigger.center + glm::vec3(-e.x, -e.y, 0), trigger.center + glm::vec3(e.x, -e.y, 0),
                    trigger.center + glm::vec3(e.x, e.y, 0), trigger.center + glm::vec3(-e.x, e.y, 0),
                    trigger.center + glm::vec3(-e.x, -e.y, 0)};
                for (int i = 1; i < 5; ++i)
                {
                    ImVec2 a, b;
                    if (project(corners[i - 1], a) && project(corners[i], b))
                        draw->AddLine(a, b, color, selected ? 2.5f : 1.2f);
                }
            }
        }
        else if (trigger.type == ScriptTriggerType::Proximity)
        {
            glm::vec3 object;
            if (objectPosition(trigger.targetKind, trigger.targetGuid, object))
                ring(object, trigger.radius, color, selected ? 2.5f : 1.2f);
        }

        glm::vec3 labelPos = trigger.center;
        if ((trigger.type == ScriptTriggerType::Interaction || trigger.type == ScriptTriggerType::Proximity) &&
            !objectPosition(trigger.targetKind, trigger.targetGuid, labelPos))
            labelPos = trigger.center;
        ImVec2 at;
        if (project(labelPos, at))
        {
            const char* kind = trigger.type == ScriptTriggerType::Area ? "AREA" :
                               trigger.type == ScriptTriggerType::Interaction ? "INTERACT" :
                               trigger.type == ScriptTriggerType::Proximity ? "NEAR" : "TIMER";
            const std::string label = std::string(kind) + "  " + trigger.name;
            draw->AddCircleFilled(at, selected ? 6.0f : 4.0f, color, 12);
            if (selected)
                draw->AddText(ImVec2(at.x + 8.0f, at.y - 10.0f), IM_COL32(255, 247, 213, 255), label.c_str());
        }
    }

    if (scriptPreviewPlayerEnabled_)
    {
        ImVec2 at;
        if (project(scriptPreviewPlayerWorld_, at))
        {
            draw->AddCircleFilled(at, 7.0f, IM_COL32(76, 238, 226, 240), 14);
            draw->AddCircle(at, 7.0f, IM_COL32(220, 255, 251, 255), 14, 1.5f);
            draw->AddText(ImVec2(at.x + 9.0f, at.y - 8.0f), IM_COL32(214, 255, 252, 255), "Script player");
        }
    }
}

void AdtViewerModule::DrawScriptTriggersPanel()
{
    if (!ImGui::Begin("Script Triggers"))
    {
        ImGui::End();
        return;
    }
    const bool mapReady = streamerInit_ && !loadedName_.empty() && !scriptTriggerMapDir_.empty();
    if (!mapReady)
    {
        ImGui::TextWrapped("Open a map to author and preview area, interaction, proximity, and timer-based script event triggers.");
        ImGui::End();
        return;
    }
    ImGui::BeginDisabled(!scriptTriggersDirty_);
    if (ImGui::Button("Save triggers"))
        SaveScriptTriggers();
    ImGui::SameLine();
    if (ImGui::Button("Revert triggers"))
        RevertScriptTriggers();
    ImGui::EndDisabled();
    if (scriptTriggersDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f), "unsaved preview");
    }
    if (!scriptTriggerStatus_.empty())
        ImGui::TextDisabled("%s", scriptTriggerStatus_.c_str());

    ImGui::SeparatorText("Preview player / interaction");
    if (ImGui::Checkbox("Enable script player", &scriptPreviewPlayerEnabled_))
        scriptTriggerRuntime_.clear();
    ImGui::SameLine();
    if (ImGui::Checkbox("Player follows camera", &scriptPreviewPlayerFollowCamera_))
        scriptPreviewPlayerEnabled_ = true;
    if (BeginFieldTable("scriptplayer", 150.0f))
    {
        FieldRow("Player X"); InputFloatField("##spx", scriptPreviewPlayerWorld_.x);
        FieldRow("Player Y"); InputFloatField("##spy", scriptPreviewPlayerWorld_.y);
        FieldRow("Player Z"); InputFloatField("##spz", scriptPreviewPlayerWorld_.z);
        EndFieldTable();
    }
    if (ImGui::Button(scriptPreviewPlayerPlacementActive_ ? "Stop placing player" : "Place player on terrain"))
    {
        scriptPreviewPlayerPlacementActive_ = !scriptPreviewPlayerPlacementActive_;
        if (scriptPreviewPlayerPlacementActive_)
        {
            inGameViewMode_ = false;
            editMode_ = true;
            terrainSculptActive_ = false;
            lightPlacementActive_ = false;
            aiPreviewTargetPlacementActive_ = false;
            brushActive_ = false;
            waypointPlacementMode_ = WaypointPlacementMode::None;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Player at camera"))
    {
        const glm::vec3 local = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye() : camera_.center();
        const glm::vec3 origin = streamer_.origin();
        scriptPreviewPlayerWorld_ = glm::vec3(local.x + origin.x, local.y + origin.y, local.z);
        scriptPreviewPlayerEnabled_ = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Click-to-interact", &scriptInteractionMode_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("With this armed, clicking an NPC or GameObject in Edit mode dispatches matching Interaction triggers.");
    if (ImGui::Checkbox("Show trigger overlays", &showScriptTriggerOverlay_))
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();

    auto addTrigger = [&](ScriptTriggerType type) {
        if (scriptTriggersEdit_.size() >= 256)
        {
            scriptTriggerStatus_ = "This map has reached the 256-trigger Studio safety limit.";
            return;
        }
        ScriptEventTrigger trigger;
        trigger.id = nextScriptTriggerId_++;
        trigger.type = type;
        trigger.name = type == ScriptTriggerType::Area ? "Area trigger " :
                       type == ScriptTriggerType::Interaction ? "Interaction trigger " :
                       type == ScriptTriggerType::Proximity ? "Proximity trigger " : "Timer trigger ";
        trigger.name += std::to_string(scriptTriggersEdit_.size() + 1);
        const glm::vec3 local = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye() : camera_.center();
        const glm::vec3 origin = streamer_.origin();
        trigger.center = glm::vec3(local.x + origin.x, local.y + origin.y, local.z);
        if ((type == ScriptTriggerType::Interaction || type == ScriptTriggerType::Proximity) && selKind_ != SelKind::None)
        {
            if (selKind_ == SelKind::Npc) { trigger.targetKind = ScriptTriggerObjectKind::Npc; trigger.targetGuid = selGuid_; }
            if (selKind_ == SelKind::GameObject) { trigger.targetKind = ScriptTriggerObjectKind::GameObject; trigger.targetGuid = selGuid_; }
        }
        scriptTriggersEdit_.push_back(std::move(trigger));
        selectedScriptTriggerId_ = scriptTriggersEdit_.back().id;
        scriptTriggerRuntime_.clear();
        MarkScriptTriggersDirty("Added a script trigger. Configure its hook/action and save when ready.");
    };

    ImGui::SeparatorText("Trigger list");
    if (ImGui::Button("+ Area")) addTrigger(ScriptTriggerType::Area);
    ImGui::SameLine();
    if (ImGui::Button("+ Interaction")) addTrigger(ScriptTriggerType::Interaction);
    ImGui::SameLine();
    if (ImGui::Button("+ Proximity")) addTrigger(ScriptTriggerType::Proximity);
    ImGui::SameLine();
    if (ImGui::Button("+ Timer")) addTrigger(ScriptTriggerType::Timer);
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##triggersearch", "filter triggers", scriptTriggerSearch_, sizeof(scriptTriggerSearch_));
    const std::string query = Lower(scriptTriggerSearch_);
    ImGui::BeginChild("##scripttriggerlist", ImVec2(0, 150), true);
    for (ScriptEventTrigger& trigger : scriptTriggersEdit_)
    {
        const char* kind = trigger.type == ScriptTriggerType::Area ? "Area" :
                           trigger.type == ScriptTriggerType::Interaction ? "Interaction" :
                           trigger.type == ScriptTriggerType::Proximity ? "Proximity" : "Timer";
        if (!query.empty() && Lower(std::string(kind) + " " + trigger.name + " " + trigger.scriptHook).find(query) == std::string::npos)
            continue;
        const std::string id = "script-trigger-" + std::to_string(trigger.id);
        ImGui::PushID(id.c_str());
        bool enabled = trigger.enabled;
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            trigger.enabled = enabled;
            scriptTriggerRuntime_.erase(trigger.id);
            MarkScriptTriggersDirty("Changed trigger enabled state.");
        }
        ImGui::SameLine();
        const std::string label = std::string(kind) + "  " + trigger.name;
        if (ImGui::Selectable(label.c_str(), selectedScriptTriggerId_ == trigger.id))
            selectedScriptTriggerId_ = trigger.id;
        ImGui::PopID();
    }
    if (scriptTriggersEdit_.empty())
        ImGui::TextDisabled("No triggers yet. Add one above, then place/configure its volume or object hook.");
    ImGui::EndChild();

    auto selected = std::find_if(scriptTriggersEdit_.begin(), scriptTriggersEdit_.end(),
                                 [&](const ScriptEventTrigger& t) { return t.id == selectedScriptTriggerId_; });
    if (selected == scriptTriggersEdit_.end())
    {
        ImGui::End();
        return;
    }
    ScriptEventTrigger& trigger = *selected;
    ImGui::SeparatorText("Trigger inspector");
    bool changed = false;
    if (BeginFieldTable("scripttriggerfields", 160.0f))
    {
        FieldRow("Name"); changed |= InputTextString("##triggername", trigger.name);
        int type = static_cast<int>(trigger.type);
        static const char* kTypes[] = {"Area entry / exit", "Interaction", "Proximity", "Timer"};
        FieldRow("Type");
        if (ImGui::Combo("##triggertype", &type, kTypes, IM_ARRAYSIZE(kTypes)))
        {
            trigger.type = static_cast<ScriptTriggerType>(std::clamp(type, 0, 3));
            scriptTriggerRuntime_.erase(trigger.id);
            changed = true;
        }
        FieldRow("Enabled"); changed |= ImGui::Checkbox("##triggerenabled", &trigger.enabled);
        FieldRow("Script hook", "Custom server/Studio hook name dispatched by this event.");
        changed |= InputTextString("##triggerhook", trigger.scriptHook);
        FieldRow("Script event ID", "Optional event/message id supplied to your server hook.");
        changed |= InputU32("##triggerevent", trigger.scriptEventId);
        FieldRow("SmartAI list ID", "Optional timed action-list / SmartAI reference recorded in the trigger manifest.");
        changed |= InputU32("##triggeractionlist", trigger.smartActionListId);
        FieldRow("Note"); changed |= InputTextString("##triggernote", trigger.note);
        EndFieldTable();
    }

    if (trigger.type == ScriptTriggerType::Area)
    {
        ImGui::SeparatorText("Area volume");
        int shape = static_cast<int>(trigger.areaShape);
        static const char* kShapes[] = {"Circle", "Box"};
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("Shape", &shape, kShapes, IM_ARRAYSIZE(kShapes)))
        {
            trigger.areaShape = static_cast<ScriptTriggerAreaShape>(shape);
            changed = true;
        }
        if (BeginFieldTable("scriptarea", 160.0f))
        {
            FieldRow("Center X"); changed |= InputFloatField("##trigx", trigger.center.x);
            FieldRow("Center Y"); changed |= InputFloatField("##trigy", trigger.center.y);
            FieldRow("Center Z"); changed |= InputFloatField("##trigz", trigger.center.z);
            if (trigger.areaShape == ScriptTriggerAreaShape::Circle)
            {
                FieldRow("Radius (yd)"); changed |= InputFloatField("##trigradius", trigger.radius);
                FieldRow("Vertical half-height", "0 = ignore vertical distance."); changed |= InputFloatField("##trigheight", trigger.height);
            }
            else
            {
                FieldRow("Half extent X"); changed |= InputFloatField("##trigex", trigger.boxExtents.x);
                FieldRow("Half extent Y"); changed |= InputFloatField("##trigey", trigger.boxExtents.y);
                FieldRow("Half extent Z"); changed |= InputFloatField("##trigez", trigger.boxExtents.z);
            }
            EndFieldTable();
        }
        if (ImGui::Button(scriptTriggerCenterPlacementActive_ ? "Stop placing center" : "Place center on terrain"))
        {
            scriptTriggerCenterPlacementActive_ = !scriptTriggerCenterPlacementActive_;
            if (scriptTriggerCenterPlacementActive_)
            {
                inGameViewMode_ = false;
                editMode_ = true;
                terrainSculptActive_ = false;
                lightPlacementActive_ = false;
                aiPreviewTargetPlacementActive_ = false;
                scriptPreviewPlayerPlacementActive_ = false;
                brushActive_ = false;
                waypointPlacementMode_ = WaypointPlacementMode::None;
            }
        }
    }
    else if (trigger.type == ScriptTriggerType::Interaction || trigger.type == ScriptTriggerType::Proximity)
    {
        ImGui::SeparatorText(trigger.type == ScriptTriggerType::Interaction ? "Interaction object" : "Proximity object");
        int kind = static_cast<int>(trigger.targetKind);
        static const char* kObjects[] = {"Any / unresolved", "NPC", "GameObject"};
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::Combo("Object kind", &kind, kObjects, IM_ARRAYSIZE(kObjects)))
        {
            trigger.targetKind = static_cast<ScriptTriggerObjectKind>(std::clamp(kind, 0, 2));
            changed = true;
        }
        if (BeginFieldTable("scriptobject", 160.0f))
        {
            FieldRow("Target guid"); changed |= InputU32("##triggertarget", trigger.targetGuid);
            if (trigger.type == ScriptTriggerType::Proximity)
            {
                FieldRow("Activation radius (yd)"); changed |= InputFloatField("##triggernear", trigger.radius);
            }
            EndFieldTable();
        }
        if ((selKind_ == SelKind::Npc || selKind_ == SelKind::GameObject) && ImGui::Button("Use selected object"))
        {
            trigger.targetKind = selKind_ == SelKind::Npc ? ScriptTriggerObjectKind::Npc
                                                           : ScriptTriggerObjectKind::GameObject;
            trigger.targetGuid = selGuid_;
            changed = true;
        }
    }
    else // Timer
    {
        ImGui::SeparatorText("Timer event");
        if (BeginFieldTable("scripttimer", 160.0f))
        {
            FieldRow("Initial delay (sec)"); changed |= InputFloatField("##timerdelay", trigger.timerDelaySeconds);
            FieldRow("Repeat interval (sec)", "0 = fire once."); changed |= InputFloatField("##timerrepeat", trigger.timerRepeatSeconds);
            EndFieldTable();
        }
    }

    ImGui::SeparatorText("Ordered action sequence");
    auto addAction = [&](ScriptTriggerActionType type) {
        if (trigger.actions.size() >= 32)
        {
            scriptTriggerStatus_ = "This trigger already has the 32-action preview safety limit.";
            return;
        }
        ScriptTriggerAction action;
        action.id = nextScriptTriggerActionId_++;
        action.type = type;
        if (type == ScriptTriggerActionType::ScriptHook)
            action.hook = trigger.scriptHook;
        else if (type == ScriptTriggerActionType::SmartActionList)
            action.value = trigger.smartActionListId;
        trigger.actions.push_back(std::move(action));
        changed = true;
    };
    if (ImGui::SmallButton("+ Hook")) addAction(ScriptTriggerActionType::ScriptHook);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ SmartAI")) addAction(ScriptTriggerActionType::SmartActionList);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Spell")) addAction(ScriptTriggerActionType::CastSpell);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Talk")) addAction(ScriptTriggerActionType::TalkText);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Toggle GO")) addAction(ScriptTriggerActionType::ToggleGameObject);

    int moveUp = -1, moveDown = -1, eraseAction = -1;
    ImGui::BeginChild("##triggeractions", ImVec2(0, 175), true);
    static const char* kActionTypes[] = {"Script hook", "SmartAI action list", "Cast spell", "Talk text", "Toggle GameObject"};
    for (int i = 0; i < static_cast<int>(trigger.actions.size()); ++i)
    {
        ScriptTriggerAction& action = trigger.actions[i];
        ImGui::PushID(static_cast<int>(action.id & 0x7fffffff));
        bool enabled = action.enabled;
        if (ImGui::Checkbox("##actionenabled", &enabled)) { action.enabled = enabled; changed = true; }
        ImGui::SameLine();
        ImGui::Text("%02d", i + 1);
        ImGui::SameLine();
        int actionType = static_cast<int>(action.type);
        ImGui::SetNextItemWidth(145.0f);
        if (ImGui::Combo("##actiontype", &actionType, kActionTypes, IM_ARRAYSIZE(kActionTypes)))
        {
            action.type = static_cast<ScriptTriggerActionType>(std::clamp(actionType, 0, 4));
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        if (ImGui::InputFloat("##actiondelay", &action.delaySeconds, 0.1f, 1.0f, "%.2fs")) changed = true;
        if (action.type == ScriptTriggerActionType::ScriptHook)
        {
            ImGui::SameLine(); ImGui::SetNextItemWidth(150.0f);
            if (InputTextString("##actionhook", action.hook)) changed = true;
        }
        else if (action.type == ScriptTriggerActionType::TalkText)
        {
            ImGui::SameLine(); ImGui::SetNextItemWidth(180.0f);
            if (InputTextString("##actiontext", action.text)) changed = true;
        }
        else
        {
            ImGui::SameLine(); ImGui::SetNextItemWidth(95.0f);
            if (InputU32("##actionvalue", action.value)) changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("^")) moveUp = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("v")) moveDown = i;
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) eraseAction = i;
        ImGui::PopID();
    }
    if (trigger.actions.empty())
        ImGui::TextDisabled("No ordered actions: legacy Script hook / Event ID / SmartAI list fields above dispatch directly.");
    ImGui::EndChild();
    if (moveUp > 0)
    {
        std::swap(trigger.actions[moveUp], trigger.actions[moveUp - 1]);
        changed = true;
    }
    else if (moveDown >= 0 && moveDown + 1 < static_cast<int>(trigger.actions.size()))
    {
        std::swap(trigger.actions[moveDown], trigger.actions[moveDown + 1]);
        changed = true;
    }
    else if (eraseAction >= 0)
    {
        trigger.actions.erase(trigger.actions.begin() + eraseAction);
        changed = true;
    }
    for (ScriptTriggerAction& action : trigger.actions)
        action.delaySeconds = std::clamp(action.delaySeconds, 0.0f, 86400.0f);

    trigger.radius = std::clamp(trigger.radius, 0.1f, 10000.0f);
    trigger.boxExtents = glm::clamp(trigger.boxExtents, glm::vec3(0.1f), glm::vec3(10000.0f));
    trigger.height = std::clamp(trigger.height, 0.0f, 10000.0f);
    trigger.timerDelaySeconds = std::clamp(trigger.timerDelaySeconds, 0.0f, 86400.0f);
    trigger.timerRepeatSeconds = std::clamp(trigger.timerRepeatSeconds, 0.0f, 86400.0f);
    if (changed)
    {
        scriptTriggerRuntime_.erase(trigger.id);
        pendingScriptTriggerActions_.erase(
            std::remove_if(pendingScriptTriggerActions_.begin(), pendingScriptTriggerActions_.end(),
                           [&](const PendingScriptTriggerAction& pending) { return pending.triggerId == trigger.id; }),
            pendingScriptTriggerActions_.end());
        MarkScriptTriggersDirty("Updated script trigger preview.");
    }

    if (ImGui::Button("Test fire now"))
        FireScriptTrigger(trigger.id, "manual test");
    ImGui::SameLine();
    if (ImGui::Button("Copy trigger manifest"))
    {
        nlohmann::json manifest = {
            {"map", scriptTriggerMapDir_}, {"id", trigger.id}, {"name", trigger.name},
            {"type", static_cast<int>(trigger.type)}, {"scriptHook", trigger.scriptHook},
            {"scriptEventId", trigger.scriptEventId}, {"smartActionListId", trigger.smartActionListId},
            {"actions", nlohmann::json::array()}
        };
        for (const ScriptTriggerAction& action : trigger.actions)
            manifest["actions"].push_back({
                {"id", action.id}, {"enabled", action.enabled}, {"type", static_cast<int>(action.type)},
                {"delay", action.delaySeconds}, {"hook", action.hook}, {"value", action.value}, {"text", action.text}
            });
        const std::string text = manifest.dump(2);
        ImGui::SetClipboardText(text.c_str());
        scriptTriggerStatus_ = "Copied selected trigger manifest JSON to the clipboard.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate trigger"))
    {
        ScriptEventTrigger copy = trigger;
        copy.id = nextScriptTriggerId_++;
        for (ScriptTriggerAction& action : copy.actions)
            action.id = nextScriptTriggerActionId_++;
        copy.name += " Copy";
        copy.center += glm::vec3(1.0f, 1.0f, 0.0f);
        scriptTriggersEdit_.push_back(std::move(copy));
        selectedScriptTriggerId_ = scriptTriggersEdit_.back().id;
        MarkScriptTriggersDirty("Duplicated script trigger.");
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete trigger"))
    {
        const uint64_t id = trigger.id;
        scriptTriggersEdit_.erase(std::remove_if(scriptTriggersEdit_.begin(), scriptTriggersEdit_.end(),
                                                  [id](const ScriptEventTrigger& t) { return t.id == id; }),
                                   scriptTriggersEdit_.end());
        scriptTriggerRuntime_.erase(id);
        pendingScriptTriggerActions_.erase(
            std::remove_if(pendingScriptTriggerActions_.begin(), pendingScriptTriggerActions_.end(),
                           [id](const PendingScriptTriggerAction& pending) { return pending.triggerId == id; }),
            pendingScriptTriggerActions_.end());
        selectedScriptTriggerId_ = scriptTriggersEdit_.empty() ? 0 : scriptTriggersEdit_.front().id;
        MarkScriptTriggersDirty("Deleted script trigger.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Preview event log");
    ImGui::BeginChild("##triggerlog", ImVec2(0, 120), true);
    for (auto it = scriptTriggerLog_.rbegin(); it != scriptTriggerLog_.rend(); ++it)
        ImGui::TextDisabled("[%06.2fs] %s", it->timeSeconds, it->text.c_str());
    if (scriptTriggerLog_.empty())
        ImGui::TextDisabled("Move the script player through an area/proximity volume, click an object with Click-to-interact armed, wait for a timer, or use Test fire now.");
    ImGui::EndChild();
    ImGui::TextDisabled("Studio preview dispatches the configured hook/event/action-list metadata and records it above. Bind the same metadata in your core/custom script to execute server gameplay logic.");
    ImGui::End();
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
    if (inGameViewMode_)
        editMode_ = false;
    ImGui::SameLine();
    { bool w = streamer_.showWdl(); if (ImGui::Checkbox("Distant", &w)) streamer_.setShowWdl(w); }
    ImGui::SameLine();
    if (ImGui::Checkbox("Doodads", &opt_.doodads)) OpenMapDir(selectedMapDir_, false);
    ImGui::SameLine();
    if (ImGui::Checkbox("WMOs", &opt_.wmos)) OpenMapDir(selectedMapDir_, false);
    ImGui::SameLine();
    if (ImGui::Checkbox("Liquid", &opt_.liquid)) OpenMapDir(selectedMapDir_, false);
    ImGui::SameLine();
    if (ImGui::Checkbox("Light preview", &lightingEdit_.previewEnabled))
        MarkLightingDirty("Toggled the World Editor lighting preview.");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Toggle authored sun, fog and point/spot lights. Configure them in Light Editor.");
    ImGui::SameLine();
    ImGui::TextColored(worldSimulationPaused_ ? ImVec4(0.95f, 0.65f, 0.25f, 1.0f)
                                               : ImVec4(0.38f, 0.90f, 0.55f, 1.0f),
                       worldSimulationPaused_ ? "PAUSED" : "LIVE");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Realtime Preview controls NPCs, transports, animated models, particles, water and the day/night clock.");
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
        ImGui::SameLine();
        ImGui::Checkbox("NPC markers", &showNpcMarkers_);
        ImGui::SameLine();
        ImGui::Checkbox("NPC labels", &showNpcMarkerLabels_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        ImGui::SliderInt("Marker cap", &npcMarkerMax_, 50, 1000);
    }

    if (!npcLayer_.modelDataReady() && npcLayer_.spawnCount() > 0)
    {
        ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.22f, 1.0f),
                           "NPC M2 data unavailable (%d display rows, %d model paths). Use a full WoW 3.3.5 client Data folder; AzerothCore server Data alone does not contain creature M2 assets.",
                           npcLayer_.displayInfoCount(), npcLayer_.modelPathCount());
    }
    else if (npcLayer_.failedDisplayCount() > 0 || npcLayer_.directModelFallbackCount() > 0)
    {
        ImGui::TextDisabled("NPC model diagnostics: %d uploaded, %d failed display(s), %d direct-model fallback(s). Markers remain selectable when a custom M2 cannot load.",
                            npcLayer_.modelCount(), npcLayer_.failedDisplayCount(),
                            npcLayer_.directModelFallbackCount());
    }

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
    {
        if (brushPlacementMode_ == 0)
            ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.42f, 1.0f),
                               "Placement brush active: %s entry %u — right-click terrain; Esc stops.",
                               brushKind_ == 0 ? "NPC" : "GameObject", brushEntry_);
        else
            ImGui::TextColored(ImVec4(0.35f, 0.82f, 0.42f, 1.0f),
                               "Grid placement active: %dx%d %s entry %u — right-click terrain; Esc stops.",
                               brushGridRows_, brushGridColumns_, brushKind_ == 0 ? "NPC" : "GameObject", brushEntry_);
    }
    if (aiPreviewTargetPlacementActive_)
        ImGui::TextColored(ImVec4(0.94f, 0.36f, 0.84f, 1.0f),
                           "AI target placement active — right-click terrain; Esc stops.");
    if (scriptPreviewPlayerPlacementActive_ || scriptTriggerCenterPlacementActive_)
        ImGui::TextColored(ImVec4(0.32f, 0.86f, 0.82f, 1.0f),
                           scriptPreviewPlayerPlacementActive_ ? "Script player placement active — right-click terrain; Esc stops."
                                                                : "Script trigger-center placement active — right-click terrain; Esc stops.");
    if (lightPlacementActive_)
        ImGui::TextColored(ImVec4(1.0f, 0.76f, 0.28f, 1.0f),
                           "%s light placement active — right-click terrain; Esc stops.",
                           WorldLightTypeName(lightPlacementType_));
    if (terrainSculptActive_)
    {
        static const char* kSculptNames[] = {"Raise", "Lower", "Flatten", "Ramp / Stairs", "Noise / Terrainify", "Terrain Stamp", "Smooth"};
        if (terrainSculptMode_ == 3)
            ImGui::TextColored(ImVec4(0.94f, 0.52f, 0.18f, 1.0f),
                               "Terrain ramp active: %s — right-click %s; Esc stops.",
                               terrainRampHasStart_ ? "right-click end point" : "right-click start point",
                               terrainRampHasStart_ ? "the ramp end" : "the ramp start");
        else
            ImGui::TextColored(ImVec4(0.94f, 0.52f, 0.18f, 1.0f),
                               "Terrain sculpt active: %s, %.1f yd radius — right-click terrain; Esc stops.",
                               kSculptNames[std::clamp(terrainSculptMode_, 0, 6)], terrainBrushRadius_);
    }

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
    UpdateRealtimePreview(io.DeltaTime);
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
    if (scriptPreviewPlayerFollowCamera_)
    {
        const glm::vec3 origin = streamer_.origin();
        scriptPreviewPlayerWorld_ = glm::vec3(focus.x + origin.x, focus.y + origin.y, focus.z);
        scriptPreviewPlayerEnabled_ = true;
    }

    // Assemble the live spawn-visibility filter once; picking, rendering, and the World Outliner
    // share this exact interpretation of phases/difficulty/events/pools/groups.
    const SpawnFilter filter = CurrentSpawnFilter();
    if (!inGameViewMode_)
    {
        BuildNpcMarkerCache(view, proj, p0, w, h, focus, filter);
        BuildLightMarkerCache(view, proj, p0, w, h, focus);
    }
    else
    {
        npcMarkers_.clear();
        lightMarkers_.clear();
    }

    // Selection: hover-highlight + click-to-select, then seed the gizmo from the live object.
    // Highlights must be set BEFORE BuildFrame / the layers' Build consume them below. Picking is
    // suppressed while the gizmo is busy so grabbing a handle doesn't reselect — using LAST frame's
    // gizmo state, since this frame's RunGizmo runs after the scene render below.
    const bool gizmoBusyPrev = editMode_ &&
                               (gizmoMouseCaptured_ || gizmoUsingPrev_ || gizmoHoveredPrev_);
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

    // Right-click on terrain (no drag) opens the add popup, stamps an armed spawn brush, or edits
    // a path/terrain brush. Escape always disarms the most-specific active terrain tool first.
    if (editMode_)
        HandleRightClickAdd(view, proj, p0, w, h, hovered, focus, filter);
    if (editMode_ && hovered && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        if (terrainSculptActive_)
        {
            terrainSculptActive_ = false;
            terrainRampHasStart_ = false;
            terrainStatus_ = "Terrain sculpt brush cancelled.";
        }
        else if (waypointPlacementMode_ != WaypointPlacementMode::None)
        {
            waypointPlacementMode_ = WaypointPlacementMode::None;
            waypointStatus_ = "Terrain waypoint tool cancelled.";
        }
        else if (scriptPreviewPlayerPlacementActive_ || scriptTriggerCenterPlacementActive_)
        {
            scriptPreviewPlayerPlacementActive_ = false;
            scriptTriggerCenterPlacementActive_ = false;
            scriptTriggerStatus_ = "Script trigger terrain placement cancelled.";
        }
        else if (aiPreviewTargetPlacementActive_)
        {
            aiPreviewTargetPlacementActive_ = false;
            aiBehaviorStatus_ = "AI target placement cancelled.";
        }
        else if (lightPlacementActive_)
        {
            lightPlacementActive_ = false;
            lightingStatus_ = "Light placement tool cancelled.";
        }
        else if (brushActive_)
        {
            brushActive_ = false;
            brushStatus_ = "Placement brush cancelled.";
        }
    }
    // Delete key removes the selected light or selected world object. Studio lights are a separate
    // editor layer (not DB/ADT placements), so they take priority when their marker is selected.
    if (editMode_ && hovered && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (selectedWorldLightId_ != 0)
            DeleteSelectedWorldLight();
        else if (selKind_ != SelKind::None)
            DeleteSelection();
    }

    streamer_.Update(focus, streamRadius_, opt_);

    if (showGrid_ && !inGameViewMode_)
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
    // One continuously advancing simulation clock drives every world system. Camera/UI input stays
    // responsive while paused; only actor/transport/doodad/effect time is frozen.
    const float worldDtMs = worldSimulationPaused_ ? 0.0f :
                            std::clamp(io.DeltaTime * 1000.0f * worldSimulationSpeed_, 0.0f, 250.0f);
    streamer_.BuildFrame(view, proj, eye, worldDtMs, frameTerrains_, frameGroups_, frameScene_);

    // NPC layer: simulate + animate the near creature spawns and append them to the same
    // scene list (world = focus + streamer origin). Uses the connected DB for lazy waypoint
    // loading; harmless when disconnected (existing NPCs keep simulating locally). The
    // spawn-visibility filter was assembled above (before picking).
    if (showNpcs_)
    {
        IDatabase* db = (svc_->connected) ? svc_->activeDb : nullptr;
        npcLayer_.Build(focus, streamer_.origin(), view, worldDtMs, db, frameScene_,
                        npcMaxDraw_, npcCullDist_, filter);
    }
    if (showGos_)
    {
        goLayer_.Build(focus, streamer_.origin(), view, worldDtMs, currentMapId_,
                       filter, frameScene_, goMaxDraw_, goCullDist_);
    }
    EvaluateScriptTriggers(worldDtMs);
    cpuBuildMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - tBuild).count();

    // Pack the map-scoped Light Editor profile into renderer-local coordinates just before the
    // world pass. Point/spot selection is camera-nearest, matching the GPU's fixed light budget.
    ApplyWorldLightingToRenderer(streamer_.origin(), focus + streamer_.origin());
    ImTextureID tex = svc_->renderer->RenderWorld(frameTerrains_.data(), (int)frameTerrains_.size(),
                                                  frameGroups_.data(), (int)frameGroups_.size(),
                                                  frameScene_.data(), (int)frameScene_.size(),
                                                  &view[0][0], &proj[0][0], w, h);
    if (tex)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + w, p0.y + h));

    // The route overlay is editor UI, not an engine primitive: it remains crisp at every zoom level,
    // labels the ordered points, and is drawn above the final 3D image but below the transform gizmo.
    // It stays visible in read-only view mode; point picking/terrain tools still require Edit.
    if (!inGameViewMode_)
    {
        DrawWaypointOverlay(view, proj, p0, w, h);
        DrawFormationOverlay(view, proj, p0, w, h);
        DrawAiBehaviorOverlay(view, proj, p0, w, h);
        DrawScriptTriggerOverlay(view, proj, p0, w, h);
        DrawLightOverlay(view, proj, p0, w, h);
        DrawNpcMarkerOverlay();
        DrawTerrainBrushOverlay(view, proj, p0, w, h, hovered);
    }

    // Spell VFX is a world-preview element rather than an editor helper, so it deliberately
    // remains visible in the clean in-game view while selection/route/light overlays are hidden.
    DrawSpellEffectOverlay(view, proj, p0, w, h, eye + streamer_.origin());

    // Transform gizmo: drawn ON TOP of the blitted image, on THIS window's draw list, with the SAME
    // view/proj the scene was rendered with (so it stays locked to the model). Runs before the camera
    // update below so its IsOver()/IsUsing() can veto the camera on the CURRENT frame.
    if (editMode_)
        RunGizmo(view, proj, p0, w, h);
    else
    {
        gizmoUsingPrev_ = false;
        gizmoHoveredPrev_ = false;
        gizmoMouseCaptured_ = false;
    }

    // Integrate the camera LAST (this frame's input -> next frame's view). The explicit mouse
    // capture persists from a handle press through the release frame, so NPC/GameObject drag arrows
    // never leak a mouse delta into orbit/fly camera movement.
    const bool gizmoBusy = editMode_ && selKind_ != SelKind::None &&
                           (gizmoMouseCaptured_ || gizmoUsingPrev_ || gizmoHoveredPrev_ ||
                            ImGuizmo::IsUsing() || ImGuizmo::IsOver());
    camera_.Update(hovered && !gizmoBusy, active && !gizmoBusy, io.DeltaTime);

    if (editMode_)
    {
        DrawAddObjectPopup();
        DrawObjectContextPopup();
    }

    if (showStats_ && !inGameViewMode_)
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
    gizmoHoveredPrev_ = false;
    gizmoMouseCaptured_ = false;
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
    bool hasSpawnDisplayOverrideColumn = true;
    if (const MapSpawn* visualSpawn = npcLayer_.FindSpawn(selGuid_))
    {
        hasSpawnDisplayOverrideColumn = visualSpawn->hasSpawnDisplayOverrideColumn;
        const ResolvedCreatureDisplay visual = visualSpawn->ResolveDisplay(CurrentSpawnFilter());
        ImGui::SeparatorText("World appearance");
        if (visual.displayId != 0)
        {
            ImGui::Text("Rendered display ID: %u", visual.displayId);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", CreatureDisplaySourceName(visual.source));
            if (visual.eventEntry != 0)
                ImGui::TextDisabled("Event #%d is replacing this NPC's normal server display in the active preview.",
                                    visual.eventEntry);
            if (visual.source == CreatureDisplaySource::TemplateModel)
                ImGui::TextDisabled("AzerothCore model idx %u (%u template rows); server display scale %.3g.",
                                    static_cast<unsigned>(visualSpawn->templateDisplayIndex),
                                    static_cast<unsigned>(visualSpawn->templateDisplayCount), visual.serverScale);
            else
                ImGui::TextDisabled("Server display scale %.3g; template scale %.3g.",
                                    visual.serverScale, visualSpawn->scale);
        }
        else
        {
            ImGui::TextColored(ImVec4(1.0f, 0.62f, 0.22f, 1.0f),
                               "No server CreatureDisplayInfo id resolved for this spawn.");
            ImGui::TextDisabled("Set a spawn model id or add a template model row, then reload the map.");
        }
    }
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
            FieldRow("Server display ID (0 = template)",
                     "Stored as creature.modelid/displayid on schemas that support a per-spawn visual override. This is a CreatureDisplayInfo.dbc id, not a CreatureModelData id.");
            ImGui::BeginDisabled(!hasSpawnDisplayOverrideColumn);
            ch |= InputU32("##mid", s.modelId);
            ImGui::EndDisabled();
            if (!hasSpawnDisplayOverrideColumn)
                ImGui::TextDisabled("This core stores NPC displays in creature_template_model; edit that template row instead.");
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
    // Modelid/displayid is a persistent server-side spawn override. Keep the template fallback
    // captured by MapSpawn so reverting the override to 0 immediately restores the correct
    // creature_template_model (AzerothCore) or modelid1..4 (TrinityCore) visual without a map reload.
    const uint32_t previousSpawnDisplay = f.spawnDisplayId;
    f.spawnDisplayId = s.modelId;
    if (s.modelId != 0)
    {
        // A loaded override already carries its exact creature_template_model DisplayScale. A newly
        // typed id is conservatively shown at 1 until the next map reload verifies its template row;
        // choosing the current fallback's scale would be visibly wrong for a different display.
        if (s.modelId != previousSpawnDisplay)
            f.spawnDisplayScale = s.modelId == f.templateDisplayId ? f.templateDisplayScale : 1.0f;
        f.displayId = s.modelId;
        f.displayScale = f.spawnDisplayScale > 0.0f ? f.spawnDisplayScale : 1.0f;
        f.displaySource = CreatureDisplaySource::SpawnOverride;
    }
    else
    {
        f.spawnDisplayScale = 1.0f;
        f.displayId = f.templateDisplayId;
        f.displayScale = f.templateDisplayScale > 0.0f ? f.templateDisplayScale : 1.0f;
        f.displaySource = f.templateDisplaySource;
    }
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

void AdtViewerModule::BuildNpcMarkerCache(const glm::mat4& view, const glm::mat4& proj,
                                                const ImVec2& p0, int w, int h,
                                                const glm::vec3& focus, const SpawnFilter& filter)
{
    npcMarkers_.clear();
    if (!showNpcMarkers_ || !showNpcs_)
        return;
    std::vector<MapSpawn> spawns;
    npcLayer_.SnapshotSpawns(spawns);
    const glm::vec3 origin = streamer_.origin();
    const glm::vec3 worldFocus = focus + origin;
    const float maxRange = std::max(npcCullDist_, 100.0f);
    const float maxRange2 = maxRange * maxRange;
    for (const MapSpawn& spawn : spawns)
    {
        if (!filter.Visible(spawn.phaseMask, spawn.spawnMask, spawn.eventEntry,
                            spawn.poolHidden, spawn.groupManual))
            continue;
        const glm::vec3 d(spawn.x - worldFocus.x, spawn.y - worldFocus.y, spawn.z - worldFocus.z);
        const float dist2 = glm::dot(d, d);
        if (dist2 > maxRange2)
            continue;
        NpcMarker marker;
        marker.guid = spawn.guid;
        marker.entry = spawn.entry;
        marker.dist2 = dist2;
        if (!ProjectWorldPoint(glm::vec3(spawn.x, spawn.y, spawn.z), origin, view, proj, p0, w, h,
                               marker.screen))
            continue;
        npcMarkers_.push_back(marker);
    }
    std::sort(npcMarkers_.begin(), npcMarkers_.end(), [](const NpcMarker& a, const NpcMarker& b) {
        return a.dist2 < b.dist2;
    });
    if (static_cast<int>(npcMarkers_.size()) > npcMarkerMax_)
        npcMarkers_.resize(std::max(npcMarkerMax_, 0));
}

bool AdtViewerModule::TrySelectNpcMarkerOverlay(bool viewportHovered)
{
    if (!viewportHovered || npcMarkers_.empty() || !ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        return false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const NpcMarker* best = nullptr;
    float bestD2 = 11.0f * 11.0f;
    for (const NpcMarker& marker : npcMarkers_)
    {
        const float dx = marker.screen.x - mouse.x;
        const float dy = marker.screen.y - mouse.y;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = &marker;
        }
    }
    if (!best)
        return false;
    SelectObject(SelKind::Npc, 0, 0, best->guid);
    return true;
}

void AdtViewerModule::DrawNpcMarkerOverlay()
{
    if (npcMarkers_.empty())
        return;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (const NpcMarker& marker : npcMarkers_)
    {
        const bool selected = selKind_ == SelKind::Npc && selGuid_ == marker.guid;
        const ImU32 fill = selected ? IM_COL32(255, 198, 64, 245) : IM_COL32(86, 230, 122, 220);
        const ImU32 outline = selected ? IM_COL32(255, 245, 210, 255) : IM_COL32(220, 255, 230, 235);
        const float r = selected ? 6.5f : 5.0f;
        draw->AddCircleFilled(marker.screen, r, fill, 12);
        draw->AddCircle(marker.screen, r, outline, 12, 1.5f);
        // A small "head" dot differentiates an NPC proxy from waypoint/formation markers.
        draw->AddCircleFilled(ImVec2(marker.screen.x, marker.screen.y - r * 0.35f), r * 0.28f,
                              IM_COL32(25, 55, 32, 255), 8);
        if (showNpcMarkerLabels_)
        {
            const std::string label = svc_ && svc_->lookups
                ? svc_->lookups->LabelCreature(marker.entry)
                : ("entry " + std::to_string(marker.entry));
            draw->AddText(ImVec2(marker.screen.x + r + 3.0f, marker.screen.y - r),
                          IM_COL32(230, 255, 236, 245), label.c_str());
        }
    }
}

void AdtViewerModule::DrawTerrainSculptPanel()
{
    if (!ImGui::Begin("Terrain Sculpt"))
    {
        ImGui::End();
        return;
    }
    const bool mapReady = streamerInit_ && !loadedName_.empty() && !streamer_.wmoOnly();
    const bool canSave = svc_ && svc_->clientData && !svc_->editRoot.empty();
    if (!mapReady)
    {
        ImGui::TextWrapped(streamer_.wmoOnly()
            ? "Terrain sculpting is unavailable on global-WMO maps."
            : "Open a terrain map before sculpting its ADT heightmap.");
        ImGui::End();
        return;
    }
    if (!canSave)
        ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.22f, 1.0f),
                           "Open a project with an edited-client folder before sculpting terrain.");

    ImGui::SeparatorText("Height brush / grade");
    ImGui::BeginDisabled(!canSave);
    const int previousTerrainMode = terrainSculptMode_;
    ImGui::RadioButton("Raise", &terrainSculptMode_, 0); ImGui::SameLine();
    ImGui::RadioButton("Lower", &terrainSculptMode_, 1); ImGui::SameLine();
    ImGui::RadioButton("Flatten", &terrainSculptMode_, 2); ImGui::SameLine();
    ImGui::RadioButton("Ramp / Stairs", &terrainSculptMode_, 3); ImGui::SameLine();
    ImGui::RadioButton("Noise / Terrainify", &terrainSculptMode_, 4); ImGui::SameLine();
    ImGui::RadioButton("Terrain Stamp", &terrainSculptMode_, 5); ImGui::SameLine();
    ImGui::RadioButton("Smooth", &terrainSculptMode_, 6);
    if (previousTerrainMode != terrainSculptMode_)
        terrainRampHasStart_ = false;

    if (terrainSculptMode_ == 3)
    {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Ramp width", &terrainRampWidth_, 2.0f, 100.0f, "%.1f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Max slope", &terrainRampMaxSlopeDegrees_, 1.0f, 60.0f, "%.1f deg");
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderInt("Stair steps (0 = smooth)", &terrainRampStepCount_, 0, 32);
        if (terrainRampHasStart_)
        {
            ImGui::Text("Ramp start: X %.2f  Y %.2f  Z %.2f", terrainRampStart_.x, terrainRampStart_.y, terrainRampStart_.z);
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear ramp start"))
                terrainRampHasStart_ = false;
        }
        else
            ImGui::TextDisabled("Arm the tool, then right-click the ramp start and end on terrain.");
    }
    else if (terrainSculptMode_ == 4)
    {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Noise radius", &terrainBrushRadius_, 2.0f, 100.0f, "%.1f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Noise amplitude", &terrainNoiseAmplitude_, 0.05f, 30.0f, "%.2f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Noise frequency", &terrainNoiseFrequency_, 0.01f, 1.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderInt("Noise octaves", &terrainNoiseOctaves_, 1, 6);
        InputU32("Noise seed", terrainNoiseSeed_);
        ImGui::SameLine();
        if (ImGui::SmallButton("New seed"))
            terrainNoiseSeed_ = terrainNoiseSeed_ * 1664525u + 1013904223u;
        ImGui::TextDisabled("Generates deterministic fractal value noise as staged Raise/Lower strokes. The exact stroke sequence is undoable and saved to ADT.");
    }
    else if (terrainSculptMode_ == 5)
    {
        static const char* kStamps[] = {"Hill", "Valley", "Crater", "Ridge"};
        ImGui::SetNextItemWidth(180.0f);
        ImGui::Combo("Stamp preset", &terrainStampPreset_, kStamps, IM_ARRAYSIZE(kStamps));
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Stamp radius", &terrainBrushRadius_, 2.0f, 100.0f, "%.1f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Stamp strength", &terrainBrushStrength_, 0.05f, 30.0f, "%.2f yd", ImGuiSliderFlags_Logarithmic);
        if (terrainStampPreset_ == 3)
        {
            ImGui::SetNextItemWidth(180.0f);
            ImGui::DragFloat("Ridge yaw", &terrainStampYawDegrees_, 1.0f, -180.0f, 180.0f, "%.1f deg");
        }
        ImGui::TextDisabled("One terrain click emits a reusable hill, valley, crater, or ridge stamp as normal staged ADT strokes.");
    }
    else if (terrainSculptMode_ == 6)
    {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Smooth radius", &terrainBrushRadius_, 2.0f, 100.0f, "%.1f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::SetNextItemWidth(180.0f);
        ImGui::SliderInt("Smooth iterations", &terrainSmoothIterations_, 1, 8);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Smooth blend", &terrainSmoothBlend_, 0.05f, 1.0f, "%.2f");
        ImGui::Checkbox("Preserve sharp edges", &terrainSmoothPreserveEdges_);
        ImGui::SameLine();
        ImGui::BeginDisabled(!terrainSmoothPreserveEdges_);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("Edge threshold", &terrainSmoothEdgeThreshold_, 0.05f, 20.0f, "%.2f yd", ImGuiSliderFlags_Logarithmic);
        ImGui::EndDisabled();
        ImGui::TextDisabled("Builds a local Laplacian-style field, then expands it to staged Flatten strokes so the live preview and saved ADT replay agree.");
    }
    else
    {
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat("Radius", &terrainBrushRadius_, 1.0f, 100.0f, "%.1f yd", ImGuiSliderFlags_Logarithmic);
        if (terrainSculptMode_ == static_cast<int>(adt::TerrainBrushMode::Flatten))
        {
            ImGui::Checkbox("Sample target height from click", &terrainSampleFlattenZ_);
            ImGui::BeginDisabled(terrainSampleFlattenZ_);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputFloat("Target Z", &terrainFlattenZ_, 0.0f, 0.0f, "%.3f");
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("Strength per stroke", &terrainBrushStrength_, 0.05f, 30.0f,
                               "%.2f yd", ImGuiSliderFlags_Logarithmic);
        }
    }

    if (ImGui::Checkbox("Live terrain preview", &liveTerrainPreview_))
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Render pending height strokes immediately in the viewport. Save ADT edits still performs the authoritative MCVT/MCNR write and tile reload.");
    if (ImGui::Button(terrainSculptActive_ ? "Stop terrain brush" : "Arm terrain brush"))
    {
        terrainSculptActive_ = !terrainSculptActive_;
        if (terrainSculptActive_)
        {
            inGameViewMode_ = false;
            editMode_ = true;
        }
        terrainRampHasStart_ = false;
        terrainStatus_ = terrainSculptActive_
            ? (terrainSculptMode_ == 3
                ? "Ramp tool armed — right-click a terrain start, then a terrain end."
                : terrainSculptMode_ == 4
                    ? "Terrainify noise armed — right-click terrain to queue a deterministic noise stamp."
                    : terrainSculptMode_ == 5
                        ? "Terrain stamp armed — right-click terrain to apply the selected preset."
                        : terrainSculptMode_ == 6
                            ? "Terrain smooth armed — right-click terrain to queue a Laplacian-style blend."
                            : "Terrain brush armed — right-click terrain to queue a smooth height stroke.")
            : "Terrain brush stopped.";
    }
    ImGui::EndDisabled();

    const int pending = adtEdits_.terrainPendingCount();
    ImGui::TextDisabled("%d pending terrain tile-stroke(s)%s", pending,
                        liveTerrainPreview_ ? " — previewed live" : "");
    if (liveTerrainPreview_ && pending > kMaxTerrainPreviewStrokes)
        ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.24f, 1.0f),
                           "Viewport previews the newest %d strokes; Save/reload applies all %d exactly.",
                           kMaxTerrainPreviewStrokes, pending);
    if (pending > 0)
    {
        ImGui::SameLine();
        if (ImGui::Button("Discard pending terrain strokes"))
        {
            adtEdits_.ClearTerrainStrokes();
            ++terrainHistoryGeneration_;  // stale undo/redo closures must not resurrect discarded work
            terrainStatus_ = "Pending terrain strokes discarded.";
        }
    }
    if (terrainSculptActive_)
        ImGui::TextColored(ImVec4(0.94f, 0.52f, 0.18f, 1.0f),
                           terrainSculptMode_ == 3
                               ? (terrainRampHasStart_ ? "Right-click the ramp end on terrain. Escape cancels the tool."
                                                        : "Right-click the ramp start on terrain. Escape cancels the tool.")
                               : "Right-click terrain in the World Editor to sculpt. Escape cancels the brush.");
    if (!terrainStatus_.empty())
        ImGui::TextDisabled("%s", terrainStatus_.c_str());

    ImGui::Separator();
    ImGui::TextWrapped("Strokes appear immediately through the real-time terrain preview, then patch the selected ADT tile's MCVT height values and rebuild MCNR normals when saved. They remain staged in the project's edited-client overlay, just like doodad/WMO placement edits. Use \"Save ADT edits\" in the World Editor toolbar to write and reload terrain. Pending strokes support Ctrl+Z/Ctrl+Y; after a save, terrain history is intentionally frozen so a stroke cannot be applied twice.");
    ImGui::End();
}

void AdtViewerModule::DrawRealtimePreviewPanel()
{
    if (!ImGui::Begin("Realtime Preview"))
    {
        ImGui::End();
        return;
    }
    const bool mapReady = streamerInit_ && !loadedName_.empty() && !lightingMapDir_.empty();
    if (!mapReady)
    {
        ImGui::TextWrapped("Open a map to drive the continuous world simulation, day/night lighting, and clean in-game view.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("World simulation");
    if (ImGui::Button(worldSimulationPaused_ ? "Play world" : "Pause world"))
    {
        worldSimulationPaused_ = !worldSimulationPaused_;
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", worldSimulationPaused_ ? "paused" : "running");
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::SliderFloat("Simulation speed", &worldSimulationSpeed_, 0.05f, 4.0f, "%.2fx",
                           ImGuiSliderFlags_Logarithmic))
    {
        worldSimulationSpeed_ = std::clamp(worldSimulationSpeed_, 0.05f, 8.0f);
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    ImGui::TextDisabled("NPC routes/wander, transport paths, M2 animation, particles and liquid frames all advance from this clock.");

    ImGui::SeparatorText("Day / night clock");
    WorldLightingProfile& profile = lightingEdit_;
    bool lightingChanged = false;
    lightingChanged |= ImGui::Checkbox("Animate day/night", &profile.dayNightCycle);
    ImGui::SameLine();
    ImGui::BeginDisabled(!profile.dayNightCycle);
    lightingChanged |= ImGui::Checkbox(profile.dayNightPlaying ? "Clock playing" : "Clock paused",
                                       &profile.dayNightPlaying);
    ImGui::EndDisabled();

    auto timeLabel = [](float minutes) {
        int total = std::clamp(static_cast<int>(std::round(minutes)), 0, 1440);
        if (total == 1440) total = 0;
        const int hours = total / 60;
        const int mins = total % 60;
        char label[16];
        std::snprintf(label, sizeof(label), "%02d:%02d", hours, mins);
        return std::string(label);
    };
    const std::string now = timeLabel(runtimeTimeOfDay_);
    ImGui::Text("Preview time: %s", now.c_str());
    float clock = runtimeTimeOfDay_;
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::SliderFloat("Clock", &clock, 0.0f, 1440.0f, "%0.f min"))
    {
        runtimeTimeOfDay_ = std::clamp(clock, 0.0f, 1440.0f);
        profile.timeOfDayMinutes = runtimeTimeOfDay_;
        profile.dayNightPlaying = false; // scrubber is deterministic; user can press play again
        lightingChanged = true;
    }
    ImGui::SetNextItemWidth(220.0f);
    ImGui::BeginDisabled(!profile.dayNightCycle);
    if (ImGui::SliderFloat("Game minutes / real second", &profile.dayMinutesPerSecond, 0.5f, 1200.0f,
                           "%.1f", ImGuiSliderFlags_Logarithmic))
    {
        profile.dayMinutesPerSecond = std::clamp(profile.dayMinutesPerSecond, 0.01f, 3600.0f);
        lightingChanged = true;
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton("Sunrise"))
    {
        runtimeTimeOfDay_ = profile.timeOfDayMinutes = 360.0f;
        profile.dayNightPlaying = false;
        lightingChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Noon"))
    {
        runtimeTimeOfDay_ = profile.timeOfDayMinutes = 720.0f;
        profile.dayNightPlaying = false;
        lightingChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Sunset"))
    {
        runtimeTimeOfDay_ = profile.timeOfDayMinutes = 1080.0f;
        profile.dayNightPlaying = false;
        lightingChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Midnight"))
    {
        runtimeTimeOfDay_ = profile.timeOfDayMinutes = 0.0f;
        profile.dayNightPlaying = false;
        lightingChanged = true;
    }
    if (ImGui::Button("Capture current time for save"))
    {
        profile.timeOfDayMinutes = runtimeTimeOfDay_;
        lightingChanged = true;
    }
    if (lightingChanged)
        MarkLightingDirty("Updated the real-time day/night preview. Save lighting to persist it.");

    ImGui::SeparatorText("Presentation");
    if (ImGui::Checkbox("In-game view", &inGameViewMode_))
    {
        if (inGameViewMode_)
            editMode_ = false; // clean render first; turn it off to resume editor interaction
        if (svc_ && svc_->requestSaveSettings)
            svc_->requestSaveSettings();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide grids, selection outlines, waypoint/light/formation helpers and editor brushes while retaining the live world simulation.");
    ImGui::TextDisabled("In-game view is a presentation toggle; it never pauses terrain streaming or live simulation.");
    ImGui::End();
}


void AdtViewerModule::DrawSpellEffectPreviewerPanel()
{
    if (!ImGui::Begin("Spell Effect Previewer"))
    {
        ImGui::End();
        return;
    }

    SpellPreviewState& preview = spellPreview_;
    ImGui::TextDisabled("Spell.dbc timing + in-world cast / projectile / impact preview");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##spellpreviewsearch", "Search spells by name or ID", preview.search, sizeof(preview.search));
    if (svc_ && svc_->lookups && svc_->lookups->SpellsLoaded())
    {
        ImGui::BeginChild("##spellpreviewresults", ImVec2(0, 120.0f), true);
        const std::vector<NameEntry> results = svc_->lookups->SearchSpells(preview.search, 100);
        for (const NameEntry& entry : results)
        {
            const std::string label = std::to_string(entry.id) + "  " + entry.name;
            if (ImGui::Selectable(label.c_str(), entry.id == preview.definition.id))
                LoadSpellPreviewDefinition(entry.id);
        }
        if (results.empty())
            ImGui::TextDisabled("No matching loaded spell names.");
        ImGui::EndChild();
    }
    else
        ImGui::TextDisabled("Load WoW client data to browse spell names; an ID can still be previewed with safe defaults.");

    uint32_t spellId = preview.definition.id;
    if (InputU32("Spell ID", spellId))
        LoadSpellPreviewDefinition(spellId);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reload DBC fields"))
        LoadSpellPreviewDefinition(preview.definition.id);

    const SpellPreviewDefinition& spell = preview.definition;
    const std::string selectedLabel = "Selected: " + spell.name;
    ImGui::SeparatorText(selectedLabel.c_str());
    if (BeginFieldTable("spellpreviewprops", 116.0f))
    {
        FieldRow("Range"); ImGui::Text("%.1f yards", spell.rangeYards);
        FieldRow("Cast time"); ImGui::Text("%.2f seconds", spell.castTimeSeconds);
        FieldRow("Cooldown"); ImGui::Text("%.2f seconds", spell.cooldownSeconds);
        FieldRow("Mana cost"); ImGui::Text("%.0f", spell.manaCost);
        FieldRow("Projectile speed"); ImGui::Text("%.1f yards/sec", spell.projectileSpeed);
        FieldRow("SpellVisual"); ImGui::Text("%u", spell.visualId);
        FieldRow("Effects"); ImGui::Text("%u, %u, %u", spell.effectIds[0], spell.effectIds[1], spell.effectIds[2]);
        ImGui::EndTable();
    }

    const bool atEnd = preview.timelineSeconds >= SpellPreviewDuration() - 1e-4f;
    if (ImGui::Button(preview.playing ? "Pause" : "Play Effect"))
    {
        if (preview.playing)
            preview.playing = false;
        else
        {
            if (atEnd)
                preview.timelineSeconds = 0.0f;
            preview.playing = true;
            inGameViewMode_ = false; // retain an authoring-visible result while playing from the panel
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop"))
    {
        preview.playing = false;
        preview.timelineSeconds = 0.0f;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &preview.loop);
    ImGui::SameLine();
    ImGui::Checkbox("Show in world", &preview.showOverlay);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("Playback speed", &preview.playbackSpeed, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Preview settings");
    static const char* kTargets[] = {"Camera forward", "Selected NPC / GameObject / doodad", "Script preview player"};
    int target = static_cast<int>(preview.targetSource);
    if (ImGui::Combo("Target", &target, kTargets, IM_ARRAYSIZE(kTargets)))
        preview.targetSource = static_cast<SpellPreviewTargetSource>(std::clamp(target, 0, 2));
    ImGui::TextDisabled("Caster: camera position");
    if (preview.targetSource == SpellPreviewTargetSource::SelectedObject && selKind_ == SelKind::None)
        ImGui::TextColored(ImVec4(1.0f, 0.69f, 0.25f, 1.0f), "Select an NPC, GameObject, or placement; camera-forward is used until then.");
    if (preview.targetSource == SpellPreviewTargetSource::ScriptPreviewPlayer && !scriptPreviewPlayerEnabled_)
        ImGui::TextColored(ImVec4(1.0f, 0.69f, 0.25f, 1.0f), "Place/enable the Script Trigger preview player; camera-forward is used until then.");

    static const char* kWeather[] = {"Clear", "Rain", "Snow", "Fog", "Storm"};
    ImGui::Combo("Weather tint", &preview.weather, kWeather, IM_ARRAYSIZE(kWeather));
    const char* timeLabel = runtimeTimeOfDay_ < 360.0f ? "Night" : runtimeTimeOfDay_ < 600.0f ? "Dawn" :
                            runtimeTimeOfDay_ < 960.0f ? "Noon" : runtimeTimeOfDay_ < 1200.0f ? "Dusk" : "Night";
    ImGui::Text("Time of day: %s", timeLabel);
    ImGui::SameLine();
    if (ImGui::SmallButton("Use noon"))
    {
        runtimeTimeOfDay_ = lightingEdit_.timeOfDayMinutes = 720.0f;
        lightingEdit_.dayNightPlaying = false;
        MarkLightingDirty("Spell preview set the world time to noon. Save lighting to persist it.");
    }

    ImGui::SeparatorText("Effect timeline");
    const float duration = SpellPreviewDuration();
    float timeline = preview.timelineSeconds;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##spellpreviewtimeline", &timeline, 0.0f, duration, "%.2f s"))
    {
        preview.timelineSeconds = std::clamp(timeline, 0.0f, duration);
        preview.playing = false;
    }
    const float castEnd = std::min(std::max(spell.castTimeSeconds, 0.0f), duration);
    const float flightEnd = std::min(castEnd + std::clamp(spell.rangeYards / std::max(spell.projectileSpeed, 1.0f), 0.15f, 8.0f), duration);
    ImGui::TextDisabled("| Cast 0–%.2fs | Projectile %.2f–%.2fs | Impact / sustain %.2f–%.2fs |",
                        castEnd, castEnd, flightEnd, flightEnd, duration);

    if (ImGui::Button("Copy preview manifest"))
    {
        const nlohmann::json manifest = {
            {"spellId", spell.id}, {"name", spell.name}, {"rangeYards", spell.rangeYards},
            {"castTimeSeconds", spell.castTimeSeconds}, {"cooldownSeconds", spell.cooldownSeconds},
            {"manaCost", spell.manaCost}, {"spellVisualId", spell.visualId},
            {"effectIds", {spell.effectIds[0], spell.effectIds[1], spell.effectIds[2]}},
            {"target", static_cast<int>(preview.targetSource)}, {"weatherTint", preview.weather}
        };
        ImGui::SetClipboardText(manifest.dump(2).c_str());
        preview.status = "Copied spell preview manifest to the clipboard.";
    }
    ImGui::SameLine();
    if (ImGui::Button("Edit particles / sounds"))
        preview.status = "SpellVisual data is read from Spell.dbc; this preview currently renders a procedural timing fallback when a portable particle mapping is unavailable.";

    if (!preview.status.empty())
        ImGui::TextDisabled("%s", preview.status.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("The World Editor renders the cast, ballistic projectile trail, and impact/sustain volume live over the map using real Spell.dbc cast time, range, cooldown, mana, speed, visual, and effect fields when available. It is a Studio-side timing/placement preview. A stock 3.3.5 server does not execute this visual solely because it appears here; use the selected spell ID in SmartAI/custom script wiring for gameplay.");
    ImGui::End();
}

void AdtViewerModule::RunWorldValidation()
{
    worldValidationIssues_.clear();
    worldValidationHasRun_ = true;
    auto add = [&](WorldValidationSeverity severity, std::string category, std::string message,
                   SelKind kind = SelKind::None, uint32_t guid = 0,
                   const glm::vec3& world = glm::vec3(0.0f), bool hasWorld = false) {
        if (worldValidationIssues_.size() >= 1000)
            return;
        worldValidationIssues_.push_back({severity, std::move(category), std::move(message), kind, guid, world, hasWorld});
    };
    const auto finitePosition = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };

    if (!streamerInit_ || loadedName_.empty())
    {
        add(WorldValidationSeverity::Error, "Map", "Open a terrain or WMO map before validating world content.");
        worldValidationStatus_ = "Validation needs an open map.";
        return;
    }
    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
        add(WorldValidationSeverity::Warning, "Client data", "Client data is unavailable; terrain, display, model and asset diagnostics are incomplete.");
    if (!svc_ || !svc_->connected || !svc_->activeDb)
        add(WorldValidationSeverity::Warning, "Database", "Database is disconnected; spawn, path and formation diagnostics are based only on any already-loaded data.");
    if (!svc_ || svc_->editRoot.empty())
        add(WorldValidationSeverity::Warning, "Project overlay", "No edited-client project folder is configured; terrain and ADT placement saves are disabled.");
    if (adtEdits_.pendingCount() > 0)
        add(WorldValidationSeverity::Warning, "Unsaved ADT edits",
            std::to_string(adtEdits_.pendingCount()) + " pending ADT edit(s) need Save Pending ADT Edits before they exist in the project overlay.");
    if (terrainSculptActive_)
        add(WorldValidationSeverity::Info, "Terrain", "Terrain brush is armed; right-clicking the viewport will queue a staged edit.");
    if (npcLayer_.failedDisplayCount() > 0)
        add(WorldValidationSeverity::Warning, "NPC models",
            std::to_string(npcLayer_.failedDisplayCount()) + " creature display ID(s) failed to resolve to a renderable client model; markers remain selectable.");

    std::vector<MapSpawn> npcs;
    npcLayer_.SnapshotSpawns(npcs);
    std::unordered_set<uint32_t> npcGuids;
    std::unordered_map<uint64_t, uint32_t> npcCells;
    const SpawnFilter filter = CurrentSpawnFilter();
    for (const MapSpawn& npc : npcs)
    {
        const glm::vec3 position(npc.x, npc.y, npc.z);
        npcGuids.insert(npc.guid);
        if (!finitePosition(position))
        {
            add(WorldValidationSeverity::Error, "Creature spawn", "Creature guid " + std::to_string(npc.guid) + " has non-finite coordinates.",
                SelKind::Npc, npc.guid);
            continue;
        }
        const ResolvedCreatureDisplay display = npc.ResolveDisplay(filter);
        if (display.displayId == 0)
            add(WorldValidationSeverity::Warning, "Creature appearance",
                "Creature guid " + std::to_string(npc.guid) + " has no resolved CreatureDisplayInfo ID.",
                SelKind::Npc, npc.guid, position, true);
        if (!std::isfinite(npc.scale) || npc.scale <= 0.0f)
            add(WorldValidationSeverity::Error, "Creature scale",
                "Creature guid " + std::to_string(npc.guid) + " has an invalid template scale.",
                SelKind::Npc, npc.guid, position, true);
        if (npc.movementType == 2 && npc.pathId == 0)
            add(WorldValidationSeverity::Warning, "Waypoint route",
                "Creature guid " + std::to_string(npc.guid) + " uses waypoint movement but has no resolved path ID.",
                SelKind::Npc, npc.guid, position, true);
        if (npc.movementType != 2 && npc.pathId != 0)
            add(WorldValidationSeverity::Info, "Waypoint route",
                "Creature guid " + std::to_string(npc.guid) + " has path " + std::to_string(npc.pathId) +
                " but its movement type is not waypoint.", SelKind::Npc, npc.guid, position, true);
        if (!std::isfinite(npc.aiBehavior.aggroRadius) || !std::isfinite(npc.aiBehavior.leashDistance) ||
            npc.aiBehavior.aggroRadius < 0.0f || npc.aiBehavior.leashDistance < 0.0f)
            add(WorldValidationSeverity::Error, "AI behavior",
                "Creature guid " + std::to_string(npc.guid) + " has an invalid aggro/leash value.",
                SelKind::Npc, npc.guid, position, true);

        const int64_t cellX = static_cast<int64_t>(std::floor(position.x / 0.75f));
        const int64_t cellY = static_cast<int64_t>(std::floor(position.y / 0.75f));
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32u) |
                             static_cast<uint32_t>(cellY);
        const auto other = npcCells.find(key);
        if (other != npcCells.end() && other->second != npc.guid)
            add(WorldValidationSeverity::Warning, "Creature overlap",
                "Creature guid " + std::to_string(npc.guid) + " shares a sub-yard spawn cell with guid " +
                std::to_string(other->second) + ".", SelKind::Npc, npc.guid, position, true);
        else
            npcCells[key] = npc.guid;
    }

    std::vector<MapGameObject> gameObjects;
    goLayer_.SnapshotGameObjects(gameObjects);
    std::unordered_map<uint64_t, uint32_t> goCells;
    for (const MapGameObject& gameObject : gameObjects)
    {
        const glm::vec3 position(gameObject.x, gameObject.y, gameObject.z);
        if (!finitePosition(position))
        {
            add(WorldValidationSeverity::Error, "GameObject spawn", "GameObject guid " + std::to_string(gameObject.guid) + " has non-finite coordinates.",
                SelKind::GameObject, gameObject.guid);
            continue;
        }
        if (gameObject.displayId == 0)
            add(WorldValidationSeverity::Warning, "GameObject appearance",
                "GameObject guid " + std::to_string(gameObject.guid) + " has no resolved display ID.",
                SelKind::GameObject, gameObject.guid, position, true);
        if (!std::isfinite(gameObject.size) || gameObject.size <= 0.0f)
            add(WorldValidationSeverity::Error, "GameObject scale",
                "GameObject guid " + std::to_string(gameObject.guid) + " has an invalid template size.",
                SelKind::GameObject, gameObject.guid, position, true);
        const float rotationLength = std::sqrt(gameObject.rot[0] * gameObject.rot[0] + gameObject.rot[1] * gameObject.rot[1] +
                                               gameObject.rot[2] * gameObject.rot[2] + gameObject.rot[3] * gameObject.rot[3]);
        if (!std::isfinite(rotationLength) || rotationLength < 1e-4f)
            add(WorldValidationSeverity::Warning, "GameObject rotation",
                "GameObject guid " + std::to_string(gameObject.guid) + " has a zero/invalid quaternion; orientation fallback is used.",
                SelKind::GameObject, gameObject.guid, position, true);
        const int64_t cellX = static_cast<int64_t>(std::floor(position.x / 0.75f));
        const int64_t cellY = static_cast<int64_t>(std::floor(position.y / 0.75f));
        const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32u) |
                             static_cast<uint32_t>(cellY);
        const auto other = goCells.find(key);
        if (other != goCells.end() && other->second != gameObject.guid)
            add(WorldValidationSeverity::Warning, "GameObject overlap",
                "GameObject guid " + std::to_string(gameObject.guid) + " shares a sub-yard spawn cell with guid " +
                std::to_string(other->second) + ".", SelKind::GameObject, gameObject.guid, position, true);
        else
            goCells[key] = gameObject.guid;
    }

    for (const CreatureFormationMember& formation : formations_)
    {
        if (npcGuids.find(formation.memberGuid) == npcGuids.end())
            add(WorldValidationSeverity::Error, "Formation", "Formation member guid " + std::to_string(formation.memberGuid) + " is not loaded on this map.");
        if (npcGuids.find(formation.leaderGuid) == npcGuids.end())
            add(WorldValidationSeverity::Error, "Formation", "Formation leader guid " + std::to_string(formation.leaderGuid) + " is not loaded on this map.");
        if (!std::isfinite(formation.distance) || formation.distance < 0.0f || !std::isfinite(formation.angle))
            add(WorldValidationSeverity::Error, "Formation", "Formation member guid " + std::to_string(formation.memberGuid) + " has invalid distance/angle data.");
    }

    if (waypointDirty_)
        add(WorldValidationSeverity::Warning, "Waypoint route", "The active waypoint route has unsaved local edits.");
    if (waypointLoaded_ && waypointEdit_.points.size() < 2 && waypointEdit_.id != 0)
        add(WorldValidationSeverity::Warning, "Waypoint route", "The active route has fewer than two points.");

    for (const ScriptEventTrigger& trigger : scriptTriggersEdit_)
    {
        if (!trigger.enabled)
            continue;
        if (!std::isfinite(trigger.radius) || trigger.radius <= 0.0f)
            add(WorldValidationSeverity::Error, "Script trigger", "Trigger " + std::to_string(trigger.id) + " has an invalid radius.");
        if (trigger.type == ScriptTriggerType::Proximity && trigger.targetKind == ScriptTriggerObjectKind::None)
            add(WorldValidationSeverity::Warning, "Script trigger", "Proximity trigger " + std::to_string(trigger.id) + " has no NPC/GameObject target.");
        if (trigger.targetKind == ScriptTriggerObjectKind::Npc && trigger.targetGuid != 0 &&
            npcGuids.find(trigger.targetGuid) == npcGuids.end())
            add(WorldValidationSeverity::Warning, "Script trigger", "Trigger " + std::to_string(trigger.id) + " references a missing NPC guid " + std::to_string(trigger.targetGuid) + ".");
        if (trigger.targetKind == ScriptTriggerObjectKind::GameObject && trigger.targetGuid != 0 &&
            std::none_of(gameObjects.begin(), gameObjects.end(), [&](const MapGameObject& value) { return value.guid == trigger.targetGuid; }))
            add(WorldValidationSeverity::Warning, "Script trigger", "Trigger " + std::to_string(trigger.id) + " references a missing GameObject guid " + std::to_string(trigger.targetGuid) + ".");
        if (trigger.scriptHook.empty() && trigger.scriptEventId == 0 && trigger.smartActionListId == 0 && trigger.actions.empty())
            add(WorldValidationSeverity::Warning, "Script trigger", "Trigger " + std::to_string(trigger.id) + " has no hook, event ID, SmartAI list, or action sequence.");
    }

    for (const WorldLight& light : lightingEdit_.lights)
    {
        if (!finitePosition(light.position) || !std::isfinite(light.range) || !std::isfinite(light.intensity) || light.range <= 0.0f)
            add(WorldValidationSeverity::Error, "World light", "Light '" + light.name + "' has invalid position/range/intensity.",
                SelKind::None, 0, light.position, true);
        if (light.type == WorldLightType::Spot && glm::length(light.direction) < 1e-4f)
            add(WorldValidationSeverity::Warning, "World light", "Spot light '" + light.name + "' has no usable direction.",
                SelKind::None, 0, light.position, true);
    }

    int errors = 0;
    int warnings = 0;
    for (const WorldValidationIssue& issue : worldValidationIssues_)
    {
        if (issue.severity == WorldValidationSeverity::Error)
            ++errors;
        else if (issue.severity == WorldValidationSeverity::Warning)
            ++warnings;
    }
    worldValidationStatus_ = "Validation complete: " + std::to_string(errors) + " error(s), " +
                             std::to_string(warnings) + " warning(s), " +
                             std::to_string(worldValidationIssues_.size()) + " finding(s).";
}

void AdtViewerModule::CopyWorldValidationReport() const
{
    std::ostringstream report;
    report << "# TrinityCore Studio World Validation\n\n";
    report << "Map: " << (loadedName_.empty() ? "<none>" : loadedName_) << "\n\n";
    for (const WorldValidationIssue& issue : worldValidationIssues_)
    {
        const char* severity = issue.severity == WorldValidationSeverity::Error ? "ERROR" :
                               issue.severity == WorldValidationSeverity::Warning ? "WARNING" : "INFO";
        report << "- [" << severity << "] " << issue.category << ": " << issue.message;
        if (issue.guid != 0)
            report << " (guid " << issue.guid << ')';
        report << '\n';
    }
    ImGui::SetClipboardText(report.str().c_str());
}

void AdtViewerModule::DrawWorldValidationPanel()
{
    if (!ImGui::Begin("World Validation"))
    {
        ImGui::End();
        return;
    }
    if (!worldValidationHasRun_)
        RunWorldValidation();

    if (ImGui::Button("Run validation"))
        RunWorldValidation();
    ImGui::SameLine();
    if (ImGui::Button("Copy Markdown report"))
    {
        CopyWorldValidationReport();
        worldValidationStatus_ = "Copied validation report to the clipboard.";
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show info", &worldValidationShowInfo_);
    if (!worldValidationStatus_.empty())
        ImGui::TextDisabled("%s", worldValidationStatus_.c_str());

    int errors = 0;
    int warnings = 0;
    for (const WorldValidationIssue& issue : worldValidationIssues_)
    {
        errors += issue.severity == WorldValidationSeverity::Error ? 1 : 0;
        warnings += issue.severity == WorldValidationSeverity::Warning ? 1 : 0;
    }
    ImGui::TextColored(errors ? ImVec4(1.0f, 0.35f, 0.30f, 1.0f) : ImVec4(0.35f, 0.88f, 0.55f, 1.0f),
                       "%d error(s)", errors);
    ImGui::SameLine();
    ImGui::TextColored(warnings ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f) : ImVec4(0.55f, 0.62f, 0.70f, 1.0f),
                       "%d warning(s)", warnings);
    ImGui::Separator();

    if (ImGui::BeginChild("##worldvalidationissues", ImVec2(0, 0), true))
    {
        for (size_t index = 0; index < worldValidationIssues_.size(); ++index)
        {
            const WorldValidationIssue& issue = worldValidationIssues_[index];
            if (!worldValidationShowInfo_ && issue.severity == WorldValidationSeverity::Info)
                continue;
            const char* severity = issue.severity == WorldValidationSeverity::Error ? "ERROR" :
                                   issue.severity == WorldValidationSeverity::Warning ? "WARN" : "INFO";
            const ImVec4 color = issue.severity == WorldValidationSeverity::Error ? ImVec4(1.0f, 0.35f, 0.30f, 1.0f) :
                                 issue.severity == WorldValidationSeverity::Warning ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f) :
                                                                                       ImVec4(0.55f, 0.72f, 0.92f, 1.0f);
            const std::string label = std::string("[") + severity + "] " + issue.category + " — " + issue.message;
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str(), false))
            {
                if (issue.kind == SelKind::Npc)
                    SelectObject(SelKind::Npc, 0, 0, issue.guid);
                else if (issue.kind == SelKind::GameObject)
                    SelectObject(SelKind::GameObject, 0, issue.guid, 0);
                if (issue.hasWorld)
                    FrameWorldPosition(issue.world, issue.kind == SelKind::Npc ? 35.0f : 55.0f);
                if (svc_ && svc_->focusWindow)
                    svc_->focusWindow("World Editor###ADT Viewer");
            }
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", severity);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Click to select/frame the referenced world item when available.");
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void AdtViewerModule::DrawLightEditorPanel()
{
    if (!ImGui::Begin("Light Editor"))
    {
        ImGui::End();
        return;
    }
    const bool mapReady = streamerInit_ && !loadedName_.empty() && !lightingMapDir_.empty();
    if (!mapReady)
    {
        ImGui::TextWrapped("Open a world map to author point and spot lights. Light Editor data is a non-destructive, map-scoped Studio settings layer rendered live in the World Editor.");
        ImGui::End();
        return;
    }

    WorldLightingProfile& profile = lightingEdit_;
    ImGui::TextDisabled("Map: %s", lightingMapDir_.c_str());
    ImGui::BeginDisabled(!lightingDirty_);
    if (ImGui::Button("Save lighting"))
        SaveLightingProfile();
    ImGui::SameLine();
    if (ImGui::Button("Revert lighting"))
        RevertLightingProfile();
    ImGui::EndDisabled();
    if (lightingDirty_)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.20f, 1.0f), "unsaved preview");
    }
    if (!lightingStatus_.empty())
        ImGui::TextDisabled("%s", lightingStatus_.c_str());

    bool environmentChanged = false;
    ImGui::SeparatorText("Viewport lighting");
    environmentChanged |= ImGui::Checkbox("Preview authored lighting", &profile.previewEnabled);
    ImGui::SameLine();
    environmentChanged |= ImGui::Checkbox("Game-style sunlight", &profile.sunEnabled);
    ImGui::SameLine();
    environmentChanged |= ImGui::Checkbox("Show light markers", &profile.showMarkers);
    ImGui::SameLine();
    environmentChanged |= ImGui::Checkbox("Show influence volumes", &profile.showVolumes);

    if (ImGui::CollapsingHeader("Sun and ambient", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (BeginFieldTable("lightenvironment", 150.0f))
        {
            FieldRow("Ambient color");
            environmentChanged |= ImGui::ColorEdit3("##ambientcolor", &profile.ambientColor.x,
                                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            FieldRow("Ambient intensity");
            environmentChanged |= ImGui::SliderFloat("##ambientintensity", &profile.ambientIntensity,
                                                      0.0f, 3.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            FieldRow("Sun color");
            environmentChanged |= ImGui::ColorEdit3("##suncolor", &profile.sunColor.x,
                                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            FieldRow("Sun intensity");
            environmentChanged |= ImGui::SliderFloat("##sunintensity", &profile.sunIntensity,
                                                      0.0f, 6.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            FieldRow("Sun azimuth", "Degrees around Z: 0° is +X, 90° is +Y.");
            environmentChanged |= InputFloatField("##sunazimuth", profile.sunAzimuth);
            FieldRow("Sun elevation", "Degrees above the horizon; negative values create night/rim lighting.");
            environmentChanged |= InputFloatField("##sunelevation", profile.sunElevation);
            EndFieldTable();
        }
        if (ImGui::SmallButton("Dawn"))
        {
            profile.sunAzimuth = 35.0f; profile.sunElevation = 12.0f;
            profile.sunColor = glm::vec3(1.0f, 0.61f, 0.34f); profile.sunIntensity = 0.72f;
            profile.ambientColor = glm::vec3(0.44f, 0.51f, 0.70f); profile.ambientIntensity = 0.46f;
            environmentChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Noon"))
        {
            profile.sunAzimuth = 48.8f; profile.sunElevation = 58.2f;
            profile.sunColor = glm::vec3(1.0f); profile.sunIntensity = 0.55f;
            profile.ambientColor = glm::vec3(1.0f); profile.ambientIntensity = 0.45f;
            environmentChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Dusk"))
        {
            profile.sunAzimuth = 225.0f; profile.sunElevation = 9.0f;
            profile.sunColor = glm::vec3(1.0f, 0.38f, 0.20f); profile.sunIntensity = 0.66f;
            profile.ambientColor = glm::vec3(0.36f, 0.28f, 0.52f); profile.ambientIntensity = 0.38f;
            environmentChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Night"))
        {
            profile.sunEnabled = false;
            profile.ambientColor = glm::vec3(0.20f, 0.28f, 0.52f); profile.ambientIntensity = 0.32f;
            environmentChanged = true;
        }
    }

    if (ImGui::CollapsingHeader("Distance fog", ImGuiTreeNodeFlags_DefaultOpen))
    {
        environmentChanged |= ImGui::Checkbox("Enable distance fog", &profile.fogEnabled);
        if (BeginFieldTable("lightfog", 150.0f))
        {
            FieldRow("Fog color");
            environmentChanged |= ImGui::ColorEdit3("##fogcolor", &profile.fogColor.x,
                                                     ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            FieldRow("Fog start"); environmentChanged |= InputFloatField("##fogstart", profile.fogStart);
            FieldRow("Fog end"); environmentChanged |= InputFloatField("##fogend", profile.fogEnd);
            EndFieldTable();
        }
        if (!std::isfinite(profile.fogStart)) profile.fogStart = 500.0f;
        if (!std::isfinite(profile.fogEnd)) profile.fogEnd = 1000.0f;
        profile.fogStart = std::max(profile.fogStart, 0.0f);
        profile.fogEnd = std::max(profile.fogEnd, profile.fogStart + 0.01f);
    }
    profile.ambientColor = glm::clamp(profile.ambientColor, glm::vec3(0.0f), glm::vec3(8.0f));
    profile.sunColor = glm::clamp(profile.sunColor, glm::vec3(0.0f), glm::vec3(8.0f));
    profile.fogColor = glm::clamp(profile.fogColor, glm::vec3(0.0f), glm::vec3(8.0f));
    profile.ambientIntensity = std::clamp(profile.ambientIntensity, 0.0f, 8.0f);
    profile.sunIntensity = std::clamp(profile.sunIntensity, 0.0f, 16.0f);
    profile.sunAzimuth = std::fmod(profile.sunAzimuth, 360.0f);
    if (profile.sunAzimuth < 0.0f)
        profile.sunAzimuth += 360.0f;
    profile.sunElevation = std::clamp(profile.sunElevation, -89.9f, 89.9f);
    if (environmentChanged)
        MarkLightingDirty("Updated the live environment preview.");

    ImGui::SeparatorText("Point / spot lights");
    const glm::vec3 localFocus = camera_.mode() == ViewportCamera::Mode::Fly ? camera_.Eye()
                                                                               : camera_.center();
    const glm::vec3 origin = streamer_.origin();
    const glm::vec3 cameraWorld(localFocus.x + origin.x, localFocus.y + origin.y, localFocus.z);
    if (ImGui::Button("Add point at camera"))
        AddWorldLight(WorldLightType::Point, cameraWorld);
    ImGui::SameLine();
    if (ImGui::Button("Add spot at camera"))
        AddWorldLight(WorldLightType::Spot, cameraWorld);
    ImGui::SameLine();
    auto armLightPlacement = [&](WorldLightType type, bool alreadyActive) {
        lightPlacementActive_ = !alreadyActive;
        lightPlacementType_ = type;
        if (lightPlacementActive_)
        {
            // Match WoWEdit's modal tool behavior: one terrain click must have one unambiguous
            // authoring meaning, rather than competing with a sculpt/path/spawn brush.
            inGameViewMode_ = false;
            editMode_ = true;
            terrainSculptActive_ = false;
            waypointPlacementMode_ = WaypointPlacementMode::None;
            brushActive_ = false;
        }
    };
    const bool placePoint = lightPlacementActive_ && lightPlacementType_ == WorldLightType::Point;
    if (ImGui::Button(placePoint ? "Stop placing" : "Place point on terrain"))
        armLightPlacement(WorldLightType::Point, placePoint);
    ImGui::SameLine();
    const bool placeSpot = lightPlacementActive_ && lightPlacementType_ == WorldLightType::Spot;
    if (ImGui::Button(placeSpot ? "Stop placing" : "Place spot on terrain"))
        armLightPlacement(WorldLightType::Spot, placeSpot);
    ImGui::SameLine();
    ImGui::Checkbox("Continuous", &lightPlacementContinuous_);
    if (lightPlacementActive_)
        ImGui::TextColored(ImVec4(1.0f, 0.77f, 0.24f, 1.0f),
                           "%s placement active — right-click terrain in the World Editor.",
                           WorldLightTypeName(lightPlacementType_));

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##lightsearch", "filter lights", lightSearch_, sizeof(lightSearch_));
    const std::string query = Lower(lightSearch_);
    ImGui::BeginChild("##worldlightlist", ImVec2(0, 154), true);
    for (WorldLight& light : profile.lights)
    {
        const std::string type = WorldLightTypeName(light.type);
        const std::string label = type + "  " + light.name + "  ##" + std::to_string(light.id);
        if (!query.empty() && Lower(type + " " + light.name + " " + std::to_string(light.id)).find(query) == std::string::npos)
            continue;
        const std::string rowId = "light-" + std::to_string(light.id);
        ImGui::PushID(rowId.c_str());
        bool enabled = light.enabled;
        if (ImGui::Checkbox("##enabled", &enabled))
        {
            light.enabled = enabled;
            MarkLightingDirty("Changed light visibility.");
        }
        ImGui::SameLine();
        if (ImGui::Selectable(label.c_str(), selectedWorldLightId_ == light.id))
        {
            ClearSelection();
            selectedWorldLightId_ = light.id;
            lightingStatus_ = "Selected " + light.name + ".";
        }
        ImGui::PopID();
    }
    if (profile.lights.empty())
        ImGui::TextDisabled("No Studio lights yet — add one at the camera or place one on terrain.");
    ImGui::EndChild();
    int enabledLights = 0;
    for (const WorldLight& light : profile.lights)
        if (light.enabled && light.intensity > 0.0f && light.range > 0.0f)
            ++enabledLights;
    ImGui::TextDisabled("%d authored / %d enabled — nearest %d affect the current viewport.",
                        static_cast<int>(profile.lights.size()), enabledLights, kMaxWorldLights);

    WorldLight* selected = FindWorldLight(selectedWorldLightId_);
    if (!selected)
    {
        ImGui::TextDisabled("Select a point/spot marker in the world or a row above to edit it.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText((std::string(WorldLightTypeName(selected->type)) + " Light Inspector").c_str());
    bool changed = false;
    if (BeginFieldTable("lightinspector", 150.0f))
    {
        FieldRow("Name"); changed |= InputTextString("##lightname", selected->name);
        int lightType = selected->type == WorldLightType::Spot ? 1 : 0;
        static const char* kLightTypes[] = {"Point", "Spot"};
        FieldRow("Type");
        if (ImGui::Combo("##lighttype", &lightType, kLightTypes, IM_ARRAYSIZE(kLightTypes)))
        {
            selected->type = lightType == 1 ? WorldLightType::Spot : WorldLightType::Point;
            changed = true;
        }
        FieldRow("Enabled"); changed |= ImGui::Checkbox("##lightenabled", &selected->enabled);
        FieldRow("Position X"); changed |= InputFloatField("##lightx", selected->position.x);
        FieldRow("Position Y"); changed |= InputFloatField("##lighty", selected->position.y);
        FieldRow("Position Z"); changed |= InputFloatField("##lightz", selected->position.z);
        FieldRow("Color"); changed |= ImGui::ColorEdit3("##lightcolor", &selected->color.x,
                                                          ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
        FieldRow("Intensity"); changed |= InputFloatField("##lightintensity", selected->intensity);
        FieldRow("Range"); changed |= InputFloatField("##lightrange", selected->range);
        FieldRow("Falloff exponent", "Higher values concentrate light near the source.");
        changed |= InputFloatField("##lightfalloff", selected->falloff);
        if (selected->type == WorldLightType::Spot)
        {
            FieldRow("Direction");
            float direction[3] = {selected->direction.x, selected->direction.y, selected->direction.z};
            if (ImGui::InputFloat3("##lightdir", direction, "%.3f"))
            {
                selected->direction = glm::vec3(direction[0], direction[1], direction[2]);
                changed = true;
            }
            FieldRow("Inner cone (deg)"); changed |= InputFloatField("##innercone", selected->innerAngle);
            FieldRow("Outer cone (deg)"); changed |= InputFloatField("##outercone", selected->outerAngle);
        }
        EndFieldTable();
    }
    if (changed)
    {
        NormalizeWorldLight(*selected);
        MarkLightingDirty("Edited the selected light.");
    }

    if (ImGui::Button("Frame light"))
        FrameSelectedWorldLight();
    ImGui::SameLine();
    if (ImGui::Button("Snap to terrain"))
    {
        glm::vec3 ground;
        float hitT = -1.0f;
        int tileX = 0, tileY = 0;
        const glm::vec3 local(selected->position.x - origin.x, selected->position.y - origin.y, selected->position.z);
        if (streamer_.GroundHit(glm::vec3(local.x, local.y, 10000.0f), glm::vec3(0, 0, -1),
                                ground, hitT, tileX, tileY))
        {
            if (liveTerrainPreview_ && adtEdits_.terrainPendingCount() > 0)
                ground.z = adtEdits_.PreviewTerrainZ(ground.z, ground.x + origin.x, ground.y + origin.y);
            selected->position.z = ground.z;
            MarkLightingDirty("Snapped the light to terrain.");
        }
        else
            lightingStatus_ = "No loaded terrain below this light.";
    }
    if (selected->type == WorldLightType::Spot)
    {
        ImGui::SameLine();
        if (ImGui::Button("Aim at camera"))
        {
            const glm::vec3 target = cameraWorld;
            if (glm::length(target - selected->position) > 1e-4f)
            {
                selected->direction = glm::normalize(target - selected->position);
                MarkLightingDirty("Aimed the spot light at the camera focus.");
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate light"))
    {
        DuplicateSelectedWorldLight();
        ImGui::End(); // vector growth may invalidate `selected`; draw the new selection next frame
        return;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete light"))
    {
        DeleteSelectedWorldLight();
        ImGui::End(); // erase invalidates `selected`; avoid touching it later in this frame
        return;
    }

    ImGui::Separator();
    ImGui::TextWrapped("The renderer evaluates the nearest %d enabled point/spot lights per frame, plus the sun, ambient and optional distance fog. Marker volumes are editor aids. Profiles save with this Studio project; the classic 3.3.5 ADT format itself has no standalone point/spot-light placement table.",
                       kMaxWorldLights);
    ImGui::End();
}



void AdtViewerModule::DrawTerrainBrushOverlay(const glm::mat4& view, const glm::mat4& proj,
                                                    const ImVec2& p0, int w, int h,
                                                    bool viewportHovered)
{
    if (!terrainSculptActive_ || !viewportHovered)
        return;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float px = mouse.x - p0.x;
    const float py = mouse.y - p0.y;
    if (px < 0.0f || py < 0.0f || px >= static_cast<float>(w) || py >= static_cast<float>(h))
        return;
    const PickRay ray = MakePickRay(view, proj, px, py, static_cast<float>(w), static_cast<float>(h));
    glm::vec3 center;
    float centerT = -1.0f;
    int tx = 0, ty = 0;
    if (!streamer_.GroundHit(ray.origin, ray.dir, center, centerT, tx, ty))
        return;
    const glm::vec3 origin = streamer_.origin();
    if (liveTerrainPreview_ && adtEdits_.terrainPendingCount() > 0)
        center.z = adtEdits_.PreviewTerrainZ(center.z, center.x + origin.x, center.y + origin.y);

    // A ramp has a deliberate two-click interaction. Render its start/end guide
    // instead of a circular brush so the grade direction and pending endpoint are
    // obvious before the author commits the staged Flatten sequence.
    if (terrainSculptMode_ == 3)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const glm::vec3 worldEnd(center.x + origin.x, center.y + origin.y, center.z);
        ImVec2 endScreen;
        if (ProjectWorldPoint(worldEnd, origin, view, proj, p0, w, h, endScreen))
        {
            draw->AddCircle(endScreen, 7.0f, IM_COL32(255, 184, 74, 245), 16, 2.0f);
            draw->AddText(ImVec2(endScreen.x + 9.0f, endScreen.y + 7.0f), IM_COL32(255, 232, 190, 255),
                          terrainRampHasStart_ ? "Ramp end" : "Ramp start");
        }
        if (terrainRampHasStart_)
        {
            ImVec2 startScreen;
            if (ProjectWorldPoint(terrainRampStart_, origin, view, proj, p0, w, h, startScreen) &&
                ProjectWorldPoint(worldEnd, origin, view, proj, p0, w, h, endScreen))
            {
                draw->AddLine(startScreen, endScreen, IM_COL32(255, 138, 50, 230), 3.0f);
                draw->AddCircleFilled(startScreen, 5.0f, IM_COL32(255, 212, 114, 255), 12);
            }
        }
        return;
    }

    constexpr int kSegments = 32;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 previous{};
    bool havePrevious = false;
    for (int i = 0; i <= kSegments; ++i)
    {
        const float angle = (static_cast<float>(i) / kSegments) * 6.28318530718f;
        glm::vec3 rim;
        float rimT = -1.0f;
        int rimTx = 0, rimTy = 0;
        const glm::vec3 top(center.x + std::cos(angle) * terrainBrushRadius_,
                            center.y + std::sin(angle) * terrainBrushRadius_, 10000.0f);
        ImVec2 projected;
        const bool hit = streamer_.GroundHit(top, glm::vec3(0.0f, 0.0f, -1.0f), rim, rimT, rimTx, rimTy) &&
                         ProjectWorldPoint(glm::vec3(rim.x + origin.x, rim.y + origin.y, rim.z), origin,
                                           view, proj, p0, w, h, projected);
        if (hit && havePrevious)
            draw->AddLine(previous, projected, IM_COL32(244, 139, 48, 225), 2.0f);
        previous = projected;
        havePrevious = hit;
    }
    ImVec2 centerScreen;
    if (ProjectWorldPoint(glm::vec3(center.x + origin.x, center.y + origin.y, center.z), origin,
                          view, proj, p0, w, h, centerScreen))
    {
        draw->AddCircleFilled(centerScreen, 4.0f, IM_COL32(255, 221, 160, 255), 10);
        static const char* kModes[] = {"Raise", "Lower", "Flatten", "Ramp / Stairs", "Noise / Terrainify", "Terrain Stamp", "Smooth"};
        draw->AddText(ImVec2(centerScreen.x + 8.0f, centerScreen.y + 6.0f),
                      IM_COL32(255, 234, 204, 255),
                      kModes[std::clamp(terrainSculptMode_, 0, 6)]);
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

void AdtViewerModule::SavePendingAdtEdits()
{
    if (adtEdits_.empty())
        return;
    if (!svc_ || !svc_->clientData || svc_->editRoot.empty())
    {
        saveStatus_ = "Open a project with an edited-client folder before saving ADT edits.";
        if (svc_ && svc_->setStatus)
            svc_->setStatus(saveStatus_);
        return;
    }

    const int terrainPending = adtEdits_.terrainPendingCount();
    std::string status;
    const bool saved = adtEdits_.Flush(*svc_->clientData, svc_->editRoot, status);
    saveStatus_ = status;
    if (terrainPending > 0)
    {
        // Flush writes tiles one at a time. Even a later I/O failure can leave an earlier
        // terrain tile safely persisted, so freeze old terrain undo closures on every save
        // attempt rather than risk replaying an additive stroke twice on retry.
        ++terrainHistoryGeneration_;
        if (saved)
        {
            // Terrain GPU meshes are immutable uploads. Reopen the current map so the
            // streamer rereads the just-written MCVT/MCNR overlay data.
            terrainStatus_ = "Terrain edits saved; reloading streamed tiles from the project overlay.";
            OpenMapDir(selectedMapDir_, false);
        }
        else
            terrainStatus_ = "Terrain save did not finish; inspect the status and retry pending edits if needed.";
    }
    if (svc_->setStatus)
        svc_->setStatus(status);
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
        if (ImGui::Button(label.c_str()))
            SavePendingAdtEdits();
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
    // Point/spot markers are an explicit authoring target, so select them before an NPC or
    // doodad underneath their influence volume.
    if (TrySelectLightMarkerOverlay(viewportHovered))
        return;
    if (TrySelectNpcMarkerOverlay(viewportHovered))
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
        selectedWorldLightId_ = 0;
        return;
    }
    SelectObject(hitKind, duid, gg, ng);
    if (scriptInteractionMode_)
    {
        if (hitKind == SelKind::Npc)
            FireInteractionTriggers(static_cast<int>(ScriptTriggerObjectKind::Npc), ng);
        else if (hitKind == SelKind::GameObject)
            FireInteractionTriggers(static_cast<int>(ScriptTriggerObjectKind::GameObject), gg);
    }
}

// Make (kind, ids) the current selection: clears any prior selection and fills the sel* fields +
// label. Shared by left-click select and right-click context. Only the id matching `kind` is used.
void AdtViewerModule::SelectObject(SelKind kind, uint64_t duid, uint32_t gg, uint32_t ng)
{
    ClearSelection();
    selectedWorldLightId_ = 0;   // object transforms and Light Editor markers are distinct selections
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
        gizmoMouseCaptured_ = false;
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
    const bool over_ = ImGuizmo::IsOver();
    // Claim the left mouse from the instant a transform handle is pressed. IsUsing() can lag
    // one frame behind the press on some ImGuizmo operations, which previously let the viewport
    // InvisibleButton rotate/fly the camera before the NPC/GameObject transform took ownership.
    if (using_ || (over_ && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                             ImGui::IsMouseDown(ImGuiMouseButton_Left))))
        gizmoMouseCaptured_ = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !using_)
        gizmoMouseCaptured_ = false;

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
    gizmoHoveredPrev_ = over_;
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
    if (liveTerrainPreview_ && adtEdits_.terrainPendingCount() > 0)
        ground.z = adtEdits_.PreviewTerrainZ(ground.z, ground.x + origin.x, ground.y + origin.y);

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
        float serverDisplayScale = 1.0f;
        CreatureDisplaySource displaySource = CreatureDisplaySource::None;
        spawnRepo_.InsertCreatureSpawn(*svc_->activeDb, currentMapId_, d.entry, d.x, d.y, d.z, d.o,
                                       guid, disp, &serverDisplayScale, &displaySource);
        MapSpawn s;
        s.guid = d.guid; s.entry = d.entry; s.x = d.x; s.y = d.y; s.z = d.z; s.o = d.o;
        s.displayId = d.displayId ? d.displayId : disp;
        s.displayScale = serverDisplayScale > 0.0f ? serverDisplayScale : 1.0f;
        s.displaySource = displaySource;
        s.templateDisplayId = s.displayId;
        s.templateDisplayScale = s.displayScale;
        s.templateDisplaySource = displaySource;
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

void AdtViewerModule::QueueTerrainStrokeAcrossTiles(
    const adt::TerrainBrushStroke& stroke, int centerTileX, int centerTileY,
    std::vector<AdtEditStore::TerrainStrokeRef>& outRefs)
{
    if (!std::isfinite(stroke.worldX) || !std::isfinite(stroke.worldY) ||
        !std::isfinite(stroke.radius) || stroke.radius <= 0.01f)
        return;
    const float halfTile = adt::kTileSize * 0.5f;
    for (int ty = std::max(0, centerTileY - 1); ty <= std::min(63, centerTileY + 1); ++ty)
        for (int tx = std::max(0, centerTileX - 1); tx <= std::min(63, centerTileX + 1); ++tx)
        {
            bool exists = false;
            for (const auto& tile : streamer_.world().tiles)
                if (tile.first == tx && tile.second == ty) { exists = true; break; }
            if (!exists)
                continue;
            const glm::vec2 tileCenter((31.5f - ty) * adt::kTileSize,
                                       (31.5f - tx) * adt::kTileSize);
            const float edgeX = std::max(std::fabs(stroke.worldX - tileCenter.x) - halfTile, 0.0f);
            const float edgeY = std::max(std::fabs(stroke.worldY - tileCenter.y) - halfTile, 0.0f);
            if (edgeX * edgeX + edgeY * edgeY > stroke.radius * stroke.radius)
                continue;
            const AdtEditStore::TerrainStrokeRef ref = adtEdits_.RecordTerrainStroke(tx, ty, stroke);
            if (ref.id != 0)
                outRefs.push_back(ref);
        }
}

void AdtViewerModule::PushTerrainStrokeUndo(const std::vector<AdtEditStore::TerrainStrokeRef>& refs,
                                            const char* label)
{
    if (refs.empty())
        return;
    const uint64_t generation = terrainHistoryGeneration_;
    undo_.Push(MakeCommand(
        [this, generation, refs]() {
            if (generation == terrainHistoryGeneration_)
                for (const auto& ref : refs)
                    adtEdits_.RemoveTerrainStroke(ref.id);
        },
        [this, generation, refs]() {
            if (generation == terrainHistoryGeneration_)
                for (const auto& ref : refs)
                    adtEdits_.RestoreTerrainStroke(ref);
        }, label));
}

void AdtViewerModule::QueueTerrainRamp(const glm::vec3& start, const glm::vec3& requestedEnd)
{
    const glm::vec2 horizontal(requestedEnd.x - start.x, requestedEnd.y - start.y);
    const float length = glm::length(horizontal);
    if (length < 0.25f)
    {
        terrainStatus_ = "Ramp endpoints are too close together.";
        return;
    }

    const float slopeRadians = glm::radians(std::clamp(terrainRampMaxSlopeDegrees_, 0.1f, 60.0f));
    const float maxDelta = std::tan(slopeRadians) * length;
    glm::vec3 end = requestedEnd;
    end.z = std::clamp(end.z, start.z - maxDelta, start.z + maxDelta);
    const float radius = std::clamp(terrainRampWidth_ * 0.5f, 1.0f, 100.0f);
    const float spacing = std::max(1.0f, radius * 0.65f);
    const int samples = std::clamp(static_cast<int>(std::ceil(length / spacing)) + 1, 2, 64);
    std::vector<AdtEditStore::TerrainStrokeRef> refs;
    refs.reserve(static_cast<size_t>(samples) * 3);
    for (int sample = 0; sample < samples; ++sample)
    {
        const float t = static_cast<float>(sample) / static_cast<float>(samples - 1);
        float heightT = t;
        if (terrainRampStepCount_ >= 2)
        {
            const float divisions = static_cast<float>(terrainRampStepCount_ - 1);
            heightT = std::round(t * divisions) / divisions;
        }
        adt::TerrainBrushStroke stroke;
        stroke.mode = adt::TerrainBrushMode::Flatten;
        stroke.worldX = glm::mix(start.x, end.x, t);
        stroke.worldY = glm::mix(start.y, end.y, t);
        stroke.radius = radius;
        stroke.strength = 1.0f;
        stroke.targetZ = glm::mix(start.z, end.z, heightT);
        // World <-> ADT tile mapping mirrors the existing location overview and
        // QueueTerrainStrokeAcrossTiles' center convention.
        const int tileX = std::clamp(static_cast<int>(std::floor(32.0f - stroke.worldY / adt::kTileSize)), 0, 63);
        const int tileY = std::clamp(static_cast<int>(std::floor(32.0f - stroke.worldX / adt::kTileSize)), 0, 63);
        QueueTerrainStrokeAcrossTiles(stroke, tileX, tileY, refs);
    }
    if (refs.empty())
    {
        terrainStatus_ = "Ramp did not intersect a loaded terrain tile.";
        return;
    }
    PushTerrainStrokeUndo(refs, terrainRampStepCount_ >= 2 ? "Build terrain stairs" : "Build terrain ramp");
    terrainStatus_ = "Queued " + std::to_string(terrainRampStepCount_ >= 2 ? terrainRampStepCount_ : samples) +
                     (terrainRampStepCount_ >= 2 ? " ramp stair level(s)" : " ramp grade sample(s)") +
                     " across " + std::to_string(refs.size()) + " tile stroke(s) — save ADT edits to apply it.";
}

void AdtViewerModule::QueueTerrainNoise(const glm::vec3& center, int centerTileX, int centerTileY)
{
    const float radius = std::clamp(terrainBrushRadius_, 2.0f, 100.0f);
    const float frequency = std::clamp(terrainNoiseFrequency_, 0.01f, 1.0f);
    const float amplitude = std::clamp(terrainNoiseAmplitude_, 0.0f, 30.0f);
    const int octaves = std::clamp(terrainNoiseOctaves_, 1, 6);
    // Four-to-five samples per side keeps a stamp responsive and below the terrain
    // GPU preview budget on ordinary tiles while still forming a coherent fractal field.
    const int side = std::clamp(3 + static_cast<int>(std::round(radius * frequency * 1.5f)), 3, 5);
    const float spacing = (radius * 2.0f) / static_cast<float>(side - 1);
    const float childRadius = std::max(1.0f, spacing * 0.95f);
    std::vector<AdtEditStore::TerrainStrokeRef> refs;
    refs.reserve(static_cast<size_t>(side * side * 2));
    int generated = 0;
    for (int gy = 0; gy < side; ++gy)
        for (int gx = 0; gx < side; ++gx)
        {
            const float localX = -radius + static_cast<float>(gx) * spacing;
            const float localY = -radius + static_cast<float>(gy) * spacing;
            const float distance = std::sqrt(localX * localX + localY * localY);
            if (distance > radius)
                continue;
            const float falloffT = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
            const float falloff = falloffT * falloffT * (3.0f - 2.0f * falloffT);
            const float noise = FractalNoise((center.x + localX) * frequency,
                                              (center.y + localY) * frequency,
                                              terrainNoiseSeed_, octaves);
            const float strength = std::fabs(noise) * amplitude * falloff;
            if (strength < 0.01f)
                continue;
            adt::TerrainBrushStroke stroke;
            stroke.mode = noise >= 0.0f ? adt::TerrainBrushMode::Raise : adt::TerrainBrushMode::Lower;
            stroke.worldX = center.x + localX;
            stroke.worldY = center.y + localY;
            stroke.radius = childRadius;
            stroke.strength = strength;
            stroke.targetZ = center.z;
            QueueTerrainStrokeAcrossTiles(stroke, centerTileX, centerTileY, refs);
            ++generated;
        }
    if (refs.empty())
    {
        terrainStatus_ = "Terrainify stamp did not intersect a loaded terrain tile.";
        return;
    }
    PushTerrainStrokeUndo(refs, "Terrainify noise");
    terrainStatus_ = "Queued deterministic Terrainify stamp (" + std::to_string(generated) +
                     " noise samples, " + std::to_string(refs.size()) +
                     " tile strokes) — save ADT edits to apply it.";
}

void AdtViewerModule::QueueTerrainStamp(const glm::vec3& center, int centerTileX, int centerTileY)
{
    const float radius = std::clamp(terrainBrushRadius_, 2.0f, 100.0f);
    const float strength = std::clamp(terrainBrushStrength_, 0.05f, 30.0f);
    std::vector<AdtEditStore::TerrainStrokeRef> refs;
    refs.reserve(32);
    const auto addStroke = [&](adt::TerrainBrushMode mode, float x, float y, float childRadius, float childStrength) {
        adt::TerrainBrushStroke stroke;
        stroke.mode = mode;
        stroke.worldX = x;
        stroke.worldY = y;
        stroke.radius = std::max(0.5f, childRadius);
        stroke.strength = std::max(0.01f, childStrength);
        stroke.targetZ = center.z;
        QueueTerrainStrokeAcrossTiles(stroke, centerTileX, centerTileY, refs);
    };

    const int preset = std::clamp(terrainStampPreset_, 0, 3);
    if (preset == 0) // Hill
        addStroke(adt::TerrainBrushMode::Raise, center.x, center.y, radius, strength);
    else if (preset == 1) // Valley
        addStroke(adt::TerrainBrushMode::Lower, center.x, center.y, radius, strength);
    else if (preset == 2) // Crater: hollow center plus an overlapping raised rim
    {
        addStroke(adt::TerrainBrushMode::Lower, center.x, center.y, radius * 0.68f, strength * 1.15f);
        constexpr int kRingSamples = 10;
        for (int sample = 0; sample < kRingSamples; ++sample)
        {
            const float angle = static_cast<float>(sample) * glm::two_pi<float>() / static_cast<float>(kRingSamples);
            addStroke(adt::TerrainBrushMode::Raise,
                      center.x + std::cos(angle) * radius * 0.68f,
                      center.y + std::sin(angle) * radius * 0.68f,
                      radius * 0.30f, strength * 0.72f);
        }
    }
    else // Ridge
    {
        const float yaw = glm::radians(terrainStampYawDegrees_);
        const glm::vec2 axis(std::cos(yaw), std::sin(yaw));
        constexpr int kRidgeSamples = 7;
        for (int sample = 0; sample < kRidgeSamples; ++sample)
        {
            const float t = static_cast<float>(sample) / static_cast<float>(kRidgeSamples - 1) * 2.0f - 1.0f;
            const float endFade = 1.0f - std::fabs(t) * 0.35f;
            addStroke(adt::TerrainBrushMode::Raise,
                      center.x + axis.x * radius * t,
                      center.y + axis.y * radius * t,
                      radius * 0.34f, strength * endFade);
        }
    }

    if (refs.empty())
    {
        terrainStatus_ = "Terrain stamp did not intersect a loaded terrain tile.";
        return;
    }
    static const char* kNames[] = {"hill", "valley", "crater", "ridge"};
    PushTerrainStrokeUndo(refs, "Apply terrain stamp");
    terrainStatus_ = "Queued " + std::string(kNames[preset]) + " terrain stamp (" +
                     std::to_string(refs.size()) + " tile strokes) — save ADT edits to apply it.";
}

void AdtViewerModule::QueueTerrainSmooth(const glm::vec3& center, int centerTileX, int centerTileY)
{
    (void)centerTileX;
    (void)centerTileY;
    const float radius = std::clamp(terrainBrushRadius_, 2.0f, 100.0f);
    const int iterations = std::clamp(terrainSmoothIterations_, 1, 8);
    const float blend = std::clamp(terrainSmoothBlend_, 0.05f, 1.0f);
    const float edgeThreshold = std::max(0.01f, terrainSmoothEdgeThreshold_);
    const int side = std::clamp(3 + static_cast<int>(std::ceil(radius / 18.0f)), 3, 5);
    const float spacing = (radius * 2.0f) / static_cast<float>(side - 1);
    const float neighborDistance = std::max(0.75f, spacing * 0.72f);
    const float childRadius = std::max(1.0f, spacing * 0.90f);
    const glm::vec3 origin = streamer_.origin();

    struct SmoothNode
    {
        float x = 0.0f, y = 0.0f, current = 0.0f, target = 0.0f;
        int tileX = 0, tileY = 0;
    };
    auto sampleHeight = [&](float worldX, float worldY, float& outHeight, int& outTileX, int& outTileY) {
        glm::vec3 hit;
        float hitDistance = -1.0f;
        if (!streamer_.GroundHit(glm::vec3(worldX - origin.x, worldY - origin.y, 10000.0f),
                                glm::vec3(0.0f, 0.0f, -1.0f), hit, hitDistance, outTileX, outTileY))
            return false;
        // PreviewTerrainZ deliberately applies every staged stroke, including smooth
        // samples emitted by earlier iterations, while GroundHit supplies the immutable
        // streamed base mesh beneath them.
        outHeight = adtEdits_.PreviewTerrainZ(hit.z, worldX, worldY);
        return std::isfinite(outHeight);
    };

    std::vector<AdtEditStore::TerrainStrokeRef> refs;
    refs.reserve(static_cast<size_t>(side * side * iterations * 2));
    int generated = 0;
    for (int iteration = 0; iteration < iterations; ++iteration)
    {
        std::vector<SmoothNode> nodes;
        nodes.reserve(static_cast<size_t>(side * side));
        for (int gy = 0; gy < side; ++gy)
            for (int gx = 0; gx < side; ++gx)
            {
                const float localX = -radius + static_cast<float>(gx) * spacing;
                const float localY = -radius + static_cast<float>(gy) * spacing;
                const float distance = std::sqrt(localX * localX + localY * localY);
                if (distance > radius)
                    continue;
                SmoothNode node;
                node.x = center.x + localX;
                node.y = center.y + localY;
                if (!sampleHeight(node.x, node.y, node.current, node.tileX, node.tileY))
                    continue;

                float sum = 0.0f;
                int count = 0;
                static constexpr float kOffsets[][2] = {{-1.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, -1.0f}, {0.0f, 1.0f}};
                for (const auto& offset : kOffsets)
                {
                    float neighborHeight = 0.0f;
                    int neighborTileX = 0, neighborTileY = 0;
                    if (!sampleHeight(node.x + offset[0] * neighborDistance,
                                      node.y + offset[1] * neighborDistance,
                                      neighborHeight, neighborTileX, neighborTileY))
                        continue;
                    if (terrainSmoothPreserveEdges_ && std::fabs(neighborHeight - node.current) > edgeThreshold)
                        continue;
                    sum += neighborHeight;
                    ++count;
                }
                if (count == 0)
                    continue;
                const float radialT = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
                const float radialWeight = radialT * radialT * (3.0f - 2.0f * radialT);
                node.target = node.current + (sum / static_cast<float>(count) - node.current) * blend * radialWeight;
                if (std::fabs(node.target - node.current) > 0.001f)
                    nodes.push_back(node);
            }

        if (nodes.empty())
            break;
        // Compute targets for the complete iteration before recording any strokes;
        // this is a proper local Laplacian pass instead of scan-order-dependent blur.
        for (const SmoothNode& node : nodes)
        {
            adt::TerrainBrushStroke stroke;
            stroke.mode = adt::TerrainBrushMode::Flatten;
            stroke.worldX = node.x;
            stroke.worldY = node.y;
            stroke.radius = childRadius;
            stroke.strength = 1.0f;
            stroke.targetZ = node.target;
            QueueTerrainStrokeAcrossTiles(stroke, node.tileX, node.tileY, refs);
            ++generated;
        }
    }

    if (refs.empty())
    {
        terrainStatus_ = "Terrain smooth did not find enough loaded ground samples.";
        return;
    }
    PushTerrainStrokeUndo(refs, "Smooth terrain");
    terrainStatus_ = "Queued Laplacian-style terrain smooth (" + std::to_string(generated) +
                     " local samples, " + std::to_string(refs.size()) +
                     " tile strokes) — save ADT edits to apply it.";
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
    {
        if (lightPlacementActive_)
            lightingStatus_ = "No loaded terrain below the cursor — add the light at the camera or fly over an ADT tile.";
        return;   // no ground under the cursor
    }
    // Keep all terrain-click tools (new strokes, flattened target sampling, light/object placement)
    // visually locked to staged height edits before their ADT overlay write/reload occurs.
    if (liveTerrainPreview_ && adtEdits_.terrainPendingCount() > 0)
        gLocal.z = adtEdits_.PreviewTerrainZ(gLocal.z, gLocal.x + origin.x, gLocal.y + origin.y);

    // Terrain sculpting is deliberately highest-priority: an author armed it to modify the ground,
    // even when a doodad/NPC happens to sit between the cursor and the terrain triangle. The actual
    // MCVT/MCNR patch stays pending in AdtEditStore until "Save ADT edits" writes the loose overlay.
    if (terrainSculptActive_)
    {
        const glm::vec3 world(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z);
        if (terrainSculptMode_ == 3)
        {
            if (!terrainRampHasStart_)
            {
                terrainRampStart_ = world;
                terrainRampHasStart_ = true;
                terrainStatus_ = "Ramp start captured — right-click the terrain end point.";
            }
            else
            {
                QueueTerrainRamp(terrainRampStart_, world);
                terrainRampHasStart_ = false;
            }
            return;
        }
        if (terrainSculptMode_ == 4)
        {
            QueueTerrainNoise(world, gtx, gty);
            return;
        }
        if (terrainSculptMode_ == 5)
        {
            QueueTerrainStamp(world, gtx, gty);
            return;
        }
        if (terrainSculptMode_ == 6)
        {
            QueueTerrainSmooth(world, gtx, gty);
            return;
        }

        adt::TerrainBrushStroke stroke;
        stroke.mode = static_cast<adt::TerrainBrushMode>(std::clamp(terrainSculptMode_, 0, 2));
        stroke.worldX = world.x;
        stroke.worldY = world.y;
        stroke.radius = terrainBrushRadius_;
        stroke.strength = terrainBrushStrength_;
        stroke.targetZ = terrainSampleFlattenZ_ ? world.z : terrainFlattenZ_;
        if (terrainSampleFlattenZ_ && stroke.mode == adt::TerrainBrushMode::Flatten)
            terrainFlattenZ_ = world.z;
        std::vector<AdtEditStore::TerrainStrokeRef> refs;
        QueueTerrainStrokeAcrossTiles(stroke, gtx, gty, refs);
        if (refs.empty())
        {
            terrainStatus_ = "Could not queue a terrain stroke for this tile.";
            return;
        }
        PushTerrainStrokeUndo(refs, "Sculpt terrain");
        terrainStatus_ = "Queued terrain stroke across " + std::to_string(refs.size()) + " tile(s) — save ADT edits to apply it.";
        return;
    }

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

    // AI target placement is intentionally above lights/spawns: it is a pure preview control and
    // must never create a persistent world object when an author is testing aggro/leash envelopes.
    if (aiPreviewTargetPlacementActive_)
    {
        TryPlaceAiPreviewTarget(glm::vec3(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z));
        return;
    }
    if (scriptPreviewPlayerPlacementActive_)
    {
        TryPlaceScriptPreviewPlayer(glm::vec3(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z));
        return;
    }
    if (scriptTriggerCenterPlacementActive_)
    {
        TryPlaceScriptTriggerCenter(glm::vec3(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z));
        return;
    }

    // WoWEdit-style point/spot placement comes next. It is a project-light authoring operation,
    // deliberately above the spawn palette so an armed light tool cannot accidentally stamp an NPC.
    if (lightPlacementActive_)
    {
        AddWorldLight(lightPlacementType_, glm::vec3(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z));
        if (!lightPlacementContinuous_)
            lightPlacementActive_ = false;
        return;
    }

    // A palette brush has the next priority: an author deliberately armed it for rapid terrain
    // stamping, so don't turn the same click into an object context menu or one-shot add popup.
    if (brushActive_ && brushEntry_ != 0)
    {
        const glm::vec3 world(gLocal.x + origin.x, gLocal.y + origin.y, gLocal.z);
        if (brushPlacementMode_ == 1)
        {
            PlaceSpawnGrid(world);
            return;
        }
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
    float serverDisplayScale = 1.0f;
    CreatureDisplaySource displaySource = CreatureDisplaySource::None;
    DbError e = spawnRepo_.InsertCreatureSpawn(*svc_->activeDb, currentMapId_, entry, world.x, world.y,
                                               world.z, yaw, guid, displayId, &serverDisplayScale,
                                               &displaySource);
    if (!e.ok || guid == 0)
    {
        saveStatus_ = "Add NPC failed: " + e.message;
        return false;
    }
    MapSpawn s;
    s.guid = guid; s.entry = entry;
    s.x = world.x; s.y = world.y; s.z = world.z; s.o = yaw;
    s.displayId = displayId;
    s.displayScale = serverDisplayScale > 0.0f ? serverDisplayScale : 1.0f;
    s.displaySource = displaySource;
    s.templateDisplayId = displayId;
    s.templateDisplayScale = s.displayScale;
    s.templateDisplaySource = displaySource;
    s.scale = 1.0f; s.phaseMask = 1; s.spawnMask = 1;
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
