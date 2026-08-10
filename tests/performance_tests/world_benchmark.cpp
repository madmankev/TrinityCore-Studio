#include "objects/doodad_manager.h"
#include <chrono>
#include <iostream>
int main()
{
    wowedit::DoodadManager manager;
    const auto start = std::chrono::steady_clock::now();
    manager.commandManager().beginMacro("100k placement benchmark");
    for (std::uint32_t i = 0; i < 100000; ++i)
    {
        wowedit::Doodad item; item.templateId = i % 32; item.name = "BenchmarkDoodad"; item.position = {float(i % 400), 0.0f, float(i / 400)};
        manager.placeDoodad(item);
    }
    manager.commandManager().endMacro();
    const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Placed " << manager.all().size() << " doodads in " << milliseconds << " ms\n";
    return manager.all().size() == 100000 ? 0 : 1;
}
