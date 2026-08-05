// AdtWriter — see AdtWriter.h.

#include "adt/AdtWriter.h"

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
            std::memcpy(mcin->data.data() + n * sizeof(AdtMcinEntry), &off, 4);
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
} // namespace

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
