#include "utils/string_utils.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace wowedit::strings
{
std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool ContainsInsensitive(const std::string& haystack, const std::string& needle)
{
    return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
}

std::vector<std::string> Split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, delimiter))
        result.push_back(part);
    return result;
}

std::string Trim(const std::string& value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}
} // namespace wowedit::strings
