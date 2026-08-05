// DbcDocument — see DbcDocument.h.

#include "editors/common/DbcDocument.h"

#include <algorithm>

#include "clientdata/ClientData.h"
#include "clientdata/DbcOverlay.h"

namespace we
{
void DbcDocument::Init(const DbcSchema* schema, std::string archivePath)
{
    schema_ = schema;
    archivePath_ = std::move(archivePath);
}

bool DbcDocument::Load(const ClientData& cd)
{
    dirty_ = false;
    if (!schema_)
        return false;
    return table_.Load(cd.ReadFile(archivePath_), *schema_);
}

bool DbcDocument::LoadBytes(const std::vector<uint8_t>& bytes)
{
    dirty_ = false;
    if (!schema_)
        return false;
    return table_.Load(bytes, *schema_);
}

void DbcDocument::InitEmpty()
{
    if (schema_)
        table_.InitEmpty(*schema_);
}

bool DbcDocument::SaveOverlay(const std::string& editRoot, std::string& err)
{
    if (!table_.IsLoaded())
    {
        err = "table not loaded";
        return false;
    }
    if (!WriteLooseFile(editRoot, archivePath_, table_.Serialize(), err))
        return false;
    dirty_ = false;
    return true;
}

const char* DbcDocument::BaseName() const
{
    size_t slash = archivePath_.find_last_of("\\/");
    return slash == std::string::npos ? archivePath_.c_str() : archivePath_.c_str() + slash + 1;
}

uint32_t DbcDocument::NextFreeId(uint32_t idCol) const
{
    uint32_t maxId = 0;
    for (uint32_t r = 0; r < table_.RecordCount(); ++r)
        maxId = std::max(maxId, table_.GetU32(r, idCol));
    return maxId + 1;
}
} // namespace we
