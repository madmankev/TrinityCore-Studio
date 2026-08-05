// Visual theme implementation. See Theme.h.

#include "ui/Theme.h"

#include "imgui.h"

#include "clientdata/ClientData.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace we
{
namespace
{
// Try a list of Windows system fonts; return the first that loads.
ImFont* TryLoadFirst(ImGuiIO& io, const char* const* paths, int count, float px,
                     const ImFontConfig* cfg)
{
    for (int i = 0; i < count; ++i)
    {
        if (std::FILE* f = std::fopen(paths[i], "rb"))
        {
            std::fclose(f);
            // ImGui 1.92 loads glyphs dynamically (incl. Cyrillic for ruRU) — no
            // glyph-range table needed.
            if (ImFont* font = io.Fonts->AddFontFromFileTTF(paths[i], px, cfg))
                return font;
        }
    }
    return nullptr;
}
} // namespace

ImFont* LoadFonts(float dpiScale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const float px = 17.0f * dpiScale;

    ImFontConfig cfg;
    cfg.OversampleH = 3;
    cfg.OversampleV = 2;
    cfg.PixelSnapH = false;

    static const char* kBody[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\SegoeUI.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
    };
    ImFont* body = TryLoadFirst(io, kBody, IM_ARRAYSIZE(kBody), px, &cfg);

    if (!body)
    {
        // Fall back to the built-in font, scaled up so it isn't microscopic.
        ImFontConfig def;
        def.SizePixels = 13.0f * dpiScale;
        body = io.Fonts->AddFontDefault(&def);
    }
    return body;
}

ImFont* AddClientFont(ClientData& cd, float dpiScale)
{
    std::vector<uint8_t> ttf = cd.ReadFile("Fonts\\FRIZQT__.TTF");
    if (ttf.empty())
        return nullptr;
    ImGuiIO& io = ImGui::GetIO();
    // ImGui takes ownership and frees with IM_FREE, so allocate with ImGui::MemAlloc.
    void* buf = ImGui::MemAlloc(ttf.size());
    if (!buf)
        return nullptr;
    std::memcpy(buf, ttf.data(), ttf.size());
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = true;
    return io.Fonts->AddFontFromMemoryTTF(buf, static_cast<int>(ttf.size()), 18.0f * dpiScale, &cfg);
}

void ApplyTheme(float dpiScale, ThemeKind kind)
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    const bool bliz = (kind == ThemeKind::Blizzard);

    // --- metrics ---
    s.WindowPadding     = ImVec2(12, 12);
    s.FramePadding      = ImVec2(9, 5);
    s.CellPadding       = ImVec2(8, 5);
    s.ItemSpacing       = ImVec2(9, 7);
    s.ItemInnerSpacing  = ImVec2(8, 6);
    s.IndentSpacing     = 20.0f;
    s.ScrollbarSize     = 13.0f;
    s.GrabMinSize       = 11.0f;

    s.WindowBorderSize  = 1.0f;
    s.ChildBorderSize   = 1.0f;
    s.PopupBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.TabBorderSize     = 0.0f;

    s.WindowRounding    = 7.0f;
    s.ChildRounding     = 7.0f;
    s.FrameRounding     = 6.0f;
    s.PopupRounding     = 6.0f;
    s.ScrollbarRounding = 9.0f;
    s.GrabRounding      = 6.0f;
    s.TabRounding       = 6.0f;

    s.WindowTitleAlign  = ImVec2(0.02f, 0.5f);
    s.WindowMenuButtonPosition = ImGuiDir_None;
    s.SeparatorTextBorderSize  = 2.0f;
    s.SeparatorTextPadding     = ImVec2(20, 6);

    // --- palette ---------------------------------------------------------
    // Blizzard: warm charcoal + gold, with a translucent window bg so the parchment
    // backdrop shows through. Dark: flat neutral charcoal (opaque), same gold accent.
    // Blizzard: docked panels are transparent so the (darkened) parchment backdrop
    // drawn by the App shows through as the panel surface. Modals/popups use the
    // opaque PopupBg, so they stay solid. Dark: fully opaque flat panels.
    const float wbgA = bliz ? 0.00f : 1.00f;   // window bg alpha
    const float mbgA = bliz ? 0.45f : 1.00f;   // menu bar bg alpha

    const ImVec4 bg        = bliz ? ImVec4(0.118f, 0.106f, 0.088f, wbgA)
                                  : ImVec4(0.107f, 0.109f, 0.122f, wbgA);
    const ImVec4 bgDark    = bliz ? ImVec4(0.086f, 0.077f, 0.063f, 1.00f)
                                  : ImVec4(0.074f, 0.076f, 0.086f, 1.00f);
    const ImVec4 child     = bliz ? ImVec4(0.146f, 0.131f, 0.108f, 1.00f)
                                  : ImVec4(0.135f, 0.138f, 0.152f, 1.00f);
    const ImVec4 frame     = bliz ? ImVec4(0.183f, 0.164f, 0.132f, 1.00f)
                                  : ImVec4(0.170f, 0.174f, 0.192f, 1.00f);
    const ImVec4 frameHov  = bliz ? ImVec4(0.230f, 0.203f, 0.156f, 1.00f)
                                  : ImVec4(0.215f, 0.220f, 0.242f, 1.00f);
    const ImVec4 frameAct  = bliz ? ImVec4(0.270f, 0.234f, 0.172f, 1.00f)
                                  : ImVec4(0.255f, 0.262f, 0.288f, 1.00f);
    const ImVec4 text      = bliz ? ImVec4(0.930f, 0.908f, 0.860f, 1.00f)
                                  : ImVec4(0.900f, 0.905f, 0.915f, 1.00f);
    const ImVec4 textDim   = bliz ? ImVec4(0.575f, 0.540f, 0.470f, 1.00f)
                                  : ImVec4(0.540f, 0.548f, 0.570f, 1.00f);
    const ImVec4 border    = ImVec4(0.000f, 0.000f, 0.000f, 0.45f);
    const ImVec4 accent    = ImVec4(0.855f, 0.647f, 0.216f, 1.00f); // WoW gold (both)
    const ImVec4 accentHov = ImVec4(0.945f, 0.745f, 0.315f, 1.00f);
    const ImVec4 accentDim = ImVec4(0.855f, 0.647f, 0.216f, 0.38f);
    const ImVec4 accentMut = ImVec4(0.430f, 0.335f, 0.150f, 1.00f); // bronze

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = text;
    c[ImGuiCol_TextDisabled]         = textDim;
    c[ImGuiCol_WindowBg]             = bg;
    c[ImGuiCol_ChildBg]              = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg]              = ImVec4(0.100f, 0.090f, 0.074f, 0.98f);
    c[ImGuiCol_Border]               = border;
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = frame;
    c[ImGuiCol_FrameBgHovered]       = frameHov;
    c[ImGuiCol_FrameBgActive]        = frameAct;
    c[ImGuiCol_TitleBg]              = bgDark;
    c[ImGuiCol_TitleBgActive]        = ImVec4(0.170f, 0.140f, 0.088f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]     = bgDark;
    c[ImGuiCol_MenuBarBg]            = bliz ? ImVec4(0.098f, 0.088f, 0.072f, mbgA)
                                            : ImVec4(0.086f, 0.088f, 0.098f, mbgA);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.26f, 0.27f, 0.31f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.34f, 0.39f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.40f, 0.41f, 0.47f, 1.00f);
    c[ImGuiCol_CheckMark]            = accent;
    c[ImGuiCol_SliderGrab]           = accent;
    c[ImGuiCol_SliderGrabActive]     = accentHov;
    c[ImGuiCol_Button]               = frame;
    c[ImGuiCol_ButtonHovered]        = accentMut;
    c[ImGuiCol_ButtonActive]         = accent;
    c[ImGuiCol_Header]               = ImVec4(0.185f, 0.200f, 0.245f, 1.00f);
    c[ImGuiCol_HeaderHovered]        = ImVec4(0.225f, 0.245f, 0.300f, 1.00f);
    c[ImGuiCol_HeaderActive]         = accentMut;
    c[ImGuiCol_Separator]            = ImVec4(0.20f, 0.21f, 0.25f, 1.00f);
    c[ImGuiCol_SeparatorHovered]     = accentMut;
    c[ImGuiCol_SeparatorActive]      = accent;
    c[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.27f, 0.31f, 0.60f);
    c[ImGuiCol_ResizeGripHovered]    = accentMut;
    c[ImGuiCol_ResizeGripActive]     = accent;
    c[ImGuiCol_Tab]                  = ImVec4(0.120f, 0.128f, 0.155f, 1.00f);
    c[ImGuiCol_TabHovered]           = accentMut;
    c[ImGuiCol_TabSelected]          = ImVec4(0.290f, 0.225f, 0.110f, 1.00f);
    c[ImGuiCol_TabDimmed]            = bgDark;
    c[ImGuiCol_TabDimmedSelected]    = ImVec4(0.205f, 0.175f, 0.115f, 1.00f);
    c[ImGuiCol_DockingPreview]       = accentDim;
    c[ImGuiCol_DockingEmptyBg]       = bliz ? ImVec4(0, 0, 0, 0) // show parchment backdrop
                                            : ImVec4(0.070f, 0.073f, 0.088f, 1.00f);
    c[ImGuiCol_TextSelectedBg]       = accentDim;
    c[ImGuiCol_NavCursor]            = accent;
    c[ImGuiCol_TableHeaderBg]        = ImVec4(0.170f, 0.150f, 0.118f, 1.00f);
    c[ImGuiCol_TableBorderStrong]    = ImVec4(0.27f, 0.24f, 0.19f, 1.00f);
    c[ImGuiCol_TableBorderLight]     = ImVec4(0.21f, 0.19f, 0.15f, 1.00f);
    c[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]        = ImVec4(1, 1, 1, 0.020f);

    // Compensate: some newer ImGui builds have these cols; guard by index count is
    // unnecessary since we compiled against this exact ImGui.

    s.ScaleAllSizes(dpiScale);
}
} // namespace we
