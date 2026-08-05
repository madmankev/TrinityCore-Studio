#include "editors/item/ItemValidator.h"

#include <algorithm>
#include <string>

#include "data/LookupCache.h"
#include "ui/Enums.h"

namespace we
{
namespace
{
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

std::vector<ValidationIssue> ItemValidator::Validate(const Item& item, const LookupCache& cache) const
{
    std::vector<ValidationIssue> issues;
    Collector add(issues);

    const ItemTemplate& t = item.tmpl;

    // --- Identity / classification ---------------------------------------
    if (t.entry == 0)
        add.Add(Severity::Error, "General", "entry", "Item entry is 0");
    if (t.name.empty())
        add.Add(Severity::Warning, "General", "name", "Item name is empty");
    if (!LabelFor(ItemClassValues(), t.cls))
        add.Add(Severity::Warning, "General", "class",
                "Unknown item class " + std::to_string(t.cls));
    else if (!LabelFor(ItemSubclassValues(t.cls), t.subclass))
        add.Add(Severity::Info, "General", "subclass",
                "Subclass " + std::to_string(t.subclass) + " is not defined for this class");
    if (!LabelFor(ItemQualityValues(), t.quality))
        add.Add(Severity::Warning, "General", "Quality",
                "Quality " + std::to_string(t.quality) + " out of range (0-7)");
    if (t.displayId == 0)
        add.Add(Severity::Info, "General", "displayid", "No display id (no icon/model)");
    if (t.bonding > 5)
        add.Add(Severity::Warning, "General", "bonding",
                "Bonding " + std::to_string(t.bonding) + " out of range (0-5)");
    if (t.containerSlots > 0 && t.cls != 1 /*Container*/)
        add.Add(Severity::Info, "General", "ContainerSlots",
                "ContainerSlots set on a non-container item");

    // --- Vendor pricing --------------------------------------------------
    if (t.buyPrice > 0 && t.sellPrice > static_cast<uint32_t>(t.buyPrice))
        add.Add(Severity::Info, "General", "SellPrice", "SellPrice exceeds BuyPrice");

    // --- Requirements (existence checks, guarded by cache load state) ----
    if (t.requiredSkill && cache.SkillsLoaded() && cache.NameOfSkill(t.requiredSkill).empty())
        add.Add(Severity::Warning, "Requirements", "RequiredSkill",
                "RequiredSkill " + std::to_string(t.requiredSkill) + " not found in SkillLine.dbc");
    if (t.requiredSpell && cache.SpellsLoaded() && cache.NameOfSpell(t.requiredSpell).empty())
        add.Add(Severity::Warning, "Requirements", "requiredspell",
                "requiredspell " + std::to_string(t.requiredSpell) + " not found in Spell.dbc");
    if (t.requiredReputationFaction && cache.FactionsLoaded() &&
        cache.NameOfFaction(t.requiredReputationFaction).empty())
        add.Add(Severity::Warning, "Requirements", "RequiredReputationFaction",
                "Faction " + std::to_string(t.requiredReputationFaction) + " not found in Faction.dbc");
    if (t.startQuest && cache.QuestsLoaded() && cache.NameOfQuest(t.startQuest).empty())
        add.Add(Severity::Warning, "Requirements", "startquest",
                "startquest " + std::to_string(t.startQuest) + " not found in quest_template");

    // --- Spells ----------------------------------------------------------
    for (int i = 0; i < 5; ++i)
    {
        if (t.spellId[i] > 0 && cache.SpellsLoaded() &&
            cache.NameOfSpell(static_cast<uint32_t>(t.spellId[i])).empty())
            add.Add(Severity::Warning, "Spells", "spellid_" + std::to_string(i + 1),
                    "spellid_" + std::to_string(i + 1) + " (" + std::to_string(t.spellId[i]) +
                        ") not found in Spell.dbc");
        if (t.spellId[i] > 0 && !LabelFor(SpellTriggerValues(), t.spellTrigger[i]))
            add.Add(Severity::Info, "Spells", "spelltrigger_" + std::to_string(i + 1),
                    "Unknown spelltrigger " + std::to_string(t.spellTrigger[i]));
    }

    // --- Stats -----------------------------------------------------------
    if (t.statsCount > 10)
        add.Add(Severity::Warning, "Stats", "StatsCount",
                "StatsCount " + std::to_string(t.statsCount) + " exceeds 10");

    std::stable_sort(issues.begin(), issues.end(),
                     [](const ValidationIssue& a, const ValidationIssue& b)
                     { return static_cast<int>(a.severity) < static_cast<int>(b.severity); });
    return issues;
}
} // namespace we
