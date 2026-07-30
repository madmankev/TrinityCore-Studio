#include "editors/quest/QuestSqlImporter.h"

#include <cstring>
#include <exception>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace qe
{
namespace
{
// --- Value ----------------------------------------------------------------
// One parsed field from a VALUES tuple. `text` holds either the raw numeric
// token or the fully-unescaped string contents. `isNull` marks a NULL literal.
struct Value
{
    bool isNull = false;
    bool isString = false;
    std::string text;
};

// One parsed INSERT/REPLACE statement.
struct ParsedStmt
{
    std::string table;                              // lowercased table name
    std::vector<std::string> cols;                  // UPPERCASED column names
    std::vector<std::vector<Value>> tuples;         // one or more VALUES tuples
};

// --- small string helpers -------------------------------------------------
char UpChar(char c)
{
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - 'a' + 'A');
    return c;
}

char LoChar(char c)
{
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c - 'A' + 'a');
    return c;
}

std::string ToUpper(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        c = UpChar(c);
    return r;
}

std::string ToLower(const std::string& s)
{
    std::string r = s;
    for (char& c : r)
        c = LoChar(c);
    return r;
}

bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

void Trim(std::string& s)
{
    size_t a = 0;
    while (a < s.size() && IsSpace(s[a]))
        ++a;
    size_t b = s.size();
    while (b > a && IsSpace(s[b - 1]))
        --b;
    s = s.substr(a, b - a);
}

size_t SkipWs(const std::string& s, size_t i)
{
    while (i < s.size() && IsSpace(s[i]))
        ++i;
    return i;
}

