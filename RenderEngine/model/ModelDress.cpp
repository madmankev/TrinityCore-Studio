// ModelDress — see ModelDress.h.

#include "model/ModelDress.h"

#include <algorithm>
#include <cctype>

#include "clientdata/ClientData.h"

namespace we
{
namespace
{
std::string ToLower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Directory part of a WoW path ("Creature\Foo\Bar.m2" -> "Creature\Foo"), backslash-delimited.
std::string DirOf(const std::string& p)
{
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : p.substr(0, slash);
}

// Filename part ("Item\...\Sword.m2" -> "Sword.m2").
std::string BaseName(const std::string& p)
{
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? p : p.substr(slash + 1);
}

// Normalize a model reference to a lowercased ".m2" name (DBCs store .mdx/.mdl).
std::string NormM2(std::string p)
{
    size_t dot = p.find_last_of('.');
    if (dot != std::string::npos) p = p.substr(0, dot);
    if (!p.empty()) p += ".m2";
    return ToLower(p);
}

// Case-insensitive "does `s` start with `prefix`".
bool StartsWithI(const std::string& s, const std::string& prefix)
{
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i]))
            return false;
    return true;
}
} // namespace

const char* EquipSlotName(EquipSlot s)
{
    switch (s)
    {
        case EquipSlot::Head: return "Head";       case EquipSlot::Shoulder: return "Shoulder";
        case EquipSlot::Back: return "Back";       case EquipSlot::Chest: return "Chest";
        case EquipSlot::Shirt: return "Shirt";     case EquipSlot::Tabard: return "Tabard";
        case EquipSlot::Wrist: return "Wrist";     case EquipSlot::Hands: return "Hands";
        case EquipSlot::Waist: return "Waist";     case EquipSlot::Legs: return "Legs";
        case EquipSlot::Feet: return "Feet";       case EquipSlot::MainHand: return "Main Hand";
        case EquipSlot::OffHand: return "Off Hand"; case EquipSlot::Ranged: return "Ranged";
        default: return "?";
    }
}

uint32_t EquipSlotAttachment(EquipSlot s)
{
    switch (s)
    {
        case EquipSlot::Head: return 11;      // Helm
        case EquipSlot::Shoulder: return 5;   // ShoulderRight (left = 6, added as a mirrored instance)
        case EquipSlot::MainHand: return 1;   // HandRight
        case EquipSlot::OffHand: return 2;    // HandLeft
        case EquipSlot::Ranged: return 1;     // approximate (bows use a dedicated attach)
        default: return 0;                    // composited armour, not an attached model
    }
}

std::string EquipSlotInvTypes(EquipSlot s)
{
    // item_template.InventoryType values (WoW INVTYPE_*) that a slot accepts.
    switch (s)
    {
        case EquipSlot::Head: return "1";
        case EquipSlot::Shoulder: return "3";
        case EquipSlot::Back: return "16";
        case EquipSlot::Chest: return "5,20";        // chest + robe
        case EquipSlot::Shirt: return "4";
        case EquipSlot::Tabard: return "19";
        case EquipSlot::Wrist: return "9";
        case EquipSlot::Hands: return "10";
        case EquipSlot::Waist: return "6";
        case EquipSlot::Legs: return "7";
        case EquipSlot::Feet: return "8";
        case EquipSlot::MainHand: return "13,17,21"; // one-hand, two-hand, main-hand
        case EquipSlot::OffHand: return "14,22,23";  // shield, off-hand, holdable
        case EquipSlot::Ranged: return "15,25,26";   // bow, thrown, gun/wand
        default: return "";
    }
}

ModelClass ClassifyModel(const std::string& m2Path)
{
    std::string p = ToLower(m2Path);
    if (StartsWithI(p, "character\\") || StartsWithI(p, "character/"))
        return ModelClass::Character;
    if (p.find("item\\objectcomponents\\") != std::string::npos ||
        p.find("item/objectcomponents/") != std::string::npos)
        return ModelClass::Item;
    if (StartsWithI(p, "creature\\") || StartsWithI(p, "creature/"))
        return ModelClass::Creature;
    return ModelClass::Unknown;
}

// --- lazy DBC loads --------------------------------------------------------

