#pragma once

// Layer E (ui) — in-app log console. Renders the shared Log ring buffer in a
// scrolling child, auto-scrolling to the newest line.

namespace qe
{
class LogPanel
{
public:
    void Draw();
};
} // namespace qe
