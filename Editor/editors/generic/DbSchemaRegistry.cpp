// DbSchemaRegistry — see DbSchemaRegistry.h. Built from the curated schemas exposed by the world-DB
// grouped editors (WorldDbTableDefs / WorldDbCompositeTableDefs). As more single-table editors expose
// their schemas, add them here so the generic editor renders them with full fidelity.

#include "editors/generic/DbSchemaRegistry.h"

#include <unordered_map>

#include "editors/common/CompositeDbSchema.h"
#include "editors/common/DbTableSchema.h"
#include "editors/worlddb/WorldDbCompositeModule.h"
#include "editors/worlddb/WorldDbTablesModule.h"

namespace we
{
namespace
{
const std::unordered_map<std::string, const DbTableSchema*>& SingleMap()
{
    static const std::unordered_map<std::string, const DbTableSchema*> m = [] {
        std::unordered_map<std::string, const DbTableSchema*> r;
        for (const DbTableSchema* s : WorldDbTableDefs())
            r[s->table] = s;
        return r;
    }();
    return m;
}
const std::unordered_map<std::string, const CompositeDbTableSchema*>& CompositeMap()
{
    static const std::unordered_map<std::string, const CompositeDbTableSchema*> m = [] {
        std::unordered_map<std::string, const CompositeDbTableSchema*> r;
        for (const CompositeDbTableSchema* s : WorldDbCompositeTableDefs())
            r[s->table] = s;
        return r;
    }();
    return m;
}
} // namespace

CuratedDbSchema LookupDbSchema(const std::string& table)
{
    CuratedDbSchema out;
    if (auto it = SingleMap().find(table); it != SingleMap().end())
        out.single = it->second;
    else if (auto it2 = CompositeMap().find(table); it2 != CompositeMap().end())
        out.composite = it2->second;
    return out;
}
} // namespace we
