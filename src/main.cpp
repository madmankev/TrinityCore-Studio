// TrinityCore Studio - application entry point.
// The GLFW/GL/ImGui loop and all editor state live in we::App (src/ui). main()
// parses flags and hands control to the App.
//   --selftest   : render a few headless frames (all panels + tabs), print
//                  "SELFTEST OK", exit 0. Validates the build/link/render chain.
//   --emit-sql [path] : run the real QuestRepository::SaveQuest path for a sample
//                  quest through SqlExportDatabase and write the generated .sql to
//                  `path` (default build/emit_test.sql). Validates SQL generation
//                  offline, with no database. Exits 0 on success.

#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/App.h"

#include "editors/quest/QuestRepository.h"
#include "db/SqlExportDatabase.h"
#include "schema/Quest.h"

#include "clientdata/ClientData.h"
#include "clientdata/DbcStore.h"
#include "clientdata/BlpDecoder.h"
#include "model/M2Loader.h"
#include "model/M2Animator.h"
#include "model/M2EffectSystem.h"
#include "model/ModelUploadBuild.h"
#include "wmo/WmoLoader.h"
#include "wmo/WmoUploadBuild.h"
#include "adt/AdtLoader.h"
#include "adt/AdtUploadBuild.h"
#include "util/ByteReader.h"
#include "viewer/ViewportCamera.h"

#include <memory>
#include "imgui.h"
#include "app/Window.h"
#include "gfx/VulkanRenderer.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
int EmitSql(const std::string& path)
{
    we::Quest q;
    q.tmpl.id = 99999;
    q.tmpl.questType = 2;
    q.tmpl.questLevel = 70;
    q.tmpl.minLevel = 68;
    q.tmpl.logTitle = "Test Quest: O'Malley's \"Grand\" Plan";  // exercise escaping
    q.tmpl.questDescription = "Line one.\nLine two with 'quotes'.";
    q.tmpl.flags = 0x8 | 0x1000;                                 // SHARABLE | DAILY
    q.tmpl.rewardItemId[0] = 1234;
    q.tmpl.rewardAmount[0] = 2;
    q.tmpl.requiredNpcOrGo[0] = 4321;
    q.tmpl.requiredNpcOrGoCount[0] = 10;
    q.tmpl.requiredItemId[0] = 555;
    q.tmpl.requiredItemCount[0] = 5;
    q.tmpl.rewardMoney = 12345;

    q.addon.present = true;
    q.addon.allowableClasses = 0x1 | 0x4;                        // Warrior | Hunter
    q.addon.prevQuestID = 99998;
    q.addon.nextQuestID = 100000;

    q.offerReward.present = true;
    q.offerReward.rewardText = "Well done, hero!";
    q.requestItems.present = true;
    q.requestItems.completionText = "Do you have it?";
    q.details.present = true;
    q.mailSender.present = true;
    q.mailSender.rewardMailSenderEntry = 4321;

    we::QuestGreeting g;
    g.id = q.tmpl.id;
    g.type = 0;
    g.greeting = "Hello there.";
    q.greetings.push_back(g);

    q.creatureStarters.push_back(1000);
    q.creatureEnders.push_back(1001);
    q.goStarters.push_back(2000);

    we::QuestPoi poi;
    poi.questID = q.tmpl.id;
    poi.id = 0;
    we::QuestPoiPoint pt;
    pt.questID = q.tmpl.id;
    pt.idx1 = 0;
    pt.idx2 = 0;
    pt.x = -8913;
    pt.y = 554;
    poi.points.push_back(pt);
    q.pois.push_back(poi);

    we::QuestLocale loc;
    loc.locale = "deDE";
    loc.title = "Testauftrag";
    loc.templatePresent = true;
    q.locales["deDE"] = loc;

    we::SqlExportDatabase db;         // no live read source needed for pure writes
    db.SetOutputPath(path);
    we::QuestRepository repo;
    q.MarkAllDirty();                 // delta-write: emit the whole sample quest
    we::DbError e = repo.SaveQuest(db, q);
    if (!e.ok)
    {
        std::fprintf(stderr, "emit-sql FAILED: %s\n", e.message.c_str());
        return 1;
    }
    std::printf("EMIT SQL OK -> %s\n", path.c_str());
    return 0;
}
} // namespace

namespace
{
// Dev tool: decode a BLP from client data and write it as a 32-bit BMP for eyeballing.
// Usage: --dump-blp <DataOrLooseRoot> <archivePath> <outBmp>
int DumpBlp(const std::string& data, const std::string& archivePath, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(data)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::BlpImage img = we::DecodeBlp(cd.ReadFile(archivePath));
    if (!img.valid()) { std::fprintf(stderr, "decode failed: %s\n", archivePath.c_str()); return 1; }
    std::printf("decoded %s -> %dx%d\n", archivePath.c_str(), img.width, img.height);
    const int w = img.width, h = img.height;
    const uint32_t rowBytes = static_cast<uint32_t>(w) * 4, pixBytes = rowBytes * h;
    const uint32_t fileSize = 54 + pixBytes;
    uint8_t hdr[54] = {0}; hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4); uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t dib = 40; std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &w, 4); int negH = -h; std::memcpy(hdr + 22, &negH, 4);
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixBytes, 4);
    FILE* f = std::fopen(outBmp.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", outBmp.c_str()); return 1; }
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t* src = img.rgba.data() + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < w; ++x)
        { row[x*4]=src[x*4+2]; row[x*4+1]=src[x*4+1]; row[x*4+2]=src[x*4]; row[x*4+3]=src[x*4+3]; }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
    std::printf("wrote %s\n", outBmp.c_str());
    return 0;
}

