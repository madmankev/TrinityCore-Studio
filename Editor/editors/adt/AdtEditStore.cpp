// AdtEditStore — see AdtEditStore.h.

#include "editors/adt/AdtEditStore.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

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
        terrain_.clear();
        nextTerrainStrokeId_ = 1;
        vertexColors_.clear();
        nextVertexColorStrokeId_ = 1;
        textures_.clear();
        nextTextureStrokeId_ = 1;
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

AdtEditStore::TerrainStrokeRef AdtEditStore::RecordTerrainStroke(
    int tileX, int tileY, const adt::TerrainBrushStroke& stroke)
{
    TerrainStrokeRef ref;
    if (tileX < 0 || tileX >= 64 || tileY < 0 || tileY >= 64)
        return ref;
    ref.id = nextTerrainStrokeId_++;
    ref.tileX = tileX;
    ref.tileY = tileY;
    ref.stroke = stroke;
    terrain_[TileKey(tileX, tileY)].push_back(ref);
    return ref;
}

bool AdtEditStore::RemoveTerrainStroke(uint64_t id)
{
    if (id == 0)
        return false;
    for (auto it = terrain_.begin(); it != terrain_.end(); ++it)
    {
        std::vector<TerrainStrokeRef>& strokes = it->second;
        const auto found = std::find_if(strokes.begin(), strokes.end(), [id](const TerrainStrokeRef& s) {
            return s.id == id;
        });
        if (found == strokes.end())
            continue;
        strokes.erase(found);
        if (strokes.empty())
            terrain_.erase(it);
        return true;
    }
    return false;
}

bool AdtEditStore::RestoreTerrainStroke(const TerrainStrokeRef& stroke)
{
    if (stroke.id == 0 || stroke.tileX < 0 || stroke.tileX >= 64 || stroke.tileY < 0 || stroke.tileY >= 64)
        return false;
    std::vector<TerrainStrokeRef>& strokes = terrain_[TileKey(stroke.tileX, stroke.tileY)];
    if (std::find_if(strokes.begin(), strokes.end(), [&](const TerrainStrokeRef& s) { return s.id == stroke.id; }) !=
        strokes.end())
        return true;   // idempotent redo
    const auto pos = std::lower_bound(strokes.begin(), strokes.end(), stroke.id,
                                      [](const TerrainStrokeRef& s, uint64_t id) { return s.id < id; });
    strokes.insert(pos, stroke);
    nextTerrainStrokeId_ = std::max(nextTerrainStrokeId_, stroke.id + 1);
    return true;
}

void AdtEditStore::ClearTerrainStrokes()
{
    terrain_.clear();
}

int AdtEditStore::terrainPendingCount() const
{
    int n = 0;
    for (const auto& kv : terrain_)
        n += static_cast<int>(kv.second.size());
    return n;
}

void AdtEditStore::SnapshotTerrainStrokes(std::vector<TerrainStrokeRef>& out) const
{
    out.clear();
    out.reserve(static_cast<size_t>(terrainPendingCount()));
    for (const auto& kv : terrain_)
        out.insert(out.end(), kv.second.begin(), kv.second.end());
    std::sort(out.begin(), out.end(), [](const TerrainStrokeRef& a, const TerrainStrokeRef& b) {
        return a.id < b.id;
    });
}

float AdtEditStore::PreviewTerrainZ(float baseZ, float worldX, float worldY) const
{
    if (!std::isfinite(baseZ) || !std::isfinite(worldX) || !std::isfinite(worldY))
        return baseZ;
    std::vector<TerrainStrokeRef> strokes;
    SnapshotTerrainStrokes(strokes);
    float z = baseZ;
    for (const TerrainStrokeRef& ref : strokes)
    {
        const adt::TerrainBrushStroke& stroke = ref.stroke;
        if (!std::isfinite(stroke.radius) || stroke.radius <= 0.01f)
            continue;
        const float dx = worldX - stroke.worldX;
        const float dy = worldY - stroke.worldY;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance > stroke.radius)
            continue;
        float t = std::clamp(1.0f - distance / stroke.radius, 0.0f, 1.0f);
        const float falloff = t * t * (3.0f - 2.0f * t); // AdtWriter::SmoothFalloff
        if (stroke.mode == adt::TerrainBrushMode::Raise && std::isfinite(stroke.strength))
            z += std::fabs(stroke.strength) * falloff;
        else if (stroke.mode == adt::TerrainBrushMode::Lower && std::isfinite(stroke.strength))
            z -= std::fabs(stroke.strength) * falloff;
        else if (std::isfinite(stroke.targetZ))
            z += (stroke.targetZ - z) * falloff;
    }
    return z;
}

AdtEditStore::VertexColorStrokeRef AdtEditStore::RecordVertexColorStroke(
    int tileX, int tileY, const adt::TerrainVertexColorStroke& stroke)
{
    VertexColorStrokeRef ref;
    if (tileX < 0 || tileX >= 64 || tileY < 0 || tileY >= 64)
        return ref;
    ref.id = nextVertexColorStrokeId_++;
    ref.tileX = tileX;
    ref.tileY = tileY;
    ref.stroke = stroke;
    vertexColors_[TileKey(tileX, tileY)].push_back(ref);
    return ref;
}

