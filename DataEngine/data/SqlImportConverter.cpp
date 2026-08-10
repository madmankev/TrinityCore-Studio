#include "data/SqlImportConverter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/CoreSupport.h"
#include "data/DbIntrospect.h"
#include "db/IDatabase.h"

namespace we
{
namespace
{
struct ParsedInsert
{
    std::string verb; // INSERT or REPLACE
    std::string table;
    bool explicitColumns = false;
    bool trailingClause = false;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> tuples;
};

struct ParsedUpdate
{
    std::string table;
    std::string assignments;
    std::string where;
};

struct ParsedDelete
{
    std::string table;
    std::string where;
};

struct TargetTable
{
    std::string name;
    std::unordered_map<std::string, std::string> columns; // lower -> actual spelling
    std::set<std::string> primaryKeys;                     // lower
};

char LowerChar(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

std::string Lower(std::string value)
{
    for (char& c : value)
        c = LowerChar(c);
    return value;
}

bool IsSpace(char value)
{
    return std::isspace(static_cast<unsigned char>(value)) != 0;
}

void TrimInPlace(std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos)
    {
        value.clear();
        return;
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n\f\v");
    value = value.substr(first, last - first + 1);
}

std::string Trim(std::string value)
{
    TrimInPlace(value);
    return value;
}

std::size_t SkipWs(const std::string& text, std::size_t at)
{
    while (at < text.size() && IsSpace(text[at]))
        ++at;
    return at;
}

bool IsIdentifierChar(char value)
{
    return std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_' || value == '$';
}

std::string StripQuotes(std::string value)
{
    value = Trim(std::move(value));
    if (value.size() >= 2 && ((value.front() == '`' && value.back() == '`') ||
                              (value.front() == '"' && value.back() == '"')))
        value = value.substr(1, value.size() - 2);
    return value;
}

// Read one SQL identifier component, accepting a backtick/double-quoted name or
// an ordinary [A-Za-z0-9_$] sequence. `at` advances past the component.
bool ReadIdentifierPart(const std::string& text, std::size_t& at, std::string& out)
{
    at = SkipWs(text, at);
    if (at >= text.size())
        return false;
    out.clear();
    const char quote = text[at] == '`' || text[at] == '"' ? text[at] : '\0';
    if (quote != '\0')
    {
        ++at;
        while (at < text.size())
        {
            if (text[at] == quote)
            {
                // MySQL escapes a backtick inside a backtick identifier by doubling it.
                if (at + 1 < text.size() && text[at + 1] == quote)
                {
                    out += quote;
                    at += 2;
                    continue;
                }
                ++at;
                return true;
            }
            out += text[at++];
        }
        return false;
    }
    while (at < text.size() && IsIdentifierChar(text[at]))
        out += text[at++];
    return !out.empty();
}

// Read `database`.`table` and retain the final component. SQL dumps often carry
// a schema qualifier that must not be replayed into the target database.
bool ReadQualifiedIdentifier(const std::string& text, std::size_t& at, std::string& out)
{
    std::string part;
    if (!ReadIdentifierPart(text, at, part))
        return false;
    out = part;
    while (true)
    {
        const std::size_t saved = at;
        at = SkipWs(text, at);
        if (at >= text.size() || text[at] != '.')
        {
            at = saved;
            break;
        }
        ++at;
        if (!ReadIdentifierPart(text, at, part))
            return false;
        out = part;
    }
    return true;
}

std::string ReadWord(const std::string& text, std::size_t& at)
{
    at = SkipWs(text, at);
    const std::size_t begin = at;
    while (at < text.size() && std::isalpha(static_cast<unsigned char>(text[at])) != 0)
        ++at;
    return Lower(text.substr(begin, at - begin));
}

// Return a balanced parenthesized payload and advance after its closing parenthesis.
// Quotes and nested function calls inside SQL values are retained verbatim.
bool ConsumeBalanced(const std::string& text, std::size_t& at, std::string& payload)
{
    at = SkipWs(text, at);
    if (at >= text.size() || text[at] != '(')
        return false;
    ++at;
    const std::size_t begin = at;
    int depth = 1;
    char quote = '\0';
    while (at < text.size())
    {
        const char c = text[at];
        if (quote != '\0')
        {
            if (c == '\\' && at + 1 < text.size())
            {
                at += 2;
                continue;
            }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < text.size() && text[at + 1] == '\'')
                {
                    at += 2;
                    continue;
                }
                quote = '\0';
            }
            ++at;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`')
        {
            quote = c;
            ++at;
            continue;
        }
        if (c == '(')
            ++depth;
        else if (c == ')')
        {
            --depth;
            if (depth == 0)
            {
                payload = text.substr(begin, at - begin);
                ++at;
                return true;
            }
        }
        ++at;
    }
    return false;
}

std::vector<std::string> SplitTopLevelComma(const std::string& text)
{
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    char quote = '\0';
    for (std::size_t at = 0; at < text.size(); ++at)
    {
        const char c = text[at];
        if (quote != '\0')
        {
            current += c;
            if (c == '\\' && at + 1 < text.size())
            {
                current += text[++at];
                continue;
            }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < text.size() && text[at + 1] == '\'')
                {
                    current += text[++at];
                    continue;
                }
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"' || c == '`')
        {
            quote = c;
            current += c;
            continue;
        }
        if (c == '(')
            ++depth;
        else if (c == ')' && depth > 0)
            --depth;
        if (c == ',' && depth == 0)
        {
            out.push_back(Trim(std::move(current)));
            current.clear();
            continue;
        }
        current += c;
    }
    out.push_back(Trim(std::move(current)));
    return out;
}

std::size_t FindTopLevelKeyword(const std::string& text, const std::string& keyword, std::size_t begin = 0)
{
    int depth = 0;
    char quote = '\0';
    const std::string lowerKeyword = Lower(keyword);
    for (std::size_t at = begin; at < text.size(); ++at)
    {
        const char c = text[at];
        if (quote != '\0')
        {
            if (c == '\\' && at + 1 < text.size()) { ++at; continue; }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < text.size() && text[at + 1] == '\'') { ++at; continue; }
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')' && depth > 0) { --depth; continue; }
        if (depth != 0 || at + lowerKeyword.size() > text.size())
            continue;
        const bool left = at == 0 || !IsIdentifierChar(text[at - 1]);
        const bool right = at + lowerKeyword.size() == text.size() || !IsIdentifierChar(text[at + lowerKeyword.size()]);
        if (left && right && Lower(text.substr(at, lowerKeyword.size())) == lowerKeyword)
            return at;
    }
    return std::string::npos;
}

std::size_t FindTopLevelEquals(const std::string& text)
{
    int depth = 0;
    char quote = '\0';
    for (std::size_t at = 0; at < text.size(); ++at)
    {
        const char c = text[at];
        if (quote != '\0')
        {
            if (c == '\\' && at + 1 < text.size()) { ++at; continue; }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < text.size() && text[at + 1] == '\'') { ++at; continue; }
                quote = '\0';
            }
            continue;
        }
        if (c == '\'' || c == '"' || c == '`') { quote = c; continue; }
        if (c == '(') { ++depth; continue; }
        if (c == ')' && depth > 0) { --depth; continue; }
        if (depth == 0 && c == '=' && (at == 0 || (text[at - 1] != '<' && text[at - 1] != '>' && text[at - 1] != '!' && text[at - 1] != ':')))
            return at;
    }
    return std::string::npos;
}

