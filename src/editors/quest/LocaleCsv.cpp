// LocaleCsv — see LocaleCsv.h.

#include "editors/quest/LocaleCsv.h"

#include "schema/Quest.h"

#include <sstream>

namespace qe
{
namespace
{
// The editable string fields of a QuestLocale, by CSV field name.
// Accessor returns a reference so import can write into it; a present-flag setter
// marks the owning row present on import.
struct FieldRef
{
    const char* name;
    std::string& (*ref)(QuestLocale&);   // accessor into the string field
    void (*markPresent)(QuestLocale&);
};

const std::vector<FieldRef>& Fields()
{
    static const std::vector<FieldRef> f = {
        {"Title", [](QuestLocale& l) -> std::string& { return l.title; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"Details", [](QuestLocale& l) -> std::string& { return l.details; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"Objectives", [](QuestLocale& l) -> std::string& { return l.objectives; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"EndText", [](QuestLocale& l) -> std::string& { return l.endText; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"CompletedText", [](QuestLocale& l) -> std::string& { return l.completedText; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"ObjectiveText1", [](QuestLocale& l) -> std::string& { return l.objectiveText[0]; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"ObjectiveText2", [](QuestLocale& l) -> std::string& { return l.objectiveText[1]; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"ObjectiveText3", [](QuestLocale& l) -> std::string& { return l.objectiveText[2]; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"ObjectiveText4", [](QuestLocale& l) -> std::string& { return l.objectiveText[3]; }, [](QuestLocale& l) { l.templatePresent = true; }},
        {"RewardText", [](QuestLocale& l) -> std::string& { return l.rewardText; }, [](QuestLocale& l) { l.offerRewardPresent = true; }},
        {"CompletionText", [](QuestLocale& l) -> std::string& { return l.completionText; }, [](QuestLocale& l) { l.requestItemsPresent = true; }},
        {"GreetingCreature", [](QuestLocale& l) -> std::string& { return l.greetingCreature; }, [](QuestLocale& l) { l.greetingCreaturePresent = true; }},
        {"GreetingGameObject", [](QuestLocale& l) -> std::string& { return l.greetingGameObject; }, [](QuestLocale& l) { l.greetingGameObjectPresent = true; }},
    };
    return f;
}

std::string CsvEscape(const std::string& s)
{
    bool needQuote = s.find_first_of(",\"\n\r") != std::string::npos;
    if (!needQuote)
        return s;
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"')
            out += "\"\"";
        else
            out += c;
    }
    out += "\"";
    return out;
}

// Parse one CSV line into fields, honoring quotes and doubled-quote escapes.
std::vector<std::string> ParseCsvLine(const std::string& line)
{
    std::vector<std::string> out;
    std::string cur;
    bool inQuotes = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (inQuotes)
        {
            if (c == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else inQuotes = false;
            }
            else
                cur += c;
        }
        else
        {
            if (c == '"')
                inQuotes = true;
            else if (c == ',')
            {
                out.push_back(cur);
                cur.clear();
            }
            else
                cur += c;
        }
    }
    out.push_back(cur);
    return out;
}
} // namespace

std::string ExportLocalesCsv(const Quest& q)
{
    std::ostringstream os;
    os << "locale,field,value\r\n";
    for (const auto& kv : q.locales)
    {
        const std::string& code = kv.first;
        const QuestLocale& l = kv.second;
        for (const FieldRef& f : Fields())
        {
            const std::string& val = f.ref(const_cast<QuestLocale&>(l));
            if (val.empty())
                continue;
            os << CsvEscape(code) << "," << f.name << "," << CsvEscape(val) << "\r\n";
        }
    }
    return os.str();
}

bool ImportLocalesCsv(const std::string& csv, Quest& q, std::string& error)
{
    error.clear();
    std::istringstream is(csv);
    std::string line;
    bool headerSeen = false;
    int applied = 0;
    while (std::getline(is, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty())
            continue;
        if (!headerSeen)
        {
            headerSeen = true;
            // Skip a header row if present.
            std::string low = line;
            if (low.rfind("locale,", 0) == 0)
                continue;
        }
        std::vector<std::string> cols = ParseCsvLine(line);
        if (cols.size() < 3)
            continue;
        const std::string& code = cols[0];
        const std::string& field = cols[1];
        const std::string& value = cols[2];
        if (code.empty())
            continue;

        const FieldRef* fr = nullptr;
        for (const FieldRef& f : Fields())
            if (field == f.name)
            {
                fr = &f;
                break;
            }
        if (!fr)
            continue;  // unknown field -> ignore

        QuestLocale& l = q.locales[code];
        l.locale = code;
        fr->ref(l) = value;
        fr->markPresent(l);
        ++applied;
    }
    q.localesDirty = true;
    if (applied == 0)
    {
        error = "No recognizable locale rows found.";
        return false;
    }
    return true;
}
} // namespace qe
