// DbcStore — see DbcStore.h.

#include "clientdata/DbcStore.h"

#include "clientdata/ClientData.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace we
{
namespace
{
uint32_t ReadLE32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

// A "name-like" string: non-empty, not too long, printable (ASCII or UTF-8 bytes).
bool IsNameLike(const std::string& s)
{
    if (s.empty() || s.size() > 100)
        return false;
    for (unsigned char c : s)
        if (c < 0x20 && c != '\t')
            return false;
    return true;
}

// Find the first localized-string column (enUS name block): a field that yields a
// name-like string for most records while the very next field (the next locale slot)
// is almost always empty. Layout-independent, so it survives patched DBCs whose
// field count differs from the canonical one. Returns UINT32_MAX if none found.
uint32_t DetectNameField(const Dbc& dbc)
{
    // Sample records spread across the file (not just the first N, which can be
    // atypical placeholder rows).
    const uint32_t total = dbc.RecordCount();
    if (total == 0 || dbc.FieldCount() < 10)
        return UINT32_MAX;
    const uint32_t n = total < 400 ? total : 400;
    const uint32_t stride = total / n ? total / n : 1;

    // A localized enUS string block: field f is a name-like string for most records,
    // the next several locale slots (f+1..f+8) are almost always empty, and the values
    // are mostly DISTINCT (rules out a constant numeric field aliasing one string).
    for (uint32_t f = 1; f + 9 < dbc.FieldCount(); ++f)
    {
        uint32_t good = 0;
        uint32_t nextEmpty = 0;
        std::unordered_map<std::string, int> distinct;
        for (uint32_t s = 0; s < n; ++s)
        {
            uint32_t r = s * stride;
            if (IsNameLike(dbc.GetString(r, f)))
            {
                ++good;
                distinct[dbc.GetString(r, f)] = 1;
            }
            bool slotsEmpty = true;
            for (uint32_t k = 1; k <= 8; ++k)
                if (!dbc.GetString(r, f + k).empty())
                {
                    slotsEmpty = false;
                    break;
                }
            if (slotsEmpty)
                ++nextEmpty;
        }
        if (good >= n * 7 / 10 && nextEmpty >= n * 95 / 100 &&
            distinct.size() >= static_cast<size_t>(n / 4))
            return f;
    }
    return UINT32_MAX;
}

// Load a name DBC and map field 0 (id) -> string at `nameField`. Empty map on any
// layout mismatch (bounds-checked) so a wrong/absent file fails safe.
std::unordered_map<uint32_t, std::string> LoadNameDbc(const ClientData& cd, const char* path,
                                                      uint32_t nameField)
{
    std::unordered_map<uint32_t, std::string> out;
    std::vector<uint8_t> bytes = cd.ReadFile(path);
    Dbc dbc;
    if (!dbc.Load(bytes))
        return out;
    if (dbc.FieldCount() <= nameField)
        return out;  // layout doesn't match this client version — don't guess
    out.reserve(dbc.RecordCount());
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        uint32_t id = dbc.GetUInt(r, 0);
        std::string name = dbc.GetString(r, nameField);
        if (!name.empty())
            out[id] = std::move(name);
    }
    return out;
}
} // namespace

bool Dbc::Load(const std::vector<uint8_t>& bytes)
{
    data.clear();
    recordCount = fieldCount = recordSize = stringSize = 0;
    if (bytes.size() < 20)
        return false;
    // 'WDBC'
    if (!(bytes[0] == 'W' && bytes[1] == 'D' && bytes[2] == 'B' && bytes[3] == 'C'))
        return false;
    recordCount = ReadLE32(&bytes[4]);
    fieldCount = ReadLE32(&bytes[8]);
    recordSize = ReadLE32(&bytes[12]);
    stringSize = ReadLE32(&bytes[16]);
    recordsOffset = 20;
    stringsOffset = recordsOffset + static_cast<size_t>(recordCount) * recordSize;
    // Sanity: the blob must be large enough to hold records + string block.
    if (recordSize < 4 || stringsOffset + stringSize > bytes.size())
        return false;
    data = bytes;
    return true;
}

uint32_t Dbc::GetUInt(uint32_t rec, uint32_t field) const
{
    if (rec >= recordCount || field >= fieldCount)
        return 0;
    size_t off = recordsOffset + static_cast<size_t>(rec) * recordSize + static_cast<size_t>(field) * 4;
    if (off + 4 > data.size())
        return 0;
    return ReadLE32(&data[off]);
}

