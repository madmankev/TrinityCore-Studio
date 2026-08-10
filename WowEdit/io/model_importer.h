#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wowedit
{
struct ImportedModel
{
    std::string sourcePath;
    std::string format;
    std::uint32_t vertexCount = 0;
    std::uint32_t triangleCount = 0;
    std::vector<std::string> materials;
    bool skeletalAnimation = false;
};

/** Asset inventory/import facade. M2 remains loaded by the WoW client-data adapter;
 * OBJ is parsed natively, and FBX/M2 are validated and delegated to registered plugins. */
class ModelImporter
{
public:
    bool inspect(const std::string& path, ImportedModel& model, std::string& error) const;
    bool supports(const std::string& path) const;
};
} // namespace wowedit
