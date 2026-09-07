#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Enum string ↔ integer resolution for Tier B. HKBuild emitted enum *strings* into XML (e.g.
// mode: MODE_LOOPING) and let the Havok XML→binary compiler map them; havok-core builds binary
// objects directly, so BehaviorBuilder resolves the strings here.
//
// The enum TABLES are no longer hardcoded here — they live in the schema data tree
// (Havok/core/Schema/enums/*.yaml, the single source) and are baked into the binary at build time
// by cmake/EmbedEnums.cmake → GeneratedEnums.cpp, which defines EnumTable(name) below. Each named
// accessor (PlaybackMode(), EventMode(), …) is a thin lookup into that generated data, so call
// sites are unchanged. The item names/values are CASE-SENSITIVE Havok data — they render a .hky
// field by name (MODE_SINGLE_PLAY, not 47) AND feed the class-signature CRC.
//
// A field's textual value may also be a bare integer (the YAML allows both) or a pipe-delimited OR
// of flag names (e.g. FLAG_IS_LOCAL_WILDCARD|FLAG_DISABLE_CONDITION). ResolveEnum handles all three.

namespace havok::model::enums {

// The generated backing store (GeneratedEnums.cpp). Returns the name→value table for the enum type
// `name` (the enum's `name:` in its yaml), or an empty table if unknown. Defined out-of-line so the
// baked data lives in one TU; callers link havok-framing.
const std::unordered_map<std::string, long>& EnumTable(const std::string& name);

// ── Named accessors (stable API; the ~40 call sites use these) ────────────────────────────────────
// Each is a thin lookup into the generated tables. To add an enum: drop a Havok/core/Schema/enums/
// <Name>.yaml file; it's baked automatically. Add a one-liner here only if code wants enums::<Name>().
inline const std::unordered_map<std::string, long>& PlaybackMode()            { return EnumTable("PlaybackMode"); }
inline const std::unordered_map<std::string, long>& VariableMode()            { return EnumTable("VariableMode"); }
inline const std::unordered_map<std::string, long>& StartStateMode()          { return EnumTable("StartStateMode"); }
inline const std::unordered_map<std::string, long>& SmSelfTransitionMode()    { return EnumTable("SmSelfTransitionMode"); }
inline const std::unordered_map<std::string, long>& SelfTransitionMode()      { return EnumTable("SelfTransitionMode"); }
inline const std::unordered_map<std::string, long>& EventMode()               { return EnumTable("EventMode"); }
inline const std::unordered_map<std::string, long>& ExpressionEventMode()     { return EnumTable("ExpressionEventMode"); }
inline const std::unordered_map<std::string, long>& EndMode()                 { return EnumTable("EndMode"); }
inline const std::unordered_map<std::string, long>& BlendCurve()              { return EnumTable("BlendCurve"); }
inline const std::unordered_map<std::string, long>& BlendModeFunction()       { return EnumTable("BlendModeFunction"); }
inline const std::unordered_map<std::string, long>& VariableType()            { return EnumTable("VariableType"); }
inline const std::unordered_map<std::string, long>& Role()                    { return EnumTable("Role"); }
inline const std::unordered_map<std::string, long>& RoleFlags()               { return EnumTable("RoleFlags"); }
inline const std::unordered_map<std::string, long>& TransitionFlags()         { return EnumTable("TransitionFlags"); }
inline const std::unordered_map<std::string, long>& FlagBits()                { return EnumTable("FlagBits"); }
inline const std::unordered_map<std::string, long>& BlenderFlags()            { return EnumTable("BlenderFlags"); }
inline const std::unordered_map<std::string, long>& ClipGeneratorFlags()      { return EnumTable("ClipGeneratorFlags"); }
inline const std::unordered_map<std::string, long>& SetAngleMethod()          { return EnumTable("SetAngleMethod"); }
inline const std::unordered_map<std::string, long>& RotationAxisCoordinates() { return EnumTable("RotationAxisCoordinates"); }
inline const std::unordered_map<std::string, long>& BindingType()             { return EnumTable("BindingType"); }
inline const std::unordered_map<std::string, long>& EventInfoFlags()          { return EnumTable("EventInfoFlags"); }

namespace detail {
inline std::string trim(std::string_view s) {
    std::size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    std::size_t e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}
inline bool tryParseLong(const std::string& s, long& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    // Havok tagfiles write a hkbRoleAttribute's leftover "unknown bits" as 0x-hex
    // ("FLAG_HIDDEN|...|0x1680"), so accept an explicit 0x/0X prefix as base 16. Use
    // base 10 otherwise — NOT strtol base 0, whose octal-on-leading-zero would silently
    // change decimal values the tagfiles never mean as octal.
    std::size_t p = (!s.empty() && (s[0] == '+' || s[0] == '-')) ? 1 : 0;
    const bool hex = s.size() > p + 2 && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X');
    long v = std::strtol(s.c_str(), &end, hex ? 16 : 10);
    if (end == s.c_str() || *end != '\0') return false;
    out = v;
    return true;
}
} // namespace detail

// Resolve a textual enum value against `table`. Accepts:
//   - a single enum name        -> its value
//   - a bare integer            -> parsed directly
//   - a pipe-OR of flag names   -> bitwise-OR of values (and/or integers)
// Unknown names fall back to 0 (the C# emitter wrote the string through verbatim;
// here we need a number, so 0 is the safe default for an unrecognised token).
inline long ResolveEnum(const std::string& raw,
                        const std::unordered_map<std::string, long>& table) {
    std::string s = detail::trim(raw);
    if (s.empty()) return 0;

    long whole = 0;
    if (detail::tryParseLong(s, whole)) return whole;

    if (s.find('|') == std::string::npos) {
        auto it = table.find(s);
        if (it != table.end()) return it->second;
        return 0;
    }

    long acc = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '|')) {
        tok = detail::trim(tok);
        if (tok.empty()) continue;
        long n = 0;
        if (detail::tryParseLong(tok, n)) { acc |= n; continue; }
        auto it = table.find(tok);
        if (it != table.end()) acc |= it->second;
    }
    return acc;
}

// Inverse of ResolveEnum for OR-able flag bitfields: decompose `value` into the pipe-OR of the
// table's flag NAMES, greedily clearing the largest matching entry first (so combined-bit entries
// win over their components), with any leftover bits appended as `0xHEX`. Round-trips exactly:
// ResolveEnum(FormatFlags(v, t), t) == v. `value == 0` -> "0" (no flags set — not a magic number,
// just empty). Deterministic output: entries are visited in descending bit value.
inline std::string FormatFlags(long value, const std::unordered_map<std::string, long>& table) {
    if (value == 0) return "0";
    std::vector<std::pair<std::string, long>> ents(table.begin(), table.end());
    std::sort(ents.begin(), ents.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    long        rem = value;
    std::string out;
    for (const auto& [name, val] : ents) {
        if (val == 0) continue;
        if ((rem & val) == val) {
            if (!out.empty()) out += '|';
            out += name;
            rem &= ~val;
        }
    }
    if (rem != 0) {  // bits with no named flag — keep them, still round-trips (0x-hex parses back)
        char buf[24];
        std::snprintf(buf, sizeof buf, "0x%lX", static_cast<unsigned long>(rem));
        if (!out.empty()) out += '|';
        out += buf;
    }
    return out;
}

} // namespace havok::model::enums