void ModelDresser::EnsureCreature()
{
    if (creatureLoaded_) return;
    creatureLoaded_ = true;
    creatureModelPaths_ = store_.LoadCreatureModelPaths(cd_);
    creatureDisplays_ = store_.LoadCreatureDisplays(cd_);
    for (const auto& kv : creatureModelPaths_)
        pathToModelIds_[NormM2(kv.second)].push_back(kv.first);
    for (const auto& kv : creatureDisplays_)
        modelToDisplays_[kv.second.modelId].push_back(kv.first);
}

void ModelDresser::EnsureItem()
{
    if (itemLoaded_) return;
    itemLoaded_ = true;
    itemsById_ = store_.LoadItemDisplays(cd_);
    for (const auto& kv : itemsById_)
    {
        const DbcStore::ItemDisplay& d = kv.second;
        for (int k = 0; k < 2; ++k)
        {
            if (d.modelName[k].empty() || d.modelTexture[k].empty())
                continue;
            itemModelToSkins_[NormM2(BaseName(d.modelName[k]))].push_back(d.modelTexture[k]);
        }
    }
}

bool ModelDresser::ItemDisplayById(uint32_t id, DbcStore::ItemDisplay& out)
{
    EnsureItem();
    auto it = itemsById_.find(id);
    if (it == itemsById_.end()) return false;
    out = it->second;
    return true;
}

void ModelDresser::EnsureChar()
{
    if (charLoaded_) return;
    charLoaded_ = true;
    charSections_ = store_.LoadCharSections(cd_);
}

const std::vector<DbcStore::CharHairGeoset>& ModelDresser::HairGeosets()
{
    if (!hairGeoLoaded_) { hairGeoLoaded_ = true; hairGeosets_ = store_.LoadCharHairGeosets(cd_); }
    return hairGeosets_;
}

const std::vector<DbcStore::FacialHairStyle>& ModelDresser::FacialStyles()
{
    if (!facialLoaded_) { facialLoaded_ = true; facialStyles_ = store_.LoadFacialHairStyles(cd_); }
    return facialStyles_;
}

std::pair<bool, std::pair<uint32_t, uint32_t>> ModelDresser::CharacterInfoFromDir(const std::string& dir)
{
    EnsureChar();
    // The base-skin (section 0) textures live in the model's gender directory; the row carries the
    // numeric race/sex, which drives every other section/geoset lookup.
    for (const DbcStore::CharSection& s : charSections_)
        if (s.baseSection == 0 && !s.textures[0].empty() && StartsWithI(s.textures[0], dir))
            return {true, {s.race, s.sex}};
    return {false, {0, 0}};
}

// --- lookups for the switcher UI -------------------------------------------

std::vector<std::array<std::string, 3>> ModelDresser::CreatureSkinsFor(const std::string& m2Path)
{
    EnsureCreature();
    std::vector<std::array<std::string, 3>> out;
    auto it = pathToModelIds_.find(NormM2(m2Path));
    if (it == pathToModelIds_.end())
        return out;
    for (uint32_t modelId : it->second)   // every modelId sharing this model file
    {
        auto dit = modelToDisplays_.find(modelId);
        if (dit == modelToDisplays_.end())
            continue;
        for (uint32_t displayId : dit->second)
        {
            auto d = creatureDisplays_.find(displayId);
            if (d == creatureDisplays_.end())
                continue;
            out.push_back({d->second.skins[0], d->second.skins[1], d->second.skins[2]});
        }
    }
    return out;
}

std::vector<std::string> ModelDresser::ItemObjectSkinsFor(const std::string& m2Path)
{
    EnsureItem();
    auto it = itemModelToSkins_.find(NormM2(BaseName(m2Path)));
    if (it == itemModelToSkins_.end())
        return {};
    // Dedupe while preserving order.
    std::vector<std::string> out;
    for (const std::string& s : it->second)
        if (std::find(out.begin(), out.end(), s) == out.end())
            out.push_back(s);
    return out;
}