// Reads a run of [A-Za-z] letters (SQL keyword). Advances i.
std::string ReadWord(const std::string& s, size_t& i)
{
    std::string r;
    while (i < s.size())
    {
        char c = s[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        {
            r += c;
            ++i;
        }
        else
            break;
    }
    return r;
}

// --- value conversions ----------------------------------------------------
long long ToI(const Value& v)
{
    if (v.isNull || v.text.empty())
        return 0;
    try
    {
        return std::stoll(v.text);
    }
    catch (...)
    {
        return 0;
    }
}

unsigned long long ToU(const Value& v)
{
    if (v.isNull || v.text.empty())
        return 0;
    try
    {
        // stoull rejects a leading '-'; fall back to signed then cast.
        if (!v.text.empty() && v.text[0] == '-')
            return static_cast<unsigned long long>(std::stoll(v.text));
        return std::stoull(v.text);
    }
    catch (...)
    {
        return 0;
    }
}

uint32_t ToU32(const Value& v)
{
    return static_cast<uint32_t>(ToU(v));
}

float ToF(const Value& v)
{
    if (v.isNull || v.text.empty())
        return 0.0f;
    try
    {
        return static_cast<float>(std::stod(v.text));
    }
    catch (...)
    {
        return 0.0f;
    }
}

std::string ToStr(const Value& v)
{
    return v.isNull ? std::string() : v.text;
}

// Returns n when `col` == base + n (n a positive integer), else 0. `col` is
// expected UPPERCASE; `base` is an uppercase literal.
int IndexSuffix(const std::string& col, const char* base)
{
    size_t bl = std::strlen(base);
    if (col.size() <= bl)
        return 0;
    if (std::memcmp(col.data(), base, bl) != 0)
        return 0;
    int val = 0;
    for (size_t k = bl; k < col.size(); ++k)
    {
        char ch = col[k];
        if (ch < '0' || ch > '9')
            return 0;
        val = val * 10 + (ch - '0');
        if (val > 100000)
            return 0;
    }
    return val;
}

const Value* FindCol(const std::vector<std::string>& cols, const std::vector<Value>& row,
                     const char* nameUpper)
{
    for (size_t i = 0; i < cols.size(); ++i)
        if (cols[i] == nameUpper)
            return i < row.size() ? &row[i] : nullptr;
    return nullptr;
}

// --- statement splitting --------------------------------------------------
// Splits SQL text on top-level ';', stripping `-- ...` line comments and
// `/* ... */` block comments. String literals (single-quoted, with backslash
// and doubled-quote escapes) are copied verbatim so their ';' / comment markers
// are ignored.
std::vector<std::string> SplitStatements(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    size_t n = text.size();
    for (size_t i = 0; i < n;)
    {
        char c = text[i];

        if (c == '-' && i + 1 < n && text[i + 1] == '-')
        {
            while (i < n && text[i] != '\n')
                ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && text[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/'))
                ++i;
            i += 2;
            continue;
        }
        if (c == '\'')
        {
            cur += c;
            ++i;
            while (i < n)
            {
                char d = text[i];
                if (d == '\\' && i + 1 < n)
                {
                    cur += d;
                    cur += text[i + 1];
                    i += 2;
                    continue;
                }
                if (d == '\'')
                {
                    if (i + 1 < n && text[i + 1] == '\'')
                    {
                        cur += "''";
                        i += 2;
                        continue;
                    }
                    cur += d;
                    ++i;
                    break;
                }
                cur += d;
                ++i;
            }
            continue;
        }
        if (c == ';')
        {
            out.push_back(cur);
            cur.clear();
            ++i;
            continue;
        }
        cur += c;
        ++i;
    }
    Trim(cur);
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

// Reads a (possibly backticked / schema-qualified) table name. Advances i.
std::string ReadTableName(const std::string& s, size_t& i)
{
    std::string r;
    while (i < s.size())
    {
        char c = s[i];
        if (c == '`')
        {
            ++i;
            while (i < s.size() && s[i] != '`')
            {
                r += s[i];
                ++i;
            }
            if (i < s.size())
                ++i;
        }
        else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                 c == '_' || c == '.' || c == '$')
        {
            r += c;
            ++i;
        }
        else
            break;
    }
    size_t dot = r.find_last_of('.');
    if (dot != std::string::npos)
        r = r.substr(dot + 1);
    return r;
}

// Parses a parenthesized column-name list starting at s[i]=='('. Advances i
// past the closing ')'. Names are stripped of backticks/quotes and uppercased.
std::vector<std::string> ParseColumnList(const std::string& s, size_t& i)
{
    std::vector<std::string> cols;
    if (i >= s.size() || s[i] != '(')
        return cols;
    ++i;
    std::string cur;
    auto flush = [&]() {
        std::string name;
        for (char c : cur)
            if (c != '`' && c != '"' && c != '\'')
                name += c;
        Trim(name);
        if (!name.empty())
            cols.push_back(ToUpper(name));
        cur.clear();
    };
    while (i < s.size())
    {
        char c = s[i];
        if (c == ')')
        {
            ++i;
            break;
        }
        if (c == ',')
        {
            flush();
            ++i;
            continue;
        }
        cur += c;
        ++i;
    }
    flush();
    return cols;
}

// Parses one value field at s[i] (already past leading ws handled by caller).
// Advances i to the char just after the field (a ',' or ')' for raw fields, or
// past the closing quote for strings).
Value ParseValue(const std::string& s, size_t& i)
{
    Value v;
    i = SkipWs(s, i);
    if (i >= s.size())
        return v;

    if (s[i] == '\'')
    {
        ++i;
        std::string out;
        while (i < s.size())
        {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size())
            {
                char nx = s[i + 1];
                switch (nx)
                {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '0': out += '\0'; break;
                    case 'b': out += '\b'; break;
                    case '\\': out += '\\'; break;
                    case '\'': out += '\''; break;
                    case '"': out += '"'; break;
                    default: out += nx; break;
                }
                i += 2;
                continue;
            }
            if (c == '\'')
            {
                if (i + 1 < s.size() && s[i + 1] == '\'')
                {
                    out += '\'';
                    i += 2;
                    continue;
                }
                ++i;
                break;
            }
            out += c;
            ++i;
        }
        v.isString = true;
        v.text = out;
        return v;
    }

    std::string raw;
    while (i < s.size() && s[i] != ',' && s[i] != ')')
    {
        raw += s[i];
        ++i;
    }
    Trim(raw);
    if (raw.empty())
    {
        v.isNull = true;
        return v;
    }
    if (ToUpper(raw) == "NULL")
    {
        v.isNull = true;
        return v;
    }
    v.text = raw;
    return v;
}

// Parses one or more comma-separated VALUES tuples beginning at s[i].
std::vector<std::vector<Value>> ParseTuples(const std::string& s, size_t i)
{
    std::vector<std::vector<Value>> tuples;
    while (true)
    {
        i = SkipWs(s, i);
        if (i >= s.size() || s[i] != '(')
            break;
        ++i;
        std::vector<Value> tuple;
        while (true)
        {
            i = SkipWs(s, i);
            if (i < s.size() && s[i] == ')')
            {
                ++i;
                break;
            }
            Value v = ParseValue(s, i);
            tuple.push_back(std::move(v));
            i = SkipWs(s, i);
            if (i < s.size() && s[i] == ',')
            {
                ++i;
                continue;
            }
            if (i < s.size() && s[i] == ')')
            {
                ++i;
                break;
            }
            break; // malformed; stop this tuple
        }
        tuples.push_back(std::move(tuple));
        i = SkipWs(s, i);
        if (i < s.size() && s[i] == ',')
        {
            ++i;
            continue;
        }
        break;
    }
    return tuples;
}

// Parses a single statement string into ParsedStmt. Returns false when the
// statement is not an INSERT/REPLACE ... VALUES ... form.
bool ParseInsert(const std::string& stmt, ParsedStmt& out)
{
    size_t i = SkipWs(stmt, 0);
    std::string kw = ToUpper(ReadWord(stmt, i));
    if (kw != "INSERT" && kw != "REPLACE")
        return false;

    // Consume optional modifiers (IGNORE, LOW_PRIORITY, DELAYED, ...) up to INTO.
    bool sawInto = false;
    for (int guard = 0; guard < 8; ++guard)
    {
        i = SkipWs(stmt, i);
        size_t save = i;
        std::string w = ToUpper(ReadWord(stmt, i));
        if (w == "INTO")
        {
            sawInto = true;
            break;
        }
        if (w.empty())
        {
            i = save;
            break;
        }
        // otherwise skip modifier keyword and continue
    }
    if (!sawInto)
        return false;

    i = SkipWs(stmt, i);
    out.table = ToLower(ReadTableName(stmt, i));
    if (out.table.empty())
        return false;

    i = SkipWs(stmt, i);
    if (i < stmt.size() && stmt[i] == '(')
        out.cols = ParseColumnList(stmt, i);

    // Expect VALUES / VALUE keyword next.
    i = SkipWs(stmt, i);
    size_t save = i;
    std::string vk = ToUpper(ReadWord(stmt, i));
    if (vk != "VALUES" && vk != "VALUE")
    {
        // Some dumps place SET syntax etc.; unsupported.
        i = save;
        return false;
    }

    out.tuples = ParseTuples(stmt, i);
    return !out.tuples.empty();
}

// --- per-table field assignment ------------------------------------------
void ApplyTemplate(QuestTemplate& t, const std::vector<std::string>& cols,
                   const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        int idx;
        if (c == "ID") t.id = ToU32(v);
        else if (c == "QUESTTYPE") t.questType = static_cast<uint8_t>(ToU(v));
        else if (c == "QUESTLEVEL") t.questLevel = static_cast<int16_t>(ToI(v));
        else if (c == "MINLEVEL") t.minLevel = static_cast<uint8_t>(ToU(v));
        else if (c == "QUESTSORTID") t.questSortID = static_cast<int16_t>(ToI(v));
        else if (c == "QUESTINFOID") t.questInfoID = static_cast<uint16_t>(ToU(v));
        else if (c == "SUGGESTEDGROUPNUM") t.suggestedGroupNum = static_cast<uint8_t>(ToU(v));
        else if (c == "REQUIREDFACTIONID1") t.requiredFactionId1 = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDFACTIONID2") t.requiredFactionId2 = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDFACTIONVALUE1") t.requiredFactionValue1 = static_cast<int32_t>(ToI(v));
        else if (c == "REQUIREDFACTIONVALUE2") t.requiredFactionValue2 = static_cast<int32_t>(ToI(v));
        else if (c == "REWARDNEXTQUEST") t.rewardNextQuest = ToU32(v);
        else if (c == "REWARDXPDIFFICULTY") t.rewardXPDifficulty = static_cast<uint8_t>(ToU(v));
        else if (c == "REWARDMONEY") t.rewardMoney = static_cast<int32_t>(ToI(v));
        else if (c == "REWARDBONUSMONEY") t.rewardBonusMoney = ToU32(v);
        else if (c == "REWARDDISPLAYSPELL") t.rewardDisplaySpell = ToU32(v);
        else if (c == "REWARDSPELL") t.rewardSpell = static_cast<int32_t>(ToI(v));
        else if (c == "REWARDHONOR") t.rewardHonor = static_cast<int32_t>(ToI(v));
        else if (c == "REWARDKILLHONOR") t.rewardKillHonor = ToF(v);
        else if (c == "STARTITEM") t.startItem = ToU32(v);
        else if (c == "FLAGS") t.flags = ToU32(v);
        else if (c == "REQUIREDPLAYERKILLS") t.requiredPlayerKills = static_cast<uint8_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "REWARDITEM")) >= 1 && idx <= 4) t.rewardItemId[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "REWARDAMOUNT")) >= 1 && idx <= 4) t.rewardAmount[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "ITEMDROPQUANTITY")) >= 1 && idx <= 4) t.itemDropQuantity[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "ITEMDROP")) >= 1 && idx <= 4) t.itemDrop[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "REWARDCHOICEITEMID")) >= 1 && idx <= 6) t.rewardChoiceItemId[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "REWARDCHOICEITEMQUANTITY")) >= 1 && idx <= 6) t.rewardChoiceItemQuantity[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if (c == "POICONTINENT") t.poiContinent = static_cast<uint16_t>(ToU(v));
        else if (c == "POIX") t.poiX = ToF(v);
        else if (c == "POIY") t.poiY = ToF(v);
        else if (c == "POIPRIORITY") t.poiPriority = ToU32(v);
        else if (c == "REWARDTITLE") t.rewardTitle = static_cast<uint8_t>(ToU(v));
        else if (c == "REWARDTALENTS") t.rewardTalents = static_cast<uint8_t>(ToU(v));
        else if (c == "REWARDARENAPOINTS") t.rewardArenaPoints = static_cast<uint16_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "REWARDFACTIONID")) >= 1 && idx <= 5) t.rewardFactionId[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "REWARDFACTIONVALUE")) >= 1 && idx <= 5) t.rewardFactionValue[idx - 1] = static_cast<int32_t>(ToI(v));
        else if ((idx = IndexSuffix(c, "REWARDFACTIONOVERRIDE")) >= 1 && idx <= 5) t.rewardFactionOverride[idx - 1] = static_cast<int32_t>(ToI(v));
        else if (c == "TIMEALLOWED") t.timeAllowed = ToU32(v);
        else if (c == "ALLOWABLERACES") t.allowableRaces = ToU32(v);
        else if (c == "LOGTITLE") t.logTitle = ToStr(v);
        else if (c == "LOGDESCRIPTION") t.logDescription = ToStr(v);
        else if (c == "QUESTDESCRIPTION") t.questDescription = ToStr(v);
        else if (c == "AREADESCRIPTION") t.areaDescription = ToStr(v);
        else if (c == "QUESTCOMPLETIONLOG") t.questCompletionLog = ToStr(v);
        else if ((idx = IndexSuffix(c, "REQUIREDNPCORGOCOUNT")) >= 1 && idx <= 4) t.requiredNpcOrGoCount[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if ((idx = IndexSuffix(c, "REQUIREDNPCORGO")) >= 1 && idx <= 4) t.requiredNpcOrGo[idx - 1] = static_cast<int32_t>(ToI(v));
        else if ((idx = IndexSuffix(c, "REQUIREDITEMID")) >= 1 && idx <= 6) t.requiredItemId[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "REQUIREDITEMCOUNT")) >= 1 && idx <= 6) t.requiredItemCount[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if (c == "REWARDFACTIONFLAGS" || c == "UNKNOWN0") t.rewardFactionFlags = ToU32(v);
        else if ((idx = IndexSuffix(c, "OBJECTIVETEXT")) >= 1 && idx <= 4) t.objectiveText[idx - 1] = ToStr(v);
        else if (c == "VERIFIEDBUILD") t.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_template: unknown column " + c);
    }
}