float Dbc::GetFloat(uint32_t rec, uint32_t field) const
{
    uint32_t bits = GetUInt(rec, field);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

std::string Dbc::GetString(uint32_t rec, uint32_t field) const
{
    uint32_t offset = GetUInt(rec, field);
    if (offset == 0)
        return {};
    size_t pos = stringsOffset + offset;
    if (pos >= data.size())
        return {};
    const char* start = reinterpret_cast<const char*>(&data[pos]);
    size_t maxLen = data.size() - pos;
    size_t len = 0;
    while (len < maxLen && start[len] != '\0')
        ++len;
    return std::string(start, len);
}

// --- 3.3.5a (12340) enUS field layouts. Verify against a real client if names
// look wrong; the FieldCount bounds-check makes a mismatch fail safe. ------------
std::unordered_map<uint32_t, std::string> DbcStore::LoadFactionTemplateNames(const ClientData& cd) const
{
    // FactionTemplate.dbc (3.3.5, ~14 fields): field 0 = template id, field 1 = the
    // referenced Faction.dbc id. Resolve that to the faction's name so a creature's
    // faction (a FactionTemplate id) shows a readable label.
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\FactionTemplate.dbc")) || dbc.FieldCount() < 2)
        return out;
    const std::unordered_map<uint32_t, std::string> factionNames = LoadFactionNames(cd);
    if (factionNames.empty())
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        const uint32_t templateId = dbc.GetUInt(r, 0);
        const uint32_t factionId = dbc.GetUInt(r, 1);
        auto it = factionNames.find(factionId);
        if (it != factionNames.end() && !it->second.empty())
            out[templateId] = it->second;
    }
    return out;
}

std::unordered_map<uint32_t, std::string> DbcStore::LoadFactionNames(const ClientData& cd) const
{
    // Faction.dbc trails with Name_lang (16+flag) + Description_lang (16+flag) = 34
    // fields, so the enUS Name is at (fieldCount - 34). This adapts across client
    // variants (verified: 53-field client -> field 19).
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\Faction.dbc")) || dbc.FieldCount() < 34)
        return out;
    const uint32_t nameField = dbc.FieldCount() - 34;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string name = dbc.GetString(r, nameField);
        if (!name.empty())
            out[dbc.GetUInt(r, 0)] = std::move(name);
    }
    return out;
}
std::unordered_map<uint32_t, std::string> DbcStore::LoadAreaNames(const ClientData& cd) const
{
    return LoadNameDbc(cd, "DBFilesClient\\AreaTable.dbc", 11);  // fieldCount ~36
}
std::unordered_map<uint32_t, std::string> DbcStore::LoadSkillNames(const ClientData& cd) const
{
    return LoadNameDbc(cd, "DBFilesClient\\SkillLine.dbc", 3);  // fieldCount ~56
}
std::unordered_map<uint32_t, std::string> DbcStore::LoadTitleNames(const ClientData& cd) const
{
    return LoadNameDbc(cd, "DBFilesClient\\CharTitles.dbc", 2);  // fieldCount ~37
}

std::unordered_map<uint32_t, std::string> DbcStore::LoadItemDisplayIcons(const ClientData& cd) const
{
    // ItemDisplayInfo.dbc: id=0, InventoryIcon_1 = field 5 (icon name without extension).
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\ItemDisplayInfo.dbc")) || dbc.FieldCount() <= 5)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string icon = dbc.GetString(r, 5);
        if (!icon.empty())
            out[dbc.GetUInt(r, 0)] = std::move(icon);
    }
    return out;
}

std::unordered_map<uint32_t, uint32_t> DbcStore::LoadSpellIconIds(const ClientData& cd) const
{
    // Spell.dbc: id=0, SpellIconID = field 133 (standard 234-field layout).
    std::unordered_map<uint32_t, uint32_t> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\Spell.dbc")) || dbc.FieldCount() <= 133)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        uint32_t iconId = dbc.GetUInt(r, 133);
        if (iconId)
            out[dbc.GetUInt(r, 0)] = iconId;
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::WorldMapAreaInfo>
DbcStore::LoadWorldMapAreas(const ClientData& cd) const
{
    // WorldMapArea.dbc: id=0, mapId=1, areaId=2, AreaName=3 (tile dir), then floats
    // LocLeft=4, LocRight=5, LocTop=6, LocBottom=7.
    std::unordered_map<uint32_t, WorldMapAreaInfo> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\WorldMapArea.dbc")) || dbc.FieldCount() < 8)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        WorldMapAreaInfo info;
        info.dir = dbc.GetString(r, 3);
        info.left = dbc.GetFloat(r, 4);
        info.right = dbc.GetFloat(r, 5);
        info.top = dbc.GetFloat(r, 6);
        info.bottom = dbc.GetFloat(r, 7);
        if (!info.dir.empty())
            out[dbc.GetUInt(r, 0)] = std::move(info);
    }
    return out;
}

