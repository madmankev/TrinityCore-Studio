// TextureCache — see TextureCache.h.

#include "gfx/TextureCache.h"

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"
#include "gfx/IRenderer.h"

namespace we
{
TextureCache::~TextureCache()
{
    Clear();
}

void TextureCache::Clear()
{
    if (renderer)
    {
        // A cached texture may still be referenced by an in-flight frame (Clear runs on
        // connect/disconnect/theme-rebuild during the loop) — drain the GPU first.
        renderer->WaitIdle();
        for (auto& kv : cache)
            if (kv.second.tex)
                renderer->DestroyTexture(kv.second.tex);
    }
    cache.clear();
}

ImTextureID TextureCache::GetOrLoad(ClientData& cd, const std::string& blpPath, int* outW, int* outH)
{
    auto setOut = [&](const Entry& e) {
        if (outW) *outW = e.w;
        if (outH) *outH = e.h;
        return e.tex;
    };

    auto it = cache.find(blpPath);
    if (it != cache.end())
        return setOut(it->second);

    Entry entry;
    BlpImage img = DecodeBlp(cd.ReadFile(blpPath));
    if (img.valid() && renderer)
    {
        entry.tex = renderer->CreateTexture(img.rgba.data(), img.width, img.height);
        entry.w = img.width;
        entry.h = img.height;
    }
    cache[blpPath] = entry;
    return setOut(entry);
}
} // namespace we
