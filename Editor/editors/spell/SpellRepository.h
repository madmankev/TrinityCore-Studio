#pragma once

// SpellRepository — the SERVER-side (world DB) half of the Spell editor. Provides:
//  - SaveSpellDbc: project the client Spell.dbc row -> the server spell_dbc mirror row
//    (one-way DBC->spell_dbc on save, so the two never diverge).
//  - Schemas for the 1:1 augmentation tables (spell_proc / bonus / threat / custom_attr /
//    difficulty), driven by the generic DbTableRepository.
//  - Column specs for the child-list tables (spell_required, spell_learn_spell, ...),
//    driven by DbTableRepository::ListChildren/ReplaceChildren.
// All DB access reuses DbTableRepository (transactional, Live/SqlExport).

#include <vector>

#include "db/DbTypes.h"
#include "editors/common/DbTableSchema.h"

namespace we
{
class IDatabase;
class DbcDocument;

// A child-list table keyed by a parent (spell id) column.
struct SpellChildSpec
{
    const char* table;
    const char* parentCol;         // column that equals the spell id
    std::vector<DbColumn> cols;    // columns to read/write (include the parent col)
};

class SpellRepository
{
public:
    // Project DBC row `row` of `doc` (client Spell.dbc) into a server spell_dbc upsert.
    DbError SaveSpellDbc(IDatabase& db, const DbcDocument& doc, uint32_t row);

    // 1:1 augmentation tables (pk = the spell id column).
    static const DbTableSchema& ProcSchema();
    static const DbTableSchema& BonusSchema();
    static const DbTableSchema& ThreatSchema();
    static const DbTableSchema& CustomAttrSchema();
    static const DbTableSchema& DifficultySchema();

    // Child-list tables.
    static const SpellChildSpec& Required();
    static const SpellChildSpec& LearnSpell();
    static const SpellChildSpec& LinkedSpell();
    static const SpellChildSpec& Ranks();
    static const SpellChildSpec& Area();
    static const SpellChildSpec& TargetPosition();
    static const SpellChildSpec& PetAuras();
    static const SpellChildSpec& ScriptNames();
    static const SpellChildSpec& Scripts();
    static const SpellChildSpec& LootTemplate();
    static const SpellChildSpec& GroupMembership();
};
} // namespace we