std::vector<DbcStore::WorldMapOverlayInfo>
DbcStore::LoadWorldMapOverlays(const ClientData& cd) const
{
    // WorldMapOverlay.dbc (17 fields): 0=ID, 1=MapAreaID, 2-5=AreaID[4], 6-7=MapPoint,
    // 8=TextureName, 9=TextureWidth, 10=TextureHeight, 11=OffsetX, 12=OffsetY,
    // 13-16=HitRect. (Matches TrinityCore's WorldMapOverlayEntry layout.)
    std::vector<WorldMapOverlayInfo> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\WorldMapOverlay.dbc")) || dbc.FieldCount() < 13)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        WorldMapOverlayInfo o;
        o.mapAreaId = dbc.GetUInt(r, 1);
        o.texture = dbc.GetString(r, 8);
        o.width = static_cast<int>(dbc.GetUInt(r, 9));
        o.height = static_cast<int>(dbc.GetUInt(r, 10));
        o.offsetX = static_cast<int>(dbc.GetUInt(r, 11));
        o.offsetY = static_cast<int>(dbc.GetUInt(r, 12));
        if (!o.texture.empty() && o.width > 0 && o.height > 0)
            out.push_back(std::move(o));
    }
    return out;
}

std::unordered_map<uint32_t, std::string> DbcStore::LoadSpellIconPaths(const ClientData& cd) const
{
    // SpellIcon.dbc: id=0, TextureFilename = field 1 (full "Interface\Icons\..." path).
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\SpellIcon.dbc")) || dbc.FieldCount() <= 1)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string path = dbc.GetString(r, 1);
        if (!path.empty())
            out[dbc.GetUInt(r, 0)] = std::move(path);
    }
    return out;
}
std::unordered_map<uint32_t, std::string> DbcStore::LoadSpellNames(const ClientData& cd) const
{
    // SpellName_lang enUS is field 136 in the standard 234-field 3.3.5a Spell.dbc
    // (verified against TrinityCore's SpellEntryfmt). A patched Spell.dbc with a
    // different field count would mis-map, so only load on the standard layout —
    // otherwise leave spells id-only rather than show garbage names.
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\Spell.dbc")))
        return out;
    // Prefer the canonical enUS SpellName field (136) on a standard 234-field file;
    // otherwise detect the first localized-name column so patched clients still work.
    uint32_t nameField = (dbc.FieldCount() == 234) ? 136u : DetectNameField(dbc);
    if (nameField == UINT32_MAX || nameField >= dbc.FieldCount())
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string name = dbc.GetString(r, nameField);
        if (!name.empty())
            out[dbc.GetUInt(r, 0)] = std::move(name);
    }
    return out;
}

