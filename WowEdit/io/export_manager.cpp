#include "io/export_manager.h"
#include "io/file_io.h"
#include <sstream>
namespace wowedit
{
bool ExportManager::exportObjectListCsv(const TerrainChunk& chunk, const std::string& path, std::string& error) const
{
    std::ostringstream stream; stream << "id,templateId,name,path,x,y,z,pitch,yaw,roll,scaleX,scaleY,scaleZ\n";
    for (const Doodad& d : chunk.doodads) stream << d.uniqueId << ',' << d.templateId << ',' << '"' << d.name << "\",\"" << d.filePath << "\"," << d.position.x << ',' << d.position.y << ',' << d.position.z << ',' << d.rotation.x << ',' << d.rotation.y << ',' << d.rotation.z << ',' << d.scale.x << ',' << d.scale.y << ',' << d.scale.z << '\n';
    return FileIo::writeTextAtomic(path, stream.str(), &error);
}
bool ExportManager::exportCollisionObj(const TerrainChunk& chunk, const std::string& path, std::string& error) const
{
    const Mesh mesh = chunk.generateCollisionMesh(); std::ostringstream stream;
    for (const MeshVertex& v : mesh.vertices) stream << "v " << v.position.x << ' ' << v.position.y << ' ' << v.position.z << '\n';
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) stream << "f " << mesh.indices[i] + 1 << ' ' << mesh.indices[i + 1] + 1 << ' ' << mesh.indices[i + 2] + 1 << '\n';
    return FileIo::writeTextAtomic(path, stream.str(), &error);
}
bool ExportManager::exportHeightmap(const TerrainChunk& chunk, const std::string& path, std::string& error) const { if (!chunk.heightmap.exportToImage(path)) { error = "Heightmap export failed"; return false; } return true; }
} // namespace wowedit
