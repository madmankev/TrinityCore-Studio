// AdtEditStore — see AdtEditStore.h.

#include "editors/adt/AdtEditStore.h"

#include "adt/AdtLoader.h"          // adt::TilePath
#include "clientdata/ClientData.h"
#include "clientdata/DbcOverlay.h"  // WriteLooseFile

namespace we
{
namespace
{
uint32_t TileKey(int x, int y) { return static_cast<uint32_t>(y * 64 + x); }
int TileX(uint32_t k) { return static_cast<int>(k % 64); }
int TileY(uint32_t k) { return static_cast<int>(k / 64); }
} // namespace

void AdtEditStore::SetMap(const std::string& mapDir)
{
    if (mapDir != mapDir_)
    {
        mapDir_ = mapDir;
        edits_.clear();
    }
}

void AdtEditStore::RecordUpsert(uint64_t uniqueId, bool isWmo, const adt::RawPlacement& raw,
                                const std::string& path, const std::vector<std::pair<int, int>>& tiles)
{
    for (const auto& t : tiles)
        edits_[TileKey(t.first, t.second)][uniqueId] = Edit{true, isWmo, raw, path};
}

void AdtEditStore::RecordRemove(uint64_t uniqueId, bool isWmo,
                                const std::vector<std::pair<int, int>>& tiles)
{
    for (const auto& t : tiles)
        edits_[TileKey(t.first, t.second)][uniqueId] = Edit{false, isWmo, {}, {}};
}

int AdtEditStore::pendingCount() const
{
    int n = 0;
    for (const auto& kv : edits_)
        n += static_cast<int>(kv.second.size());
    return n;
}

bool AdtEditStore::Flush(ClientData& cd, const std::string& editRoot, std::string& status)
{
    if (mapDir_.empty() || edits_.empty())
    {
        status = "No ADT edits to save.";
        return true;
    }
    if (editRoot.empty())
    {
        status = "No project edit folder — open a project to save ADT edits.";
        return false;
    }

    int tilesWritten = 0;
    int editsWritten = 0;
    for (const auto& tkv : edits_)
    {
        const int x = TileX(tkv.first);
        const int y = TileY(tkv.first);
        const std::string path = adt::TilePath(mapDir_, x, y);

        std::vector<uint8_t> bytes = cd.ReadFile(path);   // overlay-first, so edits accumulate
        if (bytes.empty())
        {
            status = "Failed to read tile " + path;
            return false;
        }
        for (const auto& ekv : tkv.second)
        {
            const uint64_t uid = ekv.first;
            const Edit& ed = ekv.second;
            bool ok = false;
            if (!ed.present)
            {
                ok = adt::RemovePlacement(bytes, uid, ed.isWmo);   // no-op if not present
            }
            else if (adt::PatchTilePlacement(bytes, uid, ed.isWmo, ed.raw))
            {
                ok = true;   // record existed -> updated in place
            }
            else if (!ed.path.empty())
            {
                ok = adt::AddPlacement(bytes, uid, ed.isWmo, ed.path, ed.raw);   // wasn't there -> add
            }
            if (ok)
                ++editsWritten;
        }

        std::string err;
        if (!WriteLooseFile(editRoot, path, bytes, err))
        {
            status = "Write failed for " + path + ": " + err;
            return false;
        }
        ++tilesWritten;
    }

    edits_.clear();
    status = "Saved " + std::to_string(editsWritten) + " ADT edit(s) across " +
             std::to_string(tilesWritten) + " tile(s).";
    return true;
}
} // namespace we
