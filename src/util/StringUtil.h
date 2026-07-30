// Layer D (util): small string helpers shared across the editor.
//
// Case-insensitive search helpers (IEquals/IContains) back the quest browser filter;
// MoneyToString renders copper amounts for the reward/money fields.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace qe
{
    // Remove leading/trailing ASCII whitespace ( \t\r\n\f\v and space). Non-mutating.
    std::string Trim(const std::string& s);

    // ASCII lowercase copy (only 'A'..'Z' are folded; non-ASCII bytes pass through).
    std::string ToLower(const std::string& s);

    // Split on a single-character delimiter. Empty fields are preserved, so
    // Split("a,,b", ',') yields {"a", "", "b"}. Split of "" yields {""}.
    std::vector<std::string> Split(const std::string& s, char delim);

    // Join parts with sep between them (no trailing separator).
    std::string Join(const std::vector<std::string>& parts, const std::string& sep);

    // Case-insensitive (ASCII) full-string equality.
    bool IEquals(const std::string& a, const std::string& b);

    // Case-insensitive (ASCII) substring test: is `needle` contained in `haystack`?
    // An empty needle is always contained (matches typical "no filter" behaviour).
    bool IContains(const std::string& haystack, const std::string& needle);

    // Format a copper amount as "12g 3s 45c". Zero renders as "0c". Negative amounts
    // keep their sign (e.g. "-1g 50c") so "required money" style values round-trip.
    // Only non-zero denominations are shown, except that 0 copper still prints "0c".
    std::string MoneyToString(int32_t copper);

    // Optional helper: render a bitmask as a "LABEL_A | LABEL_B" string using any table
    // of entries exposing `.bit` (uint32) and `.label` (const char*). Works directly with
    // FlagEntry from Enums.h without creating a compile dependency on it. Bits present in
    // `bits` but absent from the table are collected as a trailing "0xNN" token.
    template <typename Entry>
    std::string FormatFlags(uint32_t bits, const std::vector<Entry>& table)
    {
        std::string out;
        uint32_t matched = 0;
        for (const Entry& e : table)
        {
            if (e.bit && (bits & e.bit) == e.bit)
            {
                if (!out.empty())
                    out += " | ";
                out += e.label;
                matched |= e.bit;
            }
        }

        uint32_t leftover = bits & ~matched;
        if (leftover)
        {
            char hex[16];
            std::snprintf(hex, sizeof(hex), "0x%X", leftover);
            if (!out.empty())
                out += " | ";
            out += hex;
        }

        if (out.empty())
            out = "0";
        return out;
    }
}
