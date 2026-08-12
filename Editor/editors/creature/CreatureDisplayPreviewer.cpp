#include "editors/creature/CreatureDisplayPreviewer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "app/EditorServices.h"
#include "clientdata/ClientData.h"
#include "imgui.h"
#include "model/M2Loader.h"
#include "model/ModelUploadBuild.h"

namespace we
{
namespace
{
constexpr uint16_t kAnimStand = 0;
constexpr int kAssetsPerFrame = 1;  // avoid a hitch if a template opens with four fresh display IDs

const char* Basename(const std::string& path)
{
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path.c_str() : path.c_str() + slash + 1;
}

float SafeScale(float value)
{
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}
} // namespace

void CreatureDisplayPreviewer::Clear()
{
    for (auto& pair : assets_)
        if (pair.second)
            DestroyAsset(*pair.second);
    assets_.clear();
    failedDisplays_.clear();
    displays_.clear();
    displayExtras_.clear();
    modelPaths_.clear();
    dresser_.reset();
    displayMapsLoaded_ = false;
    compositeTexture_ = 0;
    previewTimeMs_ = 0.0f;
    for (SlotState& slot : slots_)
        slot = SlotState{};
}

void CreatureDisplayPreviewer::OnClientDataLoaded()
{
    // The renderer and old ClientData source are both still live at this lifecycle point. Do not
    // let a cached GPU upload continue to point at skins from the previous client install.
    Clear();
}

void CreatureDisplayPreviewer::EnsureDisplayMaps()
{
    if (displayMapsLoaded_ || !services_ || !services_->clientData || !services_->dbcStore ||
        !services_->clientData->IsOpen())
        return;
    displays_ = services_->dbcStore->LoadCreatureDisplays(*services_->clientData);
    displayExtras_ = services_->dbcStore->LoadCreatureDisplayExtras(*services_->clientData);
    modelPaths_ = services_->dbcStore->LoadCreatureModelPaths(*services_->clientData);
    dresser_ = std::make_unique<ModelDresser>(*services_->clientData, *services_->dbcStore);
    displayMapsLoaded_ = true;
}

int CreatureDisplayPreviewer::FindSequence(const m2::M2Model& model, uint16_t animationId)
{
    for (size_t i = 0; i < model.sequences.size(); ++i)
        if (model.sequences[i].animationId == animationId)
            return static_cast<int>(i);
    return -1;
}

void CreatureDisplayPreviewer::DressCharacterNpc(const std::string& modelPath,
                                                  const DbcStore::CreatureDisplayExtra& extra,
                                                  m2::M2Model& model, BlpImage& bodyOut,
                                                  int& bodySlotOut,
                                                  std::vector<uint8_t>& visibleOut)
{
    bodySlotOut = -1;
    if (!dresser_)
        return;

    CharCustomize custom;
    custom.skinColor = extra.skin;
    custom.faceVariation = extra.face;
    custom.hairVariation = extra.hairStyle;
    custom.hairColor = extra.hairColor;
    custom.facialVariation = extra.facialHair;

    static const EquipSlot kNpcSlots[11] = {
        EquipSlot::Head, EquipSlot::Shoulder, EquipSlot::Shirt, EquipSlot::Chest, EquipSlot::Waist,
        EquipSlot::Legs, EquipSlot::Feet, EquipSlot::Wrist, EquipSlot::Hands, EquipSlot::Tabard,
        EquipSlot::Back,
    };
    std::vector<EquippedItem> equipped;
    for (int i = 0; i < 11; ++i)
    {
        // Head/shoulder attachment models are intentionally omitted from the compact card preview;
        // body-composite slots still show the display's race, skin, hair, armour and geosets.
        if (!extra.npcItemDisplay[i] || EquipSlotAttachment(kNpcSlots[i]) != 0)
            continue;
        DbcStore::ItemDisplay item;
        if (dresser_->ItemDisplayById(extra.npcItemDisplay[i], item))
            equipped.push_back({kNpcSlots[i], item});
    }

    const std::string body = dresser_->CharacterBodyTexture(extra.race, extra.sex, custom);
    const std::string hair = dresser_->CharacterHairTexture(extra.race, extra.sex, custom);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        const uint32_t type = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (type == 1 && !body.empty()) model.texturePaths[i] = body;
        if (type == 6 && !hair.empty()) model.texturePaths[i] = hair;
        if (type == 1) bodySlotOut = static_cast<int>(i);
    }
    bodyOut = dresser_->ComposeCharacterBody(extra.race, extra.sex, custom, equipped);
    dresser_->CharacterGeosets(extra.race, extra.sex, custom, model, visibleOut, equipped);
    dresser_->ResolveDefaultTextures(modelPath, model);
}

