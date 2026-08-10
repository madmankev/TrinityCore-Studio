#include "io/model_importer.h"
#include "io/file_io.h"
#include "utils/string_utils.h"
#include <filesystem>
#include <sstream>
namespace wowedit
{
namespace { std::string Ext(const std::string& p) { return strings::ToLower(std::filesystem::path(p).extension().string()); } }
bool ModelImporter::supports(const std::string& path) const { const auto e = Ext(path); return e == ".m2" || e == ".obj" || e == ".fbx"; }
bool ModelImporter::inspect(const std::string& path, ImportedModel& model, std::string& error) const
{
    if (!supports(path)) { error = "Expected .m2, .obj, or .fbx"; return false; }
    std::string text;
    if (!FileIo::readText(path, text, &error)) return false;
    model = {}; model.sourcePath = path; model.format = Ext(path).substr(1);
    if (model.format == "obj")
    {
        std::istringstream stream(text); std::string line;
        while (std::getline(stream, line))
        {
            if (line.rfind("v ", 0) == 0) ++model.vertexCount;
            else if (line.rfind("f ", 0) == 0) ++model.triangleCount;
            else if (line.rfind("usemtl ", 0) == 0) model.materials.push_back(strings::Trim(line.substr(7)));
        }
    }
    else if (model.format == "m2")
    {
        if (text.size() < 4 || text.compare(0, 4, "MD20") != 0) { error = "M2 header is not MD20"; return false; }
        model.skeletalAnimation = true;
    }
    else
    {
        // Binary FBX parsing is deliberately delegated to an optional importer plugin;
        // ASCII headers still make the file discoverable in the browser.
        if (text.find("FBX") == std::string::npos && text.size() < 27) { error = "Invalid FBX header"; return false; }
    }
    return true;
}
} // namespace wowedit