std::vector<ModelDresser::CharChoice> ModelDresser::CharacterSkinsFor(const std::string& m2Path)
{
    EnsureChar();
    // Identify the race/sex numerically from the base-skin section (see CharacterInfoFromDir).
    // Hair (section 3) is then matched by (race, sex) ids, since hair textures live in the RACE
    // dir, not the gender dir — a path prefix match alone would miss them.
    auto info = CharacterInfoFromDir(DirOf(m2Path));
    if (!info.first)
        return {};
    const uint32_t race = info.second.first, sex = info.second.second;

    // Default hair (section 3): the first variation for this race/sex with a texture (variation 0).
    std::string hair, extra;
    for (const DbcStore::CharSection& s : charSections_)
        if (s.baseSection == 3 && s.race == race && s.sex == sex && !s.textures[0].empty())
        {
            hair = s.textures[0];
            if (s.variation == 0) break;   // prefer variation 0, else keep the first seen
        }

    std::vector<CharChoice> out;
    for (const DbcStore::CharSection& s : charSections_)
    {
        if (s.baseSection != 0 || s.race != race || s.sex != sex || s.textures[0].empty())
            continue;
        CharChoice c;
        c.body = s.textures[0];
        c.hair = hair;
        c.extra = extra;
        c.label = "skin " + std::to_string(s.color) + "_" + std::to_string(s.variation);
        out.push_back(std::move(c));
    }
    return out;
}

// --- per-class appliers ----------------------------------------------------

void ModelDresser::ApplyCreatureSkins(const std::string& m2Path, const std::string skins[3],
                                      m2::M2Model& model)
{
    const std::string dir = DirOf(m2Path);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t >= 11 && t <= 13 && !skins[t - 11].empty())
            model.texturePaths[i] = dir + "\\" + skins[t - 11] + ".blp";
    }
}

void ModelDresser::ApplyItemObjectSkin(const std::string& m2Path, const std::string& objectSkin,
                                       m2::M2Model& model)
{
    if (objectSkin.empty())
        return;
    const std::string dir = DirOf(m2Path);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t == 2)
            model.texturePaths[i] = dir + "\\" + objectSkin + ".blp";
    }
}

void ModelDresser::ApplyCharacterTextures(m2::M2Model& model, const std::string& bodyTex,
                                          const std::string& hairTex, const std::string& extraTex)
{
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t == 1 && !bodyTex.empty()) model.texturePaths[i] = bodyTex;   // body skin (full path)
        else if (t == 6 && !hairTex.empty()) model.texturePaths[i] = hairTex;   // hair
        else if (t == 8 && !extraTex.empty()) model.texturePaths[i] = extraTex; // skin extra
    }
}

