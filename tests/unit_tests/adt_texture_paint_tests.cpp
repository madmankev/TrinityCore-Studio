#include "adt/AdtTypes.h"
#include "adt/AdtWriter.h"
#include "util/ByteReader.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

void Append(std::vector<std::uint8_t>& bytes, const void* data, std::size_t size)
{
    const auto* raw = static_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(), raw, raw + size);
}

void AppendChunk(std::vector<std::uint8_t>& bytes, const char magic[4], const std::vector<std::uint8_t>& payload)
{
    bytes.insert(bytes.end(), magic, magic + 4);
    const std::uint32_t size = static_cast<std::uint32_t>(payload.size());
    Append(bytes, &size, sizeof(size));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

we::Chunk FindTop(const std::vector<std::uint8_t>& bytes, const char* name)
{
    we::ChunkIter iterator{we::ByteReader(bytes)};
    we::Chunk chunk;
    while (iterator.Next(chunk))
        if (chunk.Is(name))
            return chunk;
    throw std::runtime_error(std::string("missing top-level ") + name);
}

std::vector<std::uint8_t> BuildTextureAdt(bool compressed = false)
{
    we::adt::AdtMcnkHeader header{};
    header.nLayers = 3;
    header.position[0] = 100.0f;
    header.position[1] = 200.0f;
    header.position[2] = 0.0f;

    std::vector<std::uint8_t> mcnk(sizeof(header), 0);
    const char mcvtMagic[] = {'T', 'V', 'C', 'M'};
    header.ofsHeight = static_cast<std::uint32_t>(8 + mcnk.size());
    AppendChunk(mcnk, mcvtMagic, std::vector<std::uint8_t>(145 * sizeof(float), 0));

    std::vector<we::adt::AdtLayer> layers(3);
    layers[0].textureId = 0;
    layers[1].textureId = 1;
    layers[1].flags = we::adt::kAdtLayerUseAlphaMap |
                      (compressed ? we::adt::kAdtLayerAlphaCompressed : 0u);
    layers[1].offsetInMCAL = 0;
    layers[2].textureId = 2;
    layers[2].flags = we::adt::kAdtLayerUseAlphaMap |
                      (compressed ? we::adt::kAdtLayerAlphaCompressed : 0u);
    layers[2].offsetInMCAL = compressed ? 66 : 2048;
    std::vector<std::uint8_t> layerBytes(layers.size() * sizeof(we::adt::AdtLayer));
    std::memcpy(layerBytes.data(), layers.data(), layerBytes.size());
    const char mclyMagic[] = {'Y', 'L', 'C', 'M'};
    header.ofsLayer = static_cast<std::uint32_t>(8 + mcnk.size());
    AppendChunk(mcnk, mclyMagic, layerBytes);

    const char mcalMagic[] = {'L', 'A', 'C', 'M'};
    header.ofsAlpha = static_cast<std::uint32_t>(8 + mcnk.size());
    std::vector<std::uint8_t> alpha;
    if (compressed)
    {
        // RLE: 32 × 127 + 32 = 4096 values, 66 bytes per overlay map.
        alpha.reserve(132);
        const auto appendFillMap = [&alpha](std::uint8_t value) {
            for (int run = 0; run < 32; ++run) { alpha.push_back(0xFF); alpha.push_back(value); }
            alpha.push_back(0xA0); alpha.push_back(value);
        };
        appendFillMap(20);
        appendFillMap(200);
    }
    else
    {
        // Legacy 4-bit alpha maps (two 2048-byte overlays) exercise decode + canonical 8-bit re-emit.
        alpha.resize(4096, 0);
        std::fill(alpha.begin(), alpha.begin() + 2048, 0x11);  // 17 after nibble expansion
        std::fill(alpha.begin() + 2048, alpha.end(), 0xCC);    // 204 after nibble expansion
    }
    header.sizeAlpha = static_cast<std::uint32_t>(alpha.size());
    AppendChunk(mcnk, mcalMagic, alpha);

    // A trailing nested chunk proves offsets after a resized MCAL are repaired.
    const char shadowMagic[] = {'H', 'S', 'C', 'M'};
    header.ofsShadow = static_cast<std::uint32_t>(8 + mcnk.size());
    header.sizeShadow = 8;
    AppendChunk(mcnk, shadowMagic, std::vector<std::uint8_t>(8, 0x5A));
    const char mccvMagic[] = {'V', 'C', 'C', 'M'};
    header.ofsMCCV = static_cast<std::uint32_t>(8 + mcnk.size());
    header.flags |= we::adt::kAdtMcnkHasMccv;
    AppendChunk(mcnk, mccvMagic, std::vector<std::uint8_t>(145 * sizeof(std::uint32_t), 0x7F));
    std::memcpy(mcnk.data(), &header, sizeof(header));

    std::vector<std::uint8_t> adt;
    const char mhdrMagic[] = {'R', 'D', 'H', 'M'};
    AppendChunk(adt, mhdrMagic, std::vector<std::uint8_t>(sizeof(we::adt::AdtHeader), 0));
    const char mcinMagic[] = {'N', 'I', 'C', 'M'};
    AppendChunk(adt, mcinMagic, std::vector<std::uint8_t>(256 * sizeof(we::adt::AdtMcinEntry), 0));
    const char mtexMagic[] = {'X', 'E', 'T', 'M'};
    const std::vector<std::uint8_t> mtex = {'b','a','s','e','.','b','l','p',0,
                                             'o','v','e','r','l','a','y','1','.','b','l','p',0,
                                             'o','v','e','r','l','a','y','2','.','b','l','p',0};
    AppendChunk(adt, mtexMagic, mtex);
    const char mcnkMagic[] = {'K', 'N', 'C', 'M'};
    AppendChunk(adt, mcnkMagic, mcnk);
    return adt;
}

we::adt::AdtMcnkHeader ReadMcnkHeader(const std::vector<std::uint8_t>& bytes, const we::Chunk& mcnk)
{
    we::adt::AdtMcnkHeader header{};
    std::memcpy(&header, bytes.data() + mcnk.offset, sizeof(header));
    return header;
}
} // namespace

