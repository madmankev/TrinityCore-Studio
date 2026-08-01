#pragma once

// AdtViewerModule — an IEditorModule that browses and displays WoW ADT map tiles (terrain)
// in a 3D viewport. Read-only (no DB): it picks a map (Map.dbc) and an existing tile (from
// the map's WDT), parses the ADT through the ADT loader (terrain + textures + placements +
// liquid), uploads the terrain via the dedicated terrain pipeline and everything else via the
// mesh pipeline, and draws the offscreen result (RenderWorld) with the shared orbit/fly camera.

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "app/IEditorModule.h"
#include "gfx/IRenderer.h"
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/M2Types.h"
#include "viewer/ViewportCamera.h"
#include "adt/AdtLoader.h"
#include "adt/AdtTypes.h"
#include "clientdata/DbcStore.h"

namespace we
{
class AdtViewerModule final : public IEditorModule
{
public:
    const char* Id() const override { return "adt"; }
    const char* DisplayName() const override { return "ADT Viewer"; }
    const char* RailGlyph() const override { return "5"; }
    void Init(EditorServices* services) override { svc_ = services; }

    std::vector<PanelDesc> Panels() const override
    {
        return {{"ADT Browser", DockSlot::Left, true}, {"ADT Viewer", DockSlot::Center, true}};
    }
    void DrawPanels() override;
    void DrawModals() override {}

    void OnClientDataLoaded() override;

    bool HasRecord() const override { return terrainHandle_ != 0; }
    std::string RecordSummary() const override { return loadedName_; }

private:
    void DrawBrowserPanel();
    void DrawViewportPanel();

    void OpenTile(const std::string& mapDir, int x, int y);
    bool LoadTile(bool frameCamera);   // (re)parse loaded tile with opt_ + upload
    void BuildPlacements();            // load the resolved M2/WMO placements as instances
    void ClearScene();
    void FrameCamera();

    // A unique referenced model (M2 doodad or WMO building), loaded once.
    struct UModel
    {
        bool                     isWmo = false;
        ModelHandle              handle = 0;
        m2::M2Model              m2;         // (M2 only)
        m2::M2Animator           animator;   // (M2 only)
        int                      animIndex = 0;
        bool                     hasEmitters = false;
        std::vector<glm::mat4>   localBones;    // animated palette (M2-local), per frame
        std::vector<SubmeshAnim> submeshAnims;  // per-batch UV transform + color, per frame
        glm::vec3                boundsCenter{0.0f};
        float                    boundsRadius = 1.0f;
    };
    // One placed instance.
    struct PInst
    {
        int                                 model = -1;
        glm::mat4                           transform{1.0f};
        glm::vec3                           origin{0.0f};
        glm::vec3                           cullCenter{0.0f};
        float                               cullRadius = 1.0f;
        std::unique_ptr<m2::M2EffectSystem> effects;   // M2 with emitters only
        std::vector<glm::mat4>              palette;
    };

    EditorServices* svc_ = nullptr;

    // Loaded tile.
    adt::AdtTile      tile_;
    TerrainHandle     terrainHandle_ = 0;
    ModelHandle       liquidHandle_ = 0;
    std::string       loadedName_;
    std::string       loadedDir_;
    int               loadedX_ = 0, loadedY_ = 0;
    std::string       error_;
    adt::AdtLoadOptions opt_;   // doodad/wmo/liquid toggles (baked at load)
    adt::LiquidTypeTable liquidTypes_;
    bool              liquidTypesLoaded_ = false;

    // Instances.
    std::vector<std::unique_ptr<UModel>> models_;
    std::vector<PInst>                   insts_;
    float doodadTime_ = 0.0f;

    // Liquid frame animation.
    std::vector<SubmeshAnim> liquidAnims_;
    float liquidTime_ = 0.0f;

    ViewportCamera camera_;
    bool showGrid_ = true;

    // Browser state.
    std::unordered_map<uint32_t, DbcStore::MapInfo> maps_;
    std::vector<std::pair<uint32_t, std::string>>   mapList_;   // (id, "dir (name)") sorted
    int selectedMap_ = -1;                                      // index into mapList_
    std::vector<std::pair<int, int>> tiles_;                    // existing (x,y) for selectedMap_
    std::string selectedMapDir_;
    char search_[128] = {0};
    bool dbcLoaded_ = false;
};
} // namespace we
