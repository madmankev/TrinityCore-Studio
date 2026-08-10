#include "core/command.h"
#include "core/event_system.h"
#include "objects/doodad_manager.h"
#include "editing/world_chunk_clipboard.h"
#include "terrain/terrain_chunk.h"
#include "terrain/heightmap.h"
#include "terrain/texture_splatmap.h"
#include "ui/spell_effect_previewer.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void Near(float a, float b, float epsilon, const char* message)
{
    if (std::abs(a - b) > epsilon) throw std::runtime_error(message);
}
} // namespace

int main()
{
    try
    {
        wowedit::CommandManager commands;
        int value = 0;
        commands.executeCommand(std::make_unique<wowedit::LambdaCommand>([&] { value = 3; }, [&] { value = 0; }, "Set value"));
        Expect(value == 3 && commands.canUndo(), "command executes");
        commands.undo(); Expect(value == 0 && commands.canRedo(), "undo restores");
        commands.redo(); Expect(value == 3, "redo restores");
        commands.beginMacro("two values");
        commands.executeCommand(std::make_unique<wowedit::LambdaCommand>([&] { value += 2; }, [&] { value -= 2; }, "plus two"));
        commands.executeCommand(std::make_unique<wowedit::LambdaCommand>([&] { value *= 2; }, [&] { value /= 2; }, "times two"));
        commands.endMacro(); Expect(value == 10, "macro executes once"); commands.undo(); Expect(value == 3, "macro undo reverse order");

        wowedit::Heightmap heightmap(9, 9, 1.0f, 10.0f);
        heightmap.applyBrush({4.0f, 0.0f, 4.0f}, 2.0f, 5.0f, wowedit::BrushFalloffType::Smoothstep, wowedit::Heightmap::BrushOperation::Raise);
        Near(heightmap.getHeight(4, 4), 15.0f, 0.001f, "brush raises center");
        Near(heightmap.getHeight(0, 0), 10.0f, 0.001f, "brush leaves exterior");
        heightmap.setHeight(0, 0, 0.0f); heightmap.setHeight(1, 0, 10.0f); heightmap.setHeight(0, 1, 10.0f); heightmap.setHeight(1, 1, 20.0f);
        Near(heightmap.getInterpolatedHeight(.5f, .5f), 10.0f, .001f, "bilinear interpolation");
        Expect(glm::length(heightmap.calculateNormal(4, 4)) > .99f, "normal normalized");

        const std::filesystem::path pngPath = std::filesystem::temp_directory_path() / "wowedit_heightmap_roundtrip.png";
        wowedit::Heightmap pngSource(4, 4, 1.0f, 0.0f);
        pngSource.setHeight(3, 3, 10.0f);
        Expect(pngSource.exportToImage(pngPath.string()), "PNG heightmap export");
        wowedit::Heightmap pngLoaded;
        Expect(pngLoaded.importFromImage(pngPath.string()), "PNG heightmap import");
        Expect(pngLoaded.getWidth() == 4 && pngLoaded.getHeightCount() == 4, "PNG dimensions round trip");
        Near(pngLoaded.getHeight(0, 0), 0.0f, .001f, "PNG low sample");
        Near(pngLoaded.getHeight(3, 3), 1.0f, .001f, "PNG high sample");
        std::filesystem::remove(pngPath);

        wowedit::TextureSplatmap splatmap(8, 8);
        splatmap.paintBrush({4.0f, 4.0f}, 2.0f, 2, 1.0f, 1.0f);
        Expect(splatmap.sampleDominantLayer(4, 4) == 2, "texture brush selects layer");
        const glm::u8vec4 weight = splatmap.getTexel(4, 4);
        Expect(int(weight.r) + int(weight.g) + int(weight.b) + int(weight.a) >= 254, "splat weights normalize");

        wowedit::DoodadManager doodads;
        wowedit::Doodad doodad; doodad.name = "Test Tree"; doodad.position = {1.0f, 2.0f, 3.0f};
        const std::uint64_t id = doodads.placeDoodad(doodad); Expect(doodads.find(id) != nullptr, "place doodad");
        doodads.moveDoodad(id, {8.0f, 2.0f, 3.0f}); Expect(doodads.find(id)->position.x == 8.0f, "move doodad");
        doodads.commandManager().undo(); Expect(doodads.find(id)->position.x == 1.0f, "undo doodad move");

        wowedit::TerrainChunk source(0, 0, 8, 1.0f);
        source.heightmap.setHeight(2, 2, 42.0f);
        source.splatmap.setTexel(2, 2, {0, 0, 255, 0});
        wowedit::Doodad copiedDoodad; copiedDoodad.uniqueId = 5; copiedDoodad.name = "Copied Tree"; copiedDoodad.position = {2.0f, 0.0f, 2.0f}; copiedDoodad.recalculateBounds();
        source.doodads.push_back(copiedDoodad);
        wowedit::TerrainChunk destination(0, 0, 8, 1.0f);
        wowedit::WorldChunkClipboard clipboard(&commands);
        clipboard.setSource(&source); clipboard.setTarget(&destination);
        clipboard.copyArea({1.0f, 1.0f}, {3.0f, 3.0f}, {});
        clipboard.pasteAt({5.0f, 5.0f}, wowedit::WorldChunkClipboard::PasteMode::Overwrite);
        Near(destination.heightmap.getHeight(5, 5), 42.0f, .001f, "chunk paste terrain");
        Expect(destination.doodads.size() == 1, "chunk paste doodad");
        commands.undo();
        Near(destination.heightmap.getHeight(5, 5), 0.0f, .001f, "chunk paste undo");

        wowedit::SpellEffectPreviewer preview;
        wowedit::SpellData spell; spell.name = "Fireball"; spell.castTimeSeconds = 1.0f; spell.effects.push_back({"fire.m2", "", 1.0f, 2.0f, "Impact"});
        preview.select(spell); preview.play(true); preview.update(4.2f); Expect(preview.playing() && preview.currentTime() < preview.duration(), "looping spell timeline");

        std::cout << "WowEdit core unit tests passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "Test failure: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