int DbcTest(const std::string& path)
{
    we::ClientData cd;
    if (!cd.Open(path))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::printf("Opened: %s\n", cd.SourceDescription().c_str());
    we::DbcStore store;
    auto report = [&](const char* name, const std::unordered_map<uint32_t, std::string>& m)
    {
        std::printf("%-10s %6zu names", name, m.size());
        int shown = 0;
        for (const auto& kv : m)
        {
            if (shown++ >= 3)
                break;
            std::printf("  | %u=%s", kv.first, kv.second.c_str());
        }
        std::printf("\n");
    };
    report("factions", store.LoadFactionNames(cd));
    report("areas", store.LoadAreaNames(cd));
    report("skills", store.LoadSkillNames(cd));
    report("titles", store.LoadTitleNames(cd));
    report("spells", store.LoadSpellNames(cd));

    // Probe raw field layout of the two problem DBCs: print fieldCount and, for the
    // first record, every field index that yields a non-empty string (=name column).
    auto probe = [&](const char* file)
    {
        we::Dbc d;
        if (!d.Load(cd.ReadFile(file)))
        {
            std::printf("PROBE %s: load failed (missing?)\n", file);
            return;
        }
        std::printf("PROBE %s: records=%u fields=%u recordSize=%u\n", file, d.RecordCount(),
                    d.FieldCount(), d.RecordSize());
        for (uint32_t r = 0; r < d.RecordCount() && r < 2; ++r)
        {
            std::printf("  rec %u id=%u fields 132..144:", r, d.GetUInt(r, 0));
            for (uint32_t f = 132; f <= 144 && f < d.FieldCount(); ++f)
            {
                std::string s = d.GetString(r, f);
                std::printf(" [%u]=\"%s\"", f, s.c_str());
            }
            std::printf("\n");
        }
    };
    (void)&probe;

    // Icon chains.
    auto itemIcons = store.LoadItemDisplayIcons(cd);
    auto spellIconIds = store.LoadSpellIconIds(cd);
    auto spellIconPaths = store.LoadSpellIconPaths(cd);
    std::printf("itemDisplayIcons=%zu spellIconIds=%zu spellIconPaths=%zu\n", itemIcons.size(),
                spellIconIds.size(), spellIconPaths.size());
    for (auto& kv : itemIcons)
    {
        std::printf("  sample item displayId %u -> icon '%s'\n", kv.first, kv.second.c_str());
        break;
    }
    for (auto& kv : spellIconPaths)
    {
        std::printf("  sample spellIcon %u -> '%s'\n", kv.first, kv.second.c_str());
        break;
    }

    // BLP decode check on a known icon.
    auto tryDecode = [&](const char* p)
    {
        we::BlpImage img = we::DecodeBlp(cd.ReadFile(p));
        std::printf("BLP %s: %dx%d valid=%d\n", p, img.width, img.height, img.valid() ? 1 : 0);
    };
    tryDecode("Interface\\Icons\\INV_Misc_QuestionMark.blp");
    tryDecode("Interface\\Icons\\Spell_Fire_FlameBolt.blp");

    // WorldMapArea + a sample map tile.
    auto areas = store.LoadWorldMapAreas(cd);
    std::printf("worldMapAreas=%zu\n", areas.size());
    for (auto& kv : areas)
    {
        const auto& a = kv.second;
        if (a.dir == "Elwynn" || a.dir == "Durotar" || kv.first == 9)
        {
            std::printf("  area %u dir='%s' L=%.0f R=%.0f T=%.0f B=%.0f\n", kv.first, a.dir.c_str(),
                        a.left, a.right, a.top, a.bottom);
            tryDecode(("Interface\\WorldMap\\" + a.dir + "\\" + a.dir + "1.blp").c_str());
            break;
        }
    }
    return 0;
}

// Dev tool: parse an M2 (+ its .skin) from client data and print decoded counts, so the
// loader can be validated headless. Usage: --m2-test <clientRoot> <m2Path>
int M2Test(const std::string& clientRoot, const std::string& m2Path)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::printf("Opened: %s\n", cd.SourceDescription().c_str());
    std::printf("ListFiles(.m2) -> %zu entries\n", cd.ListFiles(".m2").size());

    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }

    std::printf("M2 OK  %s\n", model.name.c_str());
    std::printf("  vertices=%zu indices=%zu batches=%zu\n", model.vertices.size(),
                model.indices.size(), model.batches.size());
    std::printf("  textures=%zu bones=%zu sequences=%zu globalSeq=%zu\n",
                model.texturePaths.size(), model.bones.size(), model.sequences.size(),
                model.globalSequenceDurations.size());
    std::printf("  particles=%zu ribbons=%zu texAnims=%zu colors=%zu transparencies=%zu\n",
                model.particleEmitters.size(), model.ribbonEmitters.size(),
                model.textureTransforms.size(), model.colors.size(), model.transparencies.size());
    std::printf("  geoParticleModels=%zu\n", model.geoParticleModels.size());
    for (size_t i = 0; i < model.geoParticleModels.size(); ++i)
        std::printf("  geoModel[%zu] verts=%zu subs=%zu textures=%zu texBase=%d\n", i,
                    model.geoParticleModels[i].verts.size(), model.geoParticleModels[i].subs.size(),
                    model.geoParticleModels[i].textures.size(), model.geoParticleModels[i].textureBase);
    std::printf("  bounds center=(%.2f,%.2f,%.2f) radius=%.2f\n", model.boundsCenter.x,
                model.boundsCenter.y, model.boundsCenter.z, model.boundsRadius);
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
        std::printf("  tex[%zu] type=%u '%s'\n", i, model.textureTypes[i],
                    model.texturePaths[i].c_str());
    for (size_t i = 0; i < model.batches.size() && i < 8; ++i)
    {
        const we::m2::RenderBatch& b = model.batches[i];
        std::printf("  batch[%zu] idx[%u..+%u] tex=%d blend=%u submesh=%u\n", i, b.indexStart,
                    b.indexCount, b.textureIndex, b.blendMode, b.submeshId);
    }
    std::printf("M2TEST OK\n");
    return 0;
}

// Dev tool: scan every .m2 in the client and report models that use geometry-model
// particles (emitter.geometryModelFilename). Usage: --m2-geoscan <clientRoot> [limit]
int M2GeoScan(const std::string& clientRoot, int limit)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::vector<std::string> all = cd.ListFiles(".m2");
    std::printf("scanning %zu .m2 files (limit hits=%d)\n", all.size(), limit);
    int hits = 0, scanned = 0;
    for (const std::string& path : all)
    {
        we::m2::M2Model model;
        if (!we::m2::Load(cd, path, model, nullptr))
            continue;
        ++scanned;
        if (!model.geoParticleModels.empty())
        {
            ++hits;
            std::printf("HIT %s  geoModels=%zu (of %zu emitters)\n", path.c_str(),
                        model.geoParticleModels.size(), model.particleEmitters.size());
            if (limit > 0 && hits >= limit)
                break;
        }
    }
    std::printf("M2GEOSCAN done: scanned=%d hits=%d\n", scanned, hits);
    return 0;
}

