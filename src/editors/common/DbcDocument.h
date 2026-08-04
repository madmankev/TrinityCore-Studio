#pragma once

// DbcDocument — one editable DBC table bound to its overlay identity (archive path) with
// a dirty flag. Bundles the load / edit / loose-save pattern every DBC editor repeats; a
// module can hold several (the achievement editor holds Achievement + Criteria + Category).
// Read accessors forward to the underlying EditableDbc; write/row ops mark the document
// dirty; SaveOverlay serializes and writes the loose file, clearing dirty.

#include <cstdint>
#include <string>
#include <vector>

#include "clientdata/DbcSchema.h"
#include "clientdata/EditableDbc.h"

namespace we
{
class ClientData;

class DbcDocument
{
public:
    // Bind the schema + MPQ-relative path (e.g. "DBFilesClient\\CharTitles.dbc"). Idempotent.
    void Init(const DbcSchema* schema, std::string archivePath);

    bool Load(const ClientData& cd);  // ReadFile(archivePath) -> EditableDbc::Load(schema)
    bool LoadBytes(const std::vector<uint8_t>& bytes);  // load already-read bytes (single read)
    void InitEmpty();                 // start an empty table from the schema (new file / harness)
    bool SaveOverlay(const std::string& editRoot, std::string& err);  // Serialize + WriteLooseFile

    bool IsLoaded() const { return table_.IsLoaded(); }
    bool Dirty() const { return dirty_; }
    void MarkDirty() { dirty_ = true; }
    void ClearDirty() { dirty_ = false; }
    const std::string& ArchivePath() const { return archivePath_; }
    const char* BaseName() const;  // "CharTitles.dbc" (after the last slash)

    EditableDbc& table() { return table_; }
    const EditableDbc& table() const { return table_; }

    uint32_t RecordCount() const { return table_.RecordCount(); }
    uint32_t FieldCount() const { return table_.FieldCount(); }

    uint32_t           GetU32(uint32_t r, uint32_t c) const { return table_.GetU32(r, c); }
    int32_t            GetI32(uint32_t r, uint32_t c) const { return table_.GetI32(r, c); }
    float              GetF32(uint32_t r, uint32_t c) const { return table_.GetF32(r, c); }
    const std::string& GetStr(uint32_t r, uint32_t c) const { return table_.GetStr(r, c); }

    void SetU32(uint32_t r, uint32_t c, uint32_t v) { table_.SetU32(r, c, v); dirty_ = true; }
    void SetI32(uint32_t r, uint32_t c, int32_t v) { table_.SetI32(r, c, v); dirty_ = true; }
    void SetF32(uint32_t r, uint32_t c, float v) { table_.SetF32(r, c, v); dirty_ = true; }
    void SetStr(uint32_t r, uint32_t c, std::string v)
    {
        table_.SetStr(r, c, std::move(v));
        dirty_ = true;
    }

    uint32_t AddRow() { dirty_ = true; return table_.AddRow(); }
    uint32_t CloneRow(uint32_t r) { dirty_ = true; return table_.CloneRow(r); }
    void     DeleteRow(uint32_t r) { table_.DeleteRow(r); dirty_ = true; }

    // max(value in `idCol`) + 1, at least 1. For assigning a fresh id/bit index.
    uint32_t NextFreeId(uint32_t idCol) const;

private:
    const DbcSchema* schema_ = nullptr;
    std::string      archivePath_;
    EditableDbc      table_;
    bool             dirty_ = false;
};
} // namespace we
