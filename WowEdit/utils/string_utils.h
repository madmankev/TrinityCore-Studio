#pragma once

#include <string>
#include <vector>

namespace wowedit::strings
{
std::string ToLower(std::string value);
bool ContainsInsensitive(const std::string& haystack, const std::string& needle);
std::vector<std::string> Split(const std::string& value, char delimiter);
std::string Trim(const std::string& value);
} // namespace wowedit::strings