// Shared: bring up the renderer headless, render `model` to a BMP through the real
// Vulkan model pipeline. `model.texturePaths` should already have any runtime skins
// filled in by the caller. `bones`/`boneCount` optional (null => bind pose).
// Write top-down RGBA8 as a 32-bit BMP (negative height), like --dump-blp.
void WriteBmp(const std::vector<uint8_t>& rgba, int rw, int rh, const std::string& outBmp)
{
    const uint32_t rowBytes = static_cast<uint32_t>(rw) * 4, pixBytes = rowBytes * rh;
    const uint32_t fileSize = 54 + pixBytes;
    uint8_t hdr[54] = {0}; hdr[0] = 'B'; hdr[1] = 'M';
    std::memcpy(hdr + 2, &fileSize, 4); uint32_t off = 54; std::memcpy(hdr + 10, &off, 4);
    uint32_t dib = 40; std::memcpy(hdr + 14, &dib, 4);
    std::memcpy(hdr + 18, &rw, 4); int negH = -rh; std::memcpy(hdr + 22, &negH, 4);
    uint16_t planes = 1; std::memcpy(hdr + 26, &planes, 2);
    uint16_t bpp = 32; std::memcpy(hdr + 28, &bpp, 2);
    std::memcpy(hdr + 34, &pixBytes, 4);
    FILE* f = std::fopen(outBmp.c_str(), "wb");
    if (!f) return;
    std::fwrite(hdr, 1, 54, f);
    std::vector<uint8_t> row(rowBytes);
    for (int y = 0; y < rh; ++y)
    {
        const uint8_t* src = rgba.data() + static_cast<size_t>(y) * rowBytes;
        for (int x = 0; x < rw; ++x)
        { row[x*4]=src[x*4+2]; row[x*4+1]=src[x*4+1]; row[x*4+2]=src[x*4]; row[x*4+3]=src[x*4+3]; }
        std::fwrite(row.data(), 1, rowBytes, f);
    }
    std::fclose(f);
}

// Bring up the renderer headless, render an already-built ModelUpload to a BMP framed on
// the given bounds. Shared by the M2 and WMO shot harnesses (WMO passes bones=nullptr).
int RenderUploadToBmp(const we::ModelUpload& up, const glm::vec3& center, float radius,
                      const std::string& outBmp, const char* okTag,
                      const float* bones = nullptr, int boneCount = 0,
                      const we::EffectFrame* effects = nullptr)
{
    // Minimal graphics bring-up (hidden window, no swapchain present).
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "shot", /*visible=*/false))
    {
        std::fprintf(stderr, "window create failed\n");
        return 1;
    }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, /*headless=*/true))
    {
        std::fprintf(stderr, "renderer init failed\n");
        return 1;
    }

    we::ModelHandle h = renderer->CreateModel(up);
    if (!h)
    {
        std::fprintf(stderr, "CreateModel failed\n");
        return 1;
    }

    const int W = 600, H = 600;
    const glm::vec3 c = center;
    const float r = radius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));  // WoW is Z-up
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.05f, r * 30.0f);
    proj[1][1] *= -1.0f;  // Vulkan clip space Y points down

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.5f, std::max(r * 2.5f / 20.0f, 1e-4f));
    renderer->RenderModel(h, &view[0][0], &proj[0][0], bones, boneCount, nullptr, 0, effects, W, H);
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh))
    {
        std::fprintf(stderr, "capture failed\n");
        return 1;
    }

    WriteBmp(rgba, rw, rh, outBmp);

    renderer->DestroyModel(h);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("%s -> %s (%dx%d, %zu verts, %zu batches)\n", okTag, outBmp.c_str(), rw, rh,
                up.vertices.size(), up.submeshes.size());
    return 0;
}

int RenderM2ToBmp(we::ClientData& cd, const we::m2::M2Model& model, const std::string& outBmp,
                  const float* bones = nullptr, int boneCount = 0,
                  const we::EffectFrame* effects = nullptr)
{
    we::ModelUpload up = we::m2::BuildUpload(cd, model);
    return RenderUploadToBmp(up, model.boundsCenter, model.boundsRadius, outBmp, "M2SHOT OK",
                             bones, boneCount, effects);
}

// True for a WMO group file ("Foo_000.wmo".."Foo_NNN.wmo"), which the browser hides.
bool IsWmoGroupFile(const std::string& path)
{
    if (path.size() < 8)
        return false;
    const char* p = path.c_str() + path.size() - 8;   // "_NNN.wmo"
    return p[0] == '_' && std::isdigit((unsigned char)p[1]) && std::isdigit((unsigned char)p[2]) &&
           std::isdigit((unsigned char)p[3]);
}

