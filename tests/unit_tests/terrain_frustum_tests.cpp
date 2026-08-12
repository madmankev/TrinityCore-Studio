#include <cstdlib>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "viewer/Frustum.h"

namespace
{
void Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}
} // namespace

int main()
{
    // The terrain renderer uses this same normalized frustum primitive to compact visible MCNK
    // index ranges before vkCmdDrawIndexedIndirect. Keep the basic front/behind/edge guarantees
    // independently testable without a Vulkan device.
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 200.0f);
    const we::Frustum frustum(proj * view);

    Expect(we::SphereInFrustum(frustum, glm::vec3(0.0f, 0.0f, -25.0f), 8.0f),
           "terrain chunk in front of the camera remains visible");
    Expect(!we::SphereInFrustum(frustum, glm::vec3(0.0f, 0.0f, 25.0f), 8.0f),
           "terrain chunk behind the camera is rejected");
    Expect(!we::SphereInFrustum(frustum, glm::vec3(1000.0f, 0.0f, -25.0f), 8.0f),
           "terrain chunk far outside the horizontal frustum is rejected");
    Expect(we::SphereInFrustum(frustum, glm::vec3(25.0f, 0.0f, -25.0f), 8.0f, 64.0f),
           "generous live-sculpt margin keeps edge chunks conservatively visible");

    std::cout << "Terrain frustum tests passed\n";
    return 0;
}
