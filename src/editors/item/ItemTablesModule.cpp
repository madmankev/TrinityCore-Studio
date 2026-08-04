// ItemTablesModule — see ItemTablesModule.h. Layouts verified with --dbc-dump (field counts:
// Item=8, ItemDisplayInfo=25, ItemSet=53, ItemExtendedCost=16, ItemRandomProperties=24,
// ItemRandomSuffix=29, GemProperties=5, ItemLimitCategory=20, ItemBagFamily/ItemPetFood=18).

#include "editors/item/ItemTablesModule.h"

#include "clientdata/DbcSchema.h"
#include "editors/item/ItemDbcSchema.h"

namespace we
{
namespace
{
using T = DbcFieldType;

const DbcSchema& DisplayInfoSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32},
        {"ModelName1", T::String}, {"ModelName2", T::String},
        {"ModelTexture1", T::String}, {"ModelTexture2", T::String},
        {"InventoryIcon1", T::String}, {"InventoryIcon2", T::String},
        {"GeosetGroup1", T::UInt32}, {"GeosetGroup2", T::UInt32}, {"GeosetGroup3", T::UInt32},
        {"Flags", T::UInt32}, {"SpellVisualID", T::UInt32}, {"GroupSoundIndex", T::UInt32},
        {"HelmetGeosetVis1", T::UInt32}, {"HelmetGeosetVis2", T::UInt32},
        {"Texture1", T::String}, {"Texture2", T::String}, {"Texture3", T::String},
        {"Texture4", T::String}, {"Texture5", T::String}, {"Texture6", T::String},
        {"Texture7", T::String}, {"Texture8", T::String},
        {"ItemVisual", T::UInt32}, {"ParticleColorID", T::UInt32},
    }};
    return s;
}
const DbcSchema& SetSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Name", T::LangString},
        {"ItemID1", T::UInt32}, {"ItemID2", T::UInt32}, {"ItemID3", T::UInt32}, {"ItemID4", T::UInt32},
        {"ItemID5", T::UInt32}, {"ItemID6", T::UInt32}, {"ItemID7", T::UInt32}, {"ItemID8", T::UInt32},
        {"ItemID9", T::UInt32}, {"ItemID10", T::UInt32}, {"ItemID11", T::UInt32}, {"ItemID12", T::UInt32},
        {"ItemID13", T::UInt32}, {"ItemID14", T::UInt32}, {"ItemID15", T::UInt32}, {"ItemID16", T::UInt32},
        {"ItemID17", T::UInt32},
        {"SetSpellID1", T::UInt32}, {"SetSpellID2", T::UInt32}, {"SetSpellID3", T::UInt32},
        {"SetSpellID4", T::UInt32}, {"SetSpellID5", T::UInt32}, {"SetSpellID6", T::UInt32},
        {"SetSpellID7", T::UInt32}, {"SetSpellID8", T::UInt32},
        {"SetThreshold1", T::UInt32}, {"SetThreshold2", T::UInt32}, {"SetThreshold3", T::UInt32},
        {"SetThreshold4", T::UInt32}, {"SetThreshold5", T::UInt32}, {"SetThreshold6", T::UInt32},
        {"SetThreshold7", T::UInt32}, {"SetThreshold8", T::UInt32},
        {"RequiredSkill", T::UInt32}, {"RequiredSkillRank", T::UInt32},
    }};
    return s;
}
const DbcSchema& ExtendedCostSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"HonorPoints", T::UInt32}, {"ArenaPoints", T::UInt32},
        {"ArenaBracket", T::UInt32},
        {"ItemID1", T::UInt32}, {"ItemID2", T::UInt32}, {"ItemID3", T::UInt32}, {"ItemID4", T::UInt32},
        {"ItemID5", T::UInt32},
        {"ItemCount1", T::UInt32}, {"ItemCount2", T::UInt32}, {"ItemCount3", T::UInt32},
        {"ItemCount4", T::UInt32}, {"ItemCount5", T::UInt32},
        {"RequiredArenaRating", T::UInt32}, {"ItemPurchaseGroup", T::UInt32},
    }};
    return s;
}
const DbcSchema& RandomPropertiesSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"InternalName", T::String},
        {"Enchantment1", T::UInt32}, {"Enchantment2", T::UInt32}, {"Enchantment3", T::UInt32},
        {"Enchantment4", T::UInt32}, {"Enchantment5", T::UInt32},
        {"Name", T::LangString},
    }};
    return s;
}
const DbcSchema& RandomSuffixSchema()
{
    static const DbcSchema s = {{
        {"ID", T::UInt32}, {"Name", T::LangString}, {"InternalName", T::String},
        {"Enchantment1", T::UInt32}, {"Enchantment2", T::UInt32}, {"Enchantment3", T::UInt32},
        {"Enchantment4", T::UInt32}, {"Enchantment5", T::UInt32},
        {"AllocationPct1", T::UInt32}, {"AllocationPct2", T::UInt32}, {"AllocationPct3", T::UInt32},
        {"AllocationPct4", T::UInt32}, {"AllocationPct5", T::UInt32},
    }};
    return s;
}
const DbcSchema& GemPropertiesSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"EnchantID", T::UInt32},
                                 {"MaxCountInv", T::UInt32}, {"MaxCountItem", T::UInt32},
                                 {"Type", T::UInt32}}};
    return s;
}
const DbcSchema& LimitCategorySchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}, {"Quantity", T::UInt32},
                                 {"Flags", T::UInt32}}};
    return s;
}
const DbcSchema& BagFamilySchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}}};
    return s;
}
const DbcSchema& PetFoodSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Name", T::LangString}}};
    return s;
}
} // namespace

const std::vector<DbcTableDef>& ItemTableDefs()
{
    static const std::vector<DbcTableDef> defs = {
        {"Item", "DBFilesClient\\Item.dbc", &ItemDbcSchema(), {5}},
        {"ItemDisplayInfo", "DBFilesClient\\ItemDisplayInfo.dbc", &DisplayInfoSchema(), {5}},
        {"ItemSet", "DBFilesClient\\ItemSet.dbc", &SetSchema(), {1}},
        {"ItemExtendedCost", "DBFilesClient\\ItemExtendedCost.dbc", &ExtendedCostSchema(), {}},
        {"ItemRandomProperties", "DBFilesClient\\ItemRandomProperties.dbc", &RandomPropertiesSchema(), {7}},
        {"ItemRandomSuffix", "DBFilesClient\\ItemRandomSuffix.dbc", &RandomSuffixSchema(), {1}},
        {"GemProperties", "DBFilesClient\\GemProperties.dbc", &GemPropertiesSchema(), {1}},
        {"ItemLimitCategory", "DBFilesClient\\ItemLimitCategory.dbc", &LimitCategorySchema(), {1}},
        {"ItemBagFamily", "DBFilesClient\\ItemBagFamily.dbc", &BagFamilySchema(), {1}},
        {"ItemPetFood", "DBFilesClient\\ItemPetFood.dbc", &PetFoodSchema(), {1}},
    };
    return defs;
}
} // namespace we
