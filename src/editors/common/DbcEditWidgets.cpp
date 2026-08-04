// DbcEditWidgets — see DbcEditWidgets.h.

#include "editors/common/DbcEditWidgets.h"

#include <string>

#include "imgui.h"

#include "clientdata/DbcSchema.h"   // kDbcLocaleCount, DbcLocaleName
#include "editors/common/DbcDocument.h"
#include "ui/Widgets.h"

namespace we
{
bool DbcU32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col, const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = doc.GetU32(row, col);
    std::string id = std::string("##") + label;
    if (InputU32(id.c_str(), v))
    {
        doc.SetU32(row, col, v);
        return true;
    }
    return false;
}

bool DbcI32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col, const char* tip)
{
    FieldRow(label, tip);
    int32_t v = doc.GetI32(row, col);
    std::string id = std::string("##") + label;
    if (InputI32(id.c_str(), v))
    {
        doc.SetI32(row, col, v);
        return true;
    }
    return false;
}

bool DbcF32Field(const char* label, DbcDocument& doc, uint32_t row, uint32_t col, const char* tip)
{
    FieldRow(label, tip);
    float v = doc.GetF32(row, col);
    std::string id = std::string("##") + label;
    if (InputFloatField(id.c_str(), v))
    {
        doc.SetF32(row, col, v);
        return true;
    }
    return false;
}

bool DbcIdNameField(const char* label, DbcDocument& doc, uint32_t row, uint32_t col,
                    LookupCache& cache, RefKind kind, const char* tip)
{
    FieldRow(label, tip);
    uint32_t v = doc.GetU32(row, col);
    std::string id = std::string("##") + label;
    if (IdNamePicker(id.c_str(), v, cache, kind))
    {
        doc.SetU32(row, col, v);
        return true;
    }
    return false;
}

bool DbcStrField(const char* label, DbcDocument& doc, uint32_t row, uint32_t col, const char* tip)
{
    FieldRow(label, tip);
    std::string s = doc.GetStr(row, col);
    std::string id = std::string("##") + label;
    if (InputTextString(id.c_str(), s))
    {
        doc.SetStr(row, col, std::move(s));
        return true;
    }
    return false;
}

void DbcLangEditor(const char* label, DbcDocument& doc, uint32_t row, uint32_t loc0Col)
{
    ImGui::SeparatorText(label);
    std::string tableId = std::string("##lang") + label;
    if (BeginFieldTable(tableId.c_str(), 90.0f))
    {
        for (uint32_t i = 0; i < kDbcLocaleCount; ++i)
        {
            FieldRow(DbcLocaleName(i));
            std::string s = doc.GetStr(row, loc0Col + i);
            std::string id = std::string("##") + label + std::to_string(i);
            if (InputTextString(id.c_str(), s))
                doc.SetStr(row, loc0Col + i, std::move(s));
        }
        EndFieldTable();
    }
}
} // namespace we
