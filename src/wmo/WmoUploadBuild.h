#pragma once

// Build a renderer-ready ModelUpload from a parsed WmoModel: copies the merged geometry
// into the GPU vertex layout (per-vertex color carries baked MOCV lighting, bone weights
// zeroed so it renders static through the shared mesh pipeline) and decodes each texture
// slot's BLP via ClientData. Mirrors m2::BuildUpload.

#include "gfx/IRenderer.h"   // ModelUpload
#include "wmo/WmoTypes.h"

namespace we
{
class ClientData;

namespace wmo
{
ModelUpload BuildUpload(ClientData& cd, const WmoModel& model);
} // namespace wmo
} // namespace we