void ApplyAddon(QuestTemplateAddon& a, const std::vector<std::string>& cols,
                const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "ID") a.id = ToU32(v);
        else if (c == "MAXLEVEL") a.maxLevel = static_cast<uint8_t>(ToU(v));
        else if (c == "ALLOWABLECLASSES") a.allowableClasses = ToU32(v);
        else if (c == "SOURCESPELLID") a.sourceSpellID = ToU32(v);
        else if (c == "PREVQUESTID") a.prevQuestID = static_cast<int32_t>(ToI(v));
        else if (c == "NEXTQUESTID") a.nextQuestID = ToU32(v);
        else if (c == "EXCLUSIVEGROUP") a.exclusiveGroup = static_cast<int32_t>(ToI(v));
        else if (c == "BREADCRUMBFORQUESTID") a.breadcrumbForQuestId = static_cast<int32_t>(ToI(v));
        else if (c == "REWARDMAILTEMPLATEID") a.rewardMailTemplateID = ToU32(v);
        else if (c == "REWARDMAILDELAY") a.rewardMailDelay = ToU32(v);
        else if (c == "REQUIREDSKILLID") a.requiredSkillID = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDSKILLPOINTS") a.requiredSkillPoints = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDMINREPFACTION") a.requiredMinRepFaction = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDMAXREPFACTION") a.requiredMaxRepFaction = static_cast<uint16_t>(ToU(v));
        else if (c == "REQUIREDMINREPVALUE") a.requiredMinRepValue = static_cast<int32_t>(ToI(v));
        else if (c == "REQUIREDMAXREPVALUE") a.requiredMaxRepValue = static_cast<int32_t>(ToI(v));
        else if (c == "PROVIDEDITEMCOUNT") a.providedItemCount = static_cast<uint8_t>(ToU(v));
        else if (c == "SPECIALFLAGS") a.specialFlags = static_cast<uint8_t>(ToU(v));
        else warn.push_back("quest_template_addon: unknown column " + c);
    }
}

