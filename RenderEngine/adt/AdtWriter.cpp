// AdtWriter — see AdtWriter.h.

#include "adt/AdtWriter.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>

#include <glm/gtc/matrix_transform.hpp>

#include "adt/AdtTypes.h"
#include "util/ByteReader.h"

namespace we::adt
{
namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRad2Deg = 180.0f / kPi;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool EqualsNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

// A parsed top-level chunk: on-disk magic bytes (reversed FourCC) + a mutable payload copy.
struct RawChunk
{
    char magic[4];
    std::vector<uint8_t> data;
    bool Is(const char* human) const
    {
        return magic[0] == human[3] && magic[1] == human[2] && magic[2] == human[1] &&
               magic[3] == human[0];
    }
};

RawChunk* Find(std::vector<RawChunk>& chunks, const char* human)
{
    for (RawChunk& rc : chunks)
        if (rc.Is(human))
            return &rc;
    return nullptr;
}

std::vector<RawChunk> ParseChunks(const std::vector<uint8_t>& bytes)
{
    std::vector<RawChunk> chunks;
    ByteReader r(bytes.data(), bytes.size());
    ChunkIter it(r);
    Chunk c;
    while (it.Next(c))
    {
        RawChunk rc;
        std::memcpy(rc.magic, c.magic, 4);
        rc.data.assign(bytes.begin() + c.offset, bytes.begin() + c.offset + c.size);
        chunks.push_back(std::move(rc));
    }
    return chunks;
}

// Serialize chunks back to a file, recomputing the MHDR offset table + MCIN's 256 absolute MCNK
// offsets from the (possibly resized) layout. Shared by AddPlacement + RemovePlacement.
void ReEmit(std::vector<RawChunk>& chunks, std::vector<uint8_t>& out)
{
    std::vector<size_t> offAt(chunks.size());
    size_t cur = 0;
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        offAt[i] = cur;
        cur += 8 + chunks[i].data.size();
    }
    if (RawChunk* mhdr = Find(chunks, "MHDR"); mhdr && mhdr->data.size() >= sizeof(AdtHeader))
    {
        const size_t mhdrDataStart = offAt[mhdr - chunks.data()] + 8;
        AdtHeader h;
        std::memcpy(&h, mhdr->data.data(), sizeof(h));
        auto setOff = [&](uint32_t& field, const char* human) {
            RawChunk* rc = Find(chunks, human);
            field = rc ? static_cast<uint32_t>(offAt[rc - chunks.data()] - mhdrDataStart) : 0u;
        };
        setOff(h.mcin, "MCIN"); setOff(h.mtex, "MTEX"); setOff(h.mmdx, "MMDX"); setOff(h.mmid, "MMID");
        setOff(h.mwmo, "MWMO"); setOff(h.mwid, "MWID"); setOff(h.mddf, "MDDF"); setOff(h.modf, "MODF");
        setOff(h.mfbo, "MFBO"); setOff(h.mh2o, "MH2O"); setOff(h.mtxf, "MTXF");
        std::memcpy(mhdr->data.data(), &h, sizeof(h));
    }
    if (RawChunk* mcin = Find(chunks, "MCIN"); mcin && mcin->data.size() >= 256 * sizeof(AdtMcinEntry))
    {
        int n = 0;
        for (size_t i = 0; i < chunks.size() && n < 256; ++i)
        {
            if (!chunks[i].Is("MCNK"))
                continue;
            const uint32_t off = static_cast<uint32_t>(offAt[i]);
            const uint32_t size = static_cast<uint32_t>(8 + chunks[i].data.size());
            const size_t entry = static_cast<size_t>(n) * sizeof(AdtMcinEntry);
            std::memcpy(mcin->data.data() + entry, &off, sizeof(off));
            std::memcpy(mcin->data.data() + entry + sizeof(off), &size, sizeof(size));
            ++n;
        }
    }
    out.clear();
    out.reserve(cur);
    for (const RawChunk& rc : chunks)
    {
        out.insert(out.end(), rc.magic, rc.magic + 4);
        const uint32_t sz = static_cast<uint32_t>(rc.data.size());
        const uint8_t* s = reinterpret_cast<const uint8_t*>(&sz);
        out.insert(out.end(), s, s + 4);
        out.insert(out.end(), rc.data.begin(), rc.data.end());
    }
}

