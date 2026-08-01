// WmoViewerModule — see WmoViewerModule.h.

#include "editors/wmo/WmoViewerModule.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoUploadBuild.h"

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

// A WMO group file ("Foo_000.wmo".."Foo_NNN.wmo"), which the browser hides.
bool IsGroupFile(const std::string& path)
{
    if (path.size() < 8)
        return false;
    const char* p = path.c_str() + path.size() - 8;   // "_NNN.wmo"
    return p[0] == '_' && std::isdigit((unsigned char)p[1]) && std::isdigit((unsigned char)p[2]) &&
           std::isdigit((unsigned char)p[3]);
}
} // namespace

void WmoViewerModule::OnClientDataLoaded()
{
    listLoaded_ = false;   // reload lazily against the new client data
    wmoList_.clear();
}

void WmoViewerModule::DrawPanels()
{
    if (!listLoaded_ && svc_ && svc_->clientData && svc_->clientData->IsOpen())
    {
        wmoList_.clear();
        for (std::string& p : svc_->clientData->ListFiles(".wmo"))
            if (!IsGroupFile(p))
                wmoList_.push_back(std::move(p));
        std::sort(wmoList_.begin(), wmoList_.end());
        listLoaded_ = true;
    }

    DrawBrowserPanel();
    DrawViewportPanel();
}

