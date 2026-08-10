#include "ui/viewport_panel.h"
namespace wowedit
{
void ViewportPanel::focus(const AABB& bounds) { if (!bounds.valid()) return; const glm::vec3 c = bounds.center(); camera_.position = c + glm::vec3(0.0f, glm::max(4.0f, bounds.extent().y * 3.0f), glm::max(8.0f, bounds.extent().z * 4.0f)); camera_.forward = glm::normalize(c - camera_.position); }
void ViewportPanel::resetCamera() { camera_ = {}; }

bool ViewportPanel::saveBookmark(std::string name, ObjectId selectedObject)
{
    if (name.empty())
        return false;
    for (ViewportBookmark& bookmark : bookmarks_)
        if (bookmark.name == name)
        {
            bookmark.camera = camera_;
            bookmark.selectedObject = selectedObject;
            return true;
        }
    bookmarks_.push_back({std::move(name), camera_, selectedObject});
    return true;
}

bool ViewportPanel::recallBookmark(const std::string& name, ObjectId* selectedObject)
{
    for (const ViewportBookmark& bookmark : bookmarks_)
        if (bookmark.name == name)
        {
            camera_ = bookmark.camera;
            if (selectedObject)
                *selectedObject = bookmark.selectedObject;
            return true;
        }
    return false;
}
} // namespace wowedit