inline int OuterIdx(int row, int col) { return row * 17 + col; }
inline int InnerIdx(int row, int col) { return row * 17 + 9 + col; }

bool CellHole(uint16_t holes, int row, int col)
{
    const int bit = (row / 2) * 4 + (col / 2);
    return (holes & (1u << bit)) != 0;
}

bool InBytes(const std::vector<uint8_t>& bytes, size_t off, size_t count)
{
    return off <= bytes.size() && count <= bytes.size() - off;
}

float SmoothFalloff(float t)
{
    t = Clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void AddFaceNormal(std::array<glm::vec3, 145>& normals, const std::array<glm::vec3, 145>& verts,
                   int a, int b, int c)
{
    glm::vec3 n = glm::cross(verts[b] - verts[a], verts[c] - verts[a]);
    if (glm::length(n) < 1e-6f)
        return;
    // ADT's row/column axes make the loader's triangle winding vary by convention; retain the
    // upward-facing normal for terrain lighting regardless of disk winding.
    if (n.z < 0.0f)
        n = -n;
    normals[a] += n;
    normals[b] += n;
    normals[c] += n;
}


// Ensure every drawable MCNK has a fixed 145xBGRA MCCV payload. Existing MCCV
// chunks stay in place; missing ones are appended to that MCNK and ReEmit repairs
// the outer MCIN offsets/sizes. This avoids unsafe in-place file growth.
bool EnsureMccv(std::vector<uint8_t>& bytes)
{
    std::vector<RawChunk> chunks = ParseChunks(bytes);
    if (chunks.empty())
        return false;
    bool changed = false;
    for (RawChunk& chunk : chunks)
    {
        if (!chunk.Is("MCNK") || chunk.data.size() < sizeof(AdtMcnkHeader))
            continue;
        AdtMcnkHeader header{};
        std::memcpy(&header, chunk.data.data(), sizeof(header));
        bool validExisting = false;
        if (header.ofsMCCV >= 8)
        {
            const size_t offset = static_cast<size_t>(header.ofsMCCV) - 8;
            validExisting = InBytes(chunk.data, offset, 8 + 145 * sizeof(uint32_t)) &&
                            chunk.data[offset + 0] == 'V' && chunk.data[offset + 1] == 'C' &&
                            chunk.data[offset + 2] == 'C' && chunk.data[offset + 3] == 'M';
        }
        if (validExisting)
            continue;

        const uint32_t payloadSize = 145 * sizeof(uint32_t);
        header.ofsMCCV = static_cast<uint32_t>(8 + chunk.data.size());
        header.flags |= kAdtMcnkHasMccv;
        std::memcpy(chunk.data.data(), &header, sizeof(header));
        chunk.data.push_back('V'); chunk.data.push_back('C'); chunk.data.push_back('C'); chunk.data.push_back('M');
        const uint8_t* sizeBytes = reinterpret_cast<const uint8_t*>(&payloadSize);
        chunk.data.insert(chunk.data.end(), sizeBytes, sizeBytes + sizeof(payloadSize));
        const uint32_t neutral = 0x007F7F7Fu; // BGRA; 0x7F == neutral 1.0 in classic MCCV
        for (int i = 0; i < 145; ++i)
        {
            const uint8_t* colorBytes = reinterpret_cast<const uint8_t*>(&neutral);
            chunk.data.insert(chunk.data.end(), colorBytes, colorBytes + sizeof(neutral));
        }
        changed = true;
    }
    if (changed)
        ReEmit(chunks, bytes);
    return true;
}

inline uint32_t PackMccv(float r, float g, float b, uint8_t alpha)
{
    const auto channel = [](float value) {
        return static_cast<uint8_t>(std::round(Clamp(value, 0.0f, 1.0f) * 127.0f));
    };
    return static_cast<uint32_t>(channel(b)) |
           (static_cast<uint32_t>(channel(g)) << 8u) |
           (static_cast<uint32_t>(channel(r)) << 16u) |
           (static_cast<uint32_t>(alpha) << 24u);
}

} // namespace