// --- character customization (Phase 2) -------------------------------------
namespace
{
// Character body-texture regions, expressed in the classic 256x256 base-texture space (they are
// scaled to the actual skin-atlas size at composite time). Verified empirically against the real
// skin atlas: top-left = arms, bottom-left = face, right = torso/pelvis/legs/feet.
struct Rect { int x, y, w, h; };
constexpr Rect kRegionFaceUpper{0, 160, 128, 32};   // CharSections Face texture[1]
constexpr Rect kRegionFaceLower{0, 192, 128, 64};   // CharSections Face texture[0]
constexpr Rect kRegionPelvis{128, 96, 128, 64};     // underwear panties/loincloth (LegUpper region)
constexpr Rect kRegionTorsoUpper{128, 0, 128, 64};  // underwear bra (chest), female "NakedTorso"

// The eight armour regions of ItemDisplayInfo.Texture[8], in array order. The texture files live
// under Item\TextureComponents\<folder>\<name>_U.blp.
struct ArmorRegion { Rect rect; const char* folder; };
constexpr ArmorRegion kArmorRegions[8] = {
    {{0, 0, 128, 64},    "ArmUpperTexture"},    // 0 upper arm (sleeve)
    {{0, 64, 128, 64},   "ArmLowerTexture"},    // 1 lower arm (cuff)
    {{0, 128, 128, 32},  "HandTexture"},        // 2 hand (glove)
    {{128, 0, 128, 64},  "TorsoUpperTexture"},  // 3 upper torso (chest)
    {{128, 64, 128, 32}, "TorsoLowerTexture"},  // 4 lower torso (belt/shirt)
    {{128, 96, 128, 64}, "LegUpperTexture"},    // 5 upper leg (pants)
    {{128, 160, 128, 64},"LegLowerTexture"},    // 6 lower leg (pants)
    {{128, 224, 128, 32},"FootTexture"},        // 7 foot (boot)
};

// Alpha-composite `src` onto `atlas` inside `region` (given in 256-space, scaled to the atlas).
// src is stretched to the scaled region with nearest sampling; blended over by src alpha.
void BlitRegion(BlpImage& atlas, const BlpImage& src, const Rect& region256)
{
    if (!atlas.valid() || !src.valid())
        return;
    const float sx = atlas.width / 256.0f, sy = atlas.height / 256.0f;
    const int dx0 = (int)(region256.x * sx), dy0 = (int)(region256.y * sy);
    const int dw = (int)(region256.w * sx), dh = (int)(region256.h * sy);
    for (int j = 0; j < dh; ++j)
    {
        const int ay = dy0 + j;
        if (ay < 0 || ay >= atlas.height) continue;
        const int syPix = std::min(src.height - 1, j * src.height / std::max(1, dh));
        for (int i = 0; i < dw; ++i)
        {
            const int ax = dx0 + i;
            if (ax < 0 || ax >= atlas.width) continue;
            const int sxPix = std::min(src.width - 1, i * src.width / std::max(1, dw));
            const uint8_t* s = &src.rgba[(size_t)(syPix * src.width + sxPix) * 4];
            uint8_t* d = &atlas.rgba[(size_t)(ay * atlas.width + ax) * 4];
            const int a = s[3];
            if (a == 0) continue;
            if (a == 255) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; continue; }
            for (int k = 0; k < 3; ++k)
                d[k] = (uint8_t)((s[k] * a + d[k] * (255 - a)) / 255);
            d[3] = (uint8_t)std::min(255, d[3] + a);
        }
    }
}
} // namespace

const DbcStore::CharSection* ModelDresser::FindCharSection(uint32_t race, uint32_t sex, uint32_t section,
                                                          uint32_t variation, uint32_t color)
{
    EnsureChar();
    const DbcStore::CharSection* exact = nullptr;
    const DbcStore::CharSection* varOnly = nullptr;
    const DbcStore::CharSection* any = nullptr;
    for (const DbcStore::CharSection& s : charSections_)
    {
        if (s.race != race || s.sex != sex || s.baseSection != section || s.textures[0].empty())
            continue;
        if (s.variation == variation && s.color == color) { exact = &s; break; }
        if (s.variation == variation && !varOnly) varOnly = &s;
        if (!any) any = &s;
    }
    return exact ? exact : (varOnly ? varOnly : any);
}