bool ParseUpdate(const std::string& statement, ParsedUpdate& out)
{
    std::size_t at = 0;
    if (ReadWord(statement, at) != "update")
        return false;
    out = {};
    if (!ReadQualifiedIdentifier(statement, at, out.table))
        return false;
    out.table = Lower(out.table);
    if (ReadWord(statement, at) != "set")
        return false;
    const std::size_t whereAt = FindTopLevelKeyword(statement, "where", at);
    if (whereAt == std::string::npos)
    {
        out.assignments = Trim(statement.substr(at));
        return !out.assignments.empty();
    }
    out.assignments = Trim(statement.substr(at, whereAt - at));
    out.where = Trim(statement.substr(whereAt + 5));
    return !out.assignments.empty() && !out.where.empty();
}

bool ParseDelete(const std::string& statement, ParsedDelete& out)
{
    std::size_t at = 0;
    if (ReadWord(statement, at) != "delete" || ReadWord(statement, at) != "from")
        return false;
    out = {};
    if (!ReadQualifiedIdentifier(statement, at, out.table))
        return false;
    out.table = Lower(out.table);
    const std::size_t whereAt = FindTopLevelKeyword(statement, "where", at);
    if (whereAt == std::string::npos)
        return false;
    out.where = Trim(statement.substr(whereAt + 5));
    return !out.where.empty();
}