bool SculptTerrain(std::vector<uint8_t>& bytes, const TerrainBrushStroke& stroke,
                   TerrainBrushResult* result)
{
    if (result)
        *result = TerrainBrushResult{};
    if (!std::isfinite(stroke.worldX) || !std::isfinite(stroke.worldY) ||
        !std::isfinite(stroke.radius) || stroke.radius <= 0.01f)
        return false;
    if ((stroke.mode == TerrainBrushMode::Raise || stroke.mode == TerrainBrushMode::Lower) &&
        (!std::isfinite(stroke.strength) || std::fabs(stroke.strength) <= 1e-6f))
        return false;
    if (stroke.mode == TerrainBrushMode::Flatten && !std::isfinite(stroke.targetZ))
        return false;

    ByteReader reader(bytes);
    ChunkIter it(reader);
    Chunk chunk;
    TerrainBrushResult local;
    bool haveBounds = false;
    while (it.Next(chunk))
    {
        if (!chunk.Is("MCNK") || chunk.size < sizeof(AdtMcnkHeader) || chunk.offset < 8)
            continue;
        AdtMcnkHeader header{};
        std::memcpy(&header, bytes.data() + chunk.offset, sizeof(header));
        if (header.ofsHeight == 0)
            continue;
        const size_t mcnkBase = chunk.offset - 8;  // offsets in the header are relative to MCNK magic
        const size_t heightChunk = mcnkBase + header.ofsHeight;
        if (!InBytes(bytes, heightChunk, 8 + 145 * sizeof(float)) ||
            bytes[heightChunk + 0] != 'T' || bytes[heightChunk + 1] != 'V' ||
            bytes[heightChunk + 2] != 'C' || bytes[heightChunk + 3] != 'M')
            continue;
        uint32_t heightSize = 0;
        std::memcpy(&heightSize, bytes.data() + heightChunk + 4, sizeof(heightSize));
        if (heightSize < 145 * sizeof(float))
            continue;
        const size_t heightData = heightChunk + 8;
        if (!InBytes(bytes, heightData, 145 * sizeof(float)))
            continue;

        std::array<float, 145> heights{};
        std::memcpy(heights.data(), bytes.data() + heightData, 145 * sizeof(float));
        bool changed = false;
        auto sculpt = [&](int idx, float x, float y) {
            const float dx = x - stroke.worldX;
            const float dy = y - stroke.worldY;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist > stroke.radius)
                return;
            const float falloff = SmoothFalloff(1.0f - dist / stroke.radius);
            float worldZ = header.position[2] + heights[idx];
            if (stroke.mode == TerrainBrushMode::Raise)
                worldZ += std::fabs(stroke.strength) * falloff;
            else if (stroke.mode == TerrainBrushMode::Lower)
                worldZ -= std::fabs(stroke.strength) * falloff;
            else
                worldZ += (stroke.targetZ - worldZ) * falloff;
            heights[idx] = worldZ - header.position[2];
            changed = true;
            ++local.touchedVertices;
            if (!haveBounds)
            {
                local.minWorldZ = local.maxWorldZ = worldZ;
                haveBounds = true;
            }
            else
            {
                local.minWorldZ = std::min(local.minWorldZ, worldZ);
                local.maxWorldZ = std::max(local.maxWorldZ, worldZ);
            }
        };

        for (int row = 0; row <= 8; ++row)
        {
            for (int col = 0; col <= 8; ++col)
                sculpt(OuterIdx(row, col), header.position[0] - row * kUnitSize,
                        header.position[1] - col * kUnitSize);
            if (row < 8)
                for (int col = 0; col < 8; ++col)
                    sculpt(InnerIdx(row, col), header.position[0] - (row + 0.5f) * kUnitSize,
                            header.position[1] - (col + 0.5f) * kUnitSize);
        }
        if (!changed)
            continue;
        std::memcpy(bytes.data() + heightData, heights.data(), 145 * sizeof(float));
        ++local.touchedChunks;

        // Rebuild this MCNK's normals from the same diamond triangulation the loader/render path
        // uses. Heights along a shared chunk edge receive the same radial operation in both chunks,
        // so normals remain visually continuous across ordinary brush strokes.
        if (header.ofsNormal != 0)
        {
            const size_t normalChunk = mcnkBase + header.ofsNormal;
            if (InBytes(bytes, normalChunk, 8 + 145 * 3) &&
                bytes[normalChunk + 0] == 'R' && bytes[normalChunk + 1] == 'N' &&
                bytes[normalChunk + 2] == 'C' && bytes[normalChunk + 3] == 'M')
            {
                uint32_t normalSize = 0;
                std::memcpy(&normalSize, bytes.data() + normalChunk + 4, sizeof(normalSize));
                const size_t normalData = normalChunk + 8;
                if (normalSize >= 145 * 3 && InBytes(bytes, normalData, 145 * 3))
                {
                    std::array<glm::vec3, 145> verts{};
                    std::array<glm::vec3, 145> normals{};
                    for (int row = 0; row <= 8; ++row)
                    {
                        for (int col = 0; col <= 8; ++col)
                        {
                            const int idx = OuterIdx(row, col);
                            verts[idx] = glm::vec3(header.position[0] - row * kUnitSize,
                                                   header.position[1] - col * kUnitSize,
                                                   header.position[2] + heights[idx]);
                        }
                        if (row < 8)
                            for (int col = 0; col < 8; ++col)
                            {
                                const int idx = InnerIdx(row, col);
                                verts[idx] = glm::vec3(header.position[0] - (row + 0.5f) * kUnitSize,
                                                       header.position[1] - (col + 0.5f) * kUnitSize,
                                                       header.position[2] + heights[idx]);
                            }
                    }
                    for (int row = 0; row < 8; ++row)
                        for (int col = 0; col < 8; ++col)
                        {
                            if (CellHole(header.holesLowRes, row, col))
                                continue;
                            const int tl = OuterIdx(row, col);
                            const int tr = OuterIdx(row, col + 1);
                            const int bl = OuterIdx(row + 1, col);
                            const int br = OuterIdx(row + 1, col + 1);
                            const int ct = InnerIdx(row, col);
                            AddFaceNormal(normals, verts, tl, tr, ct);
                            AddFaceNormal(normals, verts, tr, br, ct);
                            AddFaceNormal(normals, verts, br, bl, ct);
                            AddFaceNormal(normals, verts, bl, tl, ct);
                        }
                    for (int idx = 0; idx < 145; ++idx)
                    {
                        glm::vec3 n = normals[idx];
                        const float len = glm::length(n);
                        n = len > 1e-6f ? n / len : glm::vec3(0.0f, 0.0f, 1.0f);
                        const auto pack = [](float v) -> int8_t {
                            return static_cast<int8_t>(std::round(Clamp(v, -1.0f, 1.0f) * 127.0f));
                        };
                        // Disk order is X, Z, Y (the loader remaps it to World X, Y, Z).
                        bytes[normalData + idx * 3 + 0] = static_cast<uint8_t>(pack(n.x));
                        bytes[normalData + idx * 3 + 1] = static_cast<uint8_t>(pack(n.z));
                        bytes[normalData + idx * 3 + 2] = static_cast<uint8_t>(pack(n.y));
                    }
                }
            }
        }
    }
    if (result)
        *result = local;
    return local.touchedVertices != 0;
}