CreatureDisplayPreviewer::PreviewAsset* CreatureDisplayPreviewer::EnsureAsset(uint32_t displayId)
{
    if (!displayId || !services_ || !services_->clientData || !services_->renderer || !displayMapsLoaded_)
        return nullptr;
    const auto cached = assets_.find(displayId);
    if (cached != assets_.end())
        return cached->second.get();
    if (failedDisplays_.count(displayId))
        return nullptr;

    const DbcStore::CreatureDisplay* display = nullptr;
    uint32_t modelId = displayId;
    const auto displayIt = displays_.find(displayId);
    if (displayIt != displays_.end())
    {
        display = &displayIt->second;
        modelId = display->modelId;
    }
    const auto pathIt = modelPaths_.find(modelId);
    if (pathIt == modelPaths_.end())
    {
        failedDisplays_[displayId] = display ? "CreatureModelData path is missing" :
                                              "unknown CreatureDisplayInfo / direct model path";
        return nullptr;
    }

    const std::string& modelPath = pathIt->second;
    m2::M2Model model;
    std::string loadError;
    if (!m2::Load(*services_->clientData, modelPath, model, &loadError))
    {
        failedDisplays_[displayId] = loadError.empty() ? "M2 load failed" : loadError;
        return nullptr;
    }

    BlpImage bodyComposite;
    int bodySlot = -1;
    std::vector<uint8_t> geosetVisible;
    const DbcStore::CreatureDisplayExtra* extra = nullptr;
    if (display && display->extendedDisplayId && ClassifyModel(modelPath) == ModelClass::Character)
    {
        const auto extraIt = displayExtras_.find(display->extendedDisplayId);
        if (extraIt != displayExtras_.end())
            extra = &extraIt->second;
    }
    if (dresser_ && extra)
    {
        DressCharacterNpc(modelPath, *extra, model, bodyComposite, bodySlot, geosetVisible);
    }
    else if (dresser_)
    {
        const std::string blankSkins[3] = {};
        dresser_->ApplyCreatureSkins(modelPath, display ? display->skins : blankSkins, model);
        dresser_->ResolveDefaultTextures(modelPath, model);
    }

    ModelUpload upload = m2::BuildUpload(*services_->clientData, model);
    if (upload.vertices.empty() || upload.indices.empty())
    {
        failedDisplays_[displayId] = "model has no drawable geometry";
        return nullptr;
    }
    // Scene previews fold a transform into the bone palette. Static/custom M2s often carry no
    // weights, so synthesize the same root-bone fallback used by the World Editor NPC renderer.
    if (upload.boneCount == 0)
        upload.boneCount = 1;
    for (ModelVertexGpu& vertex : upload.vertices)
    {
        const float weights = vertex.boneWeights[0] + vertex.boneWeights[1] +
                              vertex.boneWeights[2] + vertex.boneWeights[3];
        if (weights <= 1e-6f)
        {
            vertex.boneIndices[0] = 0;
            vertex.boneWeights[0] = 1.0f;
            vertex.boneWeights[1] = vertex.boneWeights[2] = vertex.boneWeights[3] = 0.0f;
        }
    }
    if (bodySlot >= 0 && bodyComposite.valid() && bodySlot < static_cast<int>(upload.textures.size()))
    {
        upload.textures[bodySlot].rgba = bodyComposite.rgba;
        upload.textures[bodySlot].w = bodyComposite.width;
        upload.textures[bodySlot].h = bodyComposite.height;
    }

    const ModelHandle handle = services_->renderer->CreateModel(upload);
    if (!handle)
    {
        failedDisplays_[displayId] = "GPU model upload failed";
        return nullptr;
    }
    if (!geosetVisible.empty())
        services_->renderer->SetSubmeshVisibility(handle, geosetVisible.data(),
                                                  static_cast<int>(geosetVisible.size()));

    auto asset = std::make_unique<PreviewAsset>();
    asset->displayId = displayId;
    asset->modelId = modelId;
    asset->directModelFallback = display == nullptr;
    asset->modelPath = modelPath;
    asset->model = std::make_shared<m2::M2Model>(std::move(model));
    asset->handle = handle;
    asset->displayScale = display ? SafeScale(display->scale) : 1.0f;
    asset->boundsCenter = asset->model->boundsCenter;
    asset->boundsRadius = std::max(asset->model->boundsRadius, 0.01f);
    asset->animator.SetModel(asset->model.get(), services_->clientData, modelPath);
    const int stand = FindSequence(*asset->model, kAnimStand);
    asset->standSequence = stand >= 0 ? stand : 0;

    PreviewAsset* result = asset.get();
    assets_.emplace(displayId, std::move(asset));
    return result;
}

