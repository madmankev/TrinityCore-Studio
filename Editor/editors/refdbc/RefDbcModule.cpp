// RefDbcModule — see RefDbcModule.h. Field counts + string positions verified with --dbc-dump
// against a real 3.3.5a client (SpellItemEnchantment=38, Lock=33, SoundEntries=30, Vehicle=40,
// VehicleSeat=58, MailTemplate=35, EmotesText=19, Holidays=55) and cross-checked to TC
// DBCStructure.h. Scalar u32/i32/float types are best-effort for the UI; the loose-overlay
// serializer round-trips bytes regardless, and --refdbc-roundtrip validates every schema.

#include "editors/refdbc/RefDbcModule.h"

#include <string>
#include <vector>

#include "clientdata/DbcSchema.h"

namespace we
{
namespace
{
using T = DbcFieldType;

// Owns generated array-field name strings (reserved so the const char* stay valid as it fills).
const char* Own(const std::string& s)
{
    static std::vector<std::string> store = [] {
        std::vector<std::string> v;
        v.reserve(1024);
        return v;
    }();
    store.push_back(s);
    return store.back().c_str();
}
void AddArr(std::vector<DbcFieldDef>& f, const char* prefix, int n, T t)
{
    for (int i = 1; i <= n; ++i)
        f.push_back({Own(std::string(prefix) + std::to_string(i)), t});
}

const DbcSchema& SpellItemEnchantmentSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}, {"Charges", T::UInt32}};
        AddArr(f, "Effect", 3, T::UInt32);            // aura/effect type per slot
        AddArr(f, "EffectPointsMin", 3, T::Int32);
        AddArr(f, "EffectPointsMax", 3, T::Int32);
        AddArr(f, "EffectArg", 3, T::UInt32);         // spell/stat arg per slot
        f.push_back({"Name", T::LangString});
        f.push_back({"ItemVisual", T::UInt32});
        f.push_back({"Flags", T::UInt32});
        f.push_back({"SrcItemID", T::UInt32});
        f.push_back({"Condition", T::UInt32});        // SpellItemEnchantmentCondition id
        f.push_back({"RequiredSkillID", T::UInt32});
        f.push_back({"RequiredSkillRank", T::UInt32});
        f.push_back({"MinLevel", T::UInt32});
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& LockSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}};
        AddArr(f, "Type", 8, T::UInt32);      // 1 = item, 2 = LockProperties id
        AddArr(f, "Index", 8, T::UInt32);     // the referenced property/skill/item id
        AddArr(f, "Skill", 8, T::UInt32);     // required skill amount
        AddArr(f, "Action", 8, T::UInt32);
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& SoundEntriesSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}, {"SoundType", T::UInt32}, {"Name", T::String}};
        AddArr(f, "File", 10, T::String);
        AddArr(f, "Freq", 10, T::UInt32);
        f.push_back({"DirectoryBase", T::String});
        f.push_back({"VolumeFloat", T::Float});
        f.push_back({"Flags", T::UInt32});
        f.push_back({"MinDistance", T::Float});
        f.push_back({"DistanceCutoff", T::Float});
        f.push_back({"EAXDef", T::UInt32});
        f.push_back({"SoundEntriesAdvancedID", T::UInt32});
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& VehicleSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}, {"Flags", T::UInt32}, {"TurnSpeed", T::Float},
                                      {"PitchSpeed", T::Float}, {"PitchMin", T::Float}, {"PitchMax", T::Float}};
        AddArr(f, "SeatID", 8, T::UInt32);
        f.push_back({"MouseLookOffsetPitch", T::Float});
        f.push_back({"CameraFadeDistScalarMin", T::Float});
        f.push_back({"CameraFadeDistScalarMax", T::Float});
        f.push_back({"CameraPitchOffset", T::Float});
        f.push_back({"FacingLimitRight", T::Float});
        f.push_back({"FacingLimitLeft", T::Float});
        f.push_back({"MsslTrgtTurnLingering", T::Float});
        f.push_back({"MsslTrgtPitchLingering", T::Float});
        f.push_back({"MsslTrgtMouseLingering", T::Float});
        f.push_back({"MsslTrgtEndOpacity", T::Float});
        f.push_back({"MsslTrgtArcSpeed", T::Float});
        f.push_back({"MsslTrgtArcRepeat", T::Float});
        f.push_back({"MsslTrgtArcWidth", T::Float});
        f.push_back({"MsslTrgtImpactRadius1", T::Float});
        f.push_back({"MsslTrgtImpactRadius2", T::Float});
        f.push_back({"MsslTrgtArcTexture", T::String});
        f.push_back({"MsslTrgtImpactTexture", T::String});
        f.push_back({"MsslTrgtImpactModel1", T::String});
        f.push_back({"MsslTrgtImpactModel2", T::String});
        f.push_back({"CameraYawOffset", T::Float});
        f.push_back({"UiLocomotionType", T::UInt32});
        f.push_back({"MsslTrgtImpactTexRadius", T::Float});
        f.push_back({"VehicleUIIndicatorID", T::UInt32});
        AddArr(f, "PowerDisplayID", 3, T::UInt32);
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& VehicleSeatSchema()
{
    // 58 fields; all numeric (no strings). Named generically — an advanced, rarely hand-edited table.
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}};
        AddArr(f, "Field", 57, T::UInt32);
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& MailTemplateSchema()
{
    static const DbcSchema s = {{{"ID", T::UInt32}, {"Subject", T::LangString}, {"Body", T::LangString}}};
    return s;
}
const DbcSchema& EmotesTextSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}, {"Name", T::String}, {"EmoteID", T::UInt32}};
        AddArr(f, "EmoteVariation", 16, T::UInt32);
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& CreatureDisplayInfoSchema()
{
    // 16 fields; texture-variation + portrait strings verified at physical indices 6-9 via --dbc-dump.
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {
            {"ID", T::UInt32}, {"ModelID", T::UInt32}, {"SoundID", T::UInt32},
            {"ExtendedDisplayInfoID", T::UInt32}, {"CreatureModelScale", T::Float},
            {"CreatureModelAlpha", T::UInt32}, {"TextureVariation1", T::String},
            {"TextureVariation2", T::String}, {"TextureVariation3", T::String},
            {"PortraitTextureName", T::String}, {"BloodID", T::UInt32}, {"NPCSoundID", T::UInt32},
            {"ParticleColorID", T::UInt32}, {"CreatureGeosetData", T::UInt32},
            {"ObjectEffectPackageID", T::UInt32}, {"Field16", T::UInt32},
        };
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& CreatureModelDataSchema()
{
    // 28 fields; ModelName (.mdx path) string at physical index 2.
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {
            {"ID", T::UInt32}, {"Flags", T::UInt32}, {"ModelName", T::String}, {"SizeClass", T::UInt32},
            {"ModelScale", T::Float}, {"BloodID", T::UInt32}, {"FootprintTextureID", T::UInt32},
            {"FootprintTextureLength", T::Float}, {"FootprintTextureWidth", T::Float},
            {"FootprintParticleScale", T::Float}, {"FoleyMaterialID", T::UInt32},
            {"FootstepShakeSize", T::UInt32}, {"DeathThudShakeSize", T::UInt32}, {"SoundID", T::UInt32},
            {"CollisionWidth", T::Float}, {"CollisionHeight", T::Float}, {"MountHeight", T::Float},
        };
        AddArr(f, "GeoBoxMin", 3, T::Float);
        AddArr(f, "GeoBoxMax", 3, T::Float);
        f.push_back({"WorldEffectScale", T::Float});
        f.push_back({"AttachedEffectScale", T::Float});
        f.push_back({"MissileCollisionRadius", T::Float});
        f.push_back({"MissileCollisionPush", T::Float});
        f.push_back({"MissileCollisionRaise", T::Float});
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& GameObjectDisplayInfoSchema()
{
    // 19 fields; ModelName (.mdx path) string at physical index 1.
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}, {"ModelName", T::String}};
        AddArr(f, "Sound", 10, T::UInt32);
        AddArr(f, "GeoBoxMin", 3, T::Float);
        AddArr(f, "GeoBoxMax", 3, T::Float);
        f.push_back({"ObjectEffectPackageID", T::UInt32});
        return DbcSchema{f};
    }();
    return s;
}
const DbcSchema& HolidaysSchema()
{
    static const DbcSchema s = [] {
        std::vector<DbcFieldDef> f = {{"ID", T::UInt32}};
        AddArr(f, "Duration", 10, T::UInt32);
        AddArr(f, "Date", 26, T::UInt32);
        f.push_back({"Region", T::UInt32});
        f.push_back({"Looping", T::UInt32});
        AddArr(f, "CalendarFlags", 10, T::UInt32);
        f.push_back({"HolidayNameID", T::UInt32});
        f.push_back({"HolidayDescriptionID", T::UInt32});
        f.push_back({"TextureFilename", T::String});
        f.push_back({"Priority", T::UInt32});
        f.push_back({"CalendarFilterType", T::Int32});
        f.push_back({"Flags", T::UInt32});
        return DbcSchema{f};
    }();
    return s;
}
} // namespace