bool PaintTerrainVertexColor(std::vector<uint8_t>& bytes, const TerrainVertexColorStroke& stroke,
                             TerrainVertexColorResult* result)
{
    if (result)
        *result = TerrainVertexColorResult{};
    if (!std::isfinite(stroke.worldX) || !std::isfinite(stroke.worldY) ||
        !std::isfinite(stroke.radius) || stroke.radius <= 0.01f ||
        !std::isfinite(stroke.opacity))
        return false;
    for (float channel : stroke.color)
        if (!std::isfinite(channel))
            return false;

    std::vector<uint8_t> working = bytes;
    if (!EnsureMccv(working))
        return false;

    ByteReader reader(working);
    ChunkIter it(reader);
    Chunk chunk;
    TerrainVertexColorResult local;
    while (it.Next(chunk))
    {
        if (!chunk.Is("MCNK") || chunk.size < sizeof(AdtMcnkHeader) || chunk.offset < 8)
            continue;
        AdtMcnkHeader header{};
        std::memcpy(&header, working.data() + chunk.offset, sizeof(header));
        if (header.ofsMCCV < 8)
            continue;
        const size_t mcnkBase = chunk.offset - 8;
        const size_t colorChunk = mcnkBase + header.ofsMCCV;
        if (!InBytes(working, colorChunk, 8 + 145 * sizeof(uint32_t)) ||
            working[colorChunk + 0] != 'V' || working[colorChunk + 1] != 'C' ||
            working[colorChunk + 2] != 'C' || working[colorChunk + 3] != 'M')
            continue;
        uint32_t colorSize = 0;
        std::memcpy(&colorSize, working.data() + colorChunk + 4, sizeof(colorSize));
        if (colorSize < 145 * sizeof(uint32_t))
            continue;
        const size_t colorData = colorChunk + 8;
        std::array<uint32_t, 145> colors{};
        std::memcpy(colors.data(), working.data() + colorData, colors.size() * sizeof(uint32_t));
        bool changed = false;
        auto paint = [&](int index, float x, float y) {
            const float dx = x - stroke.worldX;
            const float dy = y - stroke.worldY;
            const float distance = std::sqrt(dx * dx + dy * dy);
            if (distance > stroke.radius)
                return;
            const float amount = Clamp(stroke.opacity, 0.0f, 1.0f) *
                                 SmoothFalloff(1.0f - distance / stroke.radius);
            if (amount <= 1e-5f)
                return;
            const uint32_t packed = colors[index];
            const float b = (packed & 0xFFu) / 127.0f;
            const float g = ((packed >> 8u) & 0xFFu) / 127.0f;
            const float r = ((packed >> 16u) & 0xFFu) / 127.0f;
            const uint8_t alpha = static_cast<uint8_t>((packed >> 24u) & 0xFFu);
            const float outR = r + (Clamp(stroke.color[0], 0.0f, 1.0f) - r) * amount;
            const float outG = g + (Clamp(stroke.color[1], 0.0f, 1.0f) - g) * amount;
            const float outB = b + (Clamp(stroke.color[2], 0.0f, 1.0f) - b) * amount;
            const uint32_t updated = PackMccv(outR, outG, outB, alpha);
            if (updated == packed)
                return;
            colors[index] = updated;
            changed = true;
            ++local.touchedVertices;
        };
        for (int row = 0; row <= 8; ++row)
        {
            for (int col = 0; col <= 8; ++col)
                paint(OuterIdx(row, col), header.position[0] - row * kUnitSize,
                      header.position[1] - col * kUnitSize);
            if (row < 8)
                for (int col = 0; col < 8; ++col)
                    paint(InnerIdx(row, col), header.position[0] - (row + 0.5f) * kUnitSize,
                          header.position[1] - (col + 0.5f) * kUnitSize);
        }
        if (!changed)
            continue;
        std::memcpy(working.data() + colorData, colors.data(), colors.size() * sizeof(uint32_t));
        ++local.touchedChunks;
    }
    if (result)
        *result = local;
    if (local.touchedVertices == 0)
        return false;
    bytes = std::move(working);
    return true;
}