std::vector<std::string> SplitStatements(const std::string& text)
{
    std::vector<std::string> statements;
    std::string current;
    char quote = '\0';
    for (std::size_t at = 0; at < text.size();)
    {
        const char c = text[at];
        if (quote != '\0')
        {
            current += c;
            if (c == '\\' && at + 1 < text.size())
            {
                current += text[at + 1];
                at += 2;
                continue;
            }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < text.size() && text[at + 1] == '\'')
                {
                    current += text[at + 1];
                    at += 2;
                    continue;
                }
                quote = '\0';
            }
            ++at;
            continue;
        }
        if (c == '\'' || c == '"' || c == '`')
        {
            quote = c;
            current += c;
            ++at;
            continue;
        }
        if (c == '-' && at + 1 < text.size() && text[at + 1] == '-')
        {
            at += 2;
            while (at < text.size() && text[at] != '\n')
                ++at;
            continue;
        }
        if (c == '#')
        {
            ++at;
            while (at < text.size() && text[at] != '\n')
                ++at;
            continue;
        }
        if (c == '/' && at + 1 < text.size() && text[at + 1] == '*')
        {
            at += 2;
            while (at + 1 < text.size() && !(text[at] == '*' && text[at + 1] == '/'))
                ++at;
            if (at + 1 < text.size())
                at += 2;
            continue;
        }
        if (c == ';')
        {
            std::string statement = Trim(std::move(current));
            if (!statement.empty())
                statements.push_back(std::move(statement));
            current.clear();
            ++at;
            continue;
        }
        current += c;
        ++at;
    }
    std::string statement = Trim(std::move(current));
    if (!statement.empty())
        statements.push_back(std::move(statement));
    return statements;
}

bool ParseInsert(const std::string& statement, ParsedInsert& out)
{
    std::size_t at = 0;
    const std::string first = ReadWord(statement, at);
    if (first != "insert" && first != "replace")
        return false;
    out = {};
    out.verb = first == "replace" ? "REPLACE" : "INSERT";

    // Optional INSERT modifiers.
    for (;;)
    {
        const std::size_t saved = at;
        const std::string word = ReadWord(statement, at);
        if (word == "ignore" || word == "low_priority" || word == "delayed" || word == "high_priority")
            continue;
        if (word == "into")
            break;
        at = saved;
        // MySQL allows REPLACE table (...) VALUES without an INTO keyword.
        if (first == "replace")
            break;
        return false;
    }

    if (!ReadQualifiedIdentifier(statement, at, out.table))
        return false;
    out.table = Lower(out.table);
    at = SkipWs(statement, at);
    if (at >= statement.size() || statement[at] != '(')
        return true; // parsed as an insert, but no safe column mapping is possible
    out.explicitColumns = true;
    std::string colsPayload;
    if (!ConsumeBalanced(statement, at, colsPayload))
        return false;
    for (std::string column : SplitTopLevelComma(colsPayload))
    {
        column = StripQuotes(std::move(column));
        if (column.empty())
            return false;
        out.columns.push_back(Lower(column));
    }
    const std::string values = ReadWord(statement, at);
    if (values != "values" && values != "value")
        return false;

    while (true)
    {
        at = SkipWs(statement, at);
        if (at >= statement.size() || statement[at] != '(')
            break;
        std::string tuplePayload;
        if (!ConsumeBalanced(statement, at, tuplePayload))
            return false;
        out.tuples.push_back(SplitTopLevelComma(tuplePayload));
        at = SkipWs(statement, at);
        if (at >= statement.size() || statement[at] != ',')
            break;
        ++at;
        const std::size_t afterComma = SkipWs(statement, at);
        if (afterComma >= statement.size() || statement[afterComma] != '(')
        {
            // A trailing ON DUPLICATE KEY clause is normalized by the converter.
            out.trailingClause = true;
            break;
        }
    }
    at = SkipWs(statement, at);
    if (at < statement.size())
        out.trailingClause = true;
    return !out.tuples.empty();
}

std::string Quote(const std::string& identifier)
{
    std::string result = "`";
    for (char c : identifier)
    {
        result += c;
        if (c == '`')
            result += '`';
    }
    return result + '`';
}

