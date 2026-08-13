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
constexpr int kThumbnailSize = 224;

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
    // UI thumbnail descriptors may have been sampled by an in-flight swapchain frame. Retire all
    // of them together behind one explicit wait instead of destroying a card texture mid-frame.
    if (services_ && services_->renderer && !assets_.empty())
        services_->renderer->WaitIdle();
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
    for (SlotState& slot : slots_)
        slot = SlotState{};
}

void CreatureDisplayPreviewer::OnClientDataLoaded()
{
    // The renderer and previous ClientData source are both still live at this lifecycle point. Do
    // not let a cached model/thumbnail continue to point at skins from another client install.
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
        // Head/shoulder attachment models are omitted from a compact thumbnail, while body-composite
        // slots still show the display's race, skin, hair, armour and geosets.
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
    // Thumbnail transforms are folded into the bone palette. Static/custom M2s often carry no
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

bool CreatureDisplayPreviewer::EnsureThumbnail(PreviewAsset& asset)
{
    if (!services_ || !services_->renderer || !asset.handle)
        return false;
    // A card is fitted to its own camera, so it need not be torn down whenever the template scale
    // text changes. The display row's client scale still participates in the rendered pose/camera.
    if (asset.thumbnail)
        return true;
    const float visualScale = asset.displayScale;

    const glm::vec3 center = asset.boundsCenter * visualScale;
    const float radius = std::max(asset.boundsRadius * visualScale, 0.05f);
    const glm::vec3 eye = center + glm::normalize(glm::vec3(-1.0f, -1.35f, 0.72f)) * (radius * 2.9f);
    const glm::mat4 view = glm::lookAt(eye, center, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 projection = glm::perspective(glm::radians(42.0f), 1.0f,
                                             std::max(radius * 0.015f, 0.01f), radius * 12.0f);
    projection[1][1] *= -1.0f;

    std::vector<glm::mat4> palette;
    asset.animator.Evaluate(asset.standSequence, 0.0f, view, palette);
    if (palette.empty())
        palette.assign(1, glm::mat4(1.0f));
    const glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(visualScale));
    for (glm::mat4& bone : palette)
        bone = scaleMatrix * bone;

    std::vector<SubmeshAnim> animations(asset.model->batches.size());
    for (size_t i = 0; i < asset.model->batches.size(); ++i)
    {
        const m2::RenderBatch& batch = asset.model->batches[i];
        const glm::mat4 textureMatrix = asset.animator.TextureMatrix(batch.textureTransformIndex,
                                                                       asset.standSequence, 0.0f);
        std::memcpy(animations[i].texMatrix, &textureMatrix[0][0], sizeof(animations[i].texMatrix));
        const glm::vec4 color = asset.animator.BatchColor(batch.colorIndex, batch.textureWeightIndex,
                                                            asset.standSequence, 0.0f);
        animations[i].color[0] = color.r;
        animations[i].color[1] = color.g;
        animations[i].color[2] = color.b;
        animations[i].color[3] = color.a;
    }

    const float gridCenter[3] = {0, 0, 0};
    services_->renderer->SetGrid(false, gridCenter, 0.0f, 1.0f);
    const TextureId target = services_->renderer->RenderModel(
        asset.handle, &view[0][0], &projection[0][0],
        palette.empty() ? nullptr : reinterpret_cast<const float*>(palette.data()),
        static_cast<int>(palette.size()), animations.empty() ? nullptr : animations.data(),
        static_cast<int>(animations.size()), nullptr, kThumbnailSize, kThumbnailSize);
    if (!target)
        return false;

    std::vector<uint8_t> rgba;
    int width = 0, height = 0;
    if (!services_->renderer->CaptureModelTarget(rgba, width, height) || rgba.empty() || width <= 0 || height <= 0)
        return false;
    asset.thumbnail = services_->renderer->CreateTexture(rgba.data(), width, height);
    if (!asset.thumbnail)
        return false;
    return true;
}

void CreatureDisplayPreviewer::DestroyAsset(PreviewAsset& asset)
{
    if (!services_ || !services_->renderer)
        return;
    if (asset.thumbnail)
        services_->renderer->DestroyTexture(asset.thumbnail);
    if (asset.handle)
        services_->renderer->DestroyModel(asset.handle);
    asset.thumbnail = 0;
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

    if (slot.asset && slot.asset->thumbnail)
    {
        ImGui::Image(static_cast<ImTextureID>(slot.asset->thumbnail), ImVec2(width, height));
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
        ImGui::Button("Preview loading", ImVec2(width, height));
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
    ImGui::TextDisabled("Each card resolves the current CreatureDisplayInfo ID through CreatureModelData and applies that display's skins and scale. Editing a modelid refreshes its card before save.");
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

    int assetBudget = kAssetsPerFrame;
    int thumbnailBudget = 1;
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
        else if (assetBudget > 0)
        {
            --assetBudget;
            asset = EnsureAsset(slot.displayId);
        }
        if (!asset)
        {
            const auto failed = failedDisplays_.find(slot.displayId);
            slot.status = failed != failedDisplays_.end() ? failed->second : "queued for preview upload";
            continue;
        }
        slot.asset = asset;
        if (!asset->thumbnail)
        {
            if (thumbnailBudget > 0)
            {
                --thumbnailBudget;
                if (!EnsureThumbnail(*asset))
                    slot.status = "thumbnail render failed";
            }
            else
                slot.status = "queued for thumbnail render";
        }
        char scaleText[96];
        std::snprintf(scaleText, sizeof(scaleText), "template %.2fx × display %.2fx",
                      safeTemplateScale, asset->displayScale);
        if (slot.status.empty())
            slot.status = scaleText;
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