RawPlacement PlacementToRaw(const glm::mat4& m, const glm::vec3& origin)
{
    RawPlacement raw;

    // --- translation: local -> world -> MDDF/MODF position ---
    const glm::vec3 local(m[3][0], m[3][1], m[3][2]);
    const glm::vec3 world(local.x + origin.x, local.y + origin.y, local.z);   // Z is not offset
    // Inverse of AdtLoader's remap: world.x = kMapOrigin - pos[2], world.y = kMapOrigin - pos[0],
    // world.z = pos[1].
    raw.position[0] = kMapOrigin - world.y;
    raw.position[1] = world.z;
    raw.position[2] = kMapOrigin - world.x;

    // --- scale + rotation basis (strip scale from the columns) ---
    glm::vec3 c0(m[0][0], m[0][1], m[0][2]);
    glm::vec3 c1(m[1][0], m[1][1], m[1][2]);
    glm::vec3 c2(m[2][0], m[2][1], m[2][2]);
    const float sx = glm::length(c0);
    const float sy = glm::length(c1);
    const float sz = glm::length(c2);
    raw.scale = (sx > 0.0f) ? sx : 1.0f;   // uniform scale
    c0 = (sx > 1e-6f) ? c0 / sx : glm::vec3(1, 0, 0);
    c1 = (sy > 1e-6f) ? c1 / sy : glm::vec3(0, 1, 0);
    c2 = (sz > 1e-6f) ? c2 / sz : glm::vec3(0, 0, 1);

    // Rotation part R = Rz(A)*Ry(B)*Rx(C), where A=yaw+180, B=pitch, C=roll (AdtLoader.cpp). Extract
    // the Z-Y-X Euler angles. glm is column-major: R[col][row], so math R[row][col] = c<col>[row].
    // sB = -R[2][0] = -c0[2]; A = atan2(R[1][0], R[0][0]) = atan2(c0[1], c0[0]);
    // C = atan2(R[2][1], R[2][2]) = atan2(c1[2], c2[2]).
    const float sB = Clamp(-c0[2], -1.0f, 1.0f);
    const float B = std::asin(sB);
    float A, C;
    if (std::fabs(sB) < 0.99999f)
    {
        A = std::atan2(c0[1], c0[0]);
        C = std::atan2(c1[2], c2[2]);
    }
    else
    {
        // Gimbal lock (pitch ~ +/-90): fold roll into yaw. R[0][1] = c1[0], R[1][1] = c1[1].
        A = std::atan2(-c1[0], c1[1]);
        C = 0.0f;
    }

    raw.rotation[0] = B * kRad2Deg;              // pitch (about Y / E-W)
    raw.rotation[1] = A * kRad2Deg - 180.0f;     // yaw (about Z / up) — undo the loader's +180
    raw.rotation[2] = C * kRad2Deg;              // roll (about X / N-S)
    return raw;
}

