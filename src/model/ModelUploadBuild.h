#pragma once

// Build a renderer-ready ModelUpload from a parsed M2Model: copies geometry, converts
// vertices to the GPU layout, and decodes each texture slot's BLP via ClientData.
// Shared by the --m2-shot harness and the Model Viewer module.

#include "gfx/IRenderer.h"   // ModelUpload
#include "model/M2Types.h"

namespace we
{
class ClientData;

namespace m2
{
ModelUpload BuildUpload(ClientData& cd, const M2Model& model);
} // namespace m2
} // namespace we