bool AdtEditStore::RemoveVertexColorStroke(uint64_t id)
{
    if (id == 0)
        return false;
    for (auto it = vertexColors_.begin(); it != vertexColors_.end(); ++it)
    {
        std::vector<VertexColorStrokeRef>& strokes = it->second;
        const auto found = std::find_if(strokes.begin(), strokes.end(), [id](const VertexColorStrokeRef& ref) {
            return ref.id == id;
        });
        if (found == strokes.end())
            continue;
        strokes.erase(found);
        if (strokes.empty())
            vertexColors_.erase(it);
        return true;
    }
    return false;
}

bool AdtEditStore::RestoreVertexColorStroke(const VertexColorStrokeRef& stroke)
{
    if (stroke.id == 0 || stroke.tileX < 0 || stroke.tileX >= 64 || stroke.tileY < 0 || stroke.tileY >= 64)
        return false;
    std::vector<VertexColorStrokeRef>& strokes = vertexColors_[TileKey(stroke.tileX, stroke.tileY)];
    if (std::find_if(strokes.begin(), strokes.end(), [&](const VertexColorStrokeRef& ref) { return ref.id == stroke.id; }) !=
        strokes.end())
        return true;
    const auto position = std::lower_bound(strokes.begin(), strokes.end(), stroke.id,
                                           [](const VertexColorStrokeRef& ref, uint64_t id) { return ref.id < id; });
    strokes.insert(position, stroke);
    nextVertexColorStrokeId_ = std::max(nextVertexColorStrokeId_, stroke.id + 1);
    return true;
}

void AdtEditStore::ClearVertexColorStrokes()
{
    vertexColors_.clear();
}

int AdtEditStore::vertexColorPendingCount() const
{
    int count = 0;
    for (const auto& pair : vertexColors_)
        count += static_cast<int>(pair.second.size());
    return count;
}

void AdtEditStore::SnapshotVertexColorStrokes(std::vector<VertexColorStrokeRef>& out) const
{
    out.clear();
    out.reserve(static_cast<size_t>(vertexColorPendingCount()));
    for (const auto& pair : vertexColors_)
        out.insert(out.end(), pair.second.begin(), pair.second.end());
    std::sort(out.begin(), out.end(), [](const VertexColorStrokeRef& a, const VertexColorStrokeRef& b) {
        return a.id < b.id;
    });
}

AdtEditStore::TextureStrokeRef AdtEditStore::RecordTextureStroke(
    int tileX, int tileY, const adt::TerrainTextureBrushStroke& stroke)
{
    TextureStrokeRef ref;
    if (tileX < 0 || tileX >= 64 || tileY < 0 || tileY >= 64)
        return ref;
    ref.id = nextTextureStrokeId_++;
    ref.tileX = tileX;
    ref.tileY = tileY;
    ref.stroke = stroke;
    textures_[TileKey(tileX, tileY)].push_back(ref);
    return ref;
}

bool AdtEditStore::RemoveTextureStroke(uint64_t id)
{
    if (id == 0)
        return false;
    for (auto it = textures_.begin(); it != textures_.end(); ++it)
    {
        std::vector<TextureStrokeRef>& strokes = it->second;
        const auto found = std::find_if(strokes.begin(), strokes.end(), [id](const TextureStrokeRef& ref) {
            return ref.id == id;
        });
        if (found == strokes.end())
            continue;
        strokes.erase(found);
        if (strokes.empty())
            textures_.erase(it);
        return true;
    }
    return false;
}

bool AdtEditStore::RestoreTextureStroke(const TextureStrokeRef& stroke)
{
    if (stroke.id == 0 || stroke.tileX < 0 || stroke.tileX >= 64 ||
        stroke.tileY < 0 || stroke.tileY >= 64)
        return false;
    std::vector<TextureStrokeRef>& strokes = textures_[TileKey(stroke.tileX, stroke.tileY)];
    if (std::find_if(strokes.begin(), strokes.end(), [&](const TextureStrokeRef& ref) {
            return ref.id == stroke.id;
        }) != strokes.end())
        return true;
    const auto position = std::lower_bound(strokes.begin(), strokes.end(), stroke.id,
                                           [](const TextureStrokeRef& ref, uint64_t id) {
                                               return ref.id < id;
                                           });
    strokes.insert(position, stroke);
    nextTextureStrokeId_ = std::max(nextTextureStrokeId_, stroke.id + 1);
    return true;
}

void AdtEditStore::ClearTextureStrokes()
{
    textures_.clear();
}

int AdtEditStore::texturePendingCount() const
{
    int count = 0;
    for (const auto& pair : textures_)
        count += static_cast<int>(pair.second.size());
    return count;
}

