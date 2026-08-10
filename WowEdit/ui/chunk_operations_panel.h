#pragma once
#include "editing/world_chunk_clipboard.h"
namespace wowedit
{
class ChunkOperationsPanel
{
public:
    explicit ChunkOperationsPanel(WorldChunkClipboard& clipboard) : clipboard_(clipboard) {}
    WorldChunkClipboard& clipboard() { return clipboard_; }
    void mirrorHorizontal() { clipboard_.setMirrorHorizontal(true); }
    void mirrorVertical() { clipboard_.setMirrorVertical(true); }
    void rotateClockwise() { clipboard_.rotateClockwise(); }
private: WorldChunkClipboard& clipboard_;
};
} // namespace wowedit