std::unordered_map<uint32_t, std::string> DbcStore::LoadCreatureModelPaths(const ClientData& cd) const
{
    // CreatureModelData.dbc: id=0, ModelName (path) = field 2. The DBC stores an .mdx/
    // .mdl path; normalize to .m2 (the actual on-disk WotLK model).
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CreatureModelData.dbc")) || dbc.FieldCount() <= 2)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string path = dbc.GetString(r, 2);
        if (path.empty())
            continue;
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos)
            path = path.substr(0, dot);
        path += ".m2";
        out[dbc.GetUInt(r, 0)] = std::move(path);
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::CreatureDisplay>
DbcStore::LoadCreatureDisplays(const ClientData& cd) const
{
    // CreatureDisplayInfo.dbc: id=0, ModelId=1, ExtendedDisplayInfoID=3, TextureVariation[3]=6,7,8.
    std::unordered_map<uint32_t, CreatureDisplay> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CreatureDisplayInfo.dbc")) || dbc.FieldCount() <= 8)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        CreatureDisplay d;
        d.modelId = dbc.GetUInt(r, 1);
        d.extendedDisplayId = dbc.GetUInt(r, 3);   // -> CreatureDisplayInfoExtra (character NPCs)
        d.skins[0] = dbc.GetString(r, 6);
        d.skins[1] = dbc.GetString(r, 7);
        d.skins[2] = dbc.GetString(r, 8);
        d.particleColorId = dbc.GetUInt(r, 13);   // 0 if absent (GetUInt bounds-checks)
        out[dbc.GetUInt(r, 0)] = std::move(d);
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::CreatureDisplayExtra>
DbcStore::LoadCreatureDisplayExtras(const ClientData& cd) const
{
    // CreatureDisplayInfoExtra.dbc (12340, 21 fields): id=0, DisplayRaceID=1, DisplaySexID=2,
    // SkinID=3, FaceID=4, HairStyleID=5, HairColorID=6, FacialHairID=7, NPCItemDisplay[11]=8..18,
    // Flags=19, BakeName=20.
    std::unordered_map<uint32_t, CreatureDisplayExtra> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CreatureDisplayInfoExtra.dbc")) || dbc.FieldCount() < 19)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        CreatureDisplayExtra d;
        d.race = dbc.GetUInt(r, 1);
        d.sex = dbc.GetUInt(r, 2);
        d.skin = dbc.GetUInt(r, 3);
        d.face = dbc.GetUInt(r, 4);
        d.hairStyle = dbc.GetUInt(r, 5);
        d.hairColor = dbc.GetUInt(r, 6);
        d.facialHair = dbc.GetUInt(r, 7);
        for (int i = 0; i < 11; ++i) d.npcItemDisplay[i] = dbc.GetUInt(r, 8 + i);
        d.bakeName = dbc.GetString(r, 20);   // empty if absent (GetString bounds-checks)
        out[dbc.GetUInt(r, 0)] = std::move(d);
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::ItemDisplay>
DbcStore::LoadItemDisplays(const ClientData& cd) const
{
    // ItemDisplayInfo.dbc (12340): ModelName[2]=1,2  ModelTexture[2]=3,4  GeosetGroup[3]=7,8,9
    // HelmetGeosetVis[2]=13,14  Texture[8]=15..22  ParticleColorID=24.
    std::unordered_map<uint32_t, ItemDisplay> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\ItemDisplayInfo.dbc")) || dbc.FieldCount() < 25)
        return out;
    auto toM2 = [](std::string p) {
        size_t dot = p.find_last_of('.');
        if (dot != std::string::npos) p = p.substr(0, dot);
        return p.empty() ? p : p + ".m2";
    };
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        ItemDisplay d;
        d.modelName[0] = toM2(dbc.GetString(r, 1));
        d.modelName[1] = toM2(dbc.GetString(r, 2));
        d.modelTexture[0] = dbc.GetString(r, 3);
        d.modelTexture[1] = dbc.GetString(r, 4);
        for (int i = 0; i < 3; ++i) d.geosetGroup[i] = dbc.GetUInt(r, 7 + i);
        d.helmetGeosetVis[0] = dbc.GetUInt(r, 13);
        d.helmetGeosetVis[1] = dbc.GetUInt(r, 14);
        for (int i = 0; i < 8; ++i) d.texture[i] = dbc.GetString(r, 15 + i);
        d.particleColorId = dbc.GetUInt(r, 24);
        out[dbc.GetUInt(r, 0)] = std::move(d);
    }
    return out;
}

std::vector<DbcStore::CharSection> DbcStore::LoadCharSections(const ClientData& cd) const
{
    // CharSections.dbc (12340): RaceID=1 SexID=2 BaseSection=3 TextureName[3]=4,5,6 Flags=7
    // VariationIndex=8 ColorIndex=9.
    std::vector<CharSection> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CharSections.dbc")) || dbc.FieldCount() < 10)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        CharSection s;
        s.race = dbc.GetUInt(r, 1);
        s.sex = dbc.GetUInt(r, 2);
        s.baseSection = dbc.GetUInt(r, 3);
        s.textures[0] = dbc.GetString(r, 4);
        s.textures[1] = dbc.GetString(r, 5);
        s.textures[2] = dbc.GetString(r, 6);
        s.flags = dbc.GetUInt(r, 7);
        s.variation = dbc.GetUInt(r, 8);
        s.color = dbc.GetUInt(r, 9);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<DbcStore::CharHairGeoset> DbcStore::LoadCharHairGeosets(const ClientData& cd) const
{
    // CharHairGeosets.dbc (12340): RaceID=1 SexID=2 VariationID=3 GeosetID=4 Showscalp=5.
    std::vector<CharHairGeoset> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CharHairGeosets.dbc")) || dbc.FieldCount() < 6)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        CharHairGeoset g;
        g.race = dbc.GetUInt(r, 1);
        g.sex = dbc.GetUInt(r, 2);
        g.variation = dbc.GetUInt(r, 3);
        g.geosetId = dbc.GetUInt(r, 4);
        g.bald = dbc.GetUInt(r, 5) != 0;   // Showscalp: 1 => bald scalp (hair geoset hidden)
        out.push_back(g);
    }
    return out;
}

std::vector<DbcStore::FacialHairStyle> DbcStore::LoadFacialHairStyles(const ClientData& cd) const
{
    // CharacterFacialHairStyles.dbc (12340): no id column. RaceID=0 SexID=1 VariationID=2
    // Geoset[5]=3..7 (facial geoset groups: beard/moustache/sideburn + 2 more).
    std::vector<FacialHairStyle> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\CharacterFacialHairStyles.dbc")) || dbc.FieldCount() < 8)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        FacialHairStyle f;
        f.race = dbc.GetUInt(r, 0);
        f.sex = dbc.GetUInt(r, 1);
        f.variation = dbc.GetUInt(r, 2);
        for (int i = 0; i < 5; ++i) f.geoset[i] = dbc.GetUInt(r, 3 + i);   // raw geoset ids
        out.push_back(f);
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::ParticleColorRow>
DbcStore::LoadParticleColors(const ClientData& cd) const
{
    // ParticleColor.dbc: id=0; start[3]=fields 1..3, mid[3]=4..6, end[3]=7..9. Ramp k is
    // (start_k, mid_k, end_k) = fields (1+k, 4+k, 7+k).
    std::unordered_map<uint32_t, ParticleColorRow> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\ParticleColor.dbc")) || dbc.FieldCount() < 10)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        ParticleColorRow row;
        for (int k = 0; k < 3; ++k)
        {
            row.bgra[k][0] = dbc.GetUInt(r, 1 + k);
            row.bgra[k][1] = dbc.GetUInt(r, 4 + k);
            row.bgra[k][2] = dbc.GetUInt(r, 7 + k);
        }
        out[dbc.GetUInt(r, 0)] = row;
    }
    return out;
}

std::unordered_map<uint32_t, std::string> DbcStore::LoadGameObjectModelPaths(const ClientData& cd) const
{
    // GameObjectDisplayInfo.dbc: id=0, ModelName (path) = field 1. The path may be an .mdx/.mdl
    // (M2) model or a .wmo. Normalize M2 extensions to .m2; keep .wmo unchanged.
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\GameObjectDisplayInfo.dbc")) || dbc.FieldCount() <= 1)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string path = dbc.GetString(r, 1);
        if (path.empty())
            continue;
        // Case-insensitive ".wmo" test.
        bool isWmo = false;
        if (path.size() >= 4)
        {
            std::string ext = path.substr(path.size() - 4);
            for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            isWmo = (ext == ".wmo");
        }
        if (!isWmo)
        {
            size_t dot = path.find_last_of('.');
            if (dot != std::string::npos)
                path = path.substr(0, dot);
            path += ".m2";
        }
        out[dbc.GetUInt(r, 0)] = std::move(path);
    }
    return out;
}