BlpImage ModelDresser::ComposeCharacterBody(uint32_t race, uint32_t sex, const CharCustomize& c,
                                            const std::vector<EquippedItem>& equipped)
{
    // Base skin (full atlas) is the bottom layer; the chosen face + underwear are overlaid by region.
    const DbcStore::CharSection* skin = FindCharSection(race, sex, 0, 0, c.skinColor);
    if (!skin)
        return {};
    BlpImage atlas = DecodeBlp(cd_.ReadFile(skin->textures[0]));
    if (!atlas.valid())
        return {};

    // Underwear (section 4): panties (pelvis) + bra (chest, female). The texture NAME carries its
    // region ("...NakedTorso..." -> chest, "...NakedPelvis..." -> pelvis), which is robust to the
    // order of the section's texture slots.
    if (const DbcStore::CharSection* uw = FindCharSection(race, sex, 4, 0, c.skinColor))
        for (const std::string& tex : uw->textures)
        {
            if (tex.empty()) continue;
            const bool torso = ToLower(tex).find("torso") != std::string::npos;
            BlitRegion(atlas, DecodeBlp(cd_.ReadFile(tex)), torso ? kRegionTorsoUpper : kRegionPelvis);
        }

    // Equipped armour: each item paints up to 8 body regions (ItemDisplayInfo.Texture[8]) over the
    // skin/underwear. Later-equipped items draw on top. (Held-model slots have empty Texture[8].)
    for (const EquippedItem& e : equipped)
        for (int r = 0; r < 8; ++r)
        {
            if (e.display.texture[r].empty()) continue;
            std::string path = std::string("Item\\TextureComponents\\") + kArmorRegions[r].folder + "\\" +
                               e.display.texture[r] + "_U.blp";
            BlitRegion(atlas, DecodeBlp(cd_.ReadFile(path)), kArmorRegions[r].rect);
        }

    // Face (section 1): textures[0]=FaceLower, textures[1]=FaceUpper. Overlays the base's face.
    if (const DbcStore::CharSection* face = FindCharSection(race, sex, 1, c.faceVariation, c.skinColor))
    {
        if (!face->textures[0].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(face->textures[0])), kRegionFaceLower);
        if (!face->textures[1].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(face->textures[1])), kRegionFaceUpper);
    }

    // Scalp (hair section 3): textures[1]=ScalpLower, textures[2]=ScalpUpper are the hair drawn ONTO
    // the skin (hairline, roots, short hair), tinted by hair color; textures[0] is the hair MESH
    // texture (the type-6 slot, handled separately). Look up the EXACT hair variation here (with a
    // color fallback WITHIN that variation) — NOT FindCharSection, which skips empty-mesh rows and
    // would fall back to a HAIRED variation, painting hair onto a BALD head. A bald variation's scalp
    // textures are empty, so this correctly composites nothing.
    {
        const DbcStore::CharSection* scalp = nullptr;
        for (const DbcStore::CharSection& s : charSections_)
        {
            if (s.race != race || s.sex != sex || s.baseSection != 3 || s.variation != c.hairVariation)
                continue;
            if (s.color == c.hairColor) { scalp = &s; break; }
            if (!scalp) scalp = &s;   // first row for this variation (color fallback)
        }
        if (scalp)
        {
            if (!scalp->textures[1].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(scalp->textures[1])), kRegionFaceLower);
            if (!scalp->textures[2].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(scalp->textures[2])), kRegionFaceUpper);
        }
    }

    // Facial hair (section 2): lower-face detail (beard base), drawn over the face region. Facial
    // hair is tinted by the HAIR colour (not the skin colour) — its CharSections rows are keyed by
    // (variation = facial style, color = hair color).
    if (c.facialVariation > 0)
        if (const DbcStore::CharSection* fh = FindCharSection(race, sex, 2, c.facialVariation, c.hairColor))
        {
            if (!fh->textures[0].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(fh->textures[0])), kRegionFaceLower);
            if (!fh->textures[1].empty()) BlitRegion(atlas, DecodeBlp(cd_.ReadFile(fh->textures[1])), kRegionFaceUpper);
        }
    return atlas;
}

std::string ModelDresser::CharacterHairTexture(uint32_t race, uint32_t sex, const CharCustomize& c)
{
    const DbcStore::CharSection* hair = FindCharSection(race, sex, 3, c.hairVariation, c.hairColor);
    return hair ? hair->textures[0] : std::string{};
}

std::string ModelDresser::CharacterBodyTexture(uint32_t race, uint32_t sex, const CharCustomize& c)
{
    // Section 0 = base skin (same source ComposeCharacterBody composites onto).
    const DbcStore::CharSection* skin = FindCharSection(race, sex, 0, 0, c.skinColor);
    return skin ? skin->textures[0] : std::string{};
}