bool TryUnsigned(const std::string& raw, uint32_t& out)
{
    std::string value = Trim(raw);
    if (value.empty() || value.front() == '\'' || value.front() == '"')
        return false;
    std::size_t consumed = 0;
    try
    {
        const unsigned long long number = std::stoull(value, &consumed, 0);
        if (consumed != value.size() || number > 0xffffffffULL)
            return false;
        out = static_cast<uint32_t>(number);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

const std::string* FindValue(const ParsedInsert& statement, const std::vector<std::string>& tuple,
                             const char* column)
{
    const std::string wanted = Lower(column);
    for (std::size_t index = 0; index < statement.columns.size() && index < tuple.size(); ++index)
        if (statement.columns[index] == wanted)
            return &tuple[index];
    return nullptr;
}

bool HasColumn(const TargetTable& table, const char* name)
{
    return table.columns.find(Lower(name)) != table.columns.end();
}

const std::string* ActualColumn(const TargetTable& table, const char* name)
{
    const auto found = table.columns.find(Lower(name));
    return found == table.columns.end() ? nullptr : &found->second;
}

void AddIssue(SqlImportPlan& plan, SqlImportSeverity severity, std::size_t statement,
              const std::string& message)
{
    plan.issues.push_back({severity, statement, message});
}

std::string AliasTable(const std::string& source, const std::unordered_map<std::string, TargetTable>& tables)
{
    if (tables.find(source) != tables.end())
        return source;
    static const std::map<std::string, std::vector<std::string>> aliases = {
        {"creature_formation", {"creature_formations"}},
        {"creature_formations", {"creature_formation"}},
        {"gameobject_quest_start", {"gameobject_queststarter"}},
        {"gameobject_quest_end", {"gameobject_questender"}},
        {"creature_quest_start", {"creature_queststarter"}},
        {"creature_quest_end", {"creature_questender"}},
    };
    const auto found = aliases.find(source);
    if (found != aliases.end())
        for (const std::string& candidate : found->second)
            if (tables.find(candidate) != tables.end())
                return candidate;
    return {};
}

std::string MapColumn(const std::string& sourceTable, const std::string& targetTable,
                      const std::string& sourceColumn, const TargetTable& target)
{
    const std::string column = Lower(sourceColumn);
    const auto exact = target.columns.find(column);
    if (exact != target.columns.end())
        return exact->second;

    // Current AzerothCore stores the template entry in id1/id2/id3 while legacy
    // TrinityCore stores only id. These are the same first spawn-template slot.
    if ((sourceTable == "creature" || sourceTable == "gameobject") &&
        (targetTable == "creature" || targetTable == "gameobject"))
    {
        if (column == "id" && HasColumn(target, "id1"))
            return *ActualColumn(target, "id1");
        if (column == "id1" && HasColumn(target, "id"))
            return *ActualColumn(target, "id");
    }

    // Quest schemas have historically alternated between Title and LogTitle.
    if (sourceTable == "quest_template")
    {
        if (column == "title" && HasColumn(target, "logtitle"))
            return *ActualColumn(target, "logtitle");
        if (column == "logtitle" && HasColumn(target, "title"))
            return *ActualColumn(target, "title");
    }

    // Custom world packs use each of these aliases for a per-spawn display id.
    if (sourceTable == "creature")
    {
        if ((column == "modelid" || column == "displayid" || column == "display_id"))
        {
            for (const char* candidate : {"modelid", "displayid", "display_id"})
                if (HasColumn(target, candidate))
                    return *ActualColumn(target, candidate);
        }
    }
    return {};
}

std::string RewritePredicateIdentifiers(const std::string& expression, const std::string& sourceTable,
                                        const std::string& targetTable, const TargetTable& target,
                                        bool& hadUnknownColumn)
{
    std::string result;
    char quote = '\0';
    for (std::size_t at = 0; at < expression.size();)
    {
        const char c = expression[at];
        if (quote != '\0')
        {
            result += c;
            if (c == '\\' && at + 1 < expression.size())
            {
                result += expression[at + 1];
                at += 2;
                continue;
            }
            if (c == quote)
            {
                if (quote == '\'' && at + 1 < expression.size() && expression[at + 1] == '\'')
                {
                    result += expression[at + 1];
                    at += 2;
                    continue;
                }
                quote = '\0';
            }
            ++at;
            continue;
        }
        if (c == '\'' || c == '"')
        {
            quote = c;
            result += c;
            ++at;
            continue;
        }
        if (c == '`')
        {
            std::size_t end = at + 1;
            std::string name;
            while (end < expression.size())
            {
                if (expression[end] == '`')
                {
                    if (end + 1 < expression.size() && expression[end + 1] == '`')
                    {
                        name += '`';
                        end += 2;
                        continue;
                    }
                    break;
                }
                name += expression[end++];
            }
            if (end >= expression.size())
            {
                result += expression.substr(at);
                break;
            }
            const std::string mapped = MapColumn(sourceTable, targetTable, name, target);
            if (mapped.empty())
            {
                hadUnknownColumn = true;
                result += expression.substr(at, end - at + 1);
            }
            else
                result += Quote(mapped);
            at = end + 1;
            continue;
        }
        if (IsIdentifierChar(c))
        {
            const std::size_t begin = at;
            while (at < expression.size() && IsIdentifierChar(expression[at]))
                ++at;
            const std::string word = expression.substr(begin, at - begin);
            // Avoid rewriting SQL keywords/functions. Bare field aliases are still
            // translated, while generated SQL's preferred backticks are exact.
            static const std::set<std::string> keywords = {
                "and", "or", "not", "null", "is", "in", "between", "like", "regexp",
                "true", "false", "values", "now", "unix_timestamp", "if", "coalesce"};
            const std::string lower = Lower(word);
            const std::string mapped = keywords.count(lower) ? std::string() : MapColumn(sourceTable, targetTable, word, target);
            result += mapped.empty() ? word : Quote(mapped);
            continue;
        }
        result += c;
        ++at;
    }
    return result;
}

bool ConvertUpdate(const ParsedUpdate& source, const TargetTable& target, const std::string& targetKey,
                   SqlImportPlan& plan, std::size_t statementIndex)
{
    const std::vector<std::string> assignments = SplitTopLevelComma(source.assignments);
    std::vector<std::string> converted;
    for (const std::string& assignment : assignments)
    {
        const std::size_t equals = FindTopLevelEquals(assignment);
        if (equals == std::string::npos)
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "UPDATE assignment could not be parsed and was omitted: " + assignment);
            continue;
        }
        std::string lhs = Trim(assignment.substr(0, equals));
        std::size_t at = 0;
        std::string sourceColumn;
        if (!ReadQualifiedIdentifier(lhs, at, sourceColumn) || !Trim(lhs.substr(at)).empty())
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "UPDATE assignment has a non-column left side and was omitted: " + assignment);
            continue;
        }
        const std::string mapped = MapColumn(source.table, targetKey, sourceColumn, target);
        if (mapped.empty())
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "UPDATE column `" + source.table + "." + sourceColumn + "` has no target equivalent and was omitted");
            continue;
        }
        bool unknownInValue = false;
        const std::string rhs = RewritePredicateIdentifiers(Trim(assignment.substr(equals + 1)),
                                                             source.table, targetKey, target, unknownInValue);
        converted.push_back(Quote(mapped) + " = " + rhs);
        if (unknownInValue)
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "UPDATE value expression retains an unmapped quoted identifier; review converted SQL");
    }
    if (converted.empty())
        return false;
    bool unknownInWhere = false;
    const std::string where = RewritePredicateIdentifiers(source.where, source.table, targetKey, target, unknownInWhere);
    if (unknownInWhere)
        AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                 "UPDATE WHERE predicate retains an unmapped quoted identifier; review converted SQL");
    std::ostringstream sql;
    sql << "UPDATE " << Quote(target.name) << " SET ";
    for (std::size_t index = 0; index < converted.size(); ++index)
        sql << (index ? ", " : "") << converted[index];
    sql << " WHERE " << where << ';';
    plan.statements.push_back(sql.str());
    ++plan.convertedStatements;
    ++plan.convertedRows;
    return true;
}

