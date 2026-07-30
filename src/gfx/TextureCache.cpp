// TextureCache — see TextureCache.h.

#include "gfx/TextureCache.h"

#include <windows.h>
#include <GL/gl.h>

#include "clientdata/BlpDecoder.h"
#include "clientdata/ClientData.h"

namespace qe
{
TextureCache::~TextureCache()
{
    Clear();
}

void TextureCache::Clear()
{
    for (auto& kv : cache)
    {
        if (kv.second.tex)
        {
            GLuint tex = static_cast<GLuint>(kv.second.tex);
            glDeleteTextures(1, &tex);
        }
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
    if (img.valid())
    {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, img.rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        entry.tex = static_cast<ImTextureID>(static_cast<uintptr_t>(tex));
        entry.w = img.width;
        entry.h = img.height;
    }
    cache[blpPath] = entry;
    return setOut(entry);
}
} // namespace qe
