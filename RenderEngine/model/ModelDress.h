#pragma once

// ModelDress — resolve the RUNTIME textures (and, in later phases, geoset visibility +
// equipment) an M2 needs but does not store itself. M2 texture slots with a non-zero type
// (skin / object-skin / hair / monster-skin) carry an empty filename; the actual .blp comes
// from a display record — CreatureDisplayInfo (creatures), ItemDisplayInfo (held items) or
// CharSections (characters). Browsing a bare .m2 path has no display, so those slots render
// white. ModelDresser fills them with a sensible default per model class so nothing renders
// white, and is the shared home for the customization/equipment system built on top.
//
// The DBC lookups are cached the first time each class is needed, so the dresser is cheap to
// call per model-open (the viewer keeps one) and reusable from the headless harness.

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "clientdata/BlpDecoder.h"   // BlpImage
#include "clientdata/DbcStore.h"
#include "model/M2Types.h"

namespace we
{
class ClientData;

// Model class inferred from the file path (which display table supplies its runtime skins).
enum class ModelClass { Unknown, Creature, Item, Character };
ModelClass ClassifyModel(const std::string& m2Path);

// A player-character's customization choices. Indices are 0-based into the options a race/sex
// exposes (see ModelDresser::CharacterInfoFor). The body texture is composited from these; hair
// style/color + facial hair also drive geoset visibility.
struct CharCustomize
{
    uint32_t skinColor = 0;
    uint32_t faceVariation = 0;
    uint32_t hairVariation = 0;
    uint32_t hairColor = 0;
    uint32_t facialVariation = 0;
};

// What a character model offers the customization UI: race/sex ids + how many variants each axis
// has (so the pickers can clamp/step). `valid` is false for non-character models.
struct CharacterOptions
{
    bool valid = false;
    uint32_t race = 0, sex = 0;
    uint32_t skinColors = 1, faceVariations = 1, hairVariations = 1, hairColors = 1, facialVariations = 1;
};

// The appearance-affecting equipment slots (a subset of WoW's inventory slots). Each drives some
// mix of composited armour textures, geoset groups, and attached models — see EquipSlotInfo.
enum class EquipSlot
{
    Head, Shoulder, Back, Chest, Shirt, Tabard, Wrist, Hands, Waist, Legs, Feet, MainHand, OffHand, Ranged,
    Count
};
constexpr int kEquipSlotCount = (int)EquipSlot::Count;
const char* EquipSlotName(EquipSlot s);
// The M2 attachment id a held item in this slot hangs from (0 = not an attached-model slot, i.e.
// the item is composited armour, not a separate model). Head=11, Shoulder=5/6, MainHand=1, etc.
uint32_t EquipSlotAttachment(EquipSlot s);
// The item_template.InventoryType values that belong in this slot (for filtering an item search),
// as a comma-separated list e.g. "13,17,21". Empty if the slot has none.
std::string EquipSlotInvTypes(EquipSlot s);

// An item equipped in a slot: the slot (drives geoset/attachment semantics) + its display record.
struct EquippedItem
{
    EquipSlot slot = EquipSlot::Chest;
    DbcStore::ItemDisplay display;
};

class ModelDresser
{
public:
    ModelDresser(ClientData& cd, const DbcStore& store) : cd_(cd), store_(store) {}

    // Fill `model`'s non-zero-type texture slots with a default skin resolved from the DBCs,
    // based on the model's class/path. No-op when nothing matches or the model has only type-0
    // textures. Returns the number of slots filled. Explicit selection (a chosen display/skin)
    // overrides this — see the per-class resolvers.
    int ResolveDefaultTextures(const std::string& m2Path, m2::M2Model& model);

    // Per-class resolvers (used by the switcher UI, which supplies an explicit choice).
    // Creature: fill monster-skin slots (types 11-13) from a CreatureDisplayInfo skin triple.
    void ApplyCreatureSkins(const std::string& m2Path, const std::string skins[3], m2::M2Model& model);
    // Item: fill the object-skin slot (type 2) from an object-skin base name (same dir as model).
    void ApplyItemObjectSkin(const std::string& m2Path, const std::string& objectSkin, m2::M2Model& model);
    // Character: fill body (type 1), hair (type 6), extra (type 8) from resolved texture paths.
    void ApplyCharacterTextures(m2::M2Model& model, const std::string& bodyTex, const std::string& hairTex,
                                const std::string& extraTex);