bool ConvertDelete(const ParsedDelete& source, const TargetTable& target, const std::string& targetKey,
                   SqlImportPlan& plan, std::size_t statementIndex)
{
    bool unknownInWhere = false;
    const std::string where = RewritePredicateIdentifiers(source.where, source.table, targetKey, target, unknownInWhere);
    if (unknownInWhere)
        AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                 "DELETE WHERE predicate retains an unmapped quoted identifier; review converted SQL");
    plan.statements.push_back("DELETE FROM " + Quote(target.name) + " WHERE " + where + ';');
    ++plan.convertedStatements;
    ++plan.convertedRows;
    return true;
}

std::string BuildInsertSql(const std::string& verb, const TargetTable& target,
                           const std::vector<std::string>& columns,
                           const std::vector<std::vector<std::string>>& tuples,
                           bool useUpsert)
{
    std::ostringstream sql;
    const bool normalizeUpsert = useUpsert && !target.primaryKeys.empty();
    sql << (normalizeUpsert ? "INSERT" : verb) << " INTO " << Quote(target.name) << " (";
    for (std::size_t index = 0; index < columns.size(); ++index)
        sql << (index ? ", " : "") << Quote(columns[index]);
    sql << ") VALUES ";
    for (std::size_t row = 0; row < tuples.size(); ++row)
    {
        sql << (row ? ", (" : "(");
        for (std::size_t value = 0; value < tuples[row].size(); ++value)
            sql << (value ? ", " : "") << tuples[row][value];
        sql << ')';
    }
    if (normalizeUpsert)
    {
        std::vector<std::string> updates;
        for (const std::string& column : columns)
            if (target.primaryKeys.find(Lower(column)) == target.primaryKeys.end())
                updates.push_back(Quote(column) + " = VALUES(" + Quote(column) + ")");
        if (!updates.empty())
        {
            sql << " ON DUPLICATE KEY UPDATE ";
            for (std::size_t index = 0; index < updates.size(); ++index)
                sql << (index ? ", " : "") << updates[index];
        }
    }
    sql << ';';
    return sql.str();
}