bool PatchTilePlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo,
                        const RawPlacement& raw)
{
    ByteReader r(bytes.data(), bytes.size());
    ChunkIter it(r);
    Chunk ch;
    const char* wanted = isWmo ? "MODF" : "MDDF";
    const size_t recSize = isWmo ? sizeof(AdtMapObjDef) : sizeof(AdtDoodadDef);

    while (it.Next(ch))
    {
        if (!ch.Is(wanted))
            continue;
        const size_t count = ch.size / recSize;
        for (size_t i = 0; i < count; ++i)
        {
            const size_t recOff = ch.offset + i * recSize;
            uint32_t uid = 0;
            std::memcpy(&uid, bytes.data() + recOff + 4, sizeof(uid));   // uniqueId at offset 4
            if (uid != static_cast<uint32_t>(uniqueId))
                continue;

            if (isWmo)
            {
                AdtMapObjDef rec;
                std::memcpy(&rec, bytes.data() + recOff, sizeof(rec));
                std::memcpy(rec.position, raw.position, sizeof(rec.position));
                std::memcpy(rec.rotation, raw.rotation, sizeof(rec.rotation));
                // MODF scale is ignored by the 3.3.5a client and extents are recomputed at load, so
                // only position + rotation are persisted for WMOs.
                std::memcpy(bytes.data() + recOff, &rec, sizeof(rec));
            }
            else
            {
                AdtDoodadDef rec;
                std::memcpy(&rec, bytes.data() + recOff, sizeof(rec));
                std::memcpy(rec.position, raw.position, sizeof(rec.position));
                std::memcpy(rec.rotation, raw.rotation, sizeof(rec.rotation));
                float scaled = raw.scale * 1024.0f;
                if (scaled < 1.0f) scaled = 1.0f;
                if (scaled > 65535.0f) scaled = 65535.0f;
                rec.scale = static_cast<uint16_t>(scaled + 0.5f);
                std::memcpy(bytes.data() + recOff, &rec, sizeof(rec));
            }
            return true;
        }
    }
    return false;
}

