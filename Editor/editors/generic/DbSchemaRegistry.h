#pragma once

// DbSchemaRegistry — a curated `table-name -> schema` lookup the generic DB editor consults BEFORE
// falling back to live introspection. Returns a hand-authored schema (nicer labels/tips/FK pickers,
// locale-child wiring) when one exists for the table; otherwise empty and the editor introspects.
// Populated from the schemas the retired grab-bag editors used to own.

#include <string>

namespace we
{
struct DbTableSchema;
struct CompositeDbTableSchema;

struct CuratedDbSchema
{
    const DbTableSchema*          single = nullptr;     // curated single-PK schema, or null
    const CompositeDbTableSchema* composite = nullptr;  // curated composite-PK schema, or null
    bool found() const { return single || composite; }
};

// Curated schema for `table`, or an empty result (caller then introspects).
CuratedDbSchema LookupDbSchema(const std::string& table);
} // namespace we
