#include "ui/main_window.h"

#include <iostream>

int main(int argc, char** argv)
{
    wowedit::MainWindow editor;
    editor.newMap("Starter Island", 64);
    wowedit::TerrainChunk& island = editor.project().tiles.front().terrain;

    // A small, entirely procedural scene verifies that a fresh build immediately
    // demonstrates sculpting, texture layers, water, a doodad, and a patrol route.
    island.splatmap.setZoneTextures({"resources/textures/grass.ppm", "resources/textures/dirt.ppm",
                                    "resources/textures/rock.ppm", "resources/textures/sand.ppm",
                                    "resources/textures/snow.ppm"});
    island.splatmap.setLayer(0, "resources/textures/grass.ppm", {1.0f, 0.0f, 0.0f, 0.0f});
    island.splatmap.setLayer(1, "resources/textures/dirt.ppm", {0.0f, 1.0f, 0.0f, 0.0f});
    island.splatmap.setLayer(2, "resources/textures/rock.ppm", {0.0f, 0.0f, 1.0f, 0.0f});
    island.splatmap.setLayer(3, "resources/textures/sand.ppm", {0.0f, 0.0f, 0.0f, 1.0f});
    island.heightmap.setNoiseSettings(0.08f, 1.5f, 3, 1337);
    island.heightmap.applyBrush({32.0f, 0.0f, 32.0f}, 22.0f, 8.0f, wowedit::BrushFalloffType::Gaussian,
                                wowedit::Heightmap::BrushOperation::Noise);
    island.splatmap.applyHeightBasedBlending(island.heightmap);

    // A command-backed road demonstrates the path tool in the sample project:
    // it grades a slope-constrained trail and paints the dirt layer in one undo step.
    wowedit::RoadPathTool road(editor.commands());
    road.settings().width = 4.5f;
    road.settings().shoulderWidth = 2.0f;
    road.settings().textureLayer = 1; // dirt
    road.settings().conformToTerrain = true;
    road.build(island, {{8.0f, 0.0f, 18.0f}, {27.0f, 0.0f, 30.0f}, {48.0f, 0.0f, 49.0f}});

    island.water.setGlobalHeight(-0.2f);
    for (std::uint32_t y = 0; y < 12; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            island.water.editLiquidCell(x, y, true);

    wowedit::Doodad tree;
    tree.uniqueId = 1;
    tree.templateId = 5001;
    tree.name = "Starter Pine";
    tree.filePath = "resources/default_assets/tree_pine.obj";
    tree.position = {31.0f, island.heightmap.getInterpolatedHeight(31.0f, 31.0f), 31.0f};
    tree.scale = {2.2f, 3.0f, 2.2f};
    tree.setName = "Starter Forest";
    tree.recalculateBounds();
    island.doodads.push_back(tree);

    wowedit::CreatureSpawner wolf;
    wolf.uniqueId = 100;
    wolf.creatureTemplateId = 1542;
    wolf.creatureName = "Starter Island Wolf";
    wolf.position = {40.0f, island.heightmap.getInterpolatedHeight(40.0f, 40.0f), 40.0f};
    wolf.aiBehavior.aggroRadius = 20.0f;
    wolf.aiBehavior.leashRadius = 50.0f;
    wolf.patrolLoop = true;
    wolf.waypointSystem.add({44.0f, island.heightmap.getInterpolatedHeight(44.0f, 40.0f), 40.0f}, 1.0f);
    wolf.waypointSystem.add({44.0f, island.heightmap.getInterpolatedHeight(44.0f, 44.0f), 44.0f}, 0.0f);
    wolf.waypointSystem.add({40.0f, island.heightmap.getInterpolatedHeight(40.0f, 44.0f), 44.0f}, 1.0f);
    island.creatures.push_back(wolf);

    editor.invokeMenu("terrain.raise_lower");
    editor.update(1.0f / 60.0f);

    if (argc > 1)
    {
        std::string error;
        if (!editor.saveMapAs(argv[1], error))
        {
            std::cerr << "Could not save project: " << error << '\n';
            return 1;
        }
        std::cout << "Saved WowEdit project to " << argv[1] << '\n';
    }
    else
    {
        std::cout << "WowEdit portable core is ready. Pass a .wowedit path to create a sample project.\n";
    }
    return 0;
}
