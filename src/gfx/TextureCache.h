#pragma once

// Decodes BLP textures (via ClientData + BlpDecoder) and uploads them to OpenGL,
// caching by client path. Returns an ImTextureID usable with ImGui::Image.
// Must be used on the render thread (a GL context must be current).

#include <string>
#include <unordered_map>

#include "imgui.h"

namespace qe
{
class ClientData;

class TextureCache
{
public:
    ~TextureCache();

    // Decode+upload the BLP at `blpPath` (e.g. "Interface\\Icons\\INV_...blp"),
    // caching the result. Returns 0 on failure (also cached to avoid retrying).
    // If outW/outH are given, they receive the decoded (native, power-of-two)
    // dimensions — needed to draw padded textures (e.g. map overlays) at true scale.
    ImTextureID GetOrLoad(ClientData& cd, const std::string& blpPath, int* outW = nullptr,
                          int* outH = nullptr);

    void Clear();

private:
    struct Entry
    {
        ImTextureID tex = 0;
        int w = 0, h = 0;
    };
    std::unordered_map<std::string, Entry> cache;
};
} // namespace qe
