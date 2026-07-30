#pragma once

// Visual theme: font loading (with DPI scaling) and a modern dark ImGui style.
// Applied once during graphics init. See App::InitGraphics.

struct ImFont;

namespace qe
{
class ClientData;

// Loads the UI font (Segoe UI if available, else the ImGui default) sized for the
// given DPI scale, and returns the primary font (may be null -> default font).
ImFont* LoadFonts(float dpiScale);

// Adds the client's UI font (Fonts\FRIZQT__.TTF) from open client data at runtime,
// returning it (or null if unavailable). ImGui 1.92's dynamic atlas picks it up
// without a manual rebuild.
ImFont* AddClientFont(ClientData& cd, float dpiScale);

// Visual themes. Blizzard reuses the warm Warcraft palette with a semi-transparent
// window background so a parchment backdrop shows through (drawn by the App, only
// when client data is loaded). Dark is a flat, neutral style that needs no MPQs.
enum class ThemeKind
{
    Dark,
    Blizzard
};

// Applies the given theme (colors, rounding, spacing) scaled by dpiScale.
void ApplyTheme(float dpiScale, ThemeKind kind);
} // namespace qe