std::unordered_map<uint32_t, std::vector<DbcStore::TransportPosKey>>
DbcStore::LoadTransportAnimation(const ClientData& cd) const
{
    // TransportAnimation.dbc: id=0, TransportEntry=1, TimeIndex=2, PosX=3, PosY=4, PosZ=5.
    std::unordered_map<uint32_t, std::vector<TransportPosKey>> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\TransportAnimation.dbc")) || dbc.FieldCount() <= 5)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        uint32_t entry = dbc.GetUInt(r, 1);
        if (entry == 0)
            continue;
        TransportPosKey k;
        k.timeMs = dbc.GetUInt(r, 2);
        k.x = dbc.GetFloat(r, 3);
        k.y = dbc.GetFloat(r, 4);
        k.z = dbc.GetFloat(r, 5);
        out[entry].push_back(k);
    }
    for (auto& kv : out)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const TransportPosKey& a, const TransportPosKey& b) { return a.timeMs < b.timeMs; });
    return out;
}

std::unordered_map<uint32_t, std::vector<DbcStore::TransportRotKey>>
DbcStore::LoadTransportRotation(const ClientData& cd) const
{
    // TransportRotation.dbc: id=0, GameObjectsId=1, TimeIndex=2, Rot0..3=3,4,5,6.
    std::unordered_map<uint32_t, std::vector<TransportRotKey>> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\TransportRotation.dbc")) || dbc.FieldCount() <= 6)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        uint32_t entry = dbc.GetUInt(r, 1);
        if (entry == 0)
            continue;
        TransportRotKey k;
        k.timeMs = dbc.GetUInt(r, 2);
        k.rot[0] = dbc.GetFloat(r, 3);
        k.rot[1] = dbc.GetFloat(r, 4);
        k.rot[2] = dbc.GetFloat(r, 5);
        k.rot[3] = dbc.GetFloat(r, 6);
        out[entry].push_back(k);
    }
    for (auto& kv : out)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const TransportRotKey& a, const TransportRotKey& b) { return a.timeMs < b.timeMs; });
    return out;
}

