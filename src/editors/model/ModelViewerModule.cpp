// ModelViewerModule — see ModelViewerModule.h.

#include "editors/model/ModelViewerModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_set>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"

namespace we
{
namespace
{
std::string DirOf(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
}

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
            modelPaths_ = svc_->dbcStore->LoadCreatureModelPaths(*svc_->clientData);
            displays_ = svc_->dbcStore->LoadCreatureDisplays(*svc_->clientData);
            animNames_ = svc_->dbcStore->LoadAnimationNames(*svc_->clientData);
            // Reverse maps for skin resolution: path -> modelId, modelId -> its displays.
            pathToModelId_.clear();
            for (const auto& kv : modelPaths_)
            {
                std::string low = kv.second;
                for (char& c : low) c = (char)std::tolower((unsigned char)c);
                pathToModelId_[low] = kv.first;
            }
            modelToDisplays_.clear();
            for (const auto& kv : displays_)
                modelToDisplays_[kv.second.modelId].push_back(kv.first);
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
        ImGui::TextDisabled("(no (listfile) in this client — use path or display ID)");
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

    // --- manual path ---
    ImGui::SeparatorText("Path");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##path", "Creature\\Rat\\Rat.m2", pathBuf_, sizeof(pathBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Load path") && pathBuf_[0])
        OpenPath(pathBuf_, nullptr);

    if (!error_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());

    ImGui::End();
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

    // --- camera + grid ---
    camera_.DrawControls();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);

    // --- skin (texture variation) switcher ---
    if (skinOptions_.size() > 1)
    {
        ImGui::SetNextItemWidth(220);
        const char* cur = (curSkin_ >= 0 && curSkin_ < (int)skinOptions_.size())
                              ? skinOptions_[curSkin_].label.c_str()
                              : "(default)";
        if (ImGui::BeginCombo("Skin", cur))
        {
            for (int i = 0; i < (int)skinOptions_.size(); ++i)
                if (ImGui::Selectable(skinOptions_[i].label.c_str(), i == curSkin_))
                    ApplySkin(i);
            ImGui::EndCombo();
        }
    }

    // --- animation controls ---
    const int seqCount = animator_.SequenceCount();
    if (seqCount > 0)
    {
        curAnim_ = std::clamp(curAnim_, 0, seqCount - 1);
        const uint32_t dur = animator_.Duration(curAnim_);

        // Resolve M2Sequence.animationId -> AnimationData.dbc name ("Stand", "Walk", ...);
        // a variation index >0 disambiguates repeats of the same animation.
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

        ImGui::SetNextItemWidth(220);
        char label[96];
        fmtAnim(label, sizeof(label), curAnim_);
        (void)dur;
        if (ImGui::BeginCombo("##anim", label))
        {
            for (int i = 0; i < seqCount; ++i)
            {
                char it[96];
                fmtAnim(it, sizeof(it), i);
                if (ImGui::Selectable(it, i == curAnim_))
                {
                    curAnim_ = i;
                    animTimeMs_ = 0.0f;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
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
    ImTextureID tex = svc_->renderer->RenderModel(handle_, &view[0][0], &proj[0][0], bones, boneCount,
                                                  submeshAnims_.data(), (int)submeshAnims_.size(),
                                                  showEffects_ ? &ef : nullptr, w, h);
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

    // Resolve modelId and gather the skin variations available for this model.
    std::string low = m2Path;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    auto mit = pathToModelId_.find(low);
    loadedModelId_ = (mit != pathToModelId_.end()) ? mit->second : 0;
    BuildSkinOptions();

    // Pick the skin: the caller's (from a display), else the model's first available skin
    // (so creatures opened by path/listfile are textured, not white), else none.
    curSkin_ = -1;
    if (skins)
    {
        ApplyRuntimeSkins(skins);
        for (size_t i = 0; i < skinOptions_.size(); ++i)
            if (skinOptions_[i].skins[0] == skins[0] && skinOptions_[i].skins[1] == skins[1] &&
                skinOptions_[i].skins[2] == skins[2])
            { curSkin_ = (int)i; break; }
    }
    else if (!skinOptions_.empty())
    {
        ApplyRuntimeSkins(skinOptions_[0].skins);
        curSkin_ = 0;
    }

    animator_.SetModel(&model_, svc_->clientData, m2Path);
    effects_.SetModel(&model_, &animator_);
    curAnim_ = 0;
    animTimeMs_ = 0.0f;
    playing_ = true;

    if (!Rebuild())
        return;
    FrameCamera();
    if (svc_->setStatus)
        svc_->setStatus("Loaded model: " + m2Path);
}

void ModelViewerModule::BuildSkinOptions()
{
    skinOptions_.clear();
    auto it = modelToDisplays_.find(loadedModelId_);
    if (loadedModelId_ == 0 || it == modelToDisplays_.end())
        return;
    std::unordered_set<std::string> seen;
    for (uint32_t displayId : it->second)
    {
        auto d = displays_.find(displayId);
        if (d == displays_.end())
            continue;
        const auto& sk = d->second.skins;
        std::string key = sk[0] + "|" + sk[1] + "|" + sk[2];
        if (!seen.insert(key).second)
            continue;
        SkinOption o;
        o.skins[0] = sk[0]; o.skins[1] = sk[1]; o.skins[2] = sk[2];
        o.label = !sk[0].empty() ? sk[0] : ("display " + std::to_string(displayId));
        skinOptions_.push_back(std::move(o));
    }
    std::sort(skinOptions_.begin(), skinOptions_.end(),
              [](const SkinOption& a, const SkinOption& b) { return a.label < b.label; });
}

void ModelViewerModule::ApplyRuntimeSkins(const std::string skins[3])
{
    const std::string dir = DirOf(loadedName_);
    for (size_t i = 0; i < model_.texturePaths.size(); ++i)
    {
        uint32_t t = i < model_.textureTypes.size() ? model_.textureTypes[i] : 0;
        if (t >= 11 && t <= 13)   // monster skin 1/2/3 -> texture variation
            model_.texturePaths[i] = skins[t - 11].empty() ? std::string{} : (dir + "\\" + skins[t - 11] + ".blp");
    }
}

bool ModelViewerModule::Rebuild()
{
    if (handle_ && svc_->renderer)
        svc_->renderer->DestroyModel(handle_);
    handle_ = 0;
    ModelUpload up = m2::BuildUpload(*svc_->clientData, model_);
    handle_ = svc_->renderer->CreateModel(up);
    if (!handle_)
        error_ = "GPU upload failed";
    return handle_ != 0;
}

void ModelViewerModule::ApplySkin(int optionIndex)
{
    if (optionIndex < 0 || optionIndex >= (int)skinOptions_.size())
        return;
    ApplyRuntimeSkins(skinOptions_[optionIndex].skins);
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
}
} // namespace we