void ApplyOfferReward(QuestOfferReward& o, const std::vector<std::string>& cols,
                      const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        int idx;
        if (c == "ID") o.id = ToU32(v);
        else if ((idx = IndexSuffix(c, "EMOTEDELAY")) >= 1 && idx <= 4) o.emoteDelay[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "EMOTE")) >= 1 && idx <= 4) o.emote[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if (c == "REWARDTEXT") o.rewardText = ToStr(v);
        else if (c == "VERIFIEDBUILD") o.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_offer_reward: unknown column " + c);
    }
}

void ApplyRequestItems(QuestRequestItems& r, const std::vector<std::string>& cols,
                       const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "ID") r.id = ToU32(v);
        else if (c == "EMOTEONCOMPLETE") r.emoteOnComplete = static_cast<uint16_t>(ToU(v));
        else if (c == "EMOTEONINCOMPLETE") r.emoteOnIncomplete = static_cast<uint16_t>(ToU(v));
        else if (c == "COMPLETIONTEXT") r.completionText = ToStr(v);
        else if (c == "VERIFIEDBUILD") r.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_request_items: unknown column " + c);
    }
}

void ApplyDetails(QuestDetails& d, const std::vector<std::string>& cols,
                  const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        int idx;
        if (c == "ID") d.id = ToU32(v);
        else if ((idx = IndexSuffix(c, "EMOTEDELAY")) >= 1 && idx <= 4) d.emoteDelay[idx - 1] = ToU32(v);
        else if ((idx = IndexSuffix(c, "EMOTE")) >= 1 && idx <= 4) d.emote[idx - 1] = static_cast<uint16_t>(ToU(v));
        else if (c == "VERIFIEDBUILD") d.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_details: unknown column " + c);
    }
}

