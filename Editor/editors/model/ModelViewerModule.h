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

#include <memory>

#include "app/IEditorModule.h"
#include "clientdata/DbcStore.h"   // CreatureDisplay
#include "gfx/IRenderer.h"         // ModelHandle
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/M2Types.h"
#include "model/ModelDress.h"      // ModelDresser, ModelClass
#include "ui/ViewportCamera.h"

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
    void DrawControls();   // customization + equipment + geoset controls (hosted in the Browser panel)

    // Load an M2 by client path. `skins` (optional, from a creature display) fills the
    // model's runtime skin slots. Replaces any currently-loaded model.
    void OpenPath(const std::string& m2Path, const std::string* skins /*[3] or null*/);
    void OpenDisplay(uint32_t displayId);
    void Unload();
    void FrameCamera();  // reset orbit distance/target from the model bounds

    // Skin (texture-variation) switching for the loaded model.
    void EnsureDresser();                        // create dresser_ once client data + DBCs exist
    void BuildSkinOptions();                     // gather available skins for the loaded model's class
    void ApplySkinToModel(int optionIndex);      // reset to pristine textures + apply option's skin
    bool Rebuild();                              // rebuild the GPU model from model_
    void ApplySkin(int optionIndex);             // switch skin, keep camera/animation

    // Geoset (submesh) visibility. Sized to model_.batches; 1 = shown. The default hides geosets
    // whose runtime texture didn't resolve (they'd render white, e.g. an unequipped cape). Proper
    // hair/facial/equipment geoset selection replaces the default in later phases.
    void ComputeDefaultGeosetVisibility();
    void ApplyGeosetVisibility();                // push geosetVisible_ to the renderer (no rebuild)

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

    // Creature display picker (OpenDisplay). Loaded once when client data is ready.
    std::unordered_map<uint32_t, std::string> modelPaths_;              // modelId -> .m2
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> displays_;  // displayId -> display
    std::unordered_map<uint32_t, DbcStore::ParticleColorRow> particleColors_;  // ParticleColor.dbc
    std::unordered_map<uint32_t, std::string> animNames_;              // AnimationData.dbc id -> name
    bool dbcLoaded_ = false;

    // Resolves runtime textures (creature/item/character skins) for any model class. Created once
    // client data + the DBC store are available. Base pristine texture paths are kept so switching
    // a skin re-resolves from a clean slate rather than stacking patches.
    std::unique_ptr<ModelDresser> dresser_;
    std::vector<std::string> baseTexturePaths_;   // model_.texturePaths as loaded (before dressing)

    // Available skins (texture variations) for the loaded model. Payload is class-specific: a
    // creature skin triple, an item object-skin name, or a resolved character texture set.
    struct SkinOption
    {
        std::string label;
        ModelClass  cls = ModelClass::Unknown;
        std::string skins[3];                   // creature TextureVariation triple
        std::string objectSkin;                 // item object-skin base name
        ModelDresser::CharChoice charChoice;    // character body/hair/extra
    };
    std::vector<SkinOption> skinOptions_;
    int curSkin_ = -1;

    // Per-submesh (batch) geoset visibility, 1 = shown. Parallel to model_.batches.
    std::vector<uint8_t> geosetVisible_;

    // Character customization (Phase 2). Valid only for character models; the body texture is
    // composited from these choices and the hair/facial choices drive geoset visibility.
    CharacterOptions charOpts_;
    CharCustomize    charCustom_;
    BlpImage         bodyComposite_;   // composited type-1 body texture (base skin + face + underwear)
    int              bodySlot_ = -1;   // texture slot the composite replaces (type 1)
    // Equipment per appearance slot. The user works in item_template.entry ids (what modders know);
    // each entry is resolved to its ItemDisplayInfo id (what the dresser needs), its name, and
    // validated against the slot's InventoryTypes. slotEntry_/slotName_ are for display; the
    // dresser only reads slotDisplayId_.
    uint32_t slotEntry_[kEquipSlotCount] = {0};       // equipped item_template.entry (0 = empty)
    uint32_t slotDisplayId_[kEquipSlotCount] = {0};   // resolved ItemDisplayInfo id per slot (0 = empty)
    std::string slotName_[kEquipSlotCount];           // resolved item name (for the row label)
    int      slotInput_[kEquipSlotCount] = {0};       // per-slot entry-id entry box
    std::string equipErr_;                            // last equip error (bad id / wrong slot / no DB)
    int         equipErrSlot_ = -1;                   // slot the error belongs to (-1 = none)
    void RecomputeCharacter();         // re-composite body + hair + geosets + equipment, rebuild
    // Resolve an item entry -> display id + name, requiring it fit `slot`. False + errOut on failure.
    bool ResolveItemEntry(EquipSlot slot, uint32_t entry, uint32_t& displayOut, std::string& nameOut,
                          std::string& errOut);
    // Equip item `entry` (0 = clear) into `slot`: resolve, store, and RecomputeCharacter (or set err).
    void EquipEntryInSlot(EquipSlot slot, uint32_t entry);

    // Per-slot item search (world DB item_template by name, filtered to the slot's InventoryTypes).
    int  searchSlot_ = -1;             // slot whose search popup is open (-1 none)
    char searchBuf_[128] = {0};
    struct ItemHit { uint32_t displayId; uint32_t entry; std::string name; };
    std::vector<ItemHit> searchResults_;
    void RunItemSearch(EquipSlot slot); // query the DB for items matching searchBuf_ + slot

    // A held/worn model (weapon, shield, helm, shoulders) attached to a character bone. Rendered
    // as its own live instance whose bone palette is folded with the attachment's world transform.
    struct AttachedItem
    {
        uint32_t       displayId = 0;
        uint32_t       attachId = 0;    // M2 attachment id (1 main-hand, 2 off-hand, 11 helm, ...)
        m2::M2Model    model;
        ModelHandle    handle = 0;
        m2::M2Animator animator;
        std::vector<glm::mat4>  localBones;   // the item's own bind/anim palette
        std::vector<SubmeshAnim> subs;
        std::vector<glm::mat4>  palette;      // folded (attachment world * localBones) per frame
    };
    std::vector<std::unique_ptr<AttachedItem>> attached_;   // M2Model is move-only, so hold by ptr
    void RebuildAttachments();   // (re)load held-item models for equippedIds_ that have a held model
    void ClearAttachments();
};
} // namespace we