const std::vector<DbcTableDef>& RefDbcTableDefs()
{
    static const std::vector<DbcTableDef> defs = {
        {"SpellItemEnchantment", "DBFilesClient\\SpellItemEnchantment.dbc", &SpellItemEnchantmentSchema(), {14}},
        {"Lock", "DBFilesClient\\Lock.dbc", &LockSchema(), {}},
        {"SoundEntries", "DBFilesClient\\SoundEntries.dbc", &SoundEntriesSchema(), {2}},
        {"Vehicle", "DBFilesClient\\Vehicle.dbc", &VehicleSchema(), {}},
        {"VehicleSeat", "DBFilesClient\\VehicleSeat.dbc", &VehicleSeatSchema(), {}},
        {"MailTemplate", "DBFilesClient\\MailTemplate.dbc", &MailTemplateSchema(), {1}},
        {"EmotesText", "DBFilesClient\\EmotesText.dbc", &EmotesTextSchema(), {1}},
        {"Holidays", "DBFilesClient\\Holidays.dbc", &HolidaysSchema(), {51}},
        // Display/model DBCs (creature_template.modelid / gameobject_template.displayId point here) —
        // making them editable closes the custom-model gap. ItemDisplayInfo is in the Item Tables editor.
        {"CreatureDisplayInfo", "DBFilesClient\\CreatureDisplayInfo.dbc", &CreatureDisplayInfoSchema(), {}},
        {"CreatureModelData", "DBFilesClient\\CreatureModelData.dbc", &CreatureModelDataSchema(), {2}},
        {"GameObjectDisplayInfo", "DBFilesClient\\GameObjectDisplayInfo.dbc", &GameObjectDisplayInfoSchema(), {1}},
    };
    return defs;
}
} // namespace we
