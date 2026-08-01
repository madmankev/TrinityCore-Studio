#pragma once

// M2Loader — parse a WoW 3.3.5a .m2 (+ its Model00.skin) into an in-memory M2Model.
// Pure CPU; reads files through ClientData (MPQ/loose). See M2Types.h for the format.

#include <string>

#include "model/M2Types.h"

namespace we
{
class ClientData;

namespace m2
{
// Parse `m2Path` (e.g. "Creature\\Rat\\Rat.m2") and its sibling "...00.skin" into `out`.
// Returns false (and leaves a diagnostic in `error`) on any malformed/missing input.
bool Load(ClientData& cd, const std::string& m2Path, M2Model& out, std::string* error = nullptr);
} // namespace m2
} // namespace we