void ApplyMailSender(QuestMailSender& m, const std::vector<std::string>& cols,
                     const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "QUESTID") m.questId = ToU32(v);
        else if (c == "REWARDMAILSENDERENTRY") m.rewardMailSenderEntry = ToU32(v);
        else warn.push_back("quest_mail_sender: unknown column " + c);
    }
}

void ApplyGreeting(QuestGreeting& g, const std::vector<std::string>& cols,
                   const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "ID") g.id = ToU32(v);
        else if (c == "TYPE") g.type = static_cast<uint8_t>(ToU(v));
        else if (c == "GREETEMOTETYPE") g.greetEmoteType = static_cast<uint16_t>(ToU(v));
        else if (c == "GREETEMOTEDELAY") g.greetEmoteDelay = ToU32(v);
        else if (c == "GREETING") g.greeting = ToStr(v);
        else if (c == "VERIFIEDBUILD") g.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_greeting: unknown column " + c);
    }
}

void ApplyPoi(QuestPoi& p, const std::vector<std::string>& cols, const std::vector<Value>& row,
              std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "QUESTID") p.questID = ToU32(v);
        else if (c == "ID") p.id = ToU32(v);
        else if (c == "OBJECTIVEINDEX") p.objectiveIndex = static_cast<int32_t>(ToI(v));
        else if (c == "MAPID") p.mapID = ToU32(v);
        else if (c == "WORLDMAPAREAID") p.worldMapAreaId = ToU32(v);
        else if (c == "FLOOR") p.floor = ToU32(v);
        else if (c == "PRIORITY") p.priority = ToU32(v);
        else if (c == "FLAGS") p.flags = ToU32(v);
        else if (c == "VERIFIEDBUILD") p.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_poi: unknown column " + c);
    }
}