void CreatureDisplayPreviewer::DestroyAsset(PreviewAsset& asset)
{
    if (asset.handle && services_ && services_->renderer)
        services_->renderer->DestroyModel(asset.handle);
    asset.handle = 0;
}

void CreatureDisplayPreviewer::DrawSlotCard(int slotIndex, float width, float height)
{
    SlotState& slot = slots_[slotIndex];
    ImGui::PushID(slotIndex);
    ImGui::Text("modelid%d", slotIndex + 1);
    if (slot.displayId == 0)
    {
        ImGui::Dummy(ImVec2(width, height));
        ImGui::TextDisabled("Unused (0)");
        ImGui::PopID();
        return;
    }

    if (compositeTexture_ && slot.asset)
    {
        const float u0 = static_cast<float>(slotIndex) * 0.25f;
        const float u1 = u0 + 0.25f;
        ImGui::Image(static_cast<ImTextureID>(compositeTexture_), ImVec2(width, height),
                     ImVec2(u0, 0.0f), ImVec2(u1, 1.0f));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("CreatureDisplayInfo %u\nCreatureModelData %u\n%s",
                              slot.asset->displayId, slot.asset->modelId,
                              slot.asset->modelPath.c_str());
        ImGui::TextDisabled("Display %u → model %u", slot.asset->displayId, slot.asset->modelId);
        if (slot.asset->directModelFallback)
            ImGui::TextDisabled("Direct CreatureModelData fallback");
        else
            ImGui::TextDisabled("%s", Basename(slot.asset->modelPath));
    }
    else
    {
        ImGui::BeginDisabled();
        ImGui::Button("Preview unavailable", ImVec2(width, height));
        ImGui::EndDisabled();
        ImGui::TextWrapped("Display %u: %s", slot.displayId,
                           slot.status.empty() ? "waiting for client model" : slot.status.c_str());
    }
    ImGui::PopID();
}

