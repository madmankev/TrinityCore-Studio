#include "editors/gameobject/GameObjectValidator.h"

#include <algorithm>
#include <string>

#include "data/LookupCache.h"
#include "util/Enums.h"

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

std::vector<ValidationIssue> GameObjectValidator::Validate(const GameObject& go, const LookupCache& cache) const
{
    std::vector<ValidationIssue> issues;
    Collector add(issues);
    const GameObjectTemplate& t = go.tmpl;

    if (t.entry == 0)
        add.Add(Severity::Error, "General", "entry", "GameObject entry is 0");
    if (t.name.empty())
        add.Add(Severity::Warning, "General", "name", "GameObject name is empty");
    if (!LabelFor(GameObjectTypeValues(), t.type))
        add.Add(Severity::Warning, "General", "type", "Unknown gameobject type " + std::to_string(t.type));
    if (t.displayId == 0)
        add.Add(Severity::Info, "General", "displayId", "No displayId (no model)");
    if (t.size <= 0.0f)
        add.Add(Severity::Warning, "General", "size", "size should be > 0");

    // addon.faction is a FactionTemplate id.
    if (go.addon.present && go.addon.faction != 0 && cache.FactionTemplatesLoaded() &&
        cache.NameOfFactionTemplate(go.addon.faction).empty())
        add.Add(Severity::Warning, "Addon", "faction",
                "FactionTemplate " + std::to_string(go.addon.faction) + " not found");

    // loot rows exist but Data1 (loot id) is unset, or the type doesn't use loot.
    if (!go.loot.empty())
    {
        const bool lootType = (t.type == 3 || t.type == 25);  // CHEST / FISHINGHOLE
        if (!lootType)
            add.Add(Severity::Info, "Loot", "type", "Loot rows set but this type has no loot table");
        else if (t.data[1] == 0)
            add.Add(Severity::Warning, "Loot", "Data1", "Loot rows set but Data1 (loot id) is 0");
    }

    std::stable_sort(issues.begin(), issues.end(),
                     [](const ValidationIssue& a, const ValidationIssue& b)
                     { return static_cast<int>(a.severity) < static_cast<int>(b.severity); });
    return issues;
}
} // namespace we