// Dev tool: dump LiquidType.dbc rows (id, type, and every non-empty string field) so the
// exact 3.3.5a field layout can be confirmed. Usage: --dbc-liquid <clientRoot>
int DbcLiquid(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::Dbc dbc;
    if (!dbc.Load(cd.ReadFile("DBFilesClient\\LiquidType.dbc")))
    {
        std::fprintf(stderr, "LiquidType.dbc load failed\n");
        return 1;
    }
    std::printf("LiquidType.dbc: %u records, %u fields, recSize=%u\n", dbc.RecordCount(),
                dbc.FieldCount(), dbc.RecordSize());
    for (uint32_t r = 0; r < dbc.RecordCount() && r < 45; ++r)
    {
        std::printf("id=%u f3=%u : ", dbc.GetUInt(r, 0), dbc.GetUInt(r, 3));
        for (uint32_t f = 1; f < dbc.FieldCount(); ++f)
        {
            std::string s = dbc.GetString(r, f);
            if (s.size() > 1 && s.size() < 80)
                std::printf("[%u]=%s ", f, s.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

// Dev tool: dump AnimationData.dbc id -> name. Usage: --anim-names <clientRoot>
int AnimNames(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore dbc;
    auto names = dbc.LoadAnimationNames(cd);
    std::printf("AnimationData.dbc: %zu names\n", names.size());
    for (uint32_t id = 0; id < 30; ++id)
    {
        auto it = names.find(id);
        if (it != names.end()) std::printf("  %u = %s\n", id, it->second.c_str());
    }
    return 0;
}

// Dev tool: dump Map.dbc id -> directory + name. Usage: --dbc-map <clientRoot>
int DbcMap(const std::string& clientRoot)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore dbc;
    auto maps = dbc.LoadMaps(cd);
    std::printf("Map.dbc: %zu maps\n", maps.size());
    for (uint32_t id = 0; id < 800; ++id)
    {
        auto it = maps.find(id);
        if (it != maps.end())
            std::printf("  %u = %-24s (%s)\n", id, it->second.directory.c_str(), it->second.name.c_str());
    }
    return 0;
}

// Usage: --wmo-test <clientRoot> <wmoPath>
int WmoTest(const std::string& clientRoot, const std::string& wmoPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::wmo::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::wmo::WmoModel model;
    std::string err;
    if (!we::wmo::Load(cd, wmoPath, model, {}, &liquids, &err))
    {
        std::fprintf(stderr, "WMO load FAILED (%s): %s\n", wmoPath.c_str(), err.c_str());
        return 1;
    }
    std::printf("WMO OK  %s\n", model.name.c_str());
    std::printf("  groups=%d materials=%d vertices=%zu indices=%zu submeshes=%zu\n",
                model.groupCount, model.materialCount, model.vertices.size(),
                model.indices.size(), model.submeshes.size());
    std::printf("  doodadSets=%zu doodadDefs=%d liquidTiles=%d textures=%zu\n",
                model.doodadSets.size(), model.doodadDefCount, model.liquidTileCount,
                model.texturePaths.size());
    std::printf("  bounds center=(%.1f,%.1f,%.1f) radius=%.1f\n", model.boundsCenter.x,
                model.boundsCenter.y, model.boundsCenter.z, model.boundsRadius);
    for (size_t i = 0; i < model.texturePaths.size() && i < 8; ++i)
        std::printf("  tex[%zu] '%s'\n", i, model.texturePaths[i].c_str());
    for (size_t i = 0; i < model.doodadSets.size(); ++i)
        std::printf("  set[%zu] '%s' first=%u count=%u\n", i, model.doodadSets[i].name.c_str(),
                    model.doodadSets[i].first, model.doodadSets[i].count);
    std::printf("WMOTEST OK\n");
    return 0;
}

// Usage: --adt-test <clientRoot> <adtPath>
int AdtTest(const std::string& clientRoot, const std::string& adtPath)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::adt::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::adt::AdtTile tile;
    std::string err;
    if (!we::adt::Load(cd, adtPath, tile, {}, &liquids, &err))
    {
        std::fprintf(stderr, "ADT load FAILED (%s): %s\n", adtPath.c_str(), err.c_str());
        return 1;
    }
    std::printf("ADT OK  %s\n", tile.name.c_str());
    std::printf("  chunks=%d rendered=%d vertices=%zu indices=%zu submeshes=%zu\n",
                tile.chunkCount, tile.renderedChunks, tile.vertices.size(),
                tile.indices.size(), tile.submeshes.size());
    std::printf("  textures=%d doodadDefs=%d wmoDefs=%d liquidTiles=%d mh2o=%d mclqChunks=%d noLayerChunks=%d\n",
                tile.textureCount, tile.doodadDefCount, tile.wmoDefCount,
                tile.liquidTileCount, tile.hasMh2o ? 1 : 0, tile.mclqChunks, tile.chunksNoLayer);
    std::printf("  worldOffset=(%.1f,%.1f) bounds center=(%.1f,%.1f,%.1f) radius=%.1f\n",
                tile.worldOffset.x, tile.worldOffset.y, tile.boundsCenter.x,
                tile.boundsCenter.y, tile.boundsCenter.z, tile.boundsRadius);
    int m2p = 0, wmop = 0;
    for (const auto& p : tile.placements) (p.isWmo ? wmop : m2p)++;
    std::printf("  placements=%zu (%d M2, %d WMO)\n", tile.placements.size(), m2p, wmop);
    // Raw top-level chunk list (diagnostic): magic + size, MCNK collapsed to a count.
    {
        std::vector<uint8_t> bytes = cd.ReadFile(adtPath);
        we::ByteReader rr(bytes);
        we::ChunkIter it(rr);
        we::Chunk c;
        int mcnk = 0;
        std::string line = "  chunks:";
        while (it.Next(c))
        {
            if (c.Is("MCNK")) { ++mcnk; continue; }
            char m[5] = {c.magic[3], c.magic[2], c.magic[1], c.magic[0], 0};
            line += " " + std::string(m) + "(" + std::to_string(c.size) + ")";
        }
        line += " MCNKx" + std::to_string(mcnk);
        std::printf("%s\n", line.c_str());
    }
    std::printf("ADTTEST OK\n");
    return 0;
}

// Usage: --adt-shot <clientRoot> <adtPath> <outBmp>  (terrain via the dedicated terrain pipeline)
int AdtShot(const std::string& clientRoot, const std::string& adtPath, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::adt::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::adt::AdtTile tile;
    std::string err;
    if (!we::adt::Load(cd, adtPath, tile, {}, &liquids, &err))
    {
        std::fprintf(stderr, "ADT load FAILED (%s): %s\n", adtPath.c_str(), err.c_str());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "adt-shot", /*visible=*/false)) { std::fprintf(stderr, "window failed\n"); return 1; }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, /*headless=*/true)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    we::TerrainUpload up = we::adt::BuildTerrainUpload(cd, tile);
    we::TerrainHandle th = renderer->CreateTerrain(up);
    if (!th) { std::fprintf(stderr, "CreateTerrain failed\n"); return 1; }

    we::ModelUpload liqUp = we::adt::BuildLiquidUpload(cd, tile);
    we::ModelHandle liqH = liqUp.vertices.empty() ? 0 : renderer->CreateModel(liqUp);

    const int W = 600, H = 600;
    const glm::vec3 c = tile.boundsCenter;
    const float r = tile.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.85f)) * (r * 2.2f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.02f, r * 6.0f);
    proj[1][1] *= -1.0f;
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    // Load unique M2/WMO models referenced by placements; instance them into the scene.
    struct UM { bool isWmo = false; we::ModelHandle h = 0;
                we::m2::M2Model m2; we::m2::M2Animator anim; bool emit = false;
                std::vector<glm::mat4> local; std::vector<we::SubmeshAnim> sub;
                glm::vec3 bcenter{0.0f}; float bradius = 1.0f; };
    struct PI { int u = -1; glm::mat4 xform{1.0f}; glm::vec3 origin{0.0f};
                std::unique_ptr<we::m2::M2EffectSystem> fx; std::vector<glm::mat4> pal; };
    std::vector<std::unique_ptr<UM>> uniq;
    std::vector<PI> insts;
    std::unordered_map<std::string, int> cache;
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };

    for (const we::adt::AdtPlacement& p : tile.placements)
    {
        std::string key = lower(p.path);
        int mi;
        auto cit = cache.find(key);
        if (cit == cache.end())
        {
            auto um = std::make_unique<UM>();
            um->isWmo = p.isWmo;
            if (p.isWmo)
            {
                we::wmo::WmoModel wm;
                if (!we::wmo::Load(cd, p.path, wm, {}, &liquids, nullptr)) { cache[key] = -1; continue; }
                we::ModelUpload wup = we::wmo::BuildUpload(cd, wm);
                for (we::ModelVertexGpu& v : wup.vertices) { v.boneIndices[0] = 0; v.boneWeights[0] = 1.0f; }
                wup.boneCount = 1;   // shell rides bone 0 = the placement matrix
                um->h = renderer->CreateModel(wup);
                if (!um->h) { cache[key] = -1; continue; }
                um->bcenter = wm.boundsCenter; um->bradius = wm.boundsRadius;
            }
            else
            {
                if (!we::m2::Load(cd, p.path, um->m2, nullptr)) { cache[key] = -1; continue; }
                um->anim.SetModel(&um->m2, &cd, p.path);
                um->emit = !um->m2.particleEmitters.empty() || !um->m2.ribbonEmitters.empty();
                we::ModelUpload mup = we::m2::BuildUpload(cd, um->m2);
                um->h = renderer->CreateModel(mup);
                if (!um->h) { cache[key] = -1; continue; }
                um->anim.Evaluate(0, 0.0f, view, um->local);   // bind / stand pose
                um->sub.resize(um->m2.batches.size());
                for (size_t bi = 0; bi < um->m2.batches.size(); ++bi)
                {
                    const we::m2::RenderBatch& b = um->m2.batches[bi];
                    glm::mat4 tmx = um->anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
                    std::memcpy(um->sub[bi].texMatrix, &tmx[0][0], sizeof(um->sub[bi].texMatrix));
                    glm::vec4 col = um->anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
                    um->sub[bi].color[0] = col.r; um->sub[bi].color[1] = col.g;
                    um->sub[bi].color[2] = col.b; um->sub[bi].color[3] = col.a;
                }
                um->bcenter = um->m2.boundsCenter; um->bradius = um->m2.boundsRadius;
            }
            mi = (int)uniq.size(); cache[key] = mi; uniq.push_back(std::move(um));
        }
        else { mi = cit->second; if (mi < 0) continue; }

        PI pi; pi.u = mi;
        std::memcpy(&pi.xform[0][0], p.transform, sizeof(p.transform));
        pi.origin = glm::vec3(p.origin[0], p.origin[1], p.origin[2]);
        if (uniq[mi]->emit) { pi.fx = std::make_unique<we::m2::M2EffectSystem>(); pi.fx->SetModel(&uniq[mi]->m2, &uniq[mi]->anim); }
        insts.push_back(std::move(pi));
    }

    for (PI& pi : insts)   // fold placement transform into each palette
    {
        UM* um = uniq[pi.u].get();
        if (um->isWmo) { pi.pal.assign(1, pi.xform); }
        else
        {
            pi.pal.resize(um->local.size());
            for (size_t j = 0; j < um->local.size(); ++j) pi.pal[j] = pi.xform * um->local[j];
        }
    }
    for (int f = 0; f < 30; ++f)   // warm up particle sims
        for (PI& pi : insts)
            if (pi.fx) pi.fx->Step(0, 0.0f, 33.0f, pi.pal, camRight, camUp, eye);

    const glm::mat4 vpCull = proj * view;
    int culled = 0;
    std::vector<we::SceneInstanceGpu> scene;
    scene.reserve(insts.size() + 1);
    if (liqH) { we::SceneInstanceGpu si{}; si.handle = liqH; scene.push_back(si); }   // liquid surface
    for (PI& pi : insts)
    {
        UM* um = uniq[pi.u].get();
        glm::vec3 cc = glm::vec3(pi.xform * glm::vec4(um->bcenter, 1.0f));
        float cr = um->bradius * glm::length(glm::vec3(pi.xform[0])) + 0.01f;
        if (!we::SphereInFrustum(vpCull, cc, cr)) { ++culled; continue; }
        we::SceneInstanceGpu si{};
        si.handle = um->h;
        si.boneMatrices = pi.pal.empty() ? nullptr : reinterpret_cast<const float*>(pi.pal.data());
        si.boneCount = (int)pi.pal.size();
        si.submeshAnims = um->sub.empty() ? nullptr : um->sub.data();
        si.submeshAnimCount = (int)um->sub.size();
        if (pi.fx)
        {
            const we::m2::EffectGeometry& g = pi.fx->Geometry();
            si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
            si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        }
        si.worldOrigin[0] = pi.origin.x; si.worldOrigin[1] = pi.origin.y; si.worldOrigin[2] = pi.origin.z;
        scene.push_back(si);
    }

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.2f, std::max(r * 2.2f / 20.0f, 1e-4f));
    renderer->RenderWorld(th, scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], W, H);
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh)) { std::fprintf(stderr, "capture failed\n"); return 1; }
    WriteBmp(rgba, rw, rh, outBmp);
    if (liqH) renderer->DestroyModel(liqH);
    renderer->DestroyTerrain(th);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("ADTSHOT OK -> %s (%dx%d, %zu chunks, %zu placements / %zu models, %d culled, %zu liquidTiles)\n",
                outBmp.c_str(), rw, rh, up.submeshes.size(), insts.size(), uniq.size(), culled,
                (size_t)tile.liquidTileCount);
    return 0;
}

