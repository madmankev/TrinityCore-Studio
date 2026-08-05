// ModelViewerModule — see ModelViewerModule.h.

#include "editors/model/ModelViewerModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_set>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/EditorServices.h"
#include "db/IDatabase.h"
#include "db/ResultSet.h"
#include "clientdata/ClientData.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"

namespace we
{
namespace
{
bool ContainsNoCase(const std::string& hay, const char* needle)
{
    if (!needle || !*needle)
        return true;
    std::string h = hay, n = needle;
    auto low = [](std::string& s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); };
    low(h); low(n);
    return h.find(n) != std::string::npos;
}
} // namespace

void ModelViewerModule::OnClientDataLoaded()
{
    dbcLoaded_ = false;  // reload lazily against the new client data
    m2List_.clear();
    modelPaths_.clear();
    displays_.clear();
    dresser_.reset();    // rebound to the new client data on next model open
}

void ModelViewerModule::DrawPanels()
{
    // Lazily pull the file list + creature DBCs the first frame client data is available.
    if (!dbcLoaded_ && svc_ && svc_->clientData && svc_->clientData->IsOpen())
    {
        m2List_ = svc_->clientData->ListFiles(".m2");
        std::sort(m2List_.begin(), m2List_.end());
        if (svc_->dbcStore)
        {
            // Kept for the "Creature display ID" picker (OpenDisplay). Path-browse skin
            // resolution for every model class goes through ModelDresser instead.
            modelPaths_ = svc_->dbcStore->LoadCreatureModelPaths(*svc_->clientData);
            displays_ = svc_->dbcStore->LoadCreatureDisplays(*svc_->clientData);
            animNames_ = svc_->dbcStore->LoadAnimationNames(*svc_->clientData);
            particleColors_ = svc_->dbcStore->LoadParticleColors(*svc_->clientData);
        }
        dbcLoaded_ = true;
    }

    DrawBrowserPanel();
    DrawViewportPanel();
}

void ModelViewerModule::DrawBrowserPanel()
{
    if (!ImGui::Begin("Model Browser"))
    {
        ImGui::End();
        return;
    }

    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        ImGui::TextWrapped("Load WoW client data (File menu) to browse models.");
        ImGui::End();
        return;
    }
    if (!svc_->renderer)
    {
        ImGui::TextWrapped("Renderer unavailable.");
        ImGui::End();
        return;
    }

    // --- MPQ file list (listfile) ---
    ImGui::SeparatorText("Files");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "search .m2 files", search_, sizeof(search_));
    if (m2List_.empty())
        ImGui::TextDisabled("(no (listfile) in this client — use the display ID picker)");
    else
    {
        ImGui::BeginChild("##m2list", ImVec2(0, 240), true);
        int shown = 0;
        constexpr int kMax = 400;
        for (const std::string& path : m2List_)
        {
            if (!ContainsNoCase(path, search_))
                continue;
            if (shown++ >= kMax)
                break;
            if (ImGui::Selectable(path.c_str(), path == loadedName_))
                OpenPath(path, nullptr);
        }
        if (shown >= kMax)
            ImGui::TextDisabled("... more matches; refine the search");
        ImGui::EndChild();
    }

    // --- by creature display ID ---
    ImGui::SeparatorText("Creature display ID");
    ImGui::SetNextItemWidth(140);
    ImGui::InputInt("##dispid", &displayIdInput_);
    ImGui::SameLine();
    if (ImGui::Button("Load display") && displayIdInput_ > 0)
        OpenDisplay(static_cast<uint32_t>(displayIdInput_));
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu displays)", displays_.size());

    if (!error_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());

    // Customization + equipment + geosets for the loaded model live here (below the pickers) so the
    // Model Viewer panel stays a clean, unobstructed 3D view.
    DrawControls();

    ImGui::End();
}

