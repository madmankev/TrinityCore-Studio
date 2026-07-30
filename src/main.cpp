// TrinityCore Studio - application entry point.
// The GLFW/GL/ImGui loop and all editor state live in qe::App (src/ui). main()
// parses flags and hands control to the App.
//   --selftest   : render a few headless frames (all panels + tabs), print
//                  "SELFTEST OK", exit 0. Validates the build/link/render chain.
//   --emit-sql [path] : run the real QuestRepository::SaveQuest path for a sample
//                  quest through SqlExportDatabase and write the generated .sql to
//                  `path` (default build/emit_test.sql). Validates SQL generation
//                  offline, with no database. Exits 0 on success.

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

namespace
{
int EmitSql(const std::string& path)
{
    qe::Quest q;
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

    qe::QuestGreeting g;
    g.id = q.tmpl.id;
    g.type = 0;
    g.greeting = "Hello there.";
    q.greetings.push_back(g);

    q.creatureStarters.push_back(1000);
    q.creatureEnders.push_back(1001);
    q.goStarters.push_back(2000);

    qe::QuestPoi poi;
    poi.questID = q.tmpl.id;
    poi.id = 0;
    qe::QuestPoiPoint pt;
    pt.questID = q.tmpl.id;
    pt.idx1 = 0;
    pt.idx2 = 0;
    pt.x = -8913;
    pt.y = 554;
    poi.points.push_back(pt);
    q.pois.push_back(poi);

    qe::QuestLocale loc;
    loc.locale = "deDE";
    loc.title = "Testauftrag";
    loc.templatePresent = true;
    q.locales["deDE"] = loc;

    qe::SqlExportDatabase db;         // no live read source needed for pure writes
    db.SetOutputPath(path);
    qe::QuestRepository repo;
    qe::DbError e = repo.SaveQuest(db, q);
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
    qe::ClientData cd;
    if (!cd.Open(data)) { std::fprintf(stderr, "open failed\n"); return 1; }
    qe::BlpImage img = qe::DecodeBlp(cd.ReadFile(archivePath));
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
    qe::ClientData cd;
    if (!cd.Open(path))
    {
        std::fprintf(stderr, "open failed: %s\n", cd.SourceDescription().c_str());
        return 1;
    }
    std::printf("Opened: %s\n", cd.SourceDescription().c_str());
    qe::DbcStore store;
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
        qe::Dbc d;
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
        qe::BlpImage img = qe::DecodeBlp(cd.ReadFile(p));
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

    qe::App app;
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