// Usage: --wmo-list <clientRoot> [max]
int WmoList(const std::string& clientRoot, int maxPrint)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::vector<std::string> all = cd.ListFiles(".wmo");
    int roots = 0;
    for (const std::string& p : all)
    {
        if (IsWmoGroupFile(p))
            continue;
        if (roots < maxPrint)
            std::printf("%s\n", p.c_str());
        ++roots;
    }
    std::printf("WMOLIST done: %zu entries, %d root WMOs\n", all.size(), roots);
    return 0;
}

// Usage: --wmo-shot <clientRoot> <wmoPath> <outBmp>
// Renders the WMO shell plus its doodads as live instances (bind pose; effects warmed up a
// few frames so torches/braziers show fire) through the multi-instance scene renderer.
int WmoShot(const std::string& clientRoot, const std::string& wmoPath, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::DbcStore dbc;
    we::wmo::LiquidTypeTable liquids = dbc.LoadLiquidTypes(cd);
    we::wmo::WmoModel model;
    std::string err;
    if (!we::wmo::Load(cd, wmoPath, model, {}, &liquids, &err))
    {
        std::fprintf(stderr, "WMO load FAILED (%s): %s\n", wmoPath.c_str(), err.c_str());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    we::Window window;
    if (!window.Create(600, 600, "wmoshot", false)) { std::fprintf(stderr, "window failed\n"); return 1; }
    auto renderer = std::make_unique<we::VulkanRenderer>();
    if (!renderer->Init(window, true)) { std::fprintf(stderr, "renderer init failed\n"); return 1; }

    we::ModelUpload shellUp = we::wmo::BuildUpload(cd, model);
    we::ModelHandle shellH = renderer->CreateModel(shellUp);
    if (!shellH) { std::fprintf(stderr, "CreateModel failed\n"); return 1; }

    const int W = 600, H = 600;
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), float(W) / H, r * 0.05f, r * 30.0f);
    proj[1][1] *= -1.0f;
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    struct UD { we::ModelHandle h = 0; we::m2::M2Model model; we::m2::M2Animator anim;
                bool emit = false; std::vector<glm::mat4> local; std::vector<we::SubmeshAnim> sub; };
    struct DI { int u = -1; glm::mat4 xform{1.0f}; glm::vec3 origin{0.0f};
                std::unique_ptr<we::m2::M2EffectSystem> fx; std::vector<glm::mat4> pal; };
    std::vector<std::unique_ptr<UD>> uniq;
    std::vector<DI> insts;
    std::unordered_map<std::string, int> cache;
    auto lower = [](std::string s) { for (char& ch : s) ch = (char)std::tolower((unsigned char)ch); return s; };

    for (const we::wmo::WmoDoodadInstance& inst : model.doodadInstances)
    {
        std::string key = lower(inst.m2Path);
        int mi;
        auto it = cache.find(key);
        if (it == cache.end())
        {
            auto ud = std::make_unique<UD>();
            if (!we::m2::Load(cd, inst.m2Path, ud->model, nullptr)) { cache[key] = -1; continue; }
            ud->anim.SetModel(&ud->model, &cd, inst.m2Path);
            ud->emit = !ud->model.particleEmitters.empty() || !ud->model.ribbonEmitters.empty();
            we::ModelUpload up = we::m2::BuildUpload(cd, ud->model);
            ud->h = renderer->CreateModel(up);
            if (!ud->h) { cache[key] = -1; continue; }
            ud->anim.Evaluate(0, 0.0f, view, ud->local);   // bind / stand pose
            ud->sub.resize(ud->model.batches.size());
            for (size_t bi = 0; bi < ud->model.batches.size(); ++bi)
            {
                const we::m2::RenderBatch& b = ud->model.batches[bi];
                glm::mat4 tmx = ud->anim.TextureMatrix(b.textureTransformIndex, 0, 0.0f);
                std::memcpy(ud->sub[bi].texMatrix, &tmx[0][0], sizeof(ud->sub[bi].texMatrix));
                glm::vec4 col = ud->anim.BatchColor(b.colorIndex, b.textureWeightIndex, 0, 0.0f);
                ud->sub[bi].color[0] = col.r; ud->sub[bi].color[1] = col.g;
                ud->sub[bi].color[2] = col.b; ud->sub[bi].color[3] = col.a;
            }
            mi = (int)uniq.size(); cache[key] = mi; uniq.push_back(std::move(ud));
        }
        else { mi = it->second; if (mi < 0) continue; }

        DI di; di.u = mi;
        std::memcpy(&di.xform[0][0], inst.transform, sizeof(inst.transform));
        di.origin = glm::vec3(inst.origin[0], inst.origin[1], inst.origin[2]);
        if (uniq[mi]->emit) { di.fx = std::make_unique<we::m2::M2EffectSystem>(); di.fx->SetModel(&uniq[mi]->model, &uniq[mi]->anim); }
        insts.push_back(std::move(di));
    }

    for (DI& di : insts)   // fold instance transform into each palette
    {
        UD* ud = uniq[di.u].get();
        di.pal.resize(ud->local.size());
        for (size_t j = 0; j < ud->local.size(); ++j) di.pal[j] = di.xform * ud->local[j];
    }
    for (int f = 0; f < 45; ++f)   // warm up particle sims so fire/glow is visible
        for (DI& di : insts)
            if (di.fx) di.fx->Step(0, 0.0f, 33.0f, di.pal, camRight, camUp, eye);

    // Frustum-cull (sanity: framed on the whole WMO, nearly all instances pass).
    const glm::mat4 vpCull = proj * view;
    int culled = 0;

    std::vector<we::SceneInstanceGpu> scene;
    scene.reserve(insts.size() + 1);
    { we::SceneInstanceGpu shell{}; shell.handle = shellH; scene.push_back(shell); }
    for (DI& di : insts)
    {
        UD* ud = uniq[di.u].get();
        glm::vec3 cc = glm::vec3(di.xform * glm::vec4(ud->model.boundsCenter, 1.0f));
        float cr = ud->model.boundsRadius * glm::length(glm::vec3(di.xform[0])) + 0.01f;
        if (!we::SphereInFrustum(vpCull, cc, cr)) { ++culled; continue; }
        we::SceneInstanceGpu si{};
        si.handle = ud->h;
        si.boneMatrices = di.pal.empty() ? nullptr : reinterpret_cast<const float*>(di.pal.data());
        si.boneCount = (int)di.pal.size();
        si.submeshAnims = ud->sub.empty() ? nullptr : ud->sub.data();
        si.submeshAnimCount = (int)ud->sub.size();
        if (di.fx)
        {
            const we::m2::EffectGeometry& g = di.fx->Geometry();
            si.effectVerts = g.verts.data(); si.effectVertCount = (int)g.verts.size();
            si.effectDraws = g.draws.data(); si.effectDrawCount = (int)g.draws.size();
        }
        si.worldOrigin[0] = di.origin.x; si.worldOrigin[1] = di.origin.y; si.worldOrigin[2] = di.origin.z;
        scene.push_back(si);
    }

    const float gc[3] = {c.x, c.y, c.z - r};
    renderer->SetGrid(true, gc, r * 2.5f, std::max(r * 2.5f / 20.0f, 1e-4f));
    renderer->RenderScene(scene.data(), (int)scene.size(), &view[0][0], &proj[0][0], W, H);
    std::vector<uint8_t> rgba; int rw = 0, rh = 0;
    if (!renderer->CaptureModelTarget(rgba, rw, rh)) { std::fprintf(stderr, "capture failed\n"); return 1; }
    WriteBmp(rgba, rw, rh, outBmp);

    insts.clear();   // release effect systems before destroying models
    for (auto& ud : uniq) renderer->DestroyModel(ud->h);
    renderer->DestroyModel(shellH);
    renderer->Shutdown();
    ImGui::DestroyContext();
    std::printf("WMOSHOT OK -> %s (%dx%d, %zu doodad instances / %zu models, %d culled)\n", outBmp.c_str(),
                rw, rh, model.doodadInstances.size(), uniq.size(), culled);
    return 0;
}