void GenerateAcoreModelRows(const ParsedInsert& source, const TargetTable& targetModels,
                            SqlImportPlan& plan, std::size_t statementIndex,
                            const SqlImportOptions& options)
{
    const std::string* creatureIdCol = ActualColumn(targetModels, "CreatureID");
    const std::string* indexCol = ActualColumn(targetModels, "Idx");
    const std::string* displayCol = ActualColumn(targetModels, "CreatureDisplayID");
    if (!creatureIdCol || !indexCol || !displayCol)
    {
        AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                 "target creature_template_model lacks CreatureID/Idx/CreatureDisplayID; legacy model columns were not converted");
        return;
    }
    const std::string* scaleCol = ActualColumn(targetModels, "DisplayScale");
    const std::string* probabilityCol = ActualColumn(targetModels, "Probability");
    const std::string* verifiedCol = ActualColumn(targetModels, "VerifiedBuild");

    for (const std::vector<std::string>& tuple : source.tuples)
    {
        const std::string* entry = FindValue(source, tuple, "entry");
        if (!entry)
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "creature_template legacy model conversion skipped a row without entry");
            continue;
        }
        for (int slot = 1; slot <= 4; ++slot)
        {
            const std::string sourceColumn = "modelid" + std::to_string(slot);
            const std::string* model = FindValue(source, tuple, sourceColumn.c_str());
            uint32_t modelId = 0;
            if (!model || !TryUnsigned(*model, modelId) || modelId == 0)
                continue;
            std::vector<std::string> columns = {*creatureIdCol, *indexCol, *displayCol};
            std::vector<std::string> values = {*entry, std::to_string(slot - 1), *model};
            if (scaleCol) { columns.push_back(*scaleCol); values.push_back("1"); }
            if (probabilityCol) { columns.push_back(*probabilityCol); values.push_back("1"); }
            if (verifiedCol) { columns.push_back(*verifiedCol); values.push_back("0"); }
            plan.statements.push_back(BuildInsertSql("INSERT", targetModels, columns, {values}, options.useUpsert));
            ++plan.convertedStatements;
            ++plan.convertedRows;
        }
    }
}

void GenerateTrinityModelUpdates(const ParsedInsert& source, const TargetTable& targetTemplate,
                                 SqlImportPlan& plan, std::size_t statementIndex)
{
    const std::string* entryCol = ActualColumn(targetTemplate, "entry");
    if (!entryCol)
        return;
    for (const std::vector<std::string>& tuple : source.tuples)
    {
        const std::string* creatureId = FindValue(source, tuple, "CreatureID");
        const std::string* index = FindValue(source, tuple, "Idx");
        const std::string* display = FindValue(source, tuple, "CreatureDisplayID");
        uint32_t slot = 0;
        if (!creatureId || !index || !display || !TryUnsigned(*index, slot) || slot >= 4)
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "creature_template_model row skipped: CreatureID/Idx/CreatureDisplayID must be explicit numeric values (Idx 0..3)");
            continue;
        }
        const std::string modelColumn = "modelid" + std::to_string(slot + 1);
        const std::string* targetModel = ActualColumn(targetTemplate, modelColumn.c_str());
        if (!targetModel)
            continue;
        plan.statements.push_back("UPDATE " + Quote(targetTemplate.name) + " SET " + Quote(*targetModel) + " = " +
                                  *display + " WHERE " + Quote(*entryCol) + " = " + *creatureId + ";");
        ++plan.convertedStatements;
        ++plan.convertedRows;
        const std::string* scale = FindValue(source, tuple, "DisplayScale");
        const std::string* probability = FindValue(source, tuple, "Probability");
        if (scale || probability)
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex,
                     "creature_template_model DisplayScale/Probability has no exact TrinityCore modelid1..4 equivalent; visual ID only was converted");
    }
}

bool IsLegacyModelColumn(const std::string& column)
{
    return column.size() == 8 && column.rfind("modelid", 0) == 0 &&
           column.back() >= '1' && column.back() <= '4';
}

} // namespace

bool SqlImportPlan::HasErrors() const
{
    return std::any_of(issues.begin(), issues.end(), [](const SqlImportIssue& issue) {
        return issue.severity == SqlImportSeverity::Error;
    });
}

