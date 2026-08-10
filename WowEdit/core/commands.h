#pragma once

#include "core/command.h"
#include "creatures/creature_spawner.h"
#include "objects/doodad.h"
#include "terrain/heightmap.h"
#include "terrain/texture_splatmap.h"

#include <any>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wowedit
{
class DoodadManager;
class CreatureSpawnerRegistry;

struct TerrainHeightDelta
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    float before = 0.0f;
    float after = 0.0f;
};

class TerrainHeightModifyCommand final : public ICommand
{
public:
    TerrainHeightModifyCommand(Heightmap& heightmap, std::vector<TerrainHeightDelta> changes,
                               std::string description = "Modify terrain height");
    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    Heightmap* heightmap_ = nullptr;
    std::vector<TerrainHeightDelta> changes_;
    std::string description_;
};

struct TexturePaintChange
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    glm::u8vec4 before{0};
    glm::u8vec4 after{0};
};

class TexturePaintCommand final : public ICommand
{
public:
    TexturePaintCommand(TextureSplatmap& splatmap, std::vector<TexturePaintChange> changes,
                        std::string description = "Paint texture");
    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    TextureSplatmap* splatmap_ = nullptr;
    std::vector<TexturePaintChange> changes_;
    std::string description_;
};

struct VertexPaintChange
{
    std::uint32_t vertexIndex = 0;
    TextureSplatmap::VertexColor before{};
    TextureSplatmap::VertexColor after{};
};

class VertexPaintCommand final : public ICommand
{
public:
    VertexPaintCommand(TextureSplatmap& splatmap, std::vector<VertexPaintChange> changes,
                       std::string description = "Paint vertex colors");
    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    void apply(bool useAfter);
    TextureSplatmap* splatmap_ = nullptr;
    std::vector<VertexPaintChange> changes_;
    std::string description_;
};

class DoodadPlaceCommand final : public ICommand
{
public:
    DoodadPlaceCommand(DoodadManager& manager, Doodad doodad);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    DoodadManager* manager_ = nullptr;
    Doodad doodad_;
};

class DoodadDeleteCommand final : public ICommand
{
public:
    DoodadDeleteCommand(DoodadManager& manager, Doodad doodad);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    DoodadManager* manager_ = nullptr;
    Doodad doodad_;
};

class DoodadMoveCommand final : public ICommand
{
public:
    DoodadMoveCommand(DoodadManager& manager, std::uint64_t id, glm::vec3 before, glm::vec3 after);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    DoodadManager* manager_ = nullptr;
    std::uint64_t id_ = 0;
    glm::vec3 before_{0.0f};
    glm::vec3 after_{0.0f};
};

class DoodadTransformCommand final : public ICommand
{
public:
    DoodadTransformCommand(DoodadManager& manager, std::uint64_t id, glm::vec3 beforeRotation, glm::vec3 beforeScale,
                           glm::vec3 afterRotation, glm::vec3 afterScale);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    DoodadManager* manager_ = nullptr;
    std::uint64_t id_ = 0;
    glm::vec3 beforeRotation_{0.0f};
    glm::vec3 beforeScale_{1.0f};
    glm::vec3 afterRotation_{0.0f};
    glm::vec3 afterScale_{1.0f};
};

class CreatureSpawnerPlaceCommand final : public ICommand
{
public:
    CreatureSpawnerPlaceCommand(CreatureSpawnerRegistry& registry, CreatureSpawner spawner);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    CreatureSpawnerRegistry* registry_ = nullptr;
    CreatureSpawner spawner_;
};

class CreatureSpawnerModifyCommand final : public ICommand
{
public:
    CreatureSpawnerModifyCommand(CreatureSpawnerRegistry& registry, CreatureSpawner before, CreatureSpawner after);
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    void apply(const CreatureSpawner& value, const CreatureSpawner& other);
    CreatureSpawnerRegistry* registry_ = nullptr;
    CreatureSpawner before_;
    CreatureSpawner after_;
};

/** Callback-backed commands are used by copy/paste adapters to avoid copying an
 * entire world in history while retaining explicit command classes in the public API. */
class WorldChunkCopyCommand final : public ICommand
{
public:
    WorldChunkCopyCommand(std::function<void()> execute, std::function<void()> undo,
                          std::string description = "Copy world chunk");
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    LambdaCommand command_;
};

class WorldChunkPasteCommand final : public ICommand
{
public:
    WorldChunkPasteCommand(std::function<void()> execute, std::function<void()> undo,
                           std::string description = "Paste world chunk");
    void execute() override;
    void undo() override;
    std::string getDescription() const override;
private:
    LambdaCommand command_;
};

/** Generic property command; Make() captures a strongly typed property and values. */
class PropertyChangeCommand final : public ICommand
{
public:
    using Setter = std::function<void(const std::any&)>;

    PropertyChangeCommand(Setter setter, std::any before, std::any after, std::string description);

    template <typename TValue>
    static std::unique_ptr<PropertyChangeCommand> Make(TValue& property, TValue before, TValue after,
                                                        std::string description = "Change property")
    {
        return std::make_unique<PropertyChangeCommand>(
            [&property](const std::any& value) { property = std::any_cast<TValue>(value); },
            std::any(std::move(before)), std::any(std::move(after)), std::move(description));
    }

    void execute() override;
    void undo() override;
    std::string getDescription() const override;

private:
    Setter setter_;
    std::any before_;
    std::any after_;
    std::string description_;
};
} // namespace wowedit