// Usage: --m2-shot <clientRoot> <m2Path> <outBmp>
int M2Shot(const std::string& clientRoot, const std::string& m2Path, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }
    return RenderM2ToBmp(cd, model, outBmp);
}

// Resolve a creature display id -> model + skin variations, apply the skins, and render.
// Validates the CreatureDisplayInfo/CreatureModelData path (textures creature bodies).
// Usage: --m2-shot-display <clientRoot> <displayId> <outBmp>
int M2ShotDisplay(const std::string& clientRoot, uint32_t displayId, const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    we::DbcStore store;
    auto displays = store.LoadCreatureDisplays(cd);
    auto modelPaths = store.LoadCreatureModelPaths(cd);
    std::printf("displays=%zu creatureModels=%zu\n", displays.size(), modelPaths.size());

    auto dit = displays.find(displayId);
    if (dit == displays.end())
    {
        std::fprintf(stderr, "display %u not found; sample ids:", displayId);
        int n = 0;
        for (auto& kv : displays) { std::fprintf(stderr, " %u", kv.first); if (++n >= 10) break; }
        std::fprintf(stderr, "\n");
        return 1;
    }
    auto mit = modelPaths.find(dit->second.modelId);
    if (mit == modelPaths.end())
    {
        std::fprintf(stderr, "model %u for display %u not found\n", dit->second.modelId, displayId);
        return 1;
    }
    const std::string m2Path = mit->second;
    std::printf("display %u -> model %u '%s' skins ['%s','%s','%s']\n", displayId, dit->second.modelId,
                m2Path.c_str(), dit->second.skins[0].c_str(), dit->second.skins[1].c_str(),
                dit->second.skins[2].c_str());

    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED (%s): %s\n", m2Path.c_str(), err.c_str());
        return 1;
    }
    // Apply skins to runtime slots (types 11/12/13), combined with the model directory.
    std::string dir;
    { size_t s = m2Path.find_last_of("\\/"); dir = (s == std::string::npos) ? "" : m2Path.substr(0, s); }
    for (size_t i = 0; i < model.texturePaths.size(); ++i)
    {
        uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
        if (t >= 11 && t <= 13 && !dit->second.skins[t - 11].empty())
            model.texturePaths[i] = dir + "\\" + dit->second.skins[t - 11] + ".blp";
    }
    return RenderM2ToBmp(cd, model, outBmp);
}