int main()
{
    try
    {
        std::vector<std::uint8_t> adt = BuildTextureAdt();
        const we::Chunk beforeChunk = FindTop(adt, "MCNK");
        const we::adt::AdtMcnkHeader before = ReadMcnkHeader(adt, beforeChunk);
        std::vector<we::adt::TerrainTextureLayerInfo> sampled;
        Expect(we::adt::InspectTerrainTextureLayers(adt, 100.0f, 200.0f, sampled),
               "layer inspector resolves the clicked MCNK");
        Expect(sampled.size() == 3 && sampled[0].texturePath == "base.blp" &&
               sampled[1].texturePath == "overlay1.blp" && sampled[2].texturePath == "overlay2.blp",
               "layer inspector maps MCLY texture ids to MTEX names");

        we::adt::TerrainTextureBrushStroke stroke;
        stroke.mode = we::adt::TerrainTextureBrushMode::Paint;
        stroke.layer = 1;
        stroke.worldX = 100.0f;
        stroke.worldY = 200.0f;
        stroke.radius = 5.0f;
        stroke.opacity = 1.0f;
        we::adt::TerrainTextureBrushResult result;
        Expect(we::adt::PaintTerrainTexture(adt, stroke, &result), "texture paint succeeds");
        Expect(result.touchedChunks == 1 && result.touchedTexels > 0, "paint reports touched MCNK/texels");

        const we::Chunk afterChunk = FindTop(adt, "MCNK");
        const we::adt::AdtMcnkHeader after = ReadMcnkHeader(adt, afterChunk);
        const std::size_t mcnkMagic = afterChunk.offset - 8;
        Expect(after.sizeAlpha == 8192, "canonical 8-bit MCAL has one 4096-byte map per overlay");
        Expect(after.ofsShadow > before.ofsShadow, "nested offsets after MCAL are shifted after re-emit");
        Expect(adt[mcnkMagic + after.ofsShadow + 0] == 'H' && adt[mcnkMagic + after.ofsShadow + 1] == 'S',
               "shifted MCSH magic remains reachable");
        Expect(after.ofsMCCV > before.ofsMCCV && adt[mcnkMagic + after.ofsMCCV + 0] == 'V' &&
               adt[mcnkMagic + after.ofsMCCV + 1] == 'C',
               "existing MCCV offset remains valid after MCAL growth");
        Expect(adt[mcnkMagic + after.ofsAlpha + 0] == 'L' && adt[mcnkMagic + after.ofsAlpha + 1] == 'A',
               "MCAL magic remains valid");
        const std::size_t alphaData = mcnkMagic + after.ofsAlpha + 8;
        Expect(adt[alphaData] > 17, "selected layer alpha rises under a paint stroke");
        Expect(adt[alphaData + 4096] < 204, "higher-priority overlay fades so selected layer is visible");

        std::array<we::adt::AdtLayer, 3> layerAfter{};
        const std::size_t layerData = mcnkMagic + after.ofsLayer + 8;
        std::memcpy(layerAfter.data(), adt.data() + layerData, layerAfter.size() * sizeof(we::adt::AdtLayer));
        Expect((layerAfter[1].flags & we::adt::kAdtLayerUseAlphaMap) != 0 &&
               (layerAfter[1].flags & we::adt::kAdtLayerAlphaCompressed) == 0,
               "target MCLY layer uses canonical uncompressed alpha data");
        Expect(layerAfter[1].offsetInMCAL == 0 && layerAfter[2].offsetInMCAL == 4096,
               "MCLY alpha offsets are rebuilt in draw order");

        const std::uint8_t painted = adt[alphaData];
        stroke.mode = we::adt::TerrainTextureBrushMode::Erase;
        Expect(we::adt::PaintTerrainTexture(adt, stroke, &result), "texture erase succeeds");
        const we::Chunk erasedChunk = FindTop(adt, "MCNK");
        const we::adt::AdtMcnkHeader erased = ReadMcnkHeader(adt, erasedChunk);
        const std::size_t erasedData = erasedChunk.offset - 8 + erased.ofsAlpha + 8;
        Expect(adt[erasedData] < painted, "erase lowers selected overlay alpha");
        std::vector<std::uint8_t> baseReveal = BuildTextureAdt();
        stroke.layer = 0;
        stroke.mode = we::adt::TerrainTextureBrushMode::Paint;
        Expect(we::adt::PaintTerrainTexture(baseReveal, stroke, &result), "base reveal succeeds");
        const we::Chunk baseChunk = FindTop(baseReveal, "MCNK");
        const we::adt::AdtMcnkHeader baseHeader = ReadMcnkHeader(baseReveal, baseChunk);
        const std::size_t baseData = baseChunk.offset - 8 + baseHeader.ofsAlpha + 8;
        Expect(baseReveal[baseData + 4096] < 204,
               "base reveal fades existing overlays without assigning a new texture");

        // Re-emission must keep MCIN's absolute record valid after a potentially grown MCNK.
        const we::Chunk mcin = FindTop(adt, "MCIN");
        we::adt::AdtMcinEntry entry{};
        std::memcpy(&entry, adt.data() + mcin.offset, sizeof(entry));
        Expect(entry.offset == erasedChunk.offset - 8 && entry.size == 8 + erasedChunk.size,
               "MCIN offset/size tracks the re-emitted MCNK");

        std::vector<std::uint8_t> untouched = BuildTextureAdt();
        stroke.layer = 3; // no layer 3 in this MCNK
        Expect(!we::adt::PaintTerrainTexture(untouched, stroke, &result),
               "painting an absent layer refuses to guess a new MCLY assignment");
        Expect(result.skippedChunks > 0, "absent layer is reported as skipped");

        std::vector<std::uint8_t> compressed = BuildTextureAdt(true);
        stroke.layer = 1;
        stroke.mode = we::adt::TerrainTextureBrushMode::Paint;
        Expect(we::adt::PaintTerrainTexture(compressed, stroke, &result),
               "RLE-compressed MCAL paint succeeds");
        const we::Chunk compressedChunk = FindTop(compressed, "MCNK");
        const we::adt::AdtMcnkHeader compressedHeader = ReadMcnkHeader(compressed, compressedChunk);
        const std::size_t compressedData = compressedChunk.offset - 8 + compressedHeader.ofsAlpha + 8;
        Expect(compressedHeader.sizeAlpha == 8192 && compressed[compressedData] > 20 &&
               compressed[compressedData + 4096] < 200,
               "RLE alpha is decoded, painted, faded, and re-emitted as canonical 8-bit data");

        std::cout << "ADT texture paint writer tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "ADT texture paint writer test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
