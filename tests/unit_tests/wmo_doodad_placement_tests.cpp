#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include <glm/gtc/matrix_transform.hpp>

#include "wmo/WmoDoodadPlacement.h"

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

bool Near(float a, float b, float epsilon = 0.0001f)
{
    return std::fabs(a - b) <= epsilon;
}
} // namespace

int main()
{
    using namespace we::wmo;

    const uint64_t first = MakeEmbeddedDoodadUid(0x12345678u, 0u);
    const uint64_t next = MakeEmbeddedDoodadUid(0x12345678u, 1u);
    const uint64_t otherParent = MakeEmbeddedDoodadUid(0x12345679u, 0u);
    Expect(IsEmbeddedDoodadUid(first), "embedded child IDs carry the private namespace tag");
    Expect(first != next, "two MODD entries under one WMO receive distinct IDs");
    Expect(first != otherParent, "equal MODD indices from different WMO roots do not collide");
    Expect((first & kEmbeddedDoodadIndexMask) == 0u, "child index occupies the low UID bits");
    Expect((next & kEmbeddedDoodadIndexMask) == 1u, "child index is stable and chronological");

    glm::mat4 parent(1.0f);
    parent = glm::translate(parent, glm::vec3(10.0f, 20.0f, 30.0f));
    parent = glm::rotate(parent, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 local = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 4.0f));
    const glm::vec3 placed = glm::vec3(ComposeDoodadTransform(parent, local) * glm::vec4(0, 0, 0, 1));
    Expect(Near(placed.x, 10.0f) && Near(placed.y, 22.0f) && Near(placed.z, 34.0f),
           "WMO doodad local transforms compose beneath the ADT WMO root transform");

    const uint8_t white[4] = {255, 255, 255, 255};
    const uint8_t tintBytes[4] = {128, 64, 255, 200};
    const glm::vec4 whiteTint = DoodadTint(white);
    const glm::vec4 tint = DoodadTint(tintBytes);
    Expect(!HasVisibleDoodadTint(whiteTint), "neutral MODD color remains instancable");
    Expect(HasVisibleDoodadTint(tint), "non-neutral MODD color requests per-instance material tint");
    Expect(Near(tint.r, 128.0f / 255.0f) && Near(tint.g, 64.0f / 255.0f) &&
           Near(tint.b, 1.0f) && Near(tint.a, 200.0f / 255.0f),
           "MODD RGBA tint is normalized without swapping channels");

    std::cout << "WMO doodad placement tests passed\n";
    return 0;
}