std::string SqlImportPlan::PreviewSql() const
{
    std::ostringstream text;
    for (const std::string& statement : statements)
        text << statement << '\n';
    return text.str();
}

SqlImportPlan SqlImportConverter::Convert(const std::string& sqlText, IDatabase& target,
                                          const SqlImportOptions& options) const
{
    SqlImportPlan plan;
    const std::vector<std::string> targetTables = ListTables(target);
    std::unordered_map<std::string, TargetTable> tables;
    for (const std::string& tableName : targetTables)
    {
        TargetTable table;
        table.name = tableName;
        for (const IntrospectedColumn& column : IntrospectColumns(target, tableName))
        {
            table.columns.emplace(Lower(column.name), column.name);
            if (column.isPk)
                table.primaryKeys.insert(Lower(column.name));
        }
        if (!table.columns.empty())
            tables.emplace(Lower(tableName), std::move(table));
    }
    if (tables.empty())
    {
        AddIssue(plan, SqlImportSeverity::Error, 0,
                 "could not inspect target tables/columns; connect a readable live world database before converting SQL");
        return plan;
    }

    CoreFlavor flavor = options.targetFlavor;
    if (flavor == CoreFlavor::Auto)
        flavor = DetectCoreSchema(target).detected;
    plan.targetFlavor = flavor;
    const std::vector<std::string> sourceStatements = SplitStatements(sqlText);
    plan.sourceStatements = sourceStatements.size();

    const auto targetModelRows = tables.find("creature_template_model");
    const auto targetTemplate = tables.find("creature_template");
    for (std::size_t statementIndex = 0; statementIndex < sourceStatements.size(); ++statementIndex)
    {
        const std::string& sourceSql = sourceStatements[statementIndex];
        std::size_t firstAt = 0;
        const std::string firstWord = ReadWord(sourceSql, firstAt);
        if (firstWord == "update")
        {
            ParsedUpdate update;
            if (!ParseUpdate(sourceSql, update))
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "UPDATE statement could not be parsed safely; statement skipped");
                ++plan.skippedStatements;
                continue;
            }
            const std::string targetKey = AliasTable(update.table, tables);
            if (targetKey.empty())
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "target does not contain an equivalent table for UPDATE `" + update.table + "`; statement skipped");
                ++plan.skippedStatements;
                continue;
            }
            if (!ConvertUpdate(update, tables.at(targetKey), targetKey, plan, statementIndex + 1))
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "no compatible UPDATE assignments remained; statement skipped");
                ++plan.skippedStatements;
            }
            continue;
        }
        if (firstWord == "delete")
        {
            if (!options.includeDeletes)
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "DELETE statement skipped for safety (enable converted DELETE import explicitly to include it)");
                ++plan.skippedStatements;
                continue;
            }
            ParsedDelete deletion;
            if (!ParseDelete(sourceSql, deletion))
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "DELETE without a simple WHERE predicate was skipped for safety");
                ++plan.skippedStatements;
                continue;
            }
            const std::string targetKey = AliasTable(deletion.table, tables);
            if (targetKey.empty())
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "target does not contain an equivalent table for DELETE `" + deletion.table + "`; statement skipped");
                ++plan.skippedStatements;
                continue;
            }
            ConvertDelete(deletion, tables.at(targetKey), targetKey, plan, statementIndex + 1);
            continue;
        }

        ParsedInsert source;
        if (!ParseInsert(sourceSql, source))
        {
            if (!firstWord.empty())
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "unsupported SQL statement skipped (supported: explicit-column INSERT/REPLACE, simple UPDATE, optional DELETE)");
            ++plan.skippedStatements;
            continue;
        }
        if (!source.explicitColumns)
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                     "INSERT/REPLACE without an explicit column list was skipped; positional layouts cannot be converted safely across core revisions");
            ++plan.skippedStatements;
            continue;
        }
        bool tupleMismatch = false;
        for (const auto& tuple : source.tuples)
            if (tuple.size() != source.columns.size())
                tupleMismatch = true;
        if (tupleMismatch)
        {
            AddIssue(plan, SqlImportSeverity::Error, statementIndex + 1,
                     "VALUES tuple count does not match the explicit column list; statement skipped");
            ++plan.skippedStatements;
            continue;
        }

        // AzerothCore creature_template_model -> legacy TrinityCore modelid1..4.
        if (source.table == "creature_template_model" && tables.find(source.table) == tables.end() &&
            targetTemplate != tables.end() && HasColumn(targetTemplate->second, "modelid1"))
        {
            GenerateTrinityModelUpdates(source, targetTemplate->second, plan, statementIndex + 1);
            continue;
        }

        const std::string targetKey = AliasTable(source.table, tables);
        if (targetKey.empty())
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                     "target does not contain an equivalent table for `" + source.table + "`; statement skipped");
            ++plan.skippedStatements;
            continue;
        }
        const TargetTable& targetTable = tables.at(targetKey);
        std::vector<std::string> targetColumns;
        std::vector<std::size_t> sourceIndices;
        std::set<std::string> seenTargetColumns;
        const bool legacyToModelRows = source.table == "creature_template" &&
            targetModelRows != tables.end() && !HasColumn(targetTable, "modelid1");
        for (std::size_t columnIndex = 0; columnIndex < source.columns.size(); ++columnIndex)
        {
            const std::string& sourceColumn = source.columns[columnIndex];
            if (legacyToModelRows && IsLegacyModelColumn(sourceColumn))
                continue; // converted to child rows below
            const std::string mapped = MapColumn(source.table, targetKey, sourceColumn, targetTable);
            if (mapped.empty())
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "column `" + source.table + "." + sourceColumn + "` has no target equivalent and was omitted");
                continue;
            }
            const std::string mappedKey = Lower(mapped);
            if (!seenTargetColumns.insert(mappedKey).second)
            {
                AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                         "multiple source columns map to `" + targetTable.name + "." + mapped + "`; later duplicate was omitted");
                continue;
            }
            targetColumns.push_back(mapped);
            sourceIndices.push_back(columnIndex);
        }

        // TrinityCore's single creature/gameobject id becomes ACore's id1; id2/id3
        // are explicit zero slots in current ACore layouts and must not be left NULL.
        const bool spawnTable = targetKey == "creature" || targetKey == "gameobject";
        if (spawnTable && HasColumn(targetTable, "id1") &&
            seenTargetColumns.find("id1") != seenTargetColumns.end())
            for (const char* secondary : {"id2", "id3"})
                if (HasColumn(targetTable, secondary) && seenTargetColumns.insert(secondary).second)
                {
                    targetColumns.push_back(*ActualColumn(targetTable, secondary));
                    sourceIndices.push_back(static_cast<std::size_t>(-1)); // synthesized zero
                    AddIssue(plan, SqlImportSeverity::Info, statementIndex + 1,
                             "added target " + targetTable.name + "." + secondary + " = 0 for AzerothCore spawn layout");
                }

        if (targetColumns.empty())
        {
            AddIssue(plan, SqlImportSeverity::Warning, statementIndex + 1,
                     "no compatible columns remained for `" + source.table + "`; statement skipped");
            ++plan.skippedStatements;
            continue;
        }
        std::vector<std::vector<std::string>> targetTuples;
        targetTuples.reserve(source.tuples.size());
        for (const std::vector<std::string>& tuple : source.tuples)
        {
            std::vector<std::string> values;
            values.reserve(sourceIndices.size());
            for (std::size_t sourceIndex : sourceIndices)
                values.push_back(sourceIndex == static_cast<std::size_t>(-1) ? "0" : tuple[sourceIndex]);
            targetTuples.push_back(std::move(values));
        }
        if (source.trailingClause)
            AddIssue(plan, SqlImportSeverity::Info, statementIndex + 1,
                     "source trailing conflict clause was normalized to the target import policy");
        plan.statements.push_back(BuildInsertSql(source.verb, targetTable, targetColumns, targetTuples, options.useUpsert));
        ++plan.convertedStatements;
        plan.convertedRows += source.tuples.size();

        // Legacy TC modelid1..4 -> current ACore creature_template_model rows.
        if (legacyToModelRows)
            GenerateAcoreModelRows(source, targetModelRows->second, plan, statementIndex + 1, options);
    }

    plan.ok = !plan.HasErrors() && !plan.statements.empty();
    if (plan.statements.empty() && !plan.HasErrors())
        AddIssue(plan, SqlImportSeverity::Error, 0, "no compatible INSERT/REPLACE statements were produced");
    return plan;
}

DbError SqlImportConverter::Apply(IDatabase& target, const SqlImportPlan& plan) const
{
    if (!plan.ok || plan.HasErrors() || plan.statements.empty())
        return {false, "SQL import plan is not executable; resolve conversion errors first"};
    target.BeginTransaction();
    for (const std::string& statement : plan.statements)
    {
        DbError error;
        target.Execute(statement, error);
        if (!error.ok)
        {
            target.Rollback();
            return {false, "converted SQL failed: " + error.message};
        }
    }
    return target.Commit();
}
} // namespace we