void ModelDresser::CharacterGeosets(uint32_t race, uint32_t sex, const CharCustomize& c,
                                    const m2::M2Model& model, std::vector<uint8_t>& visibleOut,
                                    const std::vector<EquippedItem>& equipped)
{
    visibleOut.assign(model.batches.size(), 1);

    // Geoset selection follows WoW Model Viewer exactly: for each group (group = id/100) the client
    // shows the ONE geoset whose variant (id%100) equals that group's active value, and hides the
    // rest of the group; geoset 0 (skin) always shows. The naked default active value is 1 for EVERY
    // group. Variant 1 is the client's "regular"/"default" mesh, which is either the bare body part
    // (bare hand 401, bare foot 501, bare legs 1301, torso 2201) OR a geoset that "Does Not Exist"
    // in the skin (e.g. sleeve 801, tabard 1201, belt 1801) — a DNE default simply shows nothing, so
    // the bare body beneath comes from geoset 0 / a lower group. This is why forcing equipment groups
    // to 0 was wrong: it also hid the bare legs (group 13 var 1 = "legs"). Skin/racial and
    // customization exceptions override below; equipped items override their groups last.
    // Refs: WoWModelViewer setGeosetGroupDisplay + wowdev.wiki Character_Customization / ItemDisplayInfo.
    std::unordered_map<int, int> active;   // group -> active variant
    for (const m2::RenderBatch& b : model.batches)
        if ((int)b.submeshId > 0) active.emplace((int)b.submeshId / 100, 1);   // default variant 1
    active[7] = 2;                                   // ears: 701 = "no ears" (DNE placeholder), 702 = ears
    active[16] = 0;                                  // facial jewelry / eyeglow group: off by default
    active[17] = 0;                                  // eye glow (racial/DK, geoset 17xx): off unless enabled
    active[1] = 0; active[2] = 0; active[3] = 0;     // facial hair: none unless customization sets it below
    // Hair mesh geoset = CharHairGeosets.GeosetID, but GeosetID 0 (a bald/short style) maps to the
    // group-0 SCALP geoset (variant 1) — exactly WoW Model Viewer's `if (!geosetId) geosetId = 1`.
    // Showing variant 0 instead leaves a HOLE in the top of the head, because the crown/scalp is a
    // separate geoset (variant 1), not part of the always-drawn body geoset 0. (Showscalp itself
    // drives the scalp TEXTURE, composited above — it does not hide the mesh.)
    for (const DbcStore::CharHairGeoset& g : HairGeosets())
        if (g.race == race && g.sex == sex && g.variation == c.hairVariation)
        { active[0] = g.geosetId ? (int)g.geosetId : 1; break; }
    // CharacterFacialHairStyles' five Geoset columns map to groups in a SWAPPED order (a WoW quirk
    // mirrored by WoW Model Viewer): column 0 -> group 1 (beard), column 2 -> group 2, column 1 ->
    // group 3. Mapping them straight puts the sideburn/moustache geosets on the wrong groups, which
    // renders garbled geometry beside the ears/jaw.
    for (const DbcStore::FacialHairStyle& f : FacialStyles())
        if (f.race == race && f.sex == sex && f.variation == c.facialVariation)
        { active[1] = (int)f.geoset[0]; active[2] = (int)f.geoset[2]; active[3] = (int)f.geoset[1]; break; }

    // Equipped items switch their geoset groups to the item's cut. Following WoW Model Viewer's
    // character-composition rules, the active variant for a group is ItemDisplayInfo.GeosetGroup[i]
    // + 1 (value 0 = the first equipped variant "01"; the bare body is the naked default above).
    // The GeosetGroup index -> character geoset group depends on the item's slot.
    for (const EquippedItem& e : equipped)
    {
        const uint32_t* g = e.display.geosetGroup;
        switch (e.slot)
        {
            case EquipSlot::Hands: active[4] = (int)g[0] + 1; break;                       // gloves
            case EquipSlot::Feet:  active[5] = (int)g[0] + 1; break;                       // boots
            case EquipSlot::Wrist: active[8] = (int)g[0] + 1; break;                       // wristbands
            case EquipSlot::Chest:
            case EquipSlot::Shirt:
                active[8] = (int)g[0] + 1;                                                 // sleeves
                if (g[2] > 0) active[13] = (int)g[2] + 1;                                  // robe skirt
                break;
            case EquipSlot::Legs:
                active[11] = (int)g[1] + 1;                                                // trousers
                active[9]  = (int)g[2] + 1;                                                // kneepads
                if (g[2] > 0) active[13] = (int)g[2] + 1;                                  // kilt/robe
                break;
            case EquipSlot::Waist:  active[18] = (int)g[0] + 1; break;                     // belt
            case EquipSlot::Tabard: active[12] = 1; break;                                 // tabard
            case EquipSlot::Back:   active[15] = 1; break;                                 // cloak
            default: break;                                                                // Head/weapons: attached models
        }
    }

    for (size_t i = 0; i < model.batches.size(); ++i)
    {
        const int id = (int)model.batches[i].submeshId;
        // Hide submeshes whose runtime texture never resolved (unequipped cape etc.).
        int ti = model.batches[i].textureIndex;
        uint32_t type = (ti >= 0 && ti < (int)model.textureTypes.size()) ? model.textureTypes[ti] : 0;
        const bool unresolved = ti < 0 || ti >= (int)model.texturePaths.size() || model.texturePaths[ti].empty();
        if (type != 0 && unresolved) { visibleOut[i] = 0; continue; }
        if (id == 0) { visibleOut[i] = 1; continue; }   // body: always shown

        const int grp = id / 100, var = id % 100;
        auto it = active.find(grp);
        const int want = (it != active.end()) ? it->second : 0;
        visibleOut[i] = (var == want) ? 1 : 0;
    }
}

