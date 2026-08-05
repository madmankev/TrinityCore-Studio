// Layer E (ui) — POI tab. Edits the legacy single-POI fields on quest_template
// plus the modern quest_poi / quest_poi_points polygon set owned by the Quest.

#include "editors/quest/Tabs.h"
#include "editors/quest/QuestEditorContext.h"
#include "ui/Widgets.h"

#include "schema/Quest.h"
#include "schema/QuestPoi.h"

#include "imgui.h"

#include "gfx/ClientAssets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <vector>

namespace we
{
namespace
{
// WoW zone-map geometry: 12 tiles are stored as a 4x3 grid of 256x256 BLPs
// (1024x768), but the actual map art only fills the top-left 1002x668. The rest is
// black padding — we must crop it out, or it renders as dark "fog" bands and the
// whole map gets squished into the wrong aspect ratio.
constexpr float kTile = 256.0f;
constexpr float kContentW = 1002.0f;
constexpr float kContentH = 668.0f;

// Persistent per-canvas view (zoom + normalized pan). Keyed by the POI's address;
// resets harmlessly if the pois vector reallocates.
struct CanvasView
{
    float zoom = 1.0f;
    ImVec2 pan{0.0f, 0.0f};
};

// Interactive canvas for one POI's polygon points over its zone map (or a schematic
// grid when no client data / WorldMapArea is available). Left-drag moves a point,
// double-click adds one; mouse-wheel zooms toward the cursor and right-drag pans.
// Returns true if any point was moved/added this frame.
bool DrawPoiCanvas(QuestPoi& poi, uint32_t questId)
{
    static const void* dragOwner = nullptr;  // point being left-dragged
    static int dragPoint = -1;
    static const void* panOwner = nullptr;    // canvas being right-drag panned
    static std::unordered_map<const void*, CanvasView> views;

    CanvasView& view = views[&poi];
    bool changed = false;

    // Decide up front whether we have a real map so the canvas can take the map's
    // aspect ratio (1002:668) instead of a distorting square.
    ClientAssets::MapArea ma;
    bool haveMap = false;
    if (ClientAssets* a = Assets())
        if (a->Ready() && poi.worldMapAreaId != 0 && a->GetMapArea(poi.worldMapAreaId, ma) &&
            std::fabs(ma.left - ma.right) > 1.0f && std::fabs(ma.top - ma.bottom) > 1.0f)
            haveMap = true;

    const float aspect = haveMap ? (kContentH / kContentW) : (2.0f / 3.0f);
    float canvasW = ImGui::GetContentRegionAvail().x;
    canvasW = std::min(canvasW, ImGui::GetFontSize() * 46.0f); // cap so it isn't huge
    canvasW = std::max(canvasW, 200.0f);
    const ImVec2 size(canvasW, canvasW * aspect);

    // Toolbar: zoom readout + reset.
    ImGui::Text("Zoom %.0f%%", view.zoom * 100.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset view"))
    {
        view.zoom = 1.0f;
        view.pan = ImVec2(0.0f, 0.0f);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(ctrl+wheel = zoom, right-drag = pan, ctrl+left-click = add, ctrl+right-click point = delete)");

    // Child clips drawing to the canvas. Plain wheel forwards to the tab (scroll);
    // ctrl+wheel is claimed below for zoom.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##poicanvaschild", size, false, ImGuiWindowFlags_NoScrollbar);

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##poicanvas", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    dl->AddRectFilled(p0, p1, IM_COL32(20, 18, 15, 255));

    // --- view transform: content-uv (0..1 over the map/world rect) -> screen -------
    auto uvToScreen = [&](float u, float v) -> ImVec2 {
        return ImVec2(p0.x + size.x * (view.pan.x + view.zoom * u),
                      p0.y + size.y * (view.pan.y + view.zoom * v));
    };
    auto screenToUv = [&](ImVec2 s) -> ImVec2 {
        return ImVec2(((s.x - p0.x) / size.x - view.pan.x) / view.zoom,
                      ((s.y - p0.y) / size.y - view.pan.y) / view.zoom);
    };

    // --- zoom (ctrl+wheel toward cursor) + pan (right-drag), with clamping ---------
    // Ctrl-gated so a plain wheel still scrolls the tab; claim the wheel while zooming
    // so the tab doesn't scroll at the same time.
    if (hovered && ImGui::GetIO().KeyCtrl)
    {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            ImVec2 before = screenToUv(mouse);
            view.zoom = std::clamp(view.zoom * (1.0f + wheel * 0.15f), 1.0f, 16.0f);
            // Keep the content point under the cursor fixed.
            view.pan.x = (mouse.x - p0.x) / size.x - view.zoom * before.x;
            view.pan.y = (mouse.y - p0.y) / size.y - view.zoom * before.y;
        }
    }
    // A right-drag pans; a right-click that doesn't move deletes the point under the
    // cursor (hit-test deferred until toScreen exists below). Track whether the press
    // turned into a drag so a stationary click isn't swallowed by the pan.
    static ImVec2 rightPressPos{0.0f, 0.0f};
    static bool rightMoved = false;
    static bool rightPressCtrl = false;
    bool ctrlRightClickRelease = false;
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        panOwner = &poi;
        rightPressPos = mouse;
        rightMoved = false;
        rightPressCtrl = ImGui::GetIO().KeyCtrl;
    }
    if (panOwner == &poi && ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        view.pan.x += d.x / size.x;
        view.pan.y += d.y / size.y;
        float mdx = mouse.x - rightPressPos.x, mdy = mouse.y - rightPressPos.y;
        if (mdx * mdx + mdy * mdy > 16.0f) // moved > ~4px => it's a pan, not a click
            rightMoved = true;
    }
    if (panOwner == &poi && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        ctrlRightClickRelease = !rightMoved && rightPressCtrl;
        panOwner = nullptr;
    }
    // Clamp so the content always covers the canvas (no black borders creeping in).
    view.pan.x = std::clamp(view.pan.x, 1.0f - view.zoom, 0.0f);
    view.pan.y = std::clamp(view.pan.y, 1.0f - view.zoom, 0.0f);

    // --- map background: draw only the 1002x668 content, cropping black padding ----
    if (haveMap)
    {
        if (ClientAssets* a = Assets())
            for (int i = 1; i <= 12; ++i)
            {
                ImTextureID tex = a->MapTile(ma.dir, i);
                if (!tex)
                    continue;
                const int col = (i - 1) % 4, row = (i - 1) / 4;
                const float vx0 = col * kTile, vy0 = row * kTile;
                if (vx0 >= kContentW || vy0 >= kContentH)
                    continue; // fully in the padding region
                const float vx1 = std::min(vx0 + kTile, kContentW);
                const float vy1 = std::min(vy0 + kTile, kContentH);
                ImVec2 s0 = uvToScreen(vx0 / kContentW, vy0 / kContentH);
                ImVec2 s1 = uvToScreen(vx1 / kContentW, vy1 / kContentH);
                ImVec2 tuv0(0.0f, 0.0f);
                ImVec2 tuv1((vx1 - vx0) / kTile, (vy1 - vy0) / kTile); // crop the tile
                dl->AddImage(tex, s0, s1, tuv0, tuv1);
            }

        // Explored-detail overlays layered on top (this is what makes the map look
        // fully explored / "no fog of war"). Each overlay is placed at its pixel
        // offset and split into up to 256x256 sub-tiles; overlays carry their own
        // alpha, so they blend onto the base.
        if (ClientAssets* a = Assets())
            for (const ClientAssets::MapOverlay& ov : a->MapOverlays(poi.worldMapAreaId))
            {
                const int cols = (ov.width + 255) / 256;
                const int rows = (ov.height + 255) / 256;
                int index = 0;
                for (int ry = 0; ry < rows; ++ry)
                    for (int cx = 0; cx < cols; ++cx)
                    {
                        ++index;
                        // Overlay tiles are stored padded to a power-of-two with the real
                        // content in the top-left corner and transparent padding. Draw the
                        // WHOLE decoded texture at its native pixel size (padding is
                        // transparent) so content lands 1:1 — stretching to the DBC block
                        // size instead would shrink everything toward the overlay origin.
                        int tw = 0, th = 0;
                        ImTextureID tex = a->MapOverlayTile(ma.dir, ov.texture, index, &tw, &th);
                        if (!tex || tw <= 0 || th <= 0)
                            continue;
                        const float bx = static_cast<float>(ov.offsetX + cx * 256);
                        const float by = static_cast<float>(ov.offsetY + ry * 256);
                        ImVec2 s0 = uvToScreen(bx / kContentW, by / kContentH);
                        ImVec2 s1 = uvToScreen((bx + static_cast<float>(tw)) / kContentW,
                                               (by + static_cast<float>(th)) / kContentH);
                        dl->AddImage(tex, s0, s1);
                    }
            }
    }

    // Schematic bounds (fit to points) used when there's no map.
    float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
    for (const QuestPoiPoint& pt : poi.points)
    {
        minX = std::min(minX, static_cast<float>(pt.x));
        maxX = std::max(maxX, static_cast<float>(pt.x));
        minY = std::min(minY, static_cast<float>(pt.y));
        maxY = std::max(maxY, static_cast<float>(pt.y));
    }
    if (poi.points.empty() || maxX - minX < 1.0f) { minX -= 500.0f; maxX = minX + 1000.0f; if (poi.points.empty()) { minX = -500; maxX = 500; } }
    if (poi.points.empty() || maxY - minY < 1.0f) { minY -= 500.0f; maxY = minY + 1000.0f; if (poi.points.empty()) { minY = -500; maxY = 500; } }
    const float margin = 0.10f;

    // --- world <-> content-uv (map: WorldMapArea bounds; schematic: fit box) --------
    // WoW convention: horizontal from world Y via Left/Right, vertical from world X
    // via Top/Bottom. pt.x = world X, pt.y = world Y.
    std::function<ImVec2(float, float)> worldToUv;
    std::function<ImVec2(ImVec2)> uvToWorld;
    if (haveMap)
    {
        worldToUv = [&](float px, float py) -> ImVec2 {
            return ImVec2((ma.left - py) / (ma.left - ma.right),
                          (ma.top - px) / (ma.top - ma.bottom));
        };
        uvToWorld = [&](ImVec2 uv) -> ImVec2 {
            return ImVec2(ma.top - uv.y * (ma.top - ma.bottom),
                          ma.left - uv.x * (ma.left - ma.right));
        };
    }
    else
    {
        worldToUv = [&](float wx, float wy) -> ImVec2 {
            float u = margin + (1.0f - 2 * margin) * (wx - minX) / (maxX - minX);
            float v = margin + (1.0f - 2 * margin) * (1.0f - (wy - minY) / (maxY - minY));
            return ImVec2(u, v);
        };
        uvToWorld = [&](ImVec2 uv) -> ImVec2 {
            float uu = (uv.x - margin) / (1.0f - 2 * margin);
            float vv = (uv.y - margin) / (1.0f - 2 * margin);
            return ImVec2(minX + uu * (maxX - minX), minY + (1.0f - vv) * (maxY - minY));
        };
    }
    auto toScreen = [&](float px, float py) -> ImVec2 { ImVec2 uv = worldToUv(px, py); return uvToScreen(uv.x, uv.y); };
    auto toWorld = [&](ImVec2 s) -> ImVec2 { return uvToWorld(screenToUv(s)); };

    // Ctrl+right-click a point (a stationary click, not a pan) to delete it. Mirrors
    // the table's Remove: erase, then re-number the remaining points' idx2.
    if (ctrlRightClickRelease)
    {
        for (int i = 0; i < static_cast<int>(poi.points.size()); ++i)
        {
            ImVec2 sp = toScreen(static_cast<float>(poi.points[i].x), static_cast<float>(poi.points[i].y));
            float dx = sp.x - mouse.x, dy = sp.y - mouse.y;
            if (dx * dx + dy * dy <= 100.0f) // within ~10px, same radius as begin-drag
            {
                poi.points.erase(poi.points.begin() + i);
                for (int p = 0; p < static_cast<int>(poi.points.size()); ++p)
                    poi.points[p].idx2 = static_cast<uint32_t>(p);
                changed = true;
                break;
            }
        }
    }

    if (!haveMap)
    {
        const int kGrid = 8;
        for (int g = 1; g < kGrid; ++g)
        {
            ImVec2 fx = uvToScreen(static_cast<float>(g) / kGrid, 0.0f);
            ImVec2 fy = uvToScreen(0.0f, static_cast<float>(g) / kGrid);
            dl->AddLine(ImVec2(fx.x, p0.y), ImVec2(fx.x, p1.y), IM_COL32(255, 255, 255, 12));
            dl->AddLine(ImVec2(p0.x, fy.y), ImVec2(p1.x, fy.y), IM_COL32(255, 255, 255, 12));
        }
    }

    dl->AddRect(p0, p1, IM_COL32(90, 80, 60, 255));

    // Translucent blue POI region (like the quest map's POI blob). Build the screen
    // positions once, then: a single point -> a filled disc larger than the marker;
    // two-plus points -> a closed polygon (last vertex back to the 0th) filled blue.
    std::vector<ImVec2> sp;
    sp.reserve(poi.points.size());
    for (const QuestPoiPoint& pt : poi.points)
        sp.push_back(toScreen(static_cast<float>(pt.x), static_cast<float>(pt.y)));

    const ImU32 kFill = IM_COL32(40, 120, 235, 128);   // ~50% alpha blue
    const ImU32 kEdge = IM_COL32(90, 170, 255, 235);
    if (sp.size() == 1)
    {
        dl->AddCircleFilled(sp[0], 13.0f, kFill);   // marker dot is r=5, so this is clearly larger
        dl->AddCircle(sp[0], 13.0f, kEdge, 0, 2.0f);
    }
    else if (sp.size() >= 2)
    {
        if (sp.size() >= 3)
            dl->AddConvexPolyFilled(sp.data(), static_cast<int>(sp.size()), kFill);  // needs >=3 verts
        dl->AddPolyline(sp.data(), static_cast<int>(sp.size()), kEdge, 2.0f, ImDrawFlags_Closed);
    }

    // Ctrl+left-click adds a point at the cursor; a plain left-press near a point
    // begins a drag (move).
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (ImGui::GetIO().KeyCtrl)
        {
            ImVec2 w = toWorld(mouse);
            QuestPoiPoint pt;
            pt.questID = questId;
            pt.idx1 = poi.id;
            pt.idx2 = static_cast<uint32_t>(poi.points.size());
            pt.x = static_cast<int>(w.x);
            pt.y = static_cast<int>(w.y);
            poi.points.push_back(pt);
            changed = true;
        }
        else
        {
            for (int i = 0; i < static_cast<int>(poi.points.size()); ++i)
            {
                ImVec2 spm = toScreen(static_cast<float>(poi.points[i].x), static_cast<float>(poi.points[i].y));
                float dx = spm.x - mouse.x, dy = spm.y - mouse.y;
                if (dx * dx + dy * dy <= 100.0f) // within ~10px
                {
                    dragOwner = &poi;
                    dragPoint = i;
                    break;
                }
            }
        }
    }
    if (dragOwner == &poi && dragPoint >= 0 && dragPoint < static_cast<int>(poi.points.size()) &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        ImVec2 w = toWorld(mouse);
        int nx = static_cast<int>(w.x + (w.x < 0 ? -0.5f : 0.5f));
        int ny = static_cast<int>(w.y + (w.y < 0 ? -0.5f : 0.5f));
        if (poi.points[dragPoint].x != nx || poi.points[dragPoint].y != ny)
        {
            poi.points[dragPoint].x = nx;
            poi.points[dragPoint].y = ny;
            changed = true;
        }
    }
    if (dragOwner == &poi && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        dragOwner = nullptr;
        dragPoint = -1;
    }

