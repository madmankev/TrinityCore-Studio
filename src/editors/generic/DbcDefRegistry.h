#pragma once

// DbcDefRegistry — resolves a DBC's column layout (a DbcSchema) by name for the generic DBC
// editor. Lookup priority: hand-curated schema (nicest labels/FK) -> the vendored WoWDBDefs
// ".dbd" definition parsed for build 3.3.5.12340 (correct/typed for ~everything) -> nothing
// (the caller then falls back to a raw all-UInt32 schema built from the file header).
//
// Parsed .dbd results are cached and own their field-name strings (ParsedDbc). The definitions
// directory is resolved lazily from a few sensible candidates (or set explicitly).

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "clientdata/DbcSchema.h"
#include "clientdata/DbdParser.h"

namespace we
{
class DbcDefRegistry
{
public:
    // Override the vendored .dbd directory (else candidates are probed on first use).
    void SetDefinitionsDir(std::string dir)
    {
        defsDir_ = std::move(dir);
        resolved_ = true;
    }

    // Register a hand-curated schema for a DBC base name ("Item"). Wins over the .dbd. The
    // pointed-to schema must outlive the registry (use a static accessor).
    void AddCurated(const std::string& dbcBaseName, const DbcSchema* schema)
    {
        curated_[dbcBaseName] = schema;
    }

    // Schema for a DBC base name ("Item", "PowerDisplay"): curated first, else the parsed .dbd
    // for build 12340. nullptr when neither exists (caller uses a raw schema).
    const DbcSchema* Lookup(const std::string& dbcBaseName);

    // Provenance for `dbcBaseName` after a Lookup: "curated", "dbd", or "" (none/raw).
    const char* Source(const std::string& dbcBaseName);

    // Base names of every .dbd on disk (sorted). Used to offer a canonical picker list even
    // when the client's listfile is incomplete.
    std::vector<std::string> DefinedNames();

    const std::string& DefinitionsDir();  // resolves lazily

private:
    std::string defsDir_;
    bool        resolved_ = false;

    std::unordered_map<std::string, const DbcSchema*>          curated_;
    std::unordered_map<std::string, std::unique_ptr<ParsedDbc>> dbdCache_;  // null = tried, no def
};
} // namespace we
