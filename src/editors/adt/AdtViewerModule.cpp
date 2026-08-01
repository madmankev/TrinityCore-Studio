// AdtViewerModule — see AdtViewerModule.h.

#include "editors/adt/AdtViewerModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoUploadBuild.h"
#include "adt/AdtUploadBuild.h"

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
    tiles_.clear();
    selectedMap_ = -1;
}

void AdtViewerModule::DrawPanels()
{
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
        ImGui::TextWrapped("Load WoW client data (File menu) to browse map tiles.");
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
    const char* cur = (selectedMap_ >= 0 && selectedMap_ < (int)mapList_.size())
                          ? mapList_[selectedMap_].second.c_str() : "(select a map)";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##map", cur))
    {
        for (int i = 0; i < (int)mapList_.size(); ++i)
            if (ImGui::Selectable(mapList_[i].second.c_str(), i == selectedMap_))
            {
                selectedMap_ = i;
                selectedMapDir_ = maps_[mapList_[i].first].directory;
                tiles_ = adt::ListTiles(*svc_->clientData, selectedMapDir_);
                std::sort(tiles_.begin(), tiles_.end());
            }
        ImGui::EndCombo();
    }

    if (selectedMap_ >= 0)
    {
        ImGui::SeparatorText("Tiles");
        ImGui::TextDisabled("%zu tiles in %s", tiles_.size(), selectedMapDir_.c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##tsearch", "filter e.g. 32_48", search_, sizeof(search_));
        ImGui::BeginChild("##tilelist", ImVec2(0, 320), true);
        char label[32];
        for (const auto& t : tiles_)
        {
            std::snprintf(label, sizeof(label), "%d_%d", t.first, t.second);
            if (search_[0] && !std::strstr(label, search_))
                continue;
            const bool sel = (t.first == loadedX_ && t.second == loadedY_ && !loadedDir_.empty());
            if (ImGui::Selectable(label, sel))
                OpenTile(selectedMapDir_, t.first, t.second);
        }
        ImGui::EndChild();
    }

    if (!error_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", error_.c_str());
    ImGui::End();
}

void AdtViewerModule::DrawViewportPanel()
{
    if (!ImGui::Begin("ADT Viewer"))
    {
        ImGui::End();
        return;
    }
    if (!terrainHandle_ || !svc_ || !svc_->renderer)
    {
        ImGui::TextDisabled("No tile loaded. Pick a map + tile in the ADT Browser.");
        ImGui::End();
        return;
    }

    camera_.DrawControls();
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &showGrid_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Doodads", &opt_.doodads)) LoadTile(false);
    ImGui::SameLine();
    if (ImGui::Checkbox("WMOs", &opt_.wmos)) LoadTile(false);
    ImGui::SameLine();
    if (ImGui::Checkbox("Liquid", &opt_.liquid)) LoadTile(false);

    ImGui::TextDisabled("%d chunks, %zu placements, %d liquid tiles%s", tile_.renderedChunks,
                        insts_.size(), tile_.liquidTileCount, tile_.mclqChunks ? "  (MCLQ present)" : "");

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

    // Ground grid on the tile's base plane.
    {
        const float br = std::max(tile_.boundsRadius, 0.01f);
        const float ext = br * 2.2f;
        const float gc[3] = {tile_.boundsCenter.x, tile_.boundsCenter.y, tile_.boundsCenter.z - br};
        svc_->renderer->SetGrid(showGrid_, gc, ext, std::max(ext / 20.0f, 1e-4f));
    }

    // Assemble the scene: liquid surface (frame-animated) + visible placement instances.
    std::vector<SceneInstanceGpu> scene;
    scene.reserve(insts_.size() + 1);

    if (liquidHandle_ && !tile_.liquidSubmeshes.empty())
    {
        liquidTime_ += io.DeltaTime;
        liquidAnims_.assign(tile_.liquidSubmeshes.size(), SubmeshAnim{});
        const int frame = (int)(liquidTime_ * 18.0f);
        for (size_t i = 0; i < tile_.liquidSubmeshes.size(); ++i)
        {
            const adt::AdtSubmesh& sm = tile_.liquidSubmeshes[i];
            if (sm.isLiquid && sm.liquidFrameBase >= 0 && sm.liquidFrameCount > 0)
                liquidAnims_[i].textureOverride = sm.liquidFrameBase + (frame % sm.liquidFrameCount);
        }
        SceneInstanceGpu si{};
        si.handle = liquidHandle_;
        si.submeshAnims = liquidAnims_.data();
        si.submeshAnimCount = (int)liquidAnims_.size();
        scene.push_back(si);
    }

    if (!insts_.empty())
    {
        doodadTime_ += io.DeltaTime * 1000.0f;
        const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
        const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);
        const glm::mat4 vp = proj * view;
        const float cullMargin = std::max(tile_.boundsRadius * 0.04f, 4.0f);

        std::vector<char> visible(insts_.size(), 0);
        std::vector<char> uniqueVisible(models_.size(), 0);
        for (size_t i = 0; i < insts_.size(); ++i)
            if (SphereInFrustum(vp, insts_[i].cullCenter, insts_[i].cullRadius, cullMargin))
            {
                visible[i] = 1;
                uniqueVisible[insts_[i].model] = 1;
            }

        for (size_t u = 0; u < models_.size(); ++u)   // animate visible M2 models once
        {
            UModel* um = models_[u].get();
            if (!uniqueVisible[u] || um->isWmo)
                continue;
            float dur = (float)um->animator.Duration(um->animIndex);
            float t = dur > 0 ? std::fmod(doodadTime_, dur) : 0.0f;
            um->animator.Evaluate(um->animIndex, t, view, um->localBones);
            um->submeshAnims.resize(um->m2.batches.size());
            for (size_t i = 0; i < um->m2.batches.size(); ++i)
            {
                const m2::RenderBatch& b = um->m2.batches[i];
                glm::mat4 tmx = um->animator.TextureMatrix(b.textureTransformIndex, um->animIndex, t);
                std::memcpy(um->submeshAnims[i].texMatrix, &tmx[0][0], sizeof(um->submeshAnims[i].texMatrix));
                glm::vec4 col = um->animator.BatchColor(b.colorIndex, b.textureWeightIndex, um->animIndex, t);
                um->submeshAnims[i].color[0] = col.r; um->submeshAnims[i].color[1] = col.g;
                um->submeshAnims[i].color[2] = col.b; um->submeshAnims[i].color[3] = col.a;
            }
        }

        for (size_t i = 0; i < insts_.size(); ++i)
        {
            if (!visible[i])
                continue;
            PInst& pi = insts_[i];
            UModel* um = models_[pi.model].get();
            if (um->isWmo)
                pi.palette.assign(1, pi.transform);
            else
            {
                pi.palette.resize(um->localBones.size());
                for (size_t j = 0; j < um->localBones.size(); ++j)
                    pi.palette[j] = pi.transform * um->localBones[j];
            }
            SceneInstanceGpu si{};
            si.handle = um->handle;
            si.boneMatrices = pi.palette.empty() ? nullptr : reinterpret_cast<const float*>(pi.palette.data());
            si.boneCount = (int)pi.palette.size();
            si.submeshAnims = um->submeshAnims.empty() ? nullptr : um->submeshAnims.data();
            si.submeshAnimCount = (int)um->submeshAnims.size();
            if (pi.effects)
            {
                float dur = (float)um->animator.Duration(um->animIndex);
                float t = dur > 0 ? std::fmod(doodadTime_, dur) : 0.0f;
                pi.effects->Step(um->animIndex, t, io.DeltaTime * 1000.0f, pi.palette, camRight, camUp, eye);
                const m2::EffectGeometry& g = pi.effects->Geometry();
                si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
                si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
            }
            si.worldOrigin[0] = pi.origin.x; si.worldOrigin[1] = pi.origin.y; si.worldOrigin[2] = pi.origin.z;
            scene.push_back(si);
        }
    }

    ImTextureID tex = svc_->renderer->RenderWorld(terrainHandle_, scene.data(), (int)scene.size(),
                                                  &view[0][0], &proj[0][0], w, h);
    if (tex)
        ImGui::GetWindowDrawList()->AddImage(tex, p0, ImVec2(p0.x + w, p0.y + h));
    ImGui::End();
}

void AdtViewerModule::OpenTile(const std::string& mapDir, int x, int y)
{
    loadedDir_ = mapDir;
    loadedX_ = x;
    loadedY_ = y;
    if (LoadTile(true))
    {
        loadedName_ = adt::TilePath(mapDir, x, y);
        if (svc_ && svc_->setStatus)
            svc_->setStatus("Loaded ADT: " + loadedName_);
    }
}

bool AdtViewerModule::LoadTile(bool frameCamera)
{
    if (!svc_ || !svc_->clientData || !svc_->renderer || loadedDir_.empty())
        return false;
    if (!liquidTypesLoaded_ && svc_->dbcStore)
    {
        liquidTypes_ = svc_->dbcStore->LoadLiquidTypes(*svc_->clientData);
        liquidTypesLoaded_ = true;
    }

    adt::AdtTile t;
    std::string err;
    const std::string path = adt::TilePath(loadedDir_, loadedX_, loadedY_);
    if (!adt::Load(*svc_->clientData, path, t, opt_, &liquidTypes_, &err))
    {
        error_ = err;   // keep the current tile on failure
        return false;
    }
    error_.clear();
    tile_ = std::move(t);

    ClearScene();
    TerrainUpload tu = adt::BuildTerrainUpload(*svc_->clientData, tile_);
    terrainHandle_ = svc_->renderer->CreateTerrain(tu);
    if (!terrainHandle_)
    {
        error_ = "terrain GPU upload failed";
        return false;
    }
    if (opt_.liquid)
    {
        ModelUpload lu = adt::BuildLiquidUpload(*svc_->clientData, tile_);
        if (!lu.vertices.empty())
            liquidHandle_ = svc_->renderer->CreateModel(lu);
    }
    BuildPlacements();
    if (frameCamera)
        FrameCamera();
    return true;
}

void AdtViewerModule::ClearScene()
{
    if (svc_ && svc_->renderer)
    {
        for (auto& um : models_)
            if (um->handle) svc_->renderer->DestroyModel(um->handle);
        if (liquidHandle_) svc_->renderer->DestroyModel(liquidHandle_);
        if (terrainHandle_) svc_->renderer->DestroyTerrain(terrainHandle_);
    }
    models_.clear();
    insts_.clear();
    liquidHandle_ = 0;
    terrainHandle_ = 0;
    doodadTime_ = 0.0f;
    liquidTime_ = 0.0f;
}

void AdtViewerModule::BuildPlacements()
{
    std::unordered_map<std::string, int> cache;   // -1 = known bad
    for (const adt::AdtPlacement& p : tile_.placements)
    {
        const std::string key = Lower(p.path);
        int mi;
        auto it = cache.find(key);
        if (it == cache.end())
        {
            auto um = std::make_unique<UModel>();
            um->isWmo = p.isWmo;
            if (p.isWmo)
            {
                wmo::WmoModel wm;
                if (!wmo::Load(*svc_->clientData, p.path, wm, {}, &liquidTypes_, nullptr)) { cache[key] = -1; continue; }
                ModelUpload up = wmo::BuildUpload(*svc_->clientData, wm);
                for (ModelVertexGpu& v : up.vertices) { v.boneIndices[0] = 0; v.boneWeights[0] = 1.0f; }
                up.boneCount = 1;   // shell rides bone 0 = the placement matrix
                um->handle = svc_->renderer->CreateModel(up);
                if (!um->handle) { cache[key] = -1; continue; }
                um->boundsCenter = wm.boundsCenter;
                um->boundsRadius = wm.boundsRadius;
            }
            else
            {
                if (!m2::Load(*svc_->clientData, p.path, um->m2, nullptr)) { cache[key] = -1; continue; }
                um->animator.SetModel(&um->m2, svc_->clientData, p.path);
                um->hasEmitters = !um->m2.particleEmitters.empty() || !um->m2.ribbonEmitters.empty();
                ModelUpload up = m2::BuildUpload(*svc_->clientData, um->m2);
                um->handle = svc_->renderer->CreateModel(up);
                if (!um->handle) { cache[key] = -1; continue; }
                um->animator.Evaluate(0, 0.0f, glm::mat4(1.0f), um->localBones);   // bind pose
                um->boundsCenter = um->m2.boundsCenter;
                um->boundsRadius = um->m2.boundsRadius;
            }
            mi = (int)models_.size();
            cache[key] = mi;
            models_.push_back(std::move(um));
        }
        else
        {
            mi = it->second;
            if (mi < 0) continue;
        }

        PInst pi;
        pi.model = mi;
        std::memcpy(&pi.transform[0][0], p.transform, sizeof(p.transform));
        pi.origin = glm::vec3(p.origin[0], p.origin[1], p.origin[2]);
        UModel* um = models_[mi].get();
        pi.cullCenter = glm::vec3(pi.transform * glm::vec4(um->boundsCenter, 1.0f));
        pi.cullRadius = um->boundsRadius * glm::length(glm::vec3(pi.transform[0])) + 0.01f;
        if (um->hasEmitters)
        {
            pi.effects = std::make_unique<m2::M2EffectSystem>();
            pi.effects->SetModel(&um->m2, &um->animator);
        }
        insts_.push_back(std::move(pi));
    }
}

void AdtViewerModule::FrameCamera()
{
    camera_.Frame(tile_.boundsCenter, tile_.boundsRadius);
}
} // namespace we
