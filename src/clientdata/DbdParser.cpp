// DbdParser — see DbdParser.h. Parses the WoWDBDefs .dbd format (COLUMNS section + per-BUILD layout).

#include "clientdata/DbdParser.h"

#include <array>
#include <sstream>
#include <unordered_map>

namespace we
{
namespace
{
std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Strip a trailing "// comment".
std::string StripComment(const std::string& s)
{
    size_t p = s.find("//");
    return p == std::string::npos ? s : s.substr(0, p);
}

// A parsed build-layout field before array expansion.
struct LayoutField
{
    std::string name;
    bool        isPk = false;
    bool        unsigned_ = false;
    int         bits = 0;   // 8/16/32 from <size>; 0 = unspecified (defaults to 32)
    int         arrayLen = 1;
};

LayoutField ParseLayoutLine(std::string line)
{
    LayoutField f;
    line = Trim(StripComment(line));

    // $id$ / $noninline,id$ / $relation$ prefix.
    if (!line.empty() && line[0] == '$')
    {
        size_t end = line.find('$', 1);
        if (end != std::string::npos)
        {
            std::string marker = line.substr(1, end - 1);
            if (marker.find("id") != std::string::npos)
                f.isPk = true;
            line = Trim(line.substr(end + 1));
        }
    }

    // Array [N].
    if (size_t lb = line.find('['); lb != std::string::npos)
    {
        size_t rb = line.find(']', lb);
        if (rb != std::string::npos)
        {
            f.arrayLen = std::atoi(line.substr(lb + 1, rb - lb - 1).c_str());
            if (f.arrayLen < 1)
                f.arrayLen = 1;
            line = line.substr(0, lb) + line.substr(rb + 1);
        }
    }

    // Size annotation <32> / <u16> / <u8> / <8>.
    if (size_t lt = line.find('<'); lt != std::string::npos)
    {
        size_t gt = line.find('>', lt);
        if (gt != std::string::npos)
        {
            std::string sz = line.substr(lt + 1, gt - lt - 1);  // "8" | "u8" | "16" | "u32"
            size_t digits = 0;
            if (!sz.empty() && (sz[0] == 'u' || sz[0] == 'U'))
            {
                f.unsigned_ = true;
                digits = 1;
            }
            f.bits = std::atoi(sz.c_str() + digits);  // bit width (0 if non-numeric)
            line = line.substr(0, lt) + line.substr(gt + 1);
        }
    }

    f.name = Trim(line);
    return f;
}

// Parse "3.3.5.12340" (or shorter) into up to 4 components for ordered comparison.
std::array<int, 4> ParseVersion(const std::string& v)
{
    std::array<int, 4> out{0, 0, 0, 0};
    std::istringstream ss(v);
    std::string part;
    int i = 0;
    while (i < 4 && std::getline(ss, part, '.'))
        out[i++] = std::atoi(Trim(part).c_str());
    return out;
}

bool VerLE(const std::array<int, 4>& a, const std::array<int, 4>& b)
{
    for (int i = 0; i < 4; ++i)
        if (a[i] != b[i])
            return a[i] < b[i];
    return true;  // equal
}

// Does a BUILD spec cover build 3.3.5.12340? Handles comma-separated lists and "A-B" ranges.
bool BuildCovers12340(const std::string& spec)
{
    static const std::array<int, 4> kTarget{3, 3, 5, 12340};
    std::istringstream ss(spec);
    std::string tok;
    while (std::getline(ss, tok, ','))
    {
        tok = Trim(tok);
        if (tok.empty())
            continue;
        size_t dash = tok.find('-');
        if (dash != std::string::npos)
        {
            auto lo = ParseVersion(Trim(tok.substr(0, dash)));
            auto hi = ParseVersion(Trim(tok.substr(dash + 1)));
            if (VerLE(lo, kTarget) && VerLE(kTarget, hi))
                return true;
        }
        else if (ParseVersion(tok) == kTarget)
        {
            return true;
        }
    }
    return false;
}

// COLUMNS entry base type: "int" | "float" | "string" | "locstring".
std::unordered_map<std::string, std::string> ParseColumns(std::istringstream& in)
{
    std::unordered_map<std::string, std::string> types;
    std::string line;
    while (std::getline(in, line))
    {
        std::string t = Trim(StripComment(line));
        if (t.empty())
            break;  // blank line ends COLUMNS
        // base type = up to first '<' or space.
        size_t stop = t.find_first_of("< ");
        std::string base = t.substr(0, stop);
        // name = last whitespace-separated token, minus a trailing '?'.
        size_t sp = t.find_last_of(" \t");
        std::string name = sp == std::string::npos ? t : t.substr(sp + 1);
        if (!name.empty() && name.back() == '?')
            name.pop_back();
        if (!name.empty())
            types[name] = base;
    }
    return types;
}
} // namespace

std::unique_ptr<ParsedDbc> ParseDbdFor12340(const std::string& dbdText)
{
    auto out = std::make_unique<ParsedDbc>();
    std::istringstream in(dbdText);
    std::string line;

    std::unordered_map<std::string, std::string> colTypes;
    // A layout block is: a LAYOUT line, one or more BUILD lines (any of which may cover 12340 via an
    // exact build or an A-B range), then field lines, terminated by a blank line. We accumulate the
    // block's build coverage across ALL its BUILD lines before deciding whether its fields are ours.
    bool blockCoversTarget = false;
    bool sawBuild = false;

    while (std::getline(in, line))
    {
        std::string t = Trim(line);
        if (t == "COLUMNS")
        {
            colTypes = ParseColumns(in);
            continue;
        }
        if (t.empty())
        {
            if (!out->schema.fields.empty())
                break;             // end of the matched block's layout
            blockCoversTarget = false;  // block separator: reset for the next block
            continue;
        }
        if (t.rfind("LAYOUT", 0) == 0)
        {
            blockCoversTarget = false;  // new block
            continue;
        }
        if (t.rfind("COMMENT", 0) == 0)
            continue;
        if (t.rfind("BUILD", 0) == 0)
        {
            sawBuild = true;
            if (BuildCovers12340(Trim(t.substr(5))))
                blockCoversTarget = true;
            continue;
        }
        if (!blockCoversTarget)
            continue;

        LayoutField lf = ParseLayoutLine(line);
        if (lf.name.empty())
            continue;
        auto it = colTypes.find(lf.name);
        const std::string base = it != colTypes.end() ? it->second : "int";

        DbcFieldType type;
        if (base == "locstring")
            type = DbcFieldType::LangString;
        else if (base == "string")
            type = DbcFieldType::String;
        else if (base == "float")
            type = DbcFieldType::Float;
        else  // int: pick the narrow type from the .dbd bit width (default 32)
        {
            const int bits = lf.bits == 0 ? 32 : lf.bits;
            if (bits <= 8)
                type = lf.unsigned_ ? DbcFieldType::UInt8 : DbcFieldType::Int8;
            else if (bits <= 16)
                type = lf.unsigned_ ? DbcFieldType::UInt16 : DbcFieldType::Int16;
            else
                type = lf.unsigned_ ? DbcFieldType::UInt32 : DbcFieldType::Int32;
        }

        for (int i = 0; i < lf.arrayLen; ++i)
        {
            std::string nm = lf.arrayLen > 1 ? lf.name + std::to_string(i + 1) : lf.name;
            out->store.push_back(nm);
            out->schema.fields.push_back({out->store.back().c_str(), type});
        }
    }

    out->ok = sawBuild && !out->schema.fields.empty();
    if (!out->ok)
        return nullptr;
    return out;
}
} // namespace we
