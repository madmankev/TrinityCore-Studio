#pragma once

// Layer A aggregate: everything the editor knows about a single item entry.
// item_template is essentially a single-table record, so this is much simpler
// than Quest: the main row plus the localized rows from item_template_locale.

#include <cstdint>
#include <map>
#include <string>

#include "ItemTemplate.h"

namespace qe
{
// One row of item_template_locale (per `locale` code). Empty strings mean the
// column carries no localized value for that locale.
struct ItemLocale
{
    std::string locale;       // `locale`      varchar(4) (koKR, frFR, ...)
    std::string name;         // `Name`        mediumtext
    std::string description;  // `Description` mediumtext
};

struct Item
{
    ItemTemplate tmpl;                         // item_template (always present)
    std::map<std::string, ItemLocale> locales; // key = locale code (see qe::kLocales)

    // --- Dirty tracking (save writes only what changed) -------------------
    bool tmplDirty = false;
    bool localesDirty = false;

    // True when newly created in the editor and not yet written to the DB.
    bool isNew = false;

    bool AnyDirty() const { return tmplDirty || localesDirty; }

    void ClearDirty() { tmplDirty = localesDirty = false; }
};
} // namespace qe