void ApplyPoiPoint(QuestPoiPoint& pt, const std::vector<std::string>& cols,
                   const std::vector<Value>& row, std::vector<std::string>& warn)
{
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        if (c == "QUESTID") pt.questID = ToU32(v);
        else if (c == "IDX1") pt.idx1 = ToU32(v);
        else if (c == "IDX2") pt.idx2 = ToU32(v);
        else if (c == "X") pt.x = static_cast<int32_t>(ToI(v));
        else if (c == "Y") pt.y = static_cast<int32_t>(ToI(v));
        else if (c == "VERIFIEDBUILD") pt.verifiedBuild = static_cast<int32_t>(ToI(v));
        else warn.push_back("quest_poi_points: unknown column " + c);
    }
}

QuestLocale& LocaleFor(Quest& q, const std::string& code)
{
    auto it = q.locales.find(code);
    if (it == q.locales.end())
    {
        QuestLocale ql;
        ql.locale = code;
        it = q.locales.emplace(code, std::move(ql)).first;
    }
    return it->second;
}

void ApplyTemplateLocale(Quest& q, const std::vector<std::string>& cols,
                         const std::vector<Value>& row, std::vector<std::string>& warn)
{
    const Value* lc = FindCol(cols, row, "LOCALE");
    if (!lc)
    {
        warn.push_back("quest_template_locale: missing locale column");
        return;
    }
    QuestLocale& l = LocaleFor(q, ToStr(*lc));
    for (size_t i = 0; i < cols.size(); ++i)
    {
        const std::string& c = cols[i];
        Value v = i < row.size() ? row[i] : Value{};
        int idx;
        if (c == "ID" || c == "LOCALE" || c == "VERIFIEDBUILD") continue;
        else if (c == "TITLE") l.title = ToStr(v);
        else if (c == "DETAILS") l.details = ToStr(v);
        else if (c == "OBJECTIVES") l.objectives = ToStr(v);
        else if (c == "ENDTEXT") l.endText = ToStr(v);
        else if (c == "COMPLETEDTEXT") l.completedText = ToStr(v);
        else if ((idx = IndexSuffix(c, "OBJECTIVETEXT")) >= 1 && idx <= 4) l.objectiveText[idx - 1] = ToStr(v);
        else warn.push_back("quest_template_locale: unknown column " + c);
    }
    l.templatePresent = true;
}

void ApplyOfferRewardLocale(Quest& q, const std::vector<std::string>& cols,
                            const std::vector<Value>& row, std::vector<std::string>& warn)
{
    const Value* lc = FindCol(cols, row, "LOCALE");
    if (!lc)
    {
        warn.push_back("quest_offer_reward_locale: missing locale column");
        return;
    }
    QuestLocale& l = LocaleFor(q, ToStr(*lc));
    const Value* rt = FindCol(cols, row, "REWARDTEXT");
    if (rt)
        l.rewardText = ToStr(*rt);
    l.offerRewardPresent = true;
}