// List the distinct texture-variation skins available for a model, mirroring the model
// viewer's BuildSkinOptions. Fast (no rendering).
// Usage: --m2-skins <clientRoot> <m2Path>
int M2Skins(const std::string& clientRoot, const std::string& m2Path)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::DbcStore store;
    auto modelPaths = store.LoadCreatureModelPaths(cd);
    auto displays = store.LoadCreatureDisplays(cd);

    std::string low = m2Path;
    for (char& c : low) c = (char)std::tolower((unsigned char)c);
    uint32_t modelId = 0;
    for (const auto& kv : modelPaths)
    {
        std::string p = kv.second;
        for (char& c : p) c = (char)std::tolower((unsigned char)c);
        if (p == low) { modelId = kv.first; break; }
    }
    std::printf("model '%s' -> modelId %u\n", m2Path.c_str(), modelId);

    std::unordered_map<std::string, uint32_t> seen;
    for (const auto& kv : displays)
    {
        if (kv.second.modelId != modelId)
            continue;
        const auto& s = kv.second.skins;
        std::string key = s[0] + "|" + s[1] + "|" + s[2];
        if (seen.emplace(key, kv.first).second)
            std::printf("  skin: '%s' | '%s' | '%s'  (display %u)\n", s[0].c_str(), s[1].c_str(),
                        s[2].c_str(), kv.first);
    }
    std::printf("distinct skins=%zu\n", seen.size());
    return 0;
}

// Step the particle/ribbon simulation for `steps` frames (~30fps) then render one frame
// with the effects, to validate the effect system headlessly. Optional display id textures
// the body. Usage: --m2-fx <clientRoot> <m2Path> <animIndex> <steps> <outBmp> [displayId]
int M2Fx(const std::string& clientRoot, const std::string& m2Path, int animIndex, int steps,
         const std::string& outBmp, uint32_t displayId)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot)) { std::fprintf(stderr, "open failed\n"); return 1; }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err)) { std::fprintf(stderr, "load FAILED: %s\n", err.c_str()); return 1; }

    // Optional creature skin so the body is textured.
    if (displayId)
    {
        auto displays = we::DbcStore{}.LoadCreatureDisplays(cd);
        auto it = displays.find(displayId);
        if (it != displays.end())
        {
            std::string dir; size_t sl = m2Path.find_last_of("\\/"); dir = (sl==std::string::npos)?"":m2Path.substr(0,sl);
            for (size_t i = 0; i < model.texturePaths.size(); ++i)
            {
                uint32_t t = i < model.textureTypes.size() ? model.textureTypes[i] : 0;
                if (t >= 11 && t <= 13 && !it->second.skins[t-11].empty())
                    model.texturePaths[i] = dir + "\\" + it->second.skins[t-11] + ".blp";
            }
        }
    }

    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    if (anim.SequenceCount() == 0) { std::fprintf(stderr, "no sequences\n"); return 1; }
    animIndex = std::max(0, std::min(animIndex, anim.SequenceCount() - 1));
    const uint32_t dur = anim.Duration(animIndex);

    // Camera matching RenderM2ToBmp.
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    const glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 camRight(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 camUp(view[0][1], view[1][1], view[2][1]);

    we::m2::M2EffectSystem fx;
    fx.SetModel(&model, &anim);
    std::vector<glm::mat4> bones;
    const float dt = 33.0f;
    float t = 0.0f;
    for (int i = 0; i < steps; ++i)
    {
        t = dur > 0 ? std::fmod(i * dt, (float)dur) : 0.0f;
        anim.Evaluate(animIndex, t, view, bones);
        fx.Step(animIndex, t, dt, bones, camRight, camUp, eye);
    }
    std::printf("fx: live particles=%zu effect verts=%zu draws=%zu\n", fx.LiveParticleCount(),
                fx.Geometry().verts.size(), fx.Geometry().draws.size());

    we::EffectFrame ef;
    ef.verts = fx.Geometry().verts.data();
    ef.vertCount = (int)fx.Geometry().verts.size();
    ef.draws = fx.Geometry().draws.data();
    ef.drawCount = (int)fx.Geometry().draws.size();

    const float* bp = bones.empty() ? nullptr : reinterpret_cast<const float*>(bones.data());
    return RenderM2ToBmp(cd, model, outBmp, bp, (int)bones.size(), &ef);
}