    // Point markers + index labels.
    for (int i = 0; i < static_cast<int>(poi.points.size()); ++i)
    {
        ImVec2 sp = toScreen(static_cast<float>(poi.points[i].x), static_cast<float>(poi.points[i].y));
        const bool active = (dragOwner == &poi && dragPoint == i);
        dl->AddCircleFilled(sp, active ? 6.0f : 5.0f,
                            active ? IM_COL32(255, 220, 120, 255) : IM_COL32(230, 180, 80, 255));
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", i);
        dl->AddText(ImVec2(sp.x + 6.0f, sp.y - 6.0f), IM_COL32(235, 225, 205, 255), lbl);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (haveMap)
        ImGui::TextDisabled("Zone map '%s'. Left-drag points to move; ctrl+left-click to add; ctrl+right-click a point to delete.", ma.dir.c_str());
    else
        ImGui::TextDisabled("Schematic — set a WorldMapAreaId and load client data for a map. "
                            "Left-drag points to move; ctrl+left-click to add; ctrl+right-click a point to delete.");
    return changed;
}
} // namespace

void DrawPoiTab(QuestEditorContext& ctx)
{
    if (!ctx.quest)
    {
        ImGui::TextDisabled("No quest loaded.");
        return;
    }

    Quest& q = *ctx.quest;

    // --- Legacy POI (quest_template) --------------------------------------
    ImGui::SeparatorText("Legacy POI (quest_template)");

    bool tmplChanged = false;
    if (BeginFieldTable("qe_poi_legacy"))
    {
        FieldRow("POI Continent", "quest_template.POIContinent (map id)");
        tmplChanged |= InputU16("##POIContinent", q.tmpl.poiContinent);

        FieldRow("POI X", "quest_template.POIx");
        tmplChanged |= InputFloatField("##POIx", q.tmpl.poiX);

        FieldRow("POI Y", "quest_template.POIy");
        tmplChanged |= InputFloatField("##POIy", q.tmpl.poiY);

        FieldRow("POI Priority", "quest_template.POIPriority");
        tmplChanged |= InputU32("##POIPriority", q.tmpl.poiPriority);

        EndFieldTable();
    }

    if (tmplChanged)
    {
        ctx.MarkChanged();
        q.tmplDirty = true;
    }

    // --- Points of Interest (quest_poi / quest_poi_points) ----------------
    ImGui::Spacing();
    ImGui::SeparatorText("Points of Interest (quest_poi / quest_poi_points)");

    bool poisChanged = false;

    if (ImGui::Button("Add POI"))
    {
        QuestPoi poi;
        poi.questID = q.tmpl.id;

        // Assign a unique/sequential id (one past the current max).
        uint32_t nextId = 0;
        for (const QuestPoi& existing : q.pois)
            if (existing.id >= nextId)
                nextId = existing.id + 1;
        poi.id = nextId;

        q.pois.push_back(poi);
        poisChanged = true;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("(%d POI%s)", static_cast<int>(q.pois.size()),
                        q.pois.size() == 1 ? "" : "s");

    int removePoi = -1;
    for (int i = 0; i < static_cast<int>(q.pois.size()); ++i)
    {
        ImGui::PushID(i);
        QuestPoi& poi = q.pois[i];

        ImGui::Spacing();
        char poiHeader[64];
        std::snprintf(poiHeader, sizeof(poiHeader), "POI #%d (id %u)", i,
                      static_cast<unsigned>(poi.id));
        ImGui::SeparatorText(poiHeader);
        ImGui::SameLine();
        if (ImGui::Button("Remove POI"))
            removePoi = i;

        ImGui::Indent();

        // Visual canvas first (map background if available; drag/double-click to edit).
        if (DrawPoiCanvas(poi, q.tmpl.id))
            poisChanged = true;
        ImGui::Spacing();

        // Scalar fields for this POI.
        if (BeginFieldTable("qe_poi_scalars"))
        {
            FieldRow("ObjectiveIndex", "quest_poi.ObjectiveIndex");
            poisChanged |= InputI32("##ObjectiveIndex", poi.objectiveIndex);

            FieldRow("MapID", "quest_poi.MapID");
            poisChanged |= InputU32("##MapID", poi.mapID);

            FieldRow("WorldMapAreaId", "quest_poi.WorldMapAreaId");
            poisChanged |= InputU32("##WorldMapAreaId", poi.worldMapAreaId);

            FieldRow("Floor", "quest_poi.Floor");
            poisChanged |= InputU32("##Floor", poi.floor);

            FieldRow("Priority", "quest_poi.Priority");
            poisChanged |= InputU32("##Priority", poi.priority);

            FieldRow("Flags", "quest_poi.Flags");
            poisChanged |= InputU32("##Flags", poi.flags);

            EndFieldTable();
        }

        // Nested polygon points table:
        //   [ # (fixed) | X (stretch) | Y (stretch) | remove (fixed) ]
        ImGui::Spacing();
        ImGui::TextUnformatted("Points (quest_poi_points):");
        if (ImGui::BeginTable("points", 4,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 32.0f);
            ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 96.0f);
            ImGui::TableHeadersRow();

            int removePoint = -1;
            for (int p = 0; p < static_cast<int>(poi.points.size()); ++p)
            {
                ImGui::PushID(p);
                QuestPoiPoint& pt = poi.points[p];

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%d", p);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                poisChanged |= InputI32("##x", pt.x);

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                poisChanged |= InputI32("##y", pt.y);

                ImGui::TableSetColumnIndex(3);
                if (ImGui::Button("Remove"))
                    removePoint = p;

                ImGui::PopID();
            }
            ImGui::EndTable();

            if (removePoint >= 0)
            {
                poi.points.erase(poi.points.begin() + removePoint);
                // Re-number remaining points' idx2 to stay sequential.
                for (int p = 0; p < static_cast<int>(poi.points.size()); ++p)
                    poi.points[p].idx2 = static_cast<uint32_t>(p);
                poisChanged = true;
            }
        }

        if (ImGui::Button("Add Point"))
        {
            QuestPoiPoint pt;
            pt.questID = q.tmpl.id;
            pt.idx1 = poi.id;
            pt.idx2 = static_cast<uint32_t>(poi.points.size());
            poi.points.push_back(pt);
            poisChanged = true;
        }

        ImGui::Unindent();
        ImGui::PopID();
    }

    if (removePoi >= 0)
    {
        q.pois.erase(q.pois.begin() + removePoi);
        poisChanged = true;
    }

    if (poisChanged)
    {
        ctx.MarkChanged();
        q.poisDirty = true;
    }
}
} // namespace we