void WmoViewerModule::DrawBrowserPanel()
{
    if (!ImGui::Begin("WMO Browser"))
    {
        ImGui::End();
        return;
    }

    if (!svc_ || !svc_->clientData || !svc_->clientData->IsOpen())
    {
        ImGui::TextWrapped("Load WoW client data (File menu) to browse WMOs.");
        ImGui::End();
        return;
    }
    if (!svc_->renderer)
    {
        ImGui::TextWrapped("Renderer unavailable.");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Files");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "search .wmo files", search_, sizeof(search_));
    if (wmoList_.empty())
        ImGui::TextDisabled("(no (listfile) in this client — use the path box)");
    else
    {
        ImGui::BeginChild("##wmolist", ImVec2(0, 280), true);
        int shown = 0;
        constexpr int kMax = 400;
        for (const std::string& path : wmoList_)
        {
            if (!ContainsNoCase(path, search_))
                continue;
            if (shown++ >= kMax)
                break;
            if (ImGui::Selectable(path.c_str(), path == loadedName_))
                OpenPath(path);
        }
        if (shown >= kMax)
            ImGui::TextDisabled("... more matches; refine the search");
        ImGui::EndChild();
    }

    ImGui::SeparatorText("Path");
    ImGui::SetNextItemWidth(-90);
    ImGui::InputTextWithHint("##path", "World\\wmo\\...\\Foo.wmo", pathBuf_, sizeof(pathBuf_));
    ImGui::SameLine();
    if (ImGui::Button("Load path") && pathBuf_[0])
        OpenPath(pathBuf_);

    if (!error_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());

    ImGui::End();
}

void WmoViewerModule::DrawViewportPanel()
{
    if (!ImGui::Begin("WMO Viewer"))
    {
        ImGui::End();
        return;
    }

    if (!handle_ || !svc_ || !svc_->renderer)
    {
        ImGui::TextDisabled("No WMO loaded. Pick one in the WMO Browser.");
        ImGui::End();
        return;
    }

    // --- render options ---
    camera_.DrawControls();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Doodads", &opt_.doodads))   // re-bakes the model
        LoadModel(false);
    ImGui::SameLine();
    if (ImGui::Checkbox("Liquid", &opt_.liquid))
        LoadModel(false);

    if (opt_.doodads && model_.doodadSets.size() > 1)
    {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        char cur[64];
        if (opt_.doodadSet < 0)
            std::snprintf(cur, sizeof(cur), "(auto)");
        else if (opt_.doodadSet < (int)model_.doodadSets.size())
            std::snprintf(cur, sizeof(cur), "%s", model_.doodadSets[opt_.doodadSet].name.c_str());
        else
            std::snprintf(cur, sizeof(cur), "set %d", opt_.doodadSet);
        if (ImGui::BeginCombo("Doodad set", cur))
        {
            if (ImGui::Selectable("(auto)", opt_.doodadSet < 0)) { opt_.doodadSet = -1; LoadModel(false); }
            for (int i = 0; i < (int)model_.doodadSets.size(); ++i)
            {
                char it[80];
                std::snprintf(it, sizeof(it), "%s (%u)", model_.doodadSets[i].name.c_str(),
                              model_.doodadSets[i].count);
                if (ImGui::Selectable(it, i == opt_.doodadSet)) { opt_.doodadSet = i; LoadModel(false); }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::TextDisabled("%d groups, %zu verts, %zu batches, %zu doodads%s", model_.groupCount,
                        model_.vertices.size(), model_.submeshes.size(), doodadInsts_.size(),
                        model_.liquidTileCount ? "  (+liquid)" : "");

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

    // Liquid surfaces cycle through their DBC animation frames (flowing water/lava).
    const SubmeshAnim* anims = nullptr;
    int animCount = 0;
    if (hasLiquid_ && !model_.submeshes.empty())
    {
        liquidTime_ += io.DeltaTime;
        submeshAnims_.assign(model_.submeshes.size(), SubmeshAnim{});
        const int frame = (int)(liquidTime_ * 18.0f);   // ~18 fps liquid animation
        for (size_t i = 0; i < model_.submeshes.size(); ++i)
        {
            const wmo::WmoSubmesh& sm = model_.submeshes[i];
            if (sm.isLiquid && sm.liquidFrameBase >= 0 && sm.liquidFrameCount > 0)
                submeshAnims_[i].textureOverride = sm.liquidFrameBase + (frame % sm.liquidFrameCount);
        }
        anims = submeshAnims_.data();
        animCount = (int)submeshAnims_.size();
    }

    ImTextureID tex = 0;
    if (!doodadInsts_.empty())
    {
        // Scene render: WMO shell + live animated doodad instances (each with its own
        // folded palette + particle/ribbon effects).
        doodadTime_ += io.DeltaTime * 1000.0f;
        const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

        // Frustum-cull instances; only animate models that have a visible instance. The
        // margin widens the cull frustum so doodads (whose effects/billboards can exceed
        // their bounding sphere) don't pop out at the screen edges.
        const glm::mat4 vp = proj * view;
        const float cullMargin = std::max(model_.boundsRadius * 0.06f, 4.0f);
        std::vector<char> visible(doodadInsts_.size(), 0);
        std::vector<char> uniqueVisible(doodadModels_.size(), 0);
        int shown = 0;
        for (size_t i = 0; i < doodadInsts_.size(); ++i)
            if (SphereInFrustum(vp, doodadInsts_[i].cullCenter, doodadInsts_[i].cullRadius, cullMargin))
            {
                visible[i] = 1;
                uniqueVisible[doodadInsts_[i].model] = 1;
                ++shown;
            }

        for (size_t u = 0; u < doodadModels_.size(); ++u)   // animate visible models once
        {
            if (!uniqueVisible[u])
                continue;
            DoodadModel* dm = doodadModels_[u].get();
            float dur = (float)dm->animator.Duration(dm->animIndex);
            float t = dur > 0 ? std::fmod(doodadTime_, dur) : 0.0f;
            dm->animator.Evaluate(dm->animIndex, t, view, dm->localBones);
            dm->submeshAnims.resize(dm->model.batches.size());
            for (size_t i = 0; i < dm->model.batches.size(); ++i)   // per-batch material anim
            {
                const m2::RenderBatch& b = dm->model.batches[i];
                glm::mat4 tmx = dm->animator.TextureMatrix(b.textureTransformIndex, dm->animIndex, t);
                std::memcpy(dm->submeshAnims[i].texMatrix, &tmx[0][0], sizeof(dm->submeshAnims[i].texMatrix));
                glm::vec4 col = dm->animator.BatchColor(b.colorIndex, b.textureWeightIndex, dm->animIndex, t);
                dm->submeshAnims[i].color[0] = col.r; dm->submeshAnims[i].color[1] = col.g;
                dm->submeshAnims[i].color[2] = col.b; dm->submeshAnims[i].color[3] = col.a;
            }
        }

        std::vector<SceneInstanceGpu> scene;
        scene.reserve(shown + 1);
        SceneInstanceGpu shell{};
        shell.handle = handle_;
        shell.submeshAnims = anims;
        shell.submeshAnimCount = animCount;
        scene.push_back(shell);

        for (size_t i = 0; i < doodadInsts_.size(); ++i)
        {
            if (!visible[i])
                continue;
            DoodadInst& di = doodadInsts_[i];
            DoodadModel* dm = doodadModels_[di.model].get();
            di.palette.resize(dm->localBones.size());
            for (size_t j = 0; j < dm->localBones.size(); ++j)
                di.palette[j] = di.transform * dm->localBones[j];   // fold instance transform

            SceneInstanceGpu si{};
            si.handle = dm->handle;
            si.boneMatrices = di.palette.empty() ? nullptr : reinterpret_cast<const float*>(di.palette.data());
            si.boneCount = (int)di.palette.size();
            si.submeshAnims = dm->submeshAnims.empty() ? nullptr : dm->submeshAnims.data();
            si.submeshAnimCount = (int)dm->submeshAnims.size();
            if (di.effects)
            {
                float dur = (float)dm->animator.Duration(dm->animIndex);
                float t = dur > 0 ? std::fmod(doodadTime_, dur) : 0.0f;
                di.effects->Step(dm->animIndex, t, io.DeltaTime * 1000.0f, di.palette, camRight, camUp, eye);
                const m2::EffectGeometry& g = di.effects->Geometry();
                si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
                si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
            }
            si.worldOrigin[0] = di.origin.x; si.worldOrigin[1] = di.origin.y; si.worldOrigin[2] = di.origin.z;
            scene.push_back(si);
        }
        tex = svc_->renderer->RenderScene(scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], w, h);
    }
    else
    {
        tex = svc_->renderer->RenderModel(handle_, &view[0][0], &proj[0][0], nullptr, 0,
                                          anims, animCount, nullptr, w, h);
    }
    if (tex)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + w, p0.y + h));

    ImGui::End();
}

void WmoViewerModule::OpenPath(const std::string& wmoPath)
{
    loadedPath_ = wmoPath;
    if (LoadModel(true))
    {
        loadedName_ = wmoPath;
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Loaded WMO: " + wmoPath);
    }
}

bool WmoViewerModule::LoadModel(bool frameCamera)
{
    if (!svc_ || !svc_->clientData || !svc_->renderer || loadedPath_.empty())
        return false;

    if (!liquidTypesLoaded_ && svc_->dbcStore)
    {
        liquidTypes_ = svc_->dbcStore->LoadLiquidTypes(*svc_->clientData);
        liquidTypesLoaded_ = true;
    }

    wmo::WmoModel m;
    std::string err;
    if (!wmo::Load(*svc_->clientData, loadedPath_, m, opt_, &liquidTypes_, &err))
    {
        error_ = err;   // keep the current model on failure
        return false;
    }
    error_.clear();
    model_ = std::move(m);

    hasLiquid_ = false;
    for (const wmo::WmoSubmesh& s : model_.submeshes)
        if (s.isLiquid) { hasLiquid_ = true; break; }

    ClearDoodads();
    if (handle_)
        svc_->renderer->DestroyModel(handle_);
    ModelUpload up = wmo::BuildUpload(*svc_->clientData, model_);
    handle_ = svc_->renderer->CreateModel(up);
    if (!handle_)
    {
        error_ = "GPU upload failed";
        return false;
    }
    BuildDoodads();
    if (frameCamera)
        FrameCamera();
    return true;
}

void WmoViewerModule::ClearDoodads()
{
    if (svc_ && svc_->renderer)
        for (auto& dm : doodadModels_)
            if (dm->handle) svc_->renderer->DestroyModel(dm->handle);
    doodadModels_.clear();
    doodadInsts_.clear();
    doodadTime_ = 0.0f;
}

void WmoViewerModule::BuildDoodads()
{
    if (!opt_.doodads || model_.doodadInstances.empty())
        return;

    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::unordered_map<std::string, int> pathToModel;   // -1 = known bad

    for (const wmo::WmoDoodadInstance& inst : model_.doodadInstances)
    {
        const std::string key = lower(inst.m2Path);
        int mi;
        auto it = pathToModel.find(key);
        if (it == pathToModel.end())
        {
            auto dm = std::make_unique<DoodadModel>();
            std::string err;
            if (!m2::Load(*svc_->clientData, inst.m2Path, dm->model, &err))
            {
                pathToModel[key] = -1;
                continue;
            }
            dm->animator.SetModel(&dm->model, svc_->clientData, inst.m2Path);
            dm->animIndex = 0;   // default (stand) animation
            dm->hasEmitters = !dm->model.particleEmitters.empty() || !dm->model.ribbonEmitters.empty();
            ModelUpload up = m2::BuildUpload(*svc_->clientData, dm->model);
            dm->handle = svc_->renderer->CreateModel(up);
            if (!dm->handle)
            {
                pathToModel[key] = -1;
                continue;
            }
            mi = (int)doodadModels_.size();
            pathToModel[key] = mi;
            doodadModels_.push_back(std::move(dm));
        }
        else
        {
            mi = it->second;
            if (mi < 0) continue;
        }

        DoodadInst di;
        di.model = mi;
        std::memcpy(&di.transform[0][0], inst.transform, sizeof(inst.transform));
        di.origin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
        // World bounding sphere for frustum culling.
        const m2::M2Model& mm = doodadModels_[mi]->model;
        di.cullCenter = glm::vec3(di.transform * glm::vec4(mm.boundsCenter, 1.0f));
        di.cullRadius = mm.boundsRadius * glm::length(glm::vec3(di.transform[0])) + 0.01f;
        std::memcpy(di.color, inst.color, 4);
        if (doodadModels_[mi]->hasEmitters)
        {
            di.effects = std::make_unique<m2::M2EffectSystem>();
            di.effects->SetModel(&doodadModels_[mi]->model, &doodadModels_[mi]->animator);
        }
        doodadInsts_.push_back(std::move(di));
    }
}

void WmoViewerModule::FrameCamera()
{
    camera_.Frame(model_.boundsCenter, model_.boundsRadius);
}
} // namespace we
