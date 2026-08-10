#pragma once
#include "core/types.h"
#include "ui/ui_types.h"

#include <string>
#include <vector>
namespace wowedit
{
struct ViewportVisibility
{
    bool grid = true, axes = true, skybox = true, fog = true, water = true, doodads = true, creatures = true;
    bool waypoints = true, spawnRadii = true, zoneBoundaries = false, chunkBorders = false, contours = false, normals = false, statistics = true;
};
struct ViewportBookmark
{
    std::string name;
    Camera camera;
    ObjectId selectedObject = 0;
};

class ViewportPanel
{
public:
    Camera& camera() { return camera_; } const Camera& camera() const { return camera_; }
    ViewportVisibility& visibility() { return visibility_; } const ViewportVisibility& visibility() const { return visibility_; }
    void setDisplayMode(ViewportDisplayMode value) { displayMode_ = value; }
    ViewportDisplayMode displayMode() const { return displayMode_; }
    void focus(const AABB& bounds);
    void resetCamera();
    bool saveBookmark(std::string name, ObjectId selectedObject = 0);
    bool recallBookmark(const std::string& name, ObjectId* selectedObject = nullptr);
    const std::vector<ViewportBookmark>& bookmarks() const { return bookmarks_; }
private:
    Camera camera_; ViewportVisibility visibility_; ViewportDisplayMode displayMode_ = ViewportDisplayMode::Textured;
    std::vector<ViewportBookmark> bookmarks_;
};
} // namespace wowedit