void AdtEditStore::SnapshotTextureStrokes(std::vector<TextureStrokeRef>& out) const
{
    out.clear();
    out.reserve(static_cast<size_t>(texturePendingCount()));
    for (const auto& pair : textures_)
        out.insert(out.end(), pair.second.begin(), pair.second.end());
    std::sort(out.begin(), out.end(), [](const TextureStrokeRef& a, const TextureStrokeRef& b) {
        return a.id < b.id;
    });
}

int AdtEditStore::pendingCount() const
{
    int n = 0;
    for (const auto& kv : edits_)
        n += static_cast<int>(kv.second.size());
    return n + terrainPendingCount() + vertexColorPendingCount() + texturePendingCount();
}

bool AdtEditStore::Flush(ClientData& cd, const std::string& editRoot, std::string& status)
{
    if (mapDir_.empty() || (edits_.empty() && terrain_.empty() && vertexColors_.empty() && textures_.empty()))
    {
        status = "No ADT edits to save.";
        return true;
    }
    if (editRoot.empty())
    {
        status = "No project edit folder — open a project to save ADT edits.";
        return false;
    }

    // Copy keys before processing. Successful tiles are removed from their queues immediately so a
    // later file-write failure can be retried safely: terrain strokes are additive and must never be
    // replayed twice against an already-written overlay tile.
    std::unordered_set<uint32_t> keys;
    for (const auto& kv : edits_) keys.insert(kv.first);
    for (const auto& kv : terrain_) keys.insert(kv.first);
    for (const auto& kv : vertexColors_) keys.insert(kv.first);
    for (const auto& kv : textures_) keys.insert(kv.first);

    int tilesWritten = 0;
    int editsWritten = 0;
    int terrainVertices = 0;
    int textureTexels = 0;
    int textureSkipped = 0;
    int colorVertices = 0;
    for (uint32_t key : keys)
    {
        const int x = TileX(key);
        const int y = TileY(key);
        const std::string path = adt::TilePath(mapDir_, x, y);

        std::vector<uint8_t> bytes = cd.ReadFile(path);   // overlay-first, so edits accumulate
        if (bytes.empty())
        {
            status = "Failed to read tile " + path;
            return false;
        }

        if (const auto pit = edits_.find(key); pit != edits_.end())
            for (const auto& ekv : pit->second)
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
                    ok = adt::AddPlacement(bytes, uid, ed.isWmo, ed.path, ed.raw); // add-if-missing
                }
                if (ok)
                    ++editsWritten;
            }

        if (const auto tit = terrain_.find(key); tit != terrain_.end())
            for (const TerrainStrokeRef& stroke : tit->second)
            {
                adt::TerrainBrushResult sculpt;
                if (!adt::SculptTerrain(bytes, stroke.stroke, &sculpt))
                {
                    status = "Terrain sculpt missed or could not patch tile " + path;
                    return false;
                }
                ++editsWritten;
                terrainVertices += sculpt.touchedVertices;
            }

        if (const auto xit = textures_.find(key); xit != textures_.end())
            for (const TextureStrokeRef& stroke : xit->second)
            {
                adt::TerrainTextureBrushResult paint;
                if (adt::PaintTerrainTexture(bytes, stroke.stroke, &paint))
                {
                    ++editsWritten;
                    textureTexels += paint.touchedTexels;
                }
                else
                {
                    // Texture paint intentionally refuses to invent a missing MCLY/MTEX layer.
                    // Treat an absent target layer as a visible skip, not a failed whole-tile save.
                    textureSkipped += std::max(1, paint.skippedChunks);
                }
            }

        if (const auto cit = vertexColors_.find(key); cit != vertexColors_.end())
            for (const VertexColorStrokeRef& stroke : cit->second)
            {
                adt::TerrainVertexColorResult paint;
                if (!adt::PaintTerrainVertexColor(bytes, stroke.stroke, &paint))
                {
                    status = "Vertex-color paint missed or could not patch tile " + path;
                    return false;
                }
                ++editsWritten;
                colorVertices += paint.touchedVertices;
            }

        std::string err;
        if (!WriteLooseFile(editRoot, path, bytes, err))
        {
            status = "Write failed for " + path + ": " + err;
            return false;
        }
        // See the retry-safety note above. Placement edits are idempotent, but deleting them here
        // also avoids repeating the same expensive tile rebuild on retry.
        edits_.erase(key);
        terrain_.erase(key);
        vertexColors_.erase(key);
        textures_.erase(key);
        ++tilesWritten;
    }

    status = "Saved " + std::to_string(editsWritten) + " ADT edit(s) across " +
             std::to_string(tilesWritten) + " tile(s)" +
             ((terrainVertices || textureTexels || colorVertices)
                 ? " (" + std::to_string(terrainVertices) + " terrain vertices sculpted, " +
                       std::to_string(textureTexels) + " texture texels painted, " +
                       std::to_string(colorVertices) + " vertex colors painted" +
                       (textureSkipped ? "; " + std::to_string(textureSkipped) +
                                             " texture layer/chunk skip(s)" : std::string()) + ")."
                 : textureSkipped ? " (" + std::to_string(textureSkipped) +
                                      " texture layer/chunk skip(s); no missing layer was created)."
                                  : ".");
    return true;
}

} // namespace we
