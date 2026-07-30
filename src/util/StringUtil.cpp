#include "StringUtil.h"

#include <cctype>
#include <cstdio>

namespace qe
{
    namespace
    {
        inline char LowerAscii(char c)
        {
            unsigned char uc = static_cast<unsigned char>(c);
            return static_cast<char>(std::tolower(uc));
        }

        inline bool IsSpaceAscii(char c)
        {
            unsigned char uc = static_cast<unsigned char>(c);
            return std::isspace(uc) != 0;
        }
    }

    std::string Trim(const std::string& s)
    {
        std::size_t begin = 0;
        std::size_t end = s.size();
        while (begin < end && IsSpaceAscii(s[begin]))
            ++begin;
        while (end > begin && IsSpaceAscii(s[end - 1]))
            --end;
        return s.substr(begin, end - begin);
    }

    std::string ToLower(const std::string& s)
    {
        std::string out(s.size(), '\0');
        for (std::size_t i = 0; i < s.size(); ++i)
            out[i] = LowerAscii(s[i]);
        return out;
    }

    std::vector<std::string> Split(const std::string& s, char delim)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true)
        {
            std::size_t pos = s.find(delim, start);
            if (pos == std::string::npos)
            {
                parts.push_back(s.substr(start));
                break;
            }
            parts.push_back(s.substr(start, pos - start));
            start = pos + 1;
        }
        return parts;
    }

    std::string Join(const std::vector<std::string>& parts, const std::string& sep)
    {
        std::string out;
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            if (i != 0)
                out += sep;
            out += parts[i];
        }
        return out;
    }

    bool IEquals(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (LowerAscii(a[i]) != LowerAscii(b[i]))
                return false;
        return true;
    }

    bool IContains(const std::string& haystack, const std::string& needle)
    {
        if (needle.empty())
            return true;
        if (needle.size() > haystack.size())
            return false;

        const std::size_t last = haystack.size() - needle.size();
        for (std::size_t i = 0; i <= last; ++i)
        {
            std::size_t j = 0;
            for (; j < needle.size(); ++j)
                if (LowerAscii(haystack[i + j]) != LowerAscii(needle[j]))
                    break;
            if (j == needle.size())
                return true;
        }
        return false;
    }

    std::string MoneyToString(int32_t copper)
    {
        if (copper == 0)
            return "0c";

        bool negative = copper < 0;
        // Use int64 to avoid overflow when negating INT32_MIN.
        int64_t total = negative ? -static_cast<int64_t>(copper) : static_cast<int64_t>(copper);

        int64_t gold = total / 10000;
        int64_t silver = (total / 100) % 100;
        int64_t bronze = total % 100;

        std::string out;
        if (negative)
            out += "-";

        bool wrote = false;
        char buf[32];
        if (gold > 0)
        {
            std::snprintf(buf, sizeof(buf), "%lldg", static_cast<long long>(gold));
            out += buf;
            wrote = true;
        }
        if (silver > 0)
        {
            if (wrote) out += " ";
            std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(silver));
            out += buf;
            wrote = true;
        }
        if (bronze > 0)
        {
            if (wrote) out += " ";
            std::snprintf(buf, sizeof(buf), "%lldc", static_cast<long long>(bronze));
            out += buf;
            wrote = true;
        }

        return out;
    }
}
