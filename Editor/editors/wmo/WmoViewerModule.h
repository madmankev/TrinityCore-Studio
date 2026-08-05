#pragma once

// WmoViewerModule — an IEditorModule that browses and displays WoW WMO objects (buildings,
// dungeons, caves) in a 3D viewport. Read-only (no DB): it parses a WMO through the WMO
// loader (root + group files, baked doodads + liquid), uploads the merged geometry via the
// renderer's model API, and draws the offscreen result with an orbit camera. Static geometry
// — no skeleton/animation — so it renders through the shared mesh pipeline with bones=null.

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "app/IEditorModule.h"
#include "gfx/IRenderer.h"   // ModelHandle / SceneInstanceGpu
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/M2Types.h"
#include "ui/ViewportCamera.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoTypes.h"

namespace we
{
class WmoViewerModule final : public IEditorModule
{
public:
    const char* Id() const override { return "wmo"; }
    const char* DisplayName() const override { return "WMO Viewer"; }
    const char* RailGlyph() const override { return "4"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        return {{"WMO Browser", DockSlot::Left, true}, {"WMO Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}

    void OnClientDataLoaded() override;

    bool HasRecord() const override { return handle_ != 0; }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawViewportPanel();

    void OpenPath(const std::string& wmoPath);   // load fresh + frame the camera
    bool LoadModel(bool frameCamera);            // (re)parse loadedPath_ with opt_ + upload
    void BuildDoodads();                         // load the resolved doodad M2s as live instances
    void ClearDoodads();
    void FrameCamera();

    // A unique doodad model (loaded once); many instances reference it.
    struct DoodadModel
    {
        ModelHandle            handle = 0;
        m2::M2Model            model;
        m2::M2Animator         animator;
        int                      animIndex = 0;
        bool                     hasEmitters = false;
        std::vector<glm::mat4>   localBones;    // animated palette (M2-local), per frame
        std::vector<SubmeshAnim> submeshAnims;  // per-batch UV transform + color/alpha, per frame
    };
    // One placed instance: its own transform, effect sim, and folded palette.
    struct DoodadInst
    {
        int                                  model = -1;
        glm::mat4                            transform{1.0f};
        glm::vec3                            origin{0.0f};
        glm::vec3                            cullCenter{0.0f};   // world bounding sphere (frustum cull)
        float                                cullRadius = 1.0f;
        uint8_t                              color[4] = {255, 255, 255, 255};
        std::unique_ptr<m2::M2EffectSystem>  effects;   // only when the model has emitters
        std::vector<glm::mat4>               palette;    // transform * localBones, per frame
    };

    EditorServices* svc_ = nullptr;

    // Loaded model.
    wmo::WmoModel     model_;
    ModelHandle       handle_ = 0;
    std::string       loadedName_;
    std::string       loadedPath_;
    std::string       error_;
    wmo::WmoLoadOptions opt_;   // doodad set + doodad/liquid toggles (baked at load)
    wmo::LiquidTypeTable liquidTypes_;   // LiquidType.dbc, loaded once
    bool                 liquidTypesLoaded_ = false;

    // Live doodad instances (loaded from model_.doodadInstances).
    std::vector<std::unique_ptr<DoodadModel>> doodadModels_;
    std::vector<DoodadInst>                   doodadInsts_;
    float doodadTime_ = 0.0f;

    // Liquid UV scroll (per-submesh anim for isLiquid batches).
    std::vector<SubmeshAnim> submeshAnims_;
    float liquidTime_ = 0.0f;
    bool  hasLiquid_ = false;

    ViewportCamera camera_;
    bool showGrid_ = true;

    // Browser state.
    std::vector<std::string> wmoList_;   // root WMOs from (listfile)
    char search_[128] = {0};
    char pathBuf_[256] = {0};
    bool listLoaded_ = false;
};
} // namespace we
