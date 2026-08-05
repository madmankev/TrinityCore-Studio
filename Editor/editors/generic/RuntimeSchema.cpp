// RuntimeSchema — see RuntimeSchema.h.

#include "editors/generic/RuntimeSchema.h"

#include <algorithm>
#include <cctype>

namespace we
{
namespace
{
std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}
bool Contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

bool IsIntType(DbColType t)
{
    return t == DbColType::U32 || t == DbColType::I32 || t == DbColType::U16 || t == DbColType::U8;
}
} // namespace

DbColType SqlTypeToDbColType(const std::string& sqlType)
{
    const std::string t = Lower(sqlType);
    const bool uns = Contains(t, "unsigned");
    // Text before "int" checks (some contain "int" e.g. "point" — unlikely here, but order carefully).
    if (Contains(t, "tinytext") || Contains(t, "mediumtext") || Contains(t, "longtext") ||
        (Contains(t, "text") && !Contains(t, "tinyint")) || Contains(t, "blob"))
        return DbColType::Multiline;
    if (Contains(t, "float") || Contains(t, "double") || Contains(t, "decimal") || Contains(t, "real"))
        return DbColType::Float;
    if (Contains(t, "tinyint"))
        return uns ? DbColType::U8 : DbColType::I32;
    if (Contains(t, "smallint"))
        return uns ? DbColType::U16 : DbColType::I32;
    if (Contains(t, "int"))  // int / mediumint / bigint (64-bit degrades to 32-bit)
        return uns ? DbColType::U32 : DbColType::I32;
    if (Contains(t, "char") || Contains(t, "varchar") || Contains(t, "binary") || Contains(t, "enum") ||
        Contains(t, "set") || Contains(t, "year") || Contains(t, "date") || Contains(t, "time"))
        return DbColType::Text;
    return DbColType::Text;  // unknown -> safest editable form
}

void RuntimeTableSchema::Build(const std::string& table, const std::vector<IntrospectedColumn>& cols)
{
    store_.clear();
    single_ = DbTableSchema{};
    comp_ = CompositeDbTableSchema{};
    composite_ = false;
    ok_ = false;
    if (cols.empty())
        return;

    // Count PK columns and decide the path.
    std::vector<const IntrospectedColumn*> pks;
    for (const IntrospectedColumn& c : cols)
        if (c.isPk)
            pks.push_back(&c);

    const bool singlePath =
        pks.size() == 1 && IsIntType(SqlTypeToDbColType(pks.front()->sqlType));

    // First text/multiline column -> browser label (a name/comment). Computed over eligible cols below.
    const char* browserCol = nullptr;

    if (singlePath)
    {
        single_.table = Own(table);
        single_.pk = Own(pks.front()->name);
        for (const IntrospectedColumn& c : cols)
        {
            if (c.isPk)
                continue;  // DbTableSchema excludes the pk from cols
            const DbColType type = SqlTypeToDbColType(c.sqlType);
            const char* name = Own(c.name);
            single_.cols.push_back({name, type, name});
            if (!browserCol && (type == DbColType::Text || type == DbColType::Multiline))
                browserCol = name;
        }
        if (browserCol)
            single_.browserCols.push_back(browserCol);
        composite_ = false;
        ok_ = true;
        return;
    }

    // Composite path: keyCols = PK columns, or ALL columns when there is no PRIMARY KEY.
    comp_.table = Own(table);
    for (const IntrospectedColumn& c : cols)
    {
        const DbColType type = SqlTypeToDbColType(c.sqlType);
        const char* name = Own(c.name);
        comp_.cols.push_back({name, type, name});
        if (c.isPk)
            comp_.keyCols.push_back(name);
        if (!browserCol && !c.isPk && (type == DbColType::Text || type == DbColType::Multiline))
            browserCol = name;
    }
    if (comp_.keyCols.empty())  // no PK -> identity is the whole row
        for (const DbColumn& c : comp_.cols)
            comp_.keyCols.push_back(c.name);
    if (browserCol)
        comp_.browserCols.push_back(browserCol);
    composite_ = true;
    ok_ = true;
}
} // namespace we