bool AddPlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo,
                  const std::string& modelPath, const RawPlacement& raw)
{
    std::vector<RawChunk> chunks = ParseChunks(bytes);
    RawChunk* rec = Find(chunks, isWmo ? "MODF" : "MDDF");
    RawChunk* blob = Find(chunks, isWmo ? "MWMO" : "MMDX");
    RawChunk* idx = Find(chunks, isWmo ? "MWID" : "MMID");
    RawChunk* mhdr = Find(chunks, "MHDR");
    if (!rec || !blob || !idx || !mhdr || mhdr->data.size() < sizeof(AdtHeader))
        return false;   // v1: the tile must already have the placement + string chunks

    // Resolve the model path to a nameId, appending to MMDX/MMID (or MWMO/MWID) if new.
    uint32_t nameId = 0;
    bool found = false;
    const size_t idxCount = idx->data.size() / 4;
    for (size_t i = 0; i < idxCount && !found; ++i)
    {
        uint32_t off = 0;
        std::memcpy(&off, idx->data.data() + i * 4, 4);
        if (off >= blob->data.size())
            continue;
        const char* s = reinterpret_cast<const char*>(blob->data.data() + off);
        size_t maxLen = blob->data.size() - off, len = 0;
        while (len < maxLen && s[len] != '\0') ++len;
        if (EqualsNoCase(std::string(s, len), modelPath))
        {
            nameId = static_cast<uint32_t>(i);
            found = true;
        }
    }
    if (!found)
    {
        const uint32_t newOff = static_cast<uint32_t>(blob->data.size());
        blob->data.insert(blob->data.end(), modelPath.begin(), modelPath.end());
        blob->data.push_back(0);
        nameId = static_cast<uint32_t>(idxCount);
        uint8_t off4[4];
        std::memcpy(off4, &newOff, 4);
        idx->data.insert(idx->data.end(), off4, off4 + 4);
    }

    // Append the placement record.
    if (isWmo)
    {
        AdtMapObjDef m{};
        m.nameId = nameId;
        m.uniqueId = static_cast<uint32_t>(uniqueId);
        std::memcpy(m.position, raw.position, sizeof(m.position));
        std::memcpy(m.rotation, raw.rotation, sizeof(m.rotation));
        for (int k = 0; k < 3; ++k)   // approximate AABB; the client recomputes visual bounds
        {
            m.extentsMin[k] = raw.position[k] - 50.0f;
            m.extentsMax[k] = raw.position[k] + 50.0f;
        }
        m.scale = 1024;   // WotLK ignores MODF scale
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&m);
        rec->data.insert(rec->data.end(), p, p + sizeof(m));
    }
    else
    {
        AdtDoodadDef d{};
        d.nameId = nameId;
        d.uniqueId = static_cast<uint32_t>(uniqueId);
        std::memcpy(d.position, raw.position, sizeof(d.position));
        std::memcpy(d.rotation, raw.rotation, sizeof(d.rotation));
        float sc = raw.scale * 1024.0f;
        d.scale = static_cast<uint16_t>(Clamp(sc, 1.0f, 65535.0f) + 0.5f);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(&d);
        rec->data.insert(rec->data.end(), p, p + sizeof(d));
    }

    std::vector<uint8_t> out;
    ReEmit(chunks, out);
    bytes.swap(out);
    return true;
}

bool RemovePlacement(std::vector<uint8_t>& bytes, uint64_t uniqueId, bool isWmo)
{
    std::vector<RawChunk> chunks = ParseChunks(bytes);
    RawChunk* rec = Find(chunks, isWmo ? "MODF" : "MDDF");
    if (!rec)
        return false;
    const size_t recSize = isWmo ? sizeof(AdtMapObjDef) : sizeof(AdtDoodadDef);
    const size_t count = rec->data.size() / recSize;
    for (size_t i = 0; i < count; ++i)
    {
        uint32_t uid = 0;
        std::memcpy(&uid, rec->data.data() + i * recSize + 4, 4);   // uniqueId at offset 4
        if (uid != static_cast<uint32_t>(uniqueId))
            continue;
        // Erase this record; leave the (now possibly-orphaned) MMDX/MWMO entry — harmless to the client.
        rec->data.erase(rec->data.begin() + i * recSize, rec->data.begin() + (i + 1) * recSize);
        std::vector<uint8_t> out;
        ReEmit(chunks, out);
        bytes.swap(out);
        return true;
    }
    return false;
}
} // namespace we::adt
