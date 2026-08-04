// DbEditWidgets — see DbEditWidgets.h.

#include "editors/common/DbEditWidgets.h"

#include <string>

#include "imgui.h"

#include "editors/common/DbDocument.h"
#include "schema/Types.h"
#include "ui/Widgets.h"

namespace we
{
namespace
{
std::string FieldId(const char* col) { return std::string("##") + col; }
} // namespace

bool DbU32Field(const char* label, DbRecord& rec, const char* col, const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = rec.GetU32(col);
    if (InputU32(FieldId(col).c_str(), v))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}

bool DbI32Field(const char* label, DbRecord& rec, const char* col, const char* tip)
{
    FieldRow(label, tip);
    int32_t v = rec.GetI32(col);
    if (InputI32(FieldId(col).c_str(), v))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}

bool DbFloatField(const char* label, DbRecord& rec, const char* col, const char* tip)
{
    FieldRow(label, tip);
    float v = rec.GetF32(col);
    if (InputFloatField(FieldId(col).c_str(), v))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}

bool DbTextField(const char* label, DbRecord& rec, const char* col, const char* tip)
{
    FieldRow(label, tip);
    std::string s = rec.Get(col);
    if (InputTextString(FieldId(col).c_str(), s))
    {
        rec.Set(col, std::move(s));
        return true;
    }
    return false;
}

bool DbMultilineField(const char* label, DbRecord& rec, const char* col, float height)
{
    FieldRow(label);
    std::string s = rec.Get(col);
    if (InputMultiline(FieldId(col).c_str(), s, height))
    {
        rec.Set(col, std::move(s));
        return true;
    }
    return false;
}

bool DbIdNameField(const char* label, DbRecord& rec, const char* col, LookupCache& cache,
                   RefKind kind, const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = rec.GetU32(col);
    if (IdNamePicker(FieldId(col).c_str(), v, cache, kind))
    {
        rec.Set(col, std::to_string(v));
        return true;
    }
    return false;
}

bool DrawLocaleEditor(DbRecord& rec, const std::vector<const char*>& localizedCols)
{
    if (localizedCols.empty() || !ImGui::TreeNode("Translations"))
        return false;
    bool changed = false;
    const bool multi = localizedCols.size() > 1;
    if (BeginFieldTable("##dbloc", 90.0f))
    {
        for (int i = 0; i < kLocaleCount; ++i)
        {
            const char* code = kLocales[i];
            for (const char* col : localizedCols)
            {
                std::string label = multi ? std::string(code) + " " + col : code;
                FieldRow(label.c_str());

                std::string cur;
                for (const DbLocaleRow& l : rec.locales)
                    if (l.locale == code)
                    {
                        auto it = l.cells.find(col);
                        if (it != l.cells.end())
                            cur = it->second;
                        break;
                    }
                std::string edited = cur;
                if (InputTextString((std::string("##") + code + col).c_str(), edited))
                {
                    bool found = false;
                    for (DbLocaleRow& l : rec.locales)
                        if (l.locale == code)
                        {
                            l.cells[col] = std::move(edited);
                            found = true;
                            break;
                        }
                    if (!found)
                    {
                        DbLocaleRow nl;
                        nl.locale = code;
                        nl.cells[col] = std::move(edited);
                        rec.locales.push_back(std::move(nl));
                    }
                    rec.dirty = true;
                    changed = true;
                }
            }
        }
        EndFieldTable();
    }
    ImGui::TreePop();
    return changed;
}
} // namespace we
