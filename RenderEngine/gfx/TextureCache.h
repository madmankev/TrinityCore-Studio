#pragma once

// Decodes BLP textures (via ClientData + BlpDecoder) and uploads them through the
// renderer (IRenderer::CreateTexture), caching by client path. Returns a TextureId
// usable as an ImGui image. Runs on the render thread; the renderer must be set first.

#include <string>
#include <unordered_map>

#include "gfx/IRenderer.h"   // TextureId

namespace we
{
class ClientData;

class TextureCache
{
public:
    ~TextureCache();

    // Set the renderer used to upload/free textures. Must be called before GetOrLoad.
    void SetRenderer(IRenderer* r) { renderer = r; }

    // Decode+upload the BLP at `blpPath` (e.g. "Interface\\Icons\\INV_...blp"),
    // caching the result. Returns 0 on failure (also cached to avoid retrying).
    // If outW/outH are given, they receive the decoded (native, power-of-two)
    // dimensions — needed to draw padded textures (e.g. map overlays) at true scale.
    TextureId GetOrLoad(ClientData& cd, const std::string& blpPath, int* outW = nullptr,
                        int* outH = nullptr);

    void Clear();

private:
    IRenderer* renderer = nullptr;
    struct Entry
    {
        TextureId tex = 0;
        int w = 0, h = 0;
    };
    std::unordered_map<std::string, Entry> cache;
};
} // namespace we