CharacterOptions ModelDresser::CharacterInfoFor(const std::string& m2Path)
{
    CharacterOptions o;
    if (ClassifyModel(m2Path) != ModelClass::Character)
        return o;
    auto info = CharacterInfoFromDir(DirOf(m2Path));   // race/sex from base-skin dir match
    if (!info.first)
        return o;
    o.valid = true;
    o.race = info.second.first;
    o.sex = info.second.second;
    // Count distinct variants per axis for this race/sex.
    uint32_t skinColors = 0, faceVars = 0, hairVars = 0, hairColors = 0;
    for (const DbcStore::CharSection& s : charSections_)
    {
        if (s.race != o.race || s.sex != o.sex) continue;
        if (s.baseSection == 0) skinColors = std::max(skinColors, s.color + 1);
        else if (s.baseSection == 1) faceVars = std::max(faceVars, s.variation + 1);
        else if (s.baseSection == 3) { hairVars = std::max(hairVars, s.variation + 1); hairColors = std::max(hairColors, s.color + 1); }
    }
    uint32_t facialVars = 0;
    for (const DbcStore::FacialHairStyle& f : FacialStyles())
        if (f.race == o.race && f.sex == o.sex) facialVars = std::max(facialVars, f.variation + 1);
    o.skinColors = std::max<uint32_t>(1, skinColors);
    o.faceVariations = std::max<uint32_t>(1, faceVars);
    o.hairVariations = std::max<uint32_t>(1, hairVars);
    o.hairColors = std::max<uint32_t>(1, hairColors);
    o.facialVariations = std::max<uint32_t>(1, facialVars);
    return o;
}

// --- default resolution ----------------------------------------------------

int ModelDresser::ResolveDefaultTextures(const std::string& m2Path, m2::M2Model& model)
{
    // Count slots that need runtime resolution (non-zero type, still empty).
    auto countUnresolved = [&]() {
        int n = 0;
        for (size_t i = 0; i < model.texturePaths.size(); ++i)
        {
            uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
            if (t != 0 && model.texturePaths[i].empty()) ++n;
        }
        return n;
    };
    const int before = countUnresolved();
    if (before == 0)
        return 0;

    switch (ClassifyModel(m2Path))
    {
        case ModelClass::Creature:
        {
            auto skins = CreatureSkinsFor(m2Path);
            // Prefer the first display whose primary skin is non-empty.
            const std::array<std::string, 3>* pick = nullptr;
            for (const auto& s : skins)
                if (!s[0].empty()) { pick = &s; break; }
            if (!pick && !skins.empty()) pick = &skins.front();
            if (pick)
            {
                std::string s3[3] = {(*pick)[0], (*pick)[1], (*pick)[2]};
                ApplyCreatureSkins(m2Path, s3, model);
            }
            break;
        }
        case ModelClass::Item:
        {
            auto skins = ItemObjectSkinsFor(m2Path);
            if (!skins.empty())
                ApplyItemObjectSkin(m2Path, skins.front(), model);
            break;
        }
        case ModelClass::Character:
        {
            auto choices = CharacterSkinsFor(m2Path);
            if (!choices.empty())
                ApplyCharacterTextures(model, choices.front().body, choices.front().hair,
                                       choices.front().extra);
            break;
        }
        case ModelClass::Unknown:
            break;
    }
    return before - countUnresolved();
}
} // namespace we
