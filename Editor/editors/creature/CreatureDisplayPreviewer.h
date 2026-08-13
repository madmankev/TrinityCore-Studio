#pragma once

// CreatureDisplayPreviewer — the live model-card strip embedded in Creature → General.
// `creature_template.modelid1..4` are CreatureDisplayInfo.dbc IDs, not direct M2 IDs. This
// component resolves each display through CreatureModelData, applies its exact runtime skin and
// display scale, uploads/cache-shares the M2, then snapshots a card texture per display. Snapshot
// cards deliberately use the known-good RenderModel path, so four UI cards never fight over the
// renderer's single live offscreen target in the same frame.

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "clientdata/BlpDecoder.h"
#include "clientdata/DbcStore.h"
#include "gfx/IRenderer.h"
#include "model/M2Animator.h"
#include "model/M2Types.h"
#include "model/ModelDress.h"

namespace we
{
struct EditorServices;

class CreatureDisplayPreviewer
{
public:
    void Init(EditorServices* services) { services_ = services; }
    // Call while the renderer is alive before changing client-data sources or shutting down.
    void Clear();
    void OnClientDataLoaded();

    // Draw four model cards. `displayIds` maps directly to creature_template.modelid1..4 and
    // `templateScale` is creature_template.scale. The cards update from the edited values without
    // requiring a database save/reload.
    void Draw(const uint32_t displayIds[4], float templateScale, float dpiScale = 1.0f);

private:
    struct PreviewAsset
    {
        uint32_t displayId = 0;
        uint32_t modelId = 0;
        bool directModelFallback = false;
        std::string modelPath;
        std::shared_ptr<m2::M2Model> model;
        ModelHandle handle = 0;
        m2::M2Animator animator;
        int standSequence = 0;
        float displayScale = 1.0f;
        glm::vec3 boundsCenter{0.0f};
        float boundsRadius = 1.0f;
        TextureId thumbnail = 0;  // independent UI texture; safe to show alongside other cards
    };

    struct SlotState
    {
        uint32_t displayId = 0;
        PreviewAsset* asset = nullptr;
        std::string status;
    };

    void EnsureDisplayMaps();
    PreviewAsset* EnsureAsset(uint32_t displayId);
    bool EnsureThumbnail(PreviewAsset& asset);
    void DestroyAsset(PreviewAsset& asset);
    void DressCharacterNpc(const std::string& modelPath, const DbcStore::CreatureDisplayExtra& extra,
                           m2::M2Model& model, BlpImage& bodyOut, int& bodySlotOut,
                           std::vector<uint8_t>& visibleOut);
    static int FindSequence(const m2::M2Model& model, uint16_t animationId);
    void DrawSlotCard(int slot, float width, float height);

    EditorServices* services_ = nullptr;
    bool displayMapsLoaded_ = false;
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> displays_;
    std::unordered_map<uint32_t, DbcStore::CreatureDisplayExtra> displayExtras_;
    std::unordered_map<uint32_t, std::string> modelPaths_;
    std::unique_ptr<ModelDresser> dresser_;
    std::unordered_map<uint32_t, std::unique_ptr<PreviewAsset>> assets_;
    std::unordered_map<uint32_t, std::string> failedDisplays_;
    std::array<SlotState, 4> slots_;
};
} // namespace we