void ApplyRequestItemsLocale(Quest& q, const std::vector<std::string>& cols,
                             const std::vector<Value>& row, std::vector<std::string>& warn)
{
    const Value* lc = FindCol(cols, row, "LOCALE");
    if (!lc)
    {
        warn.push_back("quest_request_items_locale: missing locale column");
        return;
    }
    QuestLocale& l = LocaleFor(q, ToStr(*lc));
    const Value* ct = FindCol(cols, row, "COMPLETIONTEXT");
    if (ct)
        l.completionText = ToStr(*ct);
    l.requestItemsPresent = true;
}

void ApplyGreetingLocale(Quest& q, const std::vector<std::string>& cols,
                         const std::vector<Value>& row, std::vector<std::string>& warn)
{
    const Value* lc = FindCol(cols, row, "LOCALE");
    if (!lc)
    {
        warn.push_back("quest_greeting_locale: missing locale column");
        return;
    }
    QuestLocale& l = LocaleFor(q, ToStr(*lc));
    const Value* tv = FindCol(cols, row, "TYPE");
    const Value* gv = FindCol(cols, row, "GREETING");
    uint8_t type = tv ? static_cast<uint8_t>(ToU(*tv)) : 0;
    std::string text = gv ? ToStr(*gv) : std::string();
    if (type == 0)
    {
        l.greetingCreature = text;
        l.greetingCreaturePresent = true;
    }
    else
    {
        l.greetingGameObject = text;
        l.greetingGameObjectPresent = true;
    }
}

// Returns the column name (UPPERCASE) that carries the quest id for `table`, or
// nullptr when the table is not one we import.
const char* KeyColumnFor(const std::string& table)
{
    if (table == "quest_template") return "ID";
    if (table == "quest_template_addon") return "ID";
    if (table == "quest_offer_reward") return "ID";
    if (table == "quest_request_items") return "ID";
    if (table == "quest_details") return "ID";
    if (table == "quest_mail_sender") return "QUESTID";
    if (table == "quest_greeting") return "ID";
    if (table == "creature_queststarter") return "QUEST";
    if (table == "creature_questender") return "QUEST";
    if (table == "gameobject_queststarter") return "QUEST";
    if (table == "gameobject_questender") return "QUEST";
    if (table == "quest_poi") return "QUESTID";
    if (table == "quest_poi_points") return "QUESTID";
    if (table == "quest_template_locale") return "ID";
    if (table == "quest_offer_reward_locale") return "ID";
    if (table == "quest_request_items_locale") return "ID";
    if (table == "quest_greeting_locale") return "ID";
    return nullptr;
}

void PushLink(std::vector<uint32_t>& dst, const std::vector<std::string>& cols,
              const std::vector<Value>& row)
{
    const Value* idv = FindCol(cols, row, "ID");
    if (idv)
        dst.push_back(ToU32(*idv));
}

