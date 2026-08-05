#pragma once

// DbdParser — turns a WoWDBDefs ".dbd" definition into a DbcSchema for build 3.3.5.12340, so the
// generic DBC editor can type every DBC correctly (ints/floats/strings/locstrings, arrays, PK) with
// no hand-authored schema. Owns the produced field-name strings (DbcFieldDef uses const char*), so
// the result is heap-allocated and never moved after construction.

#include <deque>
#include <memory>
#include <string>

#include "clientdata/DbcSchema.h"

namespace we
{
struct ParsedDbc
{
    std::deque<std::string> store;   // owns field-name strings (stable addresses; never moved)
    DbcSchema               schema;
    bool                    ok = false;
};

// Parse a .dbd's text and build the DbcSchema for the 3.3.5.12340 layout. Returns nullptr (or
// ok=false) when the file has no 3.3.5.12340 build section or can't be parsed.
std::unique_ptr<ParsedDbc> ParseDbdFor12340(const std::string& dbdText);
} // namespace we