    // --- character customization (Phase 2) + equipment (Phase 3) ---
    // Race/sex ids + per-axis option counts for a character model (invalid for non-characters).
    CharacterOptions CharacterInfoFor(const std::string& m2Path);
    // Look up an ItemDisplayInfo row by id (for equipping armour/weapons). False if absent.
    bool ItemDisplayById(uint32_t id, DbcStore::ItemDisplay& out);
    // Composite the body texture (type 1): base skin + chosen face + underwear + each equipped
    // item's armour region textures (ItemDisplayInfo.Texture[8]), blitted onto the skin atlas by
    // texture-region. Returns an invalid image if not resolvable.
    BlpImage ComposeCharacterBody(uint32_t race, uint32_t sex, const CharCustomize& c,
                                  const std::vector<EquippedItem>& equipped = {});
    // The hair (type 6) texture path for the chosen hair style + color.
    std::string CharacterHairTexture(uint32_t race, uint32_t sex, const CharCustomize& c);
    // The base-skin (type 1) texture path. The rendered body is the COMPOSITE (ComposeCharacterBody),
    // but the type-1 slot still needs a real path set so it reads as "resolved" (geoset visibility
    // hides geosets whose runtime texture is missing) and as a decode fallback.
    std::string CharacterBodyTexture(uint32_t race, uint32_t sex, const CharCustomize& c);
    // Compute per-batch geoset visibility for a character: show body geoset 0, the selected hair
    // scalp geoset (CharHairGeosets) and facial-hair geosets (CharacterFacialHairStyles), the bare
    // body variant of each group, plus the geoset groups each equipped item switches (per its slot
    // + ItemDisplayInfo.GeosetGroup, following WoW Model Viewer's composition rules). Also hides
    // hair geosets covered by a helm (ItemDisplayInfo.HelmetGeosetVis). Sized to model.batches.
    void CharacterGeosets(uint32_t race, uint32_t sex, const CharCustomize& c, const m2::M2Model& model,
                          std::vector<uint8_t>& visibleOut,
                          const std::vector<EquippedItem>& equipped = {});

    // Lookups the switcher UI needs.
    // All CreatureDisplayInfo skin triples whose model == this path (for the creature skin combo).
    std::vector<std::array<std::string, 3>> CreatureSkinsFor(const std::string& m2Path);
    // All ItemDisplayInfo object-skin base names for this held-model path (weapon/shield skins).
    std::vector<std::string> ItemObjectSkinsFor(const std::string& m2Path);
    // CharSections base-skin/hair/underwear/face variations matching this character model dir.
    struct CharChoice { std::string label; std::string body, hair, extra; };
    std::vector<CharChoice> CharacterSkinsFor(const std::string& m2Path);

private:
    void EnsureCreature();
    void EnsureItem();
    void EnsureChar();
    const std::vector<DbcStore::CharHairGeoset>& HairGeosets();
    const std::vector<DbcStore::FacialHairStyle>& FacialStyles();

    // race/sex from a character model's directory (matched via the base-skin section texture path).
    // Returns {found, {race, sex}}.
    std::pair<bool, std::pair<uint32_t, uint32_t>> CharacterInfoFromDir(const std::string& dir);
    // Best CharSection for (race, sex, section, variation, color): exact match, else same variation,
    // else any for that section.
    const DbcStore::CharSection* FindCharSection(uint32_t race, uint32_t sex, uint32_t section,
                                                 uint32_t variation, uint32_t color);

    ClientData& cd_;
    const DbcStore& store_;

    // Creature caches.
    bool creatureLoaded_ = false;
    std::unordered_map<uint32_t, std::string> creatureModelPaths_;             // modelId -> .m2 (lower)
    std::unordered_map<uint32_t, DbcStore::CreatureDisplay> creatureDisplays_; // displayId -> display
    // Several modelIds can share one model file, so a path maps to a LIST of modelIds; missing
    // this is why a creature whose first modelId has an empty skin resolved to nothing.
    std::unordered_map<std::string, std::vector<uint32_t>> pathToModelIds_;    // lower(.m2) -> modelIds
    std::unordered_map<uint32_t, std::vector<uint32_t>> modelToDisplays_;      // modelId -> displayIds

    // Item caches.
    bool itemLoaded_ = false;
    std::unordered_map<uint32_t, DbcStore::ItemDisplay> itemsById_;   // ItemDisplayInfo by id
    // lower(model basename, .m2) -> object-skin base names (one per ItemDisplayInfo using it).
    std::unordered_map<std::string, std::vector<std::string>> itemModelToSkins_;

    // Character caches.
    bool charLoaded_ = false;
    std::vector<DbcStore::CharSection> charSections_;
    bool hairGeoLoaded_ = false;
    std::vector<DbcStore::CharHairGeoset> hairGeosets_;
    bool facialLoaded_ = false;
    std::vector<DbcStore::FacialHairStyle> facialStyles_;
};
} // namespace we