// Render one animation frame to a BMP through the real skinning pipeline, to validate
// the animator + GPU skinning headlessly.
// Usage: --m2-anim <clientRoot> <m2Path> <animIndex> <timeMs> <outBmp>
int M2Anim(const std::string& clientRoot, const std::string& m2Path, int animIndex, float timeMs,
           const std::string& outBmp)
{
    we::ClientData cd;
    if (!cd.Open(clientRoot))
    {
        std::fprintf(stderr, "open failed\n");
        return 1;
    }
    we::m2::M2Model model;
    std::string err;
    if (!we::m2::Load(cd, m2Path, model, &err))
    {
        std::fprintf(stderr, "M2 load FAILED: %s\n", err.c_str());
        return 1;
    }
    we::m2::M2Animator anim;
    anim.SetModel(&model, &cd, m2Path);
    const int seqCount = anim.SequenceCount();
    std::printf("sequences=%d bones=%zu\n", seqCount, model.bones.size());
    if (seqCount == 0)
    {
        std::fprintf(stderr, "no sequences\n");
        return 1;
    }
    // Diagnostic: for each sequence, show inline flag + whether its external .anim file
    // exists under our constructed name (validates the .anim filename format).
    {
        std::string base = m2Path.substr(0, m2Path.find_last_of('.'));
        int ext = 0, extFound = 0;
        for (int i = 0; i < seqCount; ++i)
        {
            const we::m2::M2Sequence& s = model.sequences[i];
            bool inl = (s.flags & 0x20) != 0;
            if (inl) continue;
            ++ext;
            char suf[24];
            std::snprintf(suf, sizeof(suf), "%04u-%02u.anim", s.animationId, s.subAnimationId);
            if (cd.HasFile(base + suf)) ++extFound;
        }
        std::printf("external sequences=%d, of which .anim found=%d\n", ext, extFound);
    }

    animIndex = std::max(0, std::min(animIndex, seqCount - 1));
    const uint32_t dur = anim.Duration(animIndex);
    const bool inl = (model.sequences[animIndex].flags & 0x20) != 0;
    std::printf("anim %d id=%u var=%u dur=%ums inline=%d\n", animIndex,
                model.sequences[animIndex].animationId, model.sequences[animIndex].subAnimationId,
                dur, inl ? 1 : 0);

    // Match RenderM2ToBmp's camera so billboard bones orient correctly.
    const glm::vec3 c = model.boundsCenter;
    const float r = model.boundsRadius;
    const glm::vec3 eye = c + glm::normalize(glm::vec3(1.0f, -1.4f, 0.55f)) * (r * 2.6f);
    const glm::mat4 view = glm::lookAt(eye, c, glm::vec3(0.0f, 0.0f, 1.0f));

    std::vector<glm::mat4> palette;
    anim.Evaluate(animIndex, timeMs, view, palette);
    const float* bones = palette.empty() ? nullptr : reinterpret_cast<const float*>(palette.data());
    return RenderM2ToBmp(cd, model, outBmp, bones, (int)palette.size());
}
} // namespace

int main(int argc, char** argv)
{
    bool selftest = false;
    bool demo = false;
    int demoTab = -1;
    bool emitSql = false;
    std::string emitPath = "build/emit_test.sql";
    std::string shotPath;
    int shotTab = 7;   // POI tab by default
    std::string clientPath;
    std::string editorId;   // --editor <id> selects the startup editor module

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--selftest") == 0)
            selftest = true;
        else if (std::strcmp(argv[i], "--demo") == 0)
            demo = true;
        else if (std::strcmp(argv[i], "--tab") == 0)
        {
            demo = true;
            if (i + 1 < argc)
                demoTab = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--emit-sql") == 0)
        {
            emitSql = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                emitPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--dbc-test") == 0 && i + 1 < argc)
        {
            return DbcTest(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--dump-blp") == 0 && i + 3 < argc)
        {
            return DumpBlp(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-test") == 0 && i + 2 < argc)
        {
            return M2Test(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--m2-geoscan") == 0 && i + 1 < argc)
        {
            int lim = (i + 2 < argc) ? std::atoi(argv[i + 2]) : 20;
            return M2GeoScan(argv[i + 1], lim);
        }
        else if (std::strcmp(argv[i], "--wmo-test") == 0 && i + 2 < argc)
        {
            return WmoTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--dbc-liquid") == 0 && i + 1 < argc)
        {
            return DbcLiquid(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--anim-names") == 0 && i + 1 < argc)
        {
            return AnimNames(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--dbc-map") == 0 && i + 1 < argc)
        {
            return DbcMap(argv[i + 1]);
        }
        else if (std::strcmp(argv[i], "--wmo-list") == 0 && i + 1 < argc)
        {
            int max = (i + 2 < argc) ? std::atoi(argv[i + 2]) : 40;
            return WmoList(argv[i + 1], max);
        }
        else if (std::strcmp(argv[i], "--wmo-shot") == 0 && i + 3 < argc)
        {
            return WmoShot(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--adt-test") == 0 && i + 2 < argc)
        {
            return AdtTest(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--adt-shot") == 0 && i + 3 < argc)
        {
            return AdtShot(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-shot") == 0 && i + 3 < argc)
        {
            return M2Shot(argv[i + 1], argv[i + 2], argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-shot-display") == 0 && i + 3 < argc)
        {
            return M2ShotDisplay(argv[i + 1], (uint32_t)std::atoi(argv[i + 2]), argv[i + 3]);
        }
        else if (std::strcmp(argv[i], "--m2-skins") == 0 && i + 2 < argc)
        {
            return M2Skins(argv[i + 1], argv[i + 2]);
        }
        else if (std::strcmp(argv[i], "--m2-anim") == 0 && i + 5 < argc)
        {
            return M2Anim(argv[i + 1], argv[i + 2], std::atoi(argv[i + 3]), (float)std::atof(argv[i + 4]),
                          argv[i + 5]);
        }
        else if (std::strcmp(argv[i], "--m2-fx") == 0 && i + 5 < argc)
        {
            uint32_t did = (i + 6 < argc && argv[i + 6][0] != '-') ? (uint32_t)std::atoi(argv[i + 6]) : 0;
            return M2Fx(argv[i + 1], argv[i + 2], std::atoi(argv[i + 3]), std::atoi(argv[i + 4]),
                        argv[i + 5], did);
        }
        else if (std::strcmp(argv[i], "--shot") == 0 && i + 1 < argc)
        {
            shotPath = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-')
                shotTab = std::atoi(argv[++i]);
            demo = true;
        }
        else if (std::strcmp(argv[i], "--client") == 0 && i + 1 < argc)
        {
            clientPath = argv[++i];
        }
        else if (std::strcmp(argv[i], "--editor") == 0 && i + 1 < argc)
        {
            editorId = argv[++i];
        }
    }

    if (emitSql)
        return EmitSql(emitPath);

    we::App app;
    if (demoTab >= 0)
        app.SetDemoTab(demoTab);
    if (!shotPath.empty())
        app.SetScreenshot(shotPath, shotTab);
    if (!clientPath.empty())
        app.SetForcedClientPath(clientPath);
    if (!editorId.empty())
        app.SetEditor(editorId);
    return app.Run(selftest, demo);
}