// Dispatches one matched row to the correct sub-struct of `q`.
void ApplyRow(Quest& q, const std::string& table, const std::vector<std::string>& cols,
              const std::vector<Value>& row, std::vector<std::string>& warn,
              bool& templateFound, std::vector<QuestPoiPoint>& pendingPoints)
{
    if (table == "quest_template")
    {
        ApplyTemplate(q.tmpl, cols, row, warn);
        templateFound = true;
    }
    else if (table == "quest_template_addon")
    {
        ApplyAddon(q.addon, cols, row, warn);
        q.addon.present = true;
    }
    else if (table == "quest_offer_reward")
    {
        ApplyOfferReward(q.offerReward, cols, row, warn);
        q.offerReward.present = true;
    }
    else if (table == "quest_request_items")
    {
        ApplyRequestItems(q.requestItems, cols, row, warn);
        q.requestItems.present = true;
    }
    else if (table == "quest_details")
    {
        ApplyDetails(q.details, cols, row, warn);
        q.details.present = true;
    }
    else if (table == "quest_mail_sender")
    {
        ApplyMailSender(q.mailSender, cols, row, warn);
        q.mailSender.present = true;
    }
    else if (table == "quest_greeting")
    {
        QuestGreeting g;
        ApplyGreeting(g, cols, row, warn);
        q.greetings.push_back(std::move(g));
    }
    else if (table == "creature_queststarter")
        PushLink(q.creatureStarters, cols, row);
    else if (table == "creature_questender")
        PushLink(q.creatureEnders, cols, row);
    else if (table == "gameobject_queststarter")
        PushLink(q.goStarters, cols, row);
    else if (table == "gameobject_questender")
        PushLink(q.goEnders, cols, row);
    else if (table == "quest_poi")
    {
        QuestPoi p;
        ApplyPoi(p, cols, row, warn);
        q.pois.push_back(std::move(p));
    }
    else if (table == "quest_poi_points")
    {
        QuestPoiPoint pt;
        ApplyPoiPoint(pt, cols, row, warn);
        pendingPoints.push_back(pt);
    }
    else if (table == "quest_template_locale")
        ApplyTemplateLocale(q, cols, row, warn);
    else if (table == "quest_offer_reward_locale")
        ApplyOfferRewardLocale(q, cols, row, warn);
    else if (table == "quest_request_items_locale")
        ApplyRequestItemsLocale(q, cols, row, warn);
    else if (table == "quest_greeting_locale")
        ApplyGreetingLocale(q, cols, row, warn);
}
} // namespace

// ---------------------------------------------------------------------------
ImportResult QuestSqlImporter::ImportFromSql(const std::string& sqlText, uint32_t onlyQuestId) const
{
    ImportResult result;
    try
    {
        std::vector<std::string> stmts = SplitStatements(sqlText);
        std::vector<ParsedStmt> parsed;
        for (const std::string& s : stmts)
        {
            ParsedStmt ps;
            if (ParseInsert(s, ps))
                parsed.push_back(std::move(ps));
        }

        // Choose the target quest id.
        uint32_t target = onlyQuestId;
        if (target == 0)
        {
            for (const ParsedStmt& ps : parsed)
            {
                if (ps.table == "quest_template" && !ps.tuples.empty())
                {
                    const Value* idv = FindCol(ps.cols, ps.tuples[0], "ID");
                    if (idv)
                    {
                        target = ToU32(*idv);
                        break;
                    }
                }
            }
        }
        if (target == 0)
        {
            result.error = "no quest_template INSERT/REPLACE statement found in SQL";
            return result;
        }

        Quest& q = result.quest;
        bool templateFound = false;
        std::vector<QuestPoiPoint> pendingPoints;

        for (const ParsedStmt& ps : parsed)
        {
            const char* keyCol = KeyColumnFor(ps.table);
            if (!keyCol)
            {
                result.warnings.push_back("unsupported table skipped: " + ps.table);
                continue;
            }
            for (const std::vector<Value>& row : ps.tuples)
            {
                const Value* kv = FindCol(ps.cols, row, keyCol);
                if (!kv)
                    continue; // cannot determine which quest this row belongs to
                if (ToU32(*kv) != target)
                    continue;
                ApplyRow(q, ps.table, ps.cols, row, result.warnings, templateFound, pendingPoints);
            }
        }

        if (!templateFound)
        {
            result.error =
                "quest_template row for ID " + std::to_string(target) + " not found in SQL";
            result.quest = Quest{};
            return result;
        }

        // Attach POI points to their owning POI header (Idx1 == poi id).
        for (const QuestPoiPoint& pt : pendingPoints)
        {
            bool attached = false;
            for (QuestPoi& p : q.pois)
            {
                if (p.id == pt.idx1)
                {
                    p.points.push_back(pt);
                    attached = true;
                    break;
                }
            }
            if (!attached)
                result.warnings.push_back("quest_poi_points: no matching quest_poi for Idx1 " +
                                          std::to_string(pt.idx1));
        }

        q.tmpl.id = target;
        q.ClearDirty();
        q.isNew = false;
        result.ok = true;
    }
    catch (const std::exception& e)
    {
        result.ok = false;
        result.error = std::string("exception during import: ") + e.what();
    }
    catch (...)
    {
        result.ok = false;
        result.error = "unknown exception during import";
    }
    return result;
}
} // namespace qe
