#pragma once

// ModelViewerModule — an IEditorModule that browses and displays WoW M2 models in a 3D
// viewport. Read-only (no DB): it loads a model through the M2 loader, uploads it via
// the renderer's model API, and draws the offscreen result in a dockable panel with an
// orbit camera. Three pickers: an MPQ (listfile) file list, a creature display ID, and a
// manual path box.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "app/IEditorModule.h"
#include "clientdata/DbcStore.h"   // CreatureDisplay
#include "gfx/IRenderer.h"         // ModelHandle
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/M2Types.h"
#include "viewer/ViewportCamera.h"

namespace we
{
class ModelViewerModule final : public IEditorModule
{
public:
    const char* Id() const override { return "model"; }
    const char* DisplayName() const override { return "Model Viewer"; }
    const char* RailGlyph() const override { return "3"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        return {{"Model Browser", DockSlot::Left, true}, {"Model Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}

    void OnClientDataLoaded() override;

    bool HasRecord() const override { return handle_ != 0; }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawViewportPanel();

    // Load an M2 by client path. `skins` (optional, from a creature display) fills the
    // model's runtime skin slots. Replaces any currently-loaded model.
    void OpenPath(const std::string& m2Path, const std::string* skins /*[3] or null*/);
    void OpenDisplay(uint32_t displayId);
    void Unload();
    void FrameCamera();  // reset orbit distance/target from the model bounds

    // Skin (texture-variation) switching for the loaded model.
    void BuildSkinOptions();                     // gather available skins for loadedModelId_
    void ApplyRuntimeSkins(const std::string skins[3]);  // patch runtime texture slots
    bool Rebuild();                              // rebuild the GPU model from model_
    void ApplySkin(int optionIndex);             // switch skin, keep camera/animation

    EditorServices* svc_ = nullptr;

    // Loaded model.
    m2::M2Model model_;
    ModelHandle handle_ = 0;
    std::string loadedName_;
    std::string error_;

    // Animation.
    m2::M2Animator animator_;
    std::vector<glm::mat4> boneMatrices_;
    std::vector<SubmeshAnim> submeshAnims_;   // per-batch UV transform + color, per frame
    int curAnim_ = 0;
    bool playing_ = true;
    float animTimeMs_ = 0.0f;

    // Effects (particles / ribbons).
    m2::M2EffectSystem effects_;
    bool showEffects_ = true;

    ViewportCamera camera_;
    bool showGrid_ = true;

    // Browser state.
    std::vector<std::string> m2List_;   // from (listfile)
    char search_[128] = {0};
    char pathBuf_[256] = {0};
    int displayIdInput_ = 0;

    // Creature model resolution (loaded once when client data is ready).
    std::unordered_map<uint32_t, std::string> modelPaths_;              // modelId -> .m2
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> displays_;  // displayId -> display
    std::unordered_map<std::string, uint32_t> pathToModelId_;          // lower(.m2) -> modelId
    std::unordered_map<uint32_t, std::vector<uint32_t>> modelToDisplays_;  // modelId -> displayIds
    std::unordered_map<uint32_t, std::string> animNames_;              // AnimationData.dbc id -> name
    bool dbcLoaded_ = false;

    // Available skins (texture variations) for the loaded model.
    struct SkinOption { std::string label; std::string skins[3]; };
    uint32_t loadedModelId_ = 0;
    std::vector<SkinOption> skinOptions_;
    int curSkin_ = -1;
};
} // namespace we
