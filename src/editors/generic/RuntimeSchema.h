#pragma once

// RuntimeSchema — builds a DbTableSchema (single integer PK) or CompositeDbTableSchema (composite /
// no / non-integer PK) at RUNTIME from live introspection, OWNING all the backing strings (the schema
// structs address columns by const char*). The generic DB editor uses this so any world-DB table is
// editable with no hand-authored schema. A curated schema from the registry is preferred when present;
// this is the fallback for the long tail.

#include <deque>
#include <string>
#include <vector>

#include "data/DbIntrospect.h"
#include "editors/common/CompositeDbSchema.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
// Map a MySQL column type string to a DbColType (best-effort; 64-bit degrades to 32-bit).
DbColType SqlTypeToDbColType(const std::string& sqlType);

class RuntimeTableSchema
{
public:
    // Build from introspected columns. Exactly one integer PK -> single-PK; otherwise composite
    // (keyCols = the PK columns, or ALL columns when the table has no PRIMARY KEY).
    void Build(const std::string& table, const std::vector<IntrospectedColumn>& cols);

    bool ok() const { return ok_; }
    bool composite() const { return composite_; }
    const DbTableSchema& single() const { return single_; }             // valid when !composite()
    const CompositeDbTableSchema& compositeSchema() const { return comp_; }  // valid when composite()

private:
    const char* Own(const std::string& s)
    {
        store_.push_back(s);
        return store_.back().c_str();  // std::deque never relocates existing elements
    }

    std::deque<std::string>  store_;
    DbTableSchema            single_{};
    CompositeDbTableSchema   comp_{};
    bool                     composite_ = false;
    bool                     ok_ = false;
};
} // namespace we