std::unordered_map<uint32_t, std::vector<DbcStore::TaxiNode>>
DbcStore::LoadTaxiPathNodes(const ClientData& cd) const
{
    // TaxiPathNode.dbc: id=0, PathID=1, NodeIndex=2, MapID=3, LocX=4, LocY=5, LocZ=6, Flags=7, Delay=8.
    std::unordered_map<uint32_t, std::vector<TaxiNode>> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\TaxiPathNode.dbc")) || dbc.FieldCount() <= 6)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        uint32_t pathId = dbc.GetUInt(r, 1);
        if (pathId == 0)
            continue;
        TaxiNode n;
        n.nodeIndex = dbc.GetUInt(r, 2);
        n.mapId = dbc.GetUInt(r, 3);
        n.x = dbc.GetFloat(r, 4);
        n.y = dbc.GetFloat(r, 5);
        n.z = dbc.GetFloat(r, 6);
        n.delay = dbc.FieldCount() > 8 ? dbc.GetUInt(r, 8) : 0;
        out[pathId].push_back(n);
    }
    for (auto& kv : out)
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const TaxiNode& a, const TaxiNode& b) { return a.nodeIndex < b.nodeIndex; });
    return out;
}

std::unordered_map<uint32_t, DbcStore::LiquidTypeInfo>
DbcStore::LoadLiquidTypes(const ClientData& cd) const
{
    // LiquidType.dbc (3.3.5a, 45 fields, verified by dump): 0=ID, 1=Name, 3=Type
    // (0 water/1 ocean/2 magma/3 slime), 15=Texture[0] (animated filename pattern, %d=frame).
    std::unordered_map<uint32_t, LiquidTypeInfo> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\LiquidType.dbc")) || dbc.FieldCount() < 16)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        LiquidTypeInfo info;
        info.category = dbc.GetUInt(r, 3);
        info.texture = dbc.GetString(r, 15);
        out[dbc.GetUInt(r, 0)] = std::move(info);
    }
    return out;
}

std::unordered_map<uint32_t, std::string>
DbcStore::LoadAnimationNames(const ClientData& cd) const
{
    // AnimationData.dbc (3.3.5a): 0=ID, 1=Name, then flags/fallback/behavior.
    std::unordered_map<uint32_t, std::string> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\AnimationData.dbc")) || dbc.FieldCount() < 2)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        std::string name = dbc.GetString(r, 1);
        if (!name.empty())
            out[dbc.GetUInt(r, 0)] = std::move(name);
    }
    return out;
}

std::unordered_map<uint32_t, DbcStore::MapInfo> DbcStore::LoadMaps(const ClientData& cd) const
{
    // Map.dbc (3.3.5a): 0=ID, 1=Directory, 2=InstanceType, 3=Flags, 4=PVP, 5=MapName[enUS]
    // (then 15 more locale slots + a flags word). Directory feeds World\Maps\<dir>\.
    std::unordered_map<uint32_t, MapInfo> out;
    Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\Map.dbc")) || dbc.FieldCount() < 6)
        return out;
    for (uint32_t r = 0; r < dbc.RecordCount(); ++r)
    {
        MapInfo mi;
        mi.directory = dbc.GetString(r, 1);
        mi.name = dbc.GetString(r, 5);
        if (!mi.directory.empty())
            out[dbc.GetUInt(r, 0)] = std::move(mi);
    }
    return out;
}
} // namespace we
