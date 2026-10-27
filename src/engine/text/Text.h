#pragma once
/// @file
/// @brief CB::engine::text — the shared string operations: trimming, line splitting, the UTF-8 BOM, the float text form
///        the YAML emitters use, and YAML scalar quoting.
/// @note No case folding lives here (CLAUDE.md case directive): path identity is codec::resource::PathKey.

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace CB::engine::text {

/// @brief `s` without leading/trailing spaces, tabs, CRs and LFs.
inline std::string_view Trim(std::string_view s)
{
    constexpr std::string_view kWs = " \t\r\n";
    const auto b = s.find_first_not_of(kWs);
    if (b == std::string_view::npos) return {};
    return s.substr(b, s.find_last_not_of(kWs) - b + 1);
}

/// @brief `s` without a leading UTF-8 byte-order mark.
inline std::string_view StripBom(std::string_view s)
{
    return s.size() >= 3 && s[0] == '\xEF' && s[1] == '\xBB' && s[2] == '\xBF' ? s.substr(3) : s;
}

/// @brief The lines of `text`, LF or CRLF terminated (one trailing CR per line dropped); a final newline doesn't
///        add an empty last line.
/// @warning Views into `text` — it must outlive the result. Never pass a temporary string (e.g. a function's return
///          value) in a range-for: `for (auto l : SplitLines(Read()))` dangles; bind the text to a named variable first.
///          The same holds for Trim and StripBom, which also return views.
inline std::vector<std::string_view> SplitLines(std::string_view text)
{
    std::vector<std::string_view> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        auto end = text.find('\n', pos);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(pos, end - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        pos = end + 1;
    }
    return out;
}

/// @brief The float text form every YAML emitter writes: printf "%.9g" (round-trips a float exactly).
/// @note NaN prints as the C runtime spells it — a payload-preserving NaN needs the raw bits (HavokModel `nan_<hex>`).
inline std::string FormatFloat(float v)
{
    char b[32];
    std::snprintf(b, sizeof b, "%.9g", v);
    return b;
}

/// @brief A YAML double-quoted scalar: `\\`, `"`, `\n`, `\r`, `\t` escaped, any other control byte as `\xNN`.
inline std::string QuoteDouble(std::string_view s)
{
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\x%02X", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    out += "\"";
    return out;
}

/// @brief A YAML single-quoted scalar (`'` doubled).
/// @warning Can't carry a control byte on one line — use Quote() for arbitrary text.
inline std::string QuoteSingle(std::string_view s)
{
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "''"; else o += c; }
    o += "'";
    return o;
}

/// @brief Single-quoted, or double-quoted with escapes when `s` holds a control byte (vanilla annotations carry
///        e.g. "FootBack\r\n").
inline std::string Quote(std::string_view s)
{
    for (unsigned char c : s)
        if (c < 0x20) return QuoteDouble(s);
    return QuoteSingle(s);
}

/// @brief `s` as a YAML scalar: as is when it reads back as the same plain string, else Quote()d. Names stay bare in
///        the common case (`NPC Root [Root]`) and quoted only where YAML would read them otherwise (a leading `[` is a
///        flow sequence: the chaurus's `[Neck0]`).
inline std::string YamlScalar(std::string_view s)
{
    const auto plainSafe = [&] {
        if (s.empty() || s.front() == ' ' || s.back() == ' ') return false;
        if (std::string_view("[]{},#&*!|>'\"%@`").find(s.front()) != std::string_view::npos) return false;
        if ((s.front() == '-' || s.front() == '?' || s.front() == ':') && (s.size() == 1 || s[1] == ' ')) return false;
        if (s.find(": ") != std::string_view::npos || s.find(" #") != std::string_view::npos || s.back() == ':') return false;
        for (unsigned char c : s)
            if (c < 0x20) return false;
        return true;
    };
    return plainSafe() ? std::string(s) : Quote(s);
}

}  // namespace CB::engine::text
