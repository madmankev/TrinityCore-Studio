// Layer E (ui) — GameObject Data tab: the 24 generic Data columns, labeled per the
// current `type` from GameObjectDataFields(). Named fields (with id-name pickers where
// they reference a table) come first; any Data index not named by the type is shown
// below as a raw "Data N" so all 24 stay editable and nothing is hidden.

#include "editors/gameobject/Tabs.h"
#include "editors/gameobject/GameObjectEditorContext.h"
#include "editors/gameobject/GameObjectDataFields.h"
#include "ui/Widgets.h"

#include "imgui.h"

#include "schema/GameObject.h"
#include "ui/Enums.h"
#include "data/LookupCache.h"

#include <cstdio>

namespace we
{
void DrawGameObjectDataTab(GameObjectEditorContext& ctx)
{
    if (!ctx.go)
    {
        ImGui::TextDisabled("No gameobject loaded.");
        return;
    }
    GameObjectTemplate& t = ctx.go->tmpl;
    LookupCache& lk = *ctx.lookups;
    auto md = [&]() { ctx.MarkChanged(); ctx.go->tmplDirty = true; };

    const char* typeName = LabelFor(GameObjectTypeValues(), t.type);
    ImGui::TextDisabled("Fields labeled for type: %s. Change the type on the General tab.",
                        typeName ? typeName : "(unknown)");

    const std::vector<GoDataField>& fields = GameObjectDataFields(t.type);
    bool named[24] = {false};

    if (!fields.empty())
    {
        ImGui::SeparatorText("Type fields");
        if (BeginFieldTable("qe_go_data_named", 240.0f))
        {
            for (const GoDataField& f : fields)
            {
                if (f.index >= 24)
                    continue;
                named[f.index] = true;
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "Data%u — %s", f.index, f.label);
                FieldRow(lbl, nullptr);
                ImGui::PushID(f.index);
                if (f.picker)
                {
                    if (IdNamePicker("##d", t.data[f.index], lk, f.kind)) md();
                }
                else
                {
                    if (InputI32("##d", t.data[f.index])) md();
                }
                ImGui::PopID();
            }
            EndFieldTable();
        }
    }

    ImGui::SeparatorText(fields.empty() ? "Data (this type names no fields)" : "Other Data (raw)");
    if (BeginFieldTable("qe_go_data_raw"))
    {
        for (int i = 0; i < 24; ++i)
        {
            if (named[i])
                continue;
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "Data%d", i);
            FieldRow(lbl, nullptr);
            ImGui::PushID(100 + i);
            if (InputI32("##raw", t.data[i])) md();
            ImGui::PopID();
        }
        EndFieldTable();
    }
}
} // namespace we
