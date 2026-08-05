#include "editors/creature/CreatureValidator.h"

#include <algorithm>
#include <string>

#include "data/LookupCache.h"
#include "ui/Enums.h"

namespace we
{
namespace
{
constexpr uint32_t UNIT_NPC_FLAG_VENDOR = 0x00000080;
constexpr uint32_t UNIT_NPC_FLAG_TRAINER = 0x00000010;

class Collector
{
public:
    explicit Collector(std::vector<ValidationIssue>& out) : out_(out) {}
    void Add(Severity sev, std::string tab, std::string field, std::string msg)
    {
        out_.push_back({sev, std::move(tab), std::move(field), std::move(msg)});
    }

private:
    std::vector<ValidationIssue>& out_;
};
} // namespace

std::vector<ValidationIssue> CreatureValidator::Validate(const Creature& c, const LookupCache& cache) const
{
    std::vector<ValidationIssue> issues;
    Collector add(issues);
    const CreatureTemplate& t = c.tmpl;

    if (t.entry == 0)
        add.Add(Severity::Error, "General", "entry", "Creature entry is 0");
    if (t.name.empty())
        add.Add(Severity::Warning, "General", "name", "Creature name is empty");
    if (!LabelFor(CreatureTypeValues(), t.type))
        add.Add(Severity::Warning, "General", "type", "Unknown creature type " + std::to_string(t.type));
    if (t.scale <= 0.0f)
        add.Add(Severity::Warning, "General", "scale", "Scale should be > 0");
    if (!LabelFor(CreatureRankValues(), t.rank))
        add.Add(Severity::Info, "General", "rank", "Unusual rank " + std::to_string(t.rank));

    if (t.minLevel > t.maxLevel)
        add.Add(Severity::Warning, "Stats", "minlevel", "minlevel is greater than maxlevel");
    if (t.unitClass != 0 && t.unitClass != 1 && t.unitClass != 2 && t.unitClass != 4 && t.unitClass != 8)
        add.Add(Severity::Info, "Stats", "unit_class",
                "unit_class " + std::to_string(t.unitClass) + " is not one of 0/1/2/4/8");
    if (t.exp < 0 || t.exp > 2)
        add.Add(Severity::Info, "Stats", "exp", "expansion should be 0, 1 or 2");

    // faction is a FactionTemplate.dbc id.
    if (t.faction == 0)
        add.Add(Severity::Info, "Combat", "faction", "Faction is 0 (no faction template)");
    else if (cache.FactionTemplatesLoaded() && cache.NameOfFactionTemplate(t.faction).empty())
        add.Add(Severity::Warning, "Combat", "faction",
                "FactionTemplate " + std::to_string(t.faction) + " not found");

    for (int i = 0; i < 8; ++i)
        if (c.spells[i] != 0 && cache.SpellsLoaded() && cache.NameOfSpell(c.spells[i]).empty())
            add.Add(Severity::Warning, "Combat", "spell",
                    "Spell " + std::to_string(c.spells[i]) + " (slot " + std::to_string(i) +
                        ") not found in Spell.dbc");

    // NPC-role flags vs the associated data actually present.
    if ((t.npcflag & UNIT_NPC_FLAG_VENDOR) && c.vendorItems.empty())
        add.Add(Severity::Info, "Vendor", "npcflag", "VENDOR flag set but no vendor items");
    if ((t.npcflag & UNIT_NPC_FLAG_TRAINER) && !c.trainer.present)
        add.Add(Severity::Info, "Trainer", "npcflag", "TRAINER flag set but no trainer bound");

    std::stable_sort(issues.begin(), issues.end(),
                     [](const ValidationIssue& a, const ValidationIssue& b)
                     { return static_cast<int>(a.severity) < static_cast<int>(b.severity); });
    return issues;
}
} // namespace we
