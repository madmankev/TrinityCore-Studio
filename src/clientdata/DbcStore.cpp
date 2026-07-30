// DbcStore — see DbcStore.h.

#include "clientdata/DbcStore.h"

#include "clientdata/ClientData.h"

#include <cstring>

namespace qe
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
} // namespace qe