void CreatureDisplayPreviewer::Draw(const uint32_t displayIds[4], float templateScale, float dpiScale)
{
    if (!displayIds)
        return;
    EnsureDisplayMaps();

    ImGui::SeparatorText("Live display-ID previews");
    ImGui::TextDisabled("Each card resolves the current CreatureDisplayInfo ID through CreatureModelData and applies that display's skins and scale. Editing a modelid updates its card without a save/reload.");
    if (!services_ || !services_->renderer || !services_->clientData || !services_->clientData->IsOpen())
    {
        ImGui::TextDisabled("Load a full WoW 3.3.5 client Data folder to preview CreatureDisplayInfo models.");
        return;
    }
    if (!displayMapsLoaded_)
    {
        ImGui::TextDisabled("CreatureDisplayInfo / CreatureModelData could not be read from the configured client data.");
        return;
    }

    const float available = std::max(ImGui::GetContentRegionAvail().x, 320.0f * dpiScale);
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float cardSide = std::clamp((available - spacing * 3.0f) * 0.25f,
                                      78.0f * dpiScale, 170.0f * dpiScale);
    const int renderHeight = std::max(128, static_cast<int>(std::lround(cardSide * 1.20f)));
    const int renderWidth = renderHeight * 4;

    previewTimeMs_ += std::clamp(ImGui::GetIO().DeltaTime * 1000.0f, 0.0f, 100.0f);
    constexpr float kCellSpacing = 4.0f;
    const glm::vec3 eye(0.0f, -17.0f, 5.2f);
    const glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 projection = glm::perspective(glm::radians(34.0f), 4.0f, 0.1f, 80.0f);
    projection[1][1] *= -1.0f;  // Vulkan clip-space presentation convention

    std::vector<SceneInstanceGpu> scene;
    scene.reserve(4);
    int loadBudget = kAssetsPerFrame;
    const float safeTemplateScale = SafeScale(templateScale);
    for (int i = 0; i < 4; ++i)
    {
        SlotState& slot = slots_[i];
        slot = SlotState{};
        slot.displayId = displayIds[i];
        if (slot.displayId == 0)
        {
            slot.status = "unused";
            continue;
        }

        PreviewAsset* asset = nullptr;
        const auto cached = assets_.find(slot.displayId);
        if (cached != assets_.end())
            asset = cached->second.get();
        else if (loadBudget > 0)
        {
            --loadBudget;
            asset = EnsureAsset(slot.displayId);
        }
        if (!asset)
        {
            const auto failed = failedDisplays_.find(slot.displayId);
            slot.status = failed != failedDisplays_.end() ? failed->second : "queued for preview upload";
            continue;
        }
        slot.asset = asset;
        const float duration = static_cast<float>(asset->animator.Duration(asset->standSequence));
        const float animationTime = duration > 0.0f ? std::fmod(previewTimeMs_, duration) : 0.0f;
        asset->animator.Evaluate(asset->standSequence, animationTime, view, slot.palette);
        if (slot.palette.empty())
            slot.palette.assign(1, glm::mat4(1.0f));

        // Keep every card legible while retaining a bounded visual response to CreatureDisplayInfo
        // and creature_template scales. The exact combined scale is printed below the card.
        const float combinedScale = safeTemplateScale * asset->displayScale;
        const float radiusAtScale = asset->boundsRadius * combinedScale;
        const float fit = std::clamp(1.35f / std::max(radiusAtScale, 0.05f), 0.35f, 2.25f);
        const float presentationScale = combinedScale * fit;
        glm::mat4 transform(1.0f);
        transform = glm::translate(transform, glm::vec3((static_cast<float>(i) - 1.5f) * kCellSpacing, 0.0f, 0.0f));
        transform = glm::scale(transform, glm::vec3(presentationScale));
        transform = glm::translate(transform, -asset->boundsCenter);
        for (glm::mat4& bone : slot.palette)
            bone = transform * bone;

        slot.submeshAnims.resize(asset->model->batches.size());
        for (size_t batch = 0; batch < asset->model->batches.size(); ++batch)
        {
            const m2::RenderBatch& renderBatch = asset->model->batches[batch];
            const glm::mat4 textureMatrix = asset->animator.TextureMatrix(renderBatch.textureTransformIndex,
                                                                            asset->standSequence, animationTime);
            std::memcpy(slot.submeshAnims[batch].texMatrix, &textureMatrix[0][0],
                        sizeof(slot.submeshAnims[batch].texMatrix));
            const glm::vec4 color = asset->animator.BatchColor(renderBatch.colorIndex,
                                                                 renderBatch.textureWeightIndex,
                                                                 asset->standSequence, animationTime);
            slot.submeshAnims[batch].color[0] = color.r;
            slot.submeshAnims[batch].color[1] = color.g;
            slot.submeshAnims[batch].color[2] = color.b;
            slot.submeshAnims[batch].color[3] = color.a;
        }

        SceneInstanceGpu instance{};
        instance.handle = asset->handle;
        instance.boneMatrices = reinterpret_cast<const float*>(slot.palette.data());
        instance.boneCount = static_cast<int>(slot.palette.size());
        instance.submeshAnims = slot.submeshAnims.empty() ? nullptr : slot.submeshAnims.data();
        instance.submeshAnimCount = static_cast<int>(slot.submeshAnims.size());
        instance.worldOrigin[0] = (static_cast<float>(i) - 1.5f) * kCellSpacing;
        scene.push_back(instance);
        char scaleText[96];
        std::snprintf(scaleText, sizeof(scaleText), "template %.2fx × display %.2fx",
                      safeTemplateScale, asset->displayScale);
        slot.status = scaleText;
    }

    compositeTexture_ = 0;
    if (!scene.empty())
    {
        const float gridCenter[3] = {0, 0, 0};
        services_->renderer->SetGrid(false, gridCenter, 0.0f, 1.0f);
        compositeTexture_ = services_->renderer->RenderScene(scene.data(), static_cast<int>(scene.size()),
                                                              &view[0][0], &projection[0][0],
                                                              renderWidth, renderHeight);
    }

    if (ImGui::BeginTable("##creaturedisplaypreviews", 4,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_RowBg))
    {
        ImGui::TableNextRow();
        for (int i = 0; i < 4; ++i)
        {
            ImGui::TableSetColumnIndex(i);
            DrawSlotCard(i, cardSide, cardSide);
            if (slots_[i].asset)
                ImGui::TextDisabled("%s", slots_[i].status.c_str());
        }
        ImGui::EndTable();
    }
}
} // namespace we
