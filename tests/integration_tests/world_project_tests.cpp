#include "io/map_serializer.h"
#include "ui/main_window.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

int main()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "wowedit_integration_test.wowedit";
    try
    {
        wowedit::MainWindow editor;
        if (!editor.newMap("Integration Island", 16)) throw std::runtime_error("new map failed");
        editor.project().tiles.front().terrain.heightmap.modifyHeight(4, 4, 12.0f);
        editor.project().tiles.front().terrain.splatmap.setZoneTextures({"textures/grass.ppm", "textures/rock.ppm"});
        editor.project().tiles.front().terrain.splatmap.setLayer(0, "textures/grass.ppm", {1.0f, 0.0f, 0.0f, 0.0f});
        editor.project().tiles.front().terrain.water.setGlobalHeight(3.5f);
        editor.project().tiles.front().terrain.water.editLiquidCell(2, 3, true);
        editor.project().tiles.front().terrain.water.setCellVariation(2, 3, 0.25f);
        wowedit::Doodad tree; tree.name = "Tree"; tree.uniqueId = 77; tree.position = {4.0f, 0.0f, 4.0f}; tree.recalculateBounds();
        editor.project().tiles.front().terrain.doodads.push_back(tree);
        std::string error;
        if (!editor.saveMapAs(path.string(), error)) throw std::runtime_error(error);
        wowedit::WorldProject reloaded; wowedit::MapSerializer serializer;
        if (!serializer.load(path.string(), reloaded, error)) throw std::runtime_error(error);
        if (reloaded.name != "Integration Island" || reloaded.tiles.size() != 1 || reloaded.tiles.front().terrain.doodads.size() != 1) throw std::runtime_error("round trip lost map data");
        if (!reloaded.tiles.front().terrain.water.hasLiquidCell(2, 3) || reloaded.tiles.front().terrain.splatmap.getZoneTextures().size() != 2) throw std::runtime_error("round trip lost water or texture layers");
        if (!editor.invokeMenu("spells.previewer") || !editor.panels().spellPreviewer) throw std::runtime_error("menu action did not open panel");
        std::cout << "WowEdit integration tests passed\n";
    }
    catch (const std::exception& error) { std::cerr << "Integration test failure: " << error.what() << '\n'; return 1; }
    std::error_code ignored; std::filesystem::remove(path, ignored); std::filesystem::remove_all(path.parent_path() / "wowedit_integration_test.blobs", ignored);
    return 0;
}