void ModelViewerModule::DrawControls()
{
    if (!handle_)
        return;

    // View controls + creature/item skin switcher.
    camera_.DrawControls();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);
    if (skinOptions_.size() > 1)
    {
        ImGui::SetNextItemWidth(220);
        const char* cur = (curSkin_ >= 0 && curSkin_ < (int)skinOptions_.size())
                              ? skinOptions_[curSkin_].label.c_str() : "(default)";
        if (ImGui::BeginCombo("Skin", cur))
        {
            for (int i = 0; i < (int)skinOptions_.size(); ++i)
                if (ImGui::Selectable(skinOptions_[i].label.c_str(), i == curSkin_))
                    ApplySkin(i);
            ImGui::EndCombo();
        }
    }

    // Character customization + equipment (character models only).
    if (charOpts_.valid)
    {
        ImGui::SeparatorText("Customization");
        bool changed = false;
        auto pick = [&](const char* label, uint32_t& val, uint32_t count) {
            if (count <= 1) return;
            int v = (int)std::min(val, count - 1);
            ImGui::SetNextItemWidth(180);
            if (ImGui::SliderInt(label, &v, 0, (int)count - 1)) { val = (uint32_t)v; changed = true; }
        };
        pick("Skin color", charCustom_.skinColor, charOpts_.skinColors);
        pick("Face", charCustom_.faceVariation, charOpts_.faceVariations);
        pick("Hair style", charCustom_.hairVariation, charOpts_.hairVariations);
        pick("Hair color", charCustom_.hairColor, charOpts_.hairColors);
        pick("Facial hair", charCustom_.facialVariation, charOpts_.facialVariations);
        if (changed)
            RecomputeCharacter();

        // Equipment: one row per appearance slot. Type an item id (item_template.entry) + Set, or
        // use Find to search items that fit the slot; armour composites onto the body + switches
        // geosets, weapons/helm attach to bones. Resolving an id/search needs a DB connection.
        ImGui::SeparatorText("Equipment");
        const bool haveDb = svc_ && svc_->activeDb;
        if (!haveDb)
            ImGui::TextDisabled("Connect a database to look up items.");
        bool openSearchPopup = false;
        for (int s = 0; s < kEquipSlotCount; ++s)
        {
            ImGui::PushID(s);
            ImGui::TextUnformatted(EquipSlotName((EquipSlot)s));
            ImGui::SameLine(90);
            ImGui::SetNextItemWidth(70);
            bool set = ImGui::InputInt("##entry", &slotInput_[s], 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("item_template.entry");
            ImGui::SameLine();
            if (ImGui::SmallButton("Set")) set = true;
            if (set)
                EquipEntryInSlot((EquipSlot)s, slotInput_[s] > 0 ? (uint32_t)slotInput_[s] : 0);
            ImGui::SameLine();
            ImGui::BeginDisabled(!haveDb);
            if (ImGui::SmallButton("Find"))
            {
                searchSlot_ = s;
                searchResults_.clear();
                searchBuf_[0] = '\0';
                openSearchPopup = true;   // open below at the SAME id scope as BeginPopup (not inside PushID(s))
            }
            ImGui::EndDisabled();
            if (slotDisplayId_[s])
            {
                ImGui::SameLine();
                if (ImGui::SmallButton("X")) EquipEntryInSlot((EquipSlot)s, 0);
                ImGui::SameLine();
                if (slotEntry_[s])
                    ImGui::Text("%u  %s", slotEntry_[s], slotName_[s].c_str());
                else
                    ImGui::Text("display %u", slotDisplayId_[s]);   // set directly, no entry
            }
            if (equipErrSlot_ == s && !equipErr_.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", equipErr_.c_str());
            ImGui::PopID();
        }

        // Open the popup here, at the same id-stack level as BeginPopup (opening it inside the
        // per-slot PushID scope above would hash a different id and the popup would never show).
        if (openSearchPopup)
            ImGui::OpenPopup("itemsearch");

        // Item-search popup: world DB item_template by name, filtered to the slot; click to equip.
        if (ImGui::BeginPopup("itemsearch") && searchSlot_ >= 0)
        {
            ImGui::Text("Find %s item", EquipSlotName((EquipSlot)searchSlot_));
            ImGui::SetNextItemWidth(220);
            const bool enter = ImGui::InputText("##q", searchBuf_, sizeof(searchBuf_),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine();
            if (ImGui::Button("Search") || enter)
                RunItemSearch((EquipSlot)searchSlot_);
            ImGui::BeginChild("results", ImVec2(340, 200), true);
            if (searchResults_.empty())
                ImGui::TextDisabled("Type a name and Search.");
            for (const ItemHit& h : searchResults_)
            {
                char lbl[192];
                std::snprintf(lbl, sizeof(lbl), "%s  (%u)##%u", h.name.c_str(), h.entry, h.entry);
                if (ImGui::Selectable(lbl))
                {
                    // Hits are already slot-filtered by RunItemSearch, so equip directly.
                    slotEntry_[searchSlot_] = h.entry;
                    slotDisplayId_[searchSlot_] = h.displayId;
                    slotName_[searchSlot_] = h.name;
                    slotInput_[searchSlot_] = (int)h.entry;
                    equipErr_.clear();
                    equipErrSlot_ = -1;
                    RecomputeCharacter();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }

    // Geoset toggles — 'advanced' for characters (auto-driven by customization + equipment), the
    // primary control for other models. Bounded + scrollable so it never dominates the panel.
    if (!model_.batches.empty() && geosetVisible_.size() == model_.batches.size() &&
        ImGui::CollapsingHeader(charOpts_.valid ? "Geosets (advanced)" : "Geosets"))
    {
        std::map<uint32_t, std::vector<int>> byGeoset;
        for (int i = 0; i < (int)model_.batches.size(); ++i)
            byGeoset[model_.batches[i].submeshId].push_back(i);
        ImGui::BeginChild("geosets", ImVec2(0, 140), true);
        for (auto& kv : byGeoset)
        {
            bool shown = false;
            for (int bi : kv.second)
                if (geosetVisible_[bi]) { shown = true; break; }
            char lbl[40];
            std::snprintf(lbl, sizeof(lbl), "geoset %u (%zu)##gs%u", kv.first, kv.second.size(), kv.first);
            if (ImGui::Checkbox(lbl, &shown))
            {
                for (int bi : kv.second)
                    geosetVisible_[bi] = shown ? 1 : 0;
                ApplyGeosetVisibility();
            }
        }
        ImGui::EndChild();
    }

    // Animation controls.
    const int seqCount = animator_.SequenceCount();
    if (seqCount > 0)
    {
        curAnim_ = std::clamp(curAnim_, 0, seqCount - 1);
        const uint32_t dur = animator_.Duration(curAnim_);
        auto fmtAnim = [&](char* buf, size_t n, int i) {
            const m2::M2Sequence& si = model_.sequences[i];
            auto nameIt = animNames_.find(si.animationId);
            const char* nm = nameIt != animNames_.end() ? nameIt->second.c_str() : nullptr;
            if (nm && si.subAnimationId > 0)
                std::snprintf(buf, n, "%s (var %u) - %ums", nm, si.subAnimationId, animator_.Duration(i));
            else if (nm)
                std::snprintf(buf, n, "%s - %ums", nm, animator_.Duration(i));
            else
                std::snprintf(buf, n, "id=%u var=%u - %ums", si.animationId, si.subAnimationId,
                              animator_.Duration(i));
        };
        ImGui::SeparatorText("Animation");
        ImGui::SetNextItemWidth(-1);
        char label[96];
        fmtAnim(label, sizeof(label), curAnim_);
        if (ImGui::BeginCombo("##anim", label))
        {
            for (int i = 0; i < seqCount; ++i)
            {
                char it[96];
                fmtAnim(it, sizeof(it), i);
                if (ImGui::Selectable(it, i == curAnim_)) { curAnim_ = i; animTimeMs_ = 0.0f; }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button(playing_ ? "Pause" : "Play"))
            playing_ = !playing_;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        float t = animTimeMs_;
        if (ImGui::SliderFloat("##time", &t, 0.0f, dur > 0 ? (float)dur : 1.0f, "%.0f ms"))
        {
            animTimeMs_ = t;
            playing_ = false;
        }
        if (playing_ && dur > 0)
        {
            animTimeMs_ += ImGui::GetIO().DeltaTime * 1000.0f;
            animTimeMs_ = std::fmod(animTimeMs_, (float)dur);
        }
    }

    // Effects toggle (particles/ribbons).
    if (!model_.particleEmitters.empty() || !model_.ribbonEmitters.empty())
    {
        ImGui::Checkbox("Effects", &showEffects_);
        if (showEffects_)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu particles", effects_.LiveParticleCount());
        }
    }
}

void ModelViewerModule::DrawViewportPanel()
{
    if (!ImGui::Begin("Model Viewer"))
    {
        ImGui::End();
        return;
    }

    if (!handle_ || !svc_ || !svc_->renderer)
    {
        ImGui::TextDisabled("No model loaded. Pick one in the Model Browser.");
        ImGui::End();
        return;
    }

    // Controls (camera, skin, customization, equipment, geosets, animation) live in the Model
    // Browser panel (DrawControls); this panel is just the 3D view, filling all available space.
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

    // Ground grid on the model's base plane.
    {
        const float br = std::max(model_.boundsRadius, 0.01f);
        const float ext = br * 2.5f;
        const float gc[3] = {model_.boundsCenter.x, model_.boundsCenter.y, model_.boundsCenter.z - br};
        svc_->renderer->SetGrid(showGrid_, gc, ext, std::max(ext / 20.0f, 1e-4f));
    }

    // Evaluate the skeleton now that we have the camera (billboard bones face it).
    animator_.Evaluate(curAnim_, animTimeMs_, view, boneMatrices_);

    // Per-batch mesh animation (UV transform + color/alpha) for this frame.
    submeshAnims_.resize(model_.batches.size());
    for (size_t i = 0; i < model_.batches.size(); ++i)
    {
        const m2::RenderBatch& b = model_.batches[i];
        glm::mat4 tm = animator_.TextureMatrix(b.textureTransformIndex, curAnim_, animTimeMs_);
        std::memcpy(submeshAnims_[i].texMatrix, &tm[0][0], sizeof(submeshAnims_[i].texMatrix));
        glm::vec4 c = animator_.BatchColor(b.colorIndex, b.textureWeightIndex, curAnim_, animTimeMs_);
        submeshAnims_[i].color[0] = c.r; submeshAnims_[i].color[1] = c.g;
        submeshAnims_[i].color[2] = c.b; submeshAnims_[i].color[3] = c.a;
    }

    // Advance the particle/ribbon simulation and gather its geometry.
    EffectFrame ef;
    if (showEffects_)
    {
        const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
        effects_.Step(curAnim_, animTimeMs_, io.DeltaTime * 1000.0f, boneMatrices_, camRight, camUp, eye);
        ef.verts = effects_.Geometry().verts.data();
        ef.vertCount = (int)effects_.Geometry().verts.size();
        ef.draws = effects_.Geometry().draws.data();
        ef.drawCount = (int)effects_.Geometry().draws.size();
    }

    const float* bones =
        boneMatrices_.empty() ? nullptr : reinterpret_cast<const float*>(boneMatrices_.data());
    const int boneCount = static_cast<int>(boneMatrices_.size());

    ImTextureID tex = 0;
    if (attached_.empty())
    {
        tex = svc_->renderer->RenderModel(handle_, &view[0][0], &proj[0][0], bones, boneCount,
                                          submeshAnims_.data(), (int)submeshAnims_.size(),
                                          showEffects_ ? &ef : nullptr, w, h);
    }
    else
    {
        // Character + attached held items: each item is a live instance whose bone palette is
        // folded with the character bone's world transform + the attachment offset.
        std::vector<SceneInstanceGpu> scene;
        scene.reserve(attached_.size() + 1);
        SceneInstanceGpu ch{};
        ch.handle = handle_;
        ch.boneMatrices = bones;
        ch.boneCount = boneCount;
        ch.submeshAnims = submeshAnims_.data();
        ch.submeshAnimCount = (int)submeshAnims_.size();
        if (showEffects_)
        {
            ch.effectVerts = ef.verts; ch.effectVertCount = ef.vertCount;
            ch.effectDraws = ef.draws; ch.effectDrawCount = ef.drawCount;
        }
        scene.push_back(ch);

        for (auto& ap : attached_)
        {
            AttachedItem& a = *ap;
            glm::mat4 attWorld(1.0f);
            for (const m2::M2Attachment& att : model_.attachments)
                if (att.id == a.attachId)
                {
                    glm::mat4 bm = att.bone < boneMatrices_.size() ? boneMatrices_[att.bone] : glm::mat4(1.0f);
                    attWorld = bm * glm::translate(glm::mat4(1.0f), glm::vec3(att.pos[0], att.pos[1], att.pos[2]));
                    break;
                }
            a.animator.Evaluate(0, 0.0f, view, a.localBones);   // item bind pose
            a.palette.resize(a.localBones.size());
            for (size_t j = 0; j < a.localBones.size(); ++j)
                a.palette[j] = attWorld * a.localBones[j];
            a.subs.resize(a.model.batches.size());
            for (size_t i = 0; i < a.model.batches.size(); ++i)
            {
                const m2::RenderBatch& b = a.model.batches[i];
                glm::mat4 tm = a.animator.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
                std::memcpy(a.subs[i].texMatrix, &tm[0][0], sizeof(a.subs[i].texMatrix));
                glm::vec4 col = a.animator.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
                a.subs[i].color[0] = col.r; a.subs[i].color[1] = col.g;
                a.subs[i].color[2] = col.b; a.subs[i].color[3] = col.a;
            }
            SceneInstanceGpu si{};
            si.handle = a.handle;
            si.boneMatrices = a.palette.empty() ? nullptr : reinterpret_cast<const float*>(a.palette.data());
            si.boneCount = (int)a.palette.size();
            si.submeshAnims = a.subs.empty() ? nullptr : a.subs.data();
            si.submeshAnimCount = (int)a.subs.size();
            scene.push_back(si);
        }
        tex = svc_->renderer->RenderScene(scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], w, h);
    }
    if (tex)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + w, p0.y + h));

    ImGui::End();
}

void ModelViewerModule::FrameCamera()
{
    camera_.Frame(model_.boundsCenter, model_.boundsRadius);
}

void ModelViewerModule::Unload()
{
    ClearAttachments();
    if (handle_ && svc_ && svc_->renderer)
        svc_->renderer->DestroyModel(handle_);
    handle_ = 0;
    model_ = m2::M2Model{};
    loadedName_.clear();
}

void ModelViewerModule::OpenPath(const std::string& m2Path, const std::string* skins)
{
    error_.clear();
    if (!svc_ || !svc_->clientData || !svc_->renderer)
    {
        error_ = "no client data / renderer";
        return;
    }

    m2::M2Model m;
    std::string err;
    if (!m2::Load(*svc_->clientData, m2Path, m, &err))
    {
        error_ = "load failed: " + err;   // keep any currently-loaded model
        return;
    }

    Unload();
    model_ = std::move(m);
    loadedName_ = m2Path;

    // Keep the pristine texture paths so switching skins re-resolves from a clean slate.
    baseTexturePaths_ = model_.texturePaths;

    EnsureDresser();
    charOpts_ = dresser_ ? dresser_->CharacterInfoFor(m2Path) : CharacterOptions{};

    animator_.SetModel(&model_, svc_->clientData, m2Path);
    effects_.SetModel(&model_, &animator_);
    effects_.SetParticleColorOverride({});   // cleared; OpenDisplay re-applies a tint if any
    curAnim_ = 0;
    animTimeMs_ = 0.0f;
    playing_ = true;

    if (charOpts_.valid)
    {
        // Player character: customization pickers drive a composited body + geoset selection,
        // instead of the flat skin dropdown.
        skinOptions_.clear();
        curSkin_ = -1;
        charCustom_ = CharCustomize{};
        for (int s = 0; s < kEquipSlotCount; ++s)
        { slotEntry_[s] = 0; slotDisplayId_[s] = 0; slotName_[s].clear(); slotInput_[s] = 0; }
        equipErr_.clear();
        equipErrSlot_ = -1;
        RecomputeCharacter();   // composites body, applies hair + geosets, and rebuilds
    }
    else
    {
        BuildSkinOptions();
        // Pick the skin: the caller's (from a creature display), else the first available (so a
        // model opened by path/listfile is textured, not white), else the class default.
        curSkin_ = -1;
        bodySlot_ = -1;
        bodyComposite_ = BlpImage{};
        if (skins && dresser_)
        {
            model_.texturePaths = baseTexturePaths_;
            dresser_->ApplyCreatureSkins(m2Path, skins, model_);
            for (size_t i = 0; i < skinOptions_.size(); ++i)
                if (skinOptions_[i].cls == ModelClass::Creature && skinOptions_[i].skins[0] == skins[0] &&
                    skinOptions_[i].skins[1] == skins[1] && skinOptions_[i].skins[2] == skins[2])
                { curSkin_ = (int)i; break; }
        }
        else if (!skinOptions_.empty())
        {
            ApplySkinToModel(0);
            curSkin_ = 0;
        }
        else if (dresser_)
        {
            dresser_->ResolveDefaultTextures(m2Path, model_);
        }
        ComputeDefaultGeosetVisibility();
        if (!Rebuild())
            return;
    }
    FrameCamera();
    if (svc_->setStatus)
        svc_->setStatus("Loaded model: " + m2Path);
}

void ModelViewerModule::EnsureDresser()
{
    if (!dresser_ && svc_ && svc_->clientData && svc_->dbcStore)
        dresser_ = std::make_unique<ModelDresser>(*svc_->clientData, *svc_->dbcStore);
}

void ModelViewerModule::BuildSkinOptions()
{
    skinOptions_.clear();
    if (!dresser_)
        return;
    const ModelClass cls = ClassifyModel(loadedName_);
    std::unordered_set<std::string> seen;
    if (cls == ModelClass::Creature)
    {
        for (const auto& tr : dresser_->CreatureSkinsFor(loadedName_))
        {
            std::string key = tr[0] + "|" + tr[1] + "|" + tr[2];
            if (!seen.insert(key).second)
                continue;
            SkinOption o;
            o.cls = cls;
            o.skins[0] = tr[0]; o.skins[1] = tr[1]; o.skins[2] = tr[2];
            o.label = !tr[0].empty() ? tr[0] : "(default)";
            skinOptions_.push_back(std::move(o));
        }
    }
    else if (cls == ModelClass::Item)
    {
        for (const std::string& s : dresser_->ItemObjectSkinsFor(loadedName_))
        {
            SkinOption o;
            o.cls = cls; o.objectSkin = s; o.label = s;
            skinOptions_.push_back(std::move(o));
        }
    }
    else if (cls == ModelClass::Character)
    {
        for (const auto& c : dresser_->CharacterSkinsFor(loadedName_))
        {
            SkinOption o;
            o.cls = cls; o.charChoice = c; o.label = c.label;
            skinOptions_.push_back(std::move(o));
        }
    }
    std::sort(skinOptions_.begin(), skinOptions_.end(),
              [](const SkinOption& a, const SkinOption& b) { return a.label < b.label; });
}

void ModelViewerModule::ApplySkinToModel(int optionIndex)
{
    if (optionIndex < 0 || optionIndex >= (int)skinOptions_.size() || !dresser_)
        return;
    model_.texturePaths = baseTexturePaths_;   // reset the runtime slots to pristine
    const SkinOption& o = skinOptions_[optionIndex];
    switch (o.cls)
    {
        case ModelClass::Creature: dresser_->ApplyCreatureSkins(loadedName_, o.skins, model_); break;
        case ModelClass::Item:     dresser_->ApplyItemObjectSkin(loadedName_, o.objectSkin, model_); break;
        case ModelClass::Character:
            dresser_->ApplyCharacterTextures(model_, o.charChoice.body, o.charChoice.hair, o.charChoice.extra);
            break;
        default: break;
    }
}

bool ModelViewerModule::Rebuild()
{
    if (handle_ && svc_->renderer)
        svc_->renderer->DestroyModel(handle_);
    handle_ = 0;
    ModelUpload up = m2::BuildUpload(*svc_->clientData, model_);
    // Characters: replace the decoded base-skin body slot with the composited body texture.
    if (charOpts_.valid && bodySlot_ >= 0 && bodyComposite_.valid() && bodySlot_ < (int)up.textures.size())
    {
        up.textures[bodySlot_].rgba = bodyComposite_.rgba;
        up.textures[bodySlot_].w = bodyComposite_.width;
        up.textures[bodySlot_].h = bodyComposite_.height;
    }
    handle_ = svc_->renderer->CreateModel(up);
    if (!handle_)
        error_ = "GPU upload failed";
    else
        ApplyGeosetVisibility();   // a fresh upload defaults to all-visible; restore our toggles
    return handle_ != 0;
}

void ModelViewerModule::RecomputeCharacter()
{
    if (!charOpts_.valid || !dresser_)
        return;
    // Gather equipped items per slot. Armour slots composite onto the body + drive geosets; held
    // slots (a positive attachment id) are attached as separate models by RebuildAttachments.
    std::vector<EquippedItem> equipped;
    for (int s = 0; s < kEquipSlotCount; ++s)
    {
        if (slotDisplayId_[s] == 0) continue;
        DbcStore::ItemDisplay it;
        if (dresser_->ItemDisplayById(slotDisplayId_[s], it) && EquipSlotAttachment((EquipSlot)s) == 0)
            equipped.push_back({(EquipSlot)s, it});
    }
    RebuildAttachments();
    // Reset to pristine, set the body (type 1) + hair (type 6) texture paths, locate the body slot.
    // The rendered body is bodyComposite_ (injected in Rebuild), but the type-1 slot still needs a
    // real path: CharacterGeosets hides any geoset whose runtime texture is unresolved, so an empty
    // type-1 path would hide the whole body (the harness sets it via ResolveDefaultTextures; the
    // viewer must too). It also serves as the decode fallback if the composite is invalid.
    model_.texturePaths = baseTexturePaths_;
    const std::string body = dresser_->CharacterBodyTexture(charOpts_.race, charOpts_.sex, charCustom_);
    const std::string hair = dresser_->CharacterHairTexture(charOpts_.race, charOpts_.sex, charCustom_);
    bodySlot_ = -1;
    for (size_t i = 0; i < model_.texturePaths.size(); ++i)
    {
        uint32_t t = i < model_.textureTypes.size() ? model_.textureTypes[i] : 0;
        if (t == 1 && !body.empty()) model_.texturePaths[i] = body;
        if (t == 6 && !hair.empty()) model_.texturePaths[i] = hair;
        if (t == 1) bodySlot_ = (int)i;
    }
    bodyComposite_ = dresser_->ComposeCharacterBody(charOpts_.race, charOpts_.sex, charCustom_, equipped);
    dresser_->CharacterGeosets(charOpts_.race, charOpts_.sex, charCustom_, model_, geosetVisible_, equipped);
    Rebuild();
}

void ModelViewerModule::RunItemSearch(EquipSlot slot)
{
    searchResults_.clear();
    if (!svc_ || !svc_->activeDb)
        return;
    const std::string types = EquipSlotInvTypes(slot);
    if (types.empty())
        return;
    const std::string q = svc_->activeDb->EscapeString(searchBuf_);
    const std::string sql =
        "SELECT displayid, entry, name FROM item_template WHERE InventoryType IN (" + types +
        ") AND displayid > 0 AND name LIKE '%" + q + "%' ORDER BY name LIMIT 100";
    DbError err;
    auto rs = svc_->activeDb->Query(sql, err);
    if (!rs)
        return;
    while (rs->Next())
        searchResults_.push_back({rs->GetUInt32(0), rs->GetUInt32(1), rs->GetString(2)});
}

bool ModelViewerModule::ResolveItemEntry(EquipSlot slot, uint32_t entry, uint32_t& displayOut,
                                         std::string& nameOut, std::string& errOut)
{
    displayOut = 0;
    nameOut.clear();
    errOut.clear();
    if (!svc_ || !svc_->activeDb)
    {
        errOut = "Connect a database to look up items.";
        return false;
    }
    DbError err;
    auto rs = svc_->activeDb->Query(
        "SELECT displayid, name, InventoryType FROM item_template WHERE entry = " +
            std::to_string(entry) + " LIMIT 1",
        err);
    if (!rs || !rs->Next())
    {
        errOut = "No item with id " + std::to_string(entry);
        return false;
    }
    const uint32_t display = rs->GetUInt32(0);
    std::string name = rs->GetString(1);
    const uint32_t invType = rs->GetUInt32(2);

    // The item must fit this appearance slot (same InventoryType set the Find search filters on).
    bool fits = false;
    const std::string want = std::to_string(invType);
    const std::string types = EquipSlotInvTypes(slot);
    for (size_t i = 0; i < types.size();)
    {
        size_t j = types.find(',', i);
        if (j == std::string::npos) j = types.size();
        if (types.compare(i, j - i, want) == 0) { fits = true; break; }
        i = j + 1;
    }
    if (!fits)
    {
        errOut = (name.empty() ? std::string("That item") : name) +
                 " doesn't fit the " + EquipSlotName(slot) + " slot";
        return false;
    }
    if (display == 0)
    {
        errOut = (name.empty() ? std::string("That item") : name) + " has no display model";
        return false;
    }
    displayOut = display;
    nameOut = std::move(name);
    return true;
}

void ModelViewerModule::EquipEntryInSlot(EquipSlot slot, uint32_t entry)
{
    const int s = (int)slot;
    equipErr_.clear();
    equipErrSlot_ = -1;
    if (entry == 0)   // clear the slot
    {
        slotEntry_[s] = 0;
        slotDisplayId_[s] = 0;
        slotName_[s].clear();
        slotInput_[s] = 0;
        RecomputeCharacter();
        return;
    }
    uint32_t display = 0;
    std::string name, err;
    if (!ResolveItemEntry(slot, entry, display, name, err))
    {
        equipErr_ = err;         // leave any currently-equipped item untouched
        equipErrSlot_ = s;
        return;
    }
    slotEntry_[s] = entry;
    slotDisplayId_[s] = display;
    slotName_[s] = std::move(name);
    slotInput_[s] = (int)entry;
    RecomputeCharacter();
}

void ModelViewerModule::ClearAttachments()
{
    for (auto& a : attached_)
        if (a->handle && svc_ && svc_->renderer)
            svc_->renderer->DestroyModel(a->handle);
    attached_.clear();
}

void ModelViewerModule::RebuildAttachments()
{
    ClearAttachments();
    if (!charOpts_.valid || !dresser_ || !svc_ || !svc_->clientData || !svc_->renderer)
        return;
    // Item\ObjectComponents\<Component>\ folder per attachment slot (where the held model lives).
    auto componentFolder = [](EquipSlot s) -> const char* {
        switch (s)
        {
            case EquipSlot::Head: return "Head";
            case EquipSlot::Shoulder: return "Shoulder";
            case EquipSlot::OffHand: return "Shield";   // off-hand is often a shield
            default: return "Weapon";                    // main hand / ranged
        }
    };
    // Load one held/worn model and attach it to `attachId`. Tries the slot's component folder, then
    // falls back to Weapon (some client data is inconsistent). modelName/objectSkin are the specific
    // ItemDisplayInfo slot (index 0 or 1) to load.
    auto add = [&](uint32_t displayId, uint32_t attachId, EquipSlot slot, const std::string& modelName,
                   const std::string& objectSkin) {
        if (attachId == 0 || modelName.empty())
            return;
        std::string path = std::string("Item\\ObjectComponents\\") + componentFolder(slot) + "\\" + modelName;
        auto a = std::make_unique<AttachedItem>();
        a->displayId = displayId;
        a->attachId = attachId;
        if (!m2::Load(*svc_->clientData, path, a->model, nullptr))
        {
            path = "Item\\ObjectComponents\\Weapon\\" + modelName;
            if (!m2::Load(*svc_->clientData, path, a->model, nullptr))
                return;
        }
        dresser_->ApplyItemObjectSkin(path, objectSkin, a->model);   // type-2 object skin
        a->animator.SetModel(&a->model, svc_->clientData, path);
        ModelUpload up = m2::BuildUpload(*svc_->clientData, a->model);
        a->handle = svc_->renderer->CreateModel(up);
        if (!a->handle)
            return;
        attached_.push_back(std::move(a));
    };
    for (int s = 0; s < kEquipSlotCount; ++s)
    {
        const EquipSlot slot = (EquipSlot)s;
        const uint32_t attachId = EquipSlotAttachment(slot);
        if (attachId == 0 || slotDisplayId_[s] == 0)
            continue;
        DbcStore::ItemDisplay it;
        if (!dresser_->ItemDisplayById(slotDisplayId_[s], it))
            continue;
        add(slotDisplayId_[s], attachId, slot, it.modelName[0], it.modelTexture[0]);
        // Shoulders carry a SECOND model (the left pad) that hangs from the left-shoulder attachment
        // (id 6); modelName[0]/[1] are the right/left pads. Without this only one shoulder shows.
        if (slot == EquipSlot::Shoulder && !it.modelName[1].empty())
            add(slotDisplayId_[s], 6 /* ShoulderLeft */, slot, it.modelName[1], it.modelTexture[1]);
    }
}

void ModelViewerModule::ComputeDefaultGeosetVisibility()
{
    // Default: show everything, except a submesh whose runtime (non-zero type) texture never
    // resolved to a file — it would render as a white patch (e.g. an unequipped cape's geoset,
    // which references the empty type-2 object-skin slot). Proper hair/facial/equipment geoset
    // selection lands in the customization/equipment phases.
    geosetVisible_.assign(model_.batches.size(), 1);
    for (size_t i = 0; i < model_.batches.size(); ++i)
    {
        int ti = model_.batches[i].textureIndex;
        uint32_t type = (ti >= 0 && ti < (int)model_.textureTypes.size()) ? model_.textureTypes[ti] : 0;
        const bool unresolved = ti < 0 || ti >= (int)model_.texturePaths.size() || model_.texturePaths[ti].empty();
        if (type != 0 && unresolved)
            geosetVisible_[i] = 0;
    }
}

void ModelViewerModule::ApplyGeosetVisibility()
{
    if (handle_ && svc_ && svc_->renderer && !geosetVisible_.empty())
        svc_->renderer->SetSubmeshVisibility(handle_, geosetVisible_.data(), (int)geosetVisible_.size());
}

void ModelViewerModule::ApplySkin(int optionIndex)
{
    if (optionIndex < 0 || optionIndex >= (int)skinOptions_.size())
        return;
    ApplySkinToModel(optionIndex);
    curSkin_ = optionIndex;
    Rebuild();   // keep camera + animation state
}

void ModelViewerModule::OpenDisplay(uint32_t displayId)
{
    error_.clear();
    auto it = displays_.find(displayId);
    if (it == displays_.end())
    {
        error_ = "unknown display id " + std::to_string(displayId);
        return;
    }
    auto mit = modelPaths_.find(it->second.modelId);
    if (mit == modelPaths_.end())
    {
        error_ = "display " + std::to_string(displayId) + " -> model " +
                 std::to_string(it->second.modelId) + " not found";
        return;
    }
    OpenPath(mit->second, it->second.skins);

    // Apply the display's ParticleColor.dbc tint (emitters with particleColorIndex 11/12/13).
    if (it->second.particleColorId)
    {
        auto cit = particleColors_.find(it->second.particleColorId);
        if (cit != particleColors_.end())
        {
            m2::M2EffectSystem::ParticleColorRamps r;
            auto toRgb = [](uint32_t v) {
                return glm::vec3(((v >> 16) & 0xFF), ((v >> 8) & 0xFF), (v & 0xFF)) * (1.0f / 255.0f);
            };
            for (int k = 0; k < 3; ++k)
                for (int p = 0; p < 3; ++p)
                    r.c[k][p] = toRgb(cit->second.bgra[k][p]);
            r.present = true;
            effects_.SetParticleColorOverride(r);
        }
    }
}
} // namespace we
