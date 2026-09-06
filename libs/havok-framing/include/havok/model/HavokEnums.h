#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Enum string → integer resolution for Tier B. HKBuild emitted enum *strings*
// into XML (e.g. mode: MODE_LOOPING) and let the Havok XML→binary compiler map
// them; havok-core builds binary objects directly, so BehaviorBuilder resolves
// the strings here. Tables are faithful ports of HKX2E\HKX2\Autogen\Enums\*.cs
// and Manual\Enum\*.cs.
//
// A field's textual value may also be a bare integer (the YAML allows both) or a
// pipe-delimited OR of flag names (e.g. FLAG_IS_LOCAL_WILDCARD|FLAG_DISABLE_CONDITION).
// ResolveEnum handles all three forms.

namespace havok::model::enums {

// PlaybackMode.cs (sbyte)
inline const std::unordered_map<std::string, long>& PlaybackMode() {
    static const std::unordered_map<std::string, long> m = {
        {"MODE_SINGLE_PLAY", 0}, {"MODE_LOOPING", 1}, {"MODE_USER_CONTROLLED", 2},
        {"MODE_PING_PONG", 3}, {"MODE_COUNT", 4},
    };
    return m;
}

// VariableMode.cs (sbyte)
inline const std::unordered_map<std::string, long>& VariableMode() {
    static const std::unordered_map<std::string, long> m = {
        {"VARIABLE_MODE_DISCARD_WHEN_INACTIVE", 0},
        {"VARIABLE_MODE_MAINTAIN_VALUES_WHEN_INACTIVE", 1},
    };
    return m;
}

// StartStateMode.cs (sbyte)
inline const std::unordered_map<std::string, long>& StartStateMode() {
    static const std::unordered_map<std::string, long> m = {
        {"START_STATE_MODE_DEFAULT", 0}, {"START_STATE_MODE_SYNC", 1},
        {"START_STATE_MODE_RANDOM", 2}, {"START_STATE_MODE_CHOOSER", 3},
    };
    return m;
}

// StateMachineSelfTransitionMode.cs (sbyte) — used by hkbStateMachine.selfTransitionMode
inline const std::unordered_map<std::string, long>& SmSelfTransitionMode() {
    static const std::unordered_map<std::string, long> m = {
        {"SELF_TRANSITION_MODE_NO_TRANSITION", 0},
        {"SELF_TRANSITION_MODE_TRANSITION_TO_START_STATE", 1},
        {"SELF_TRANSITION_MODE_FORCE_TRANSITION_TO_START_STATE", 2},
    };
    return m;
}

// SelfTransitionMode.cs (sbyte) — used by hkbTransitionEffect.selfTransitionMode
inline const std::unordered_map<std::string, long>& SelfTransitionMode() {
    static const std::unordered_map<std::string, long> m = {
        {"SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC", 0},
        {"SELF_TRANSITION_MODE_CONTINUE", 1},
        {"SELF_TRANSITION_MODE_RESET", 2},
        {"SELF_TRANSITION_MODE_BLEND", 3},
    };
    return m;
}

// EventMode.cs (sbyte) — hkbEventBase / transition event mode.
inline const std::unordered_map<std::string, long>& EventMode() {
    static const std::unordered_map<std::string, long> m = {
        {"EVENT_MODE_DEFAULT", 0}, {"EVENT_MODE_PROCESS_ALL", 1},
        {"EVENT_MODE_IGNORE_FROM_GENERATOR", 2}, {"EVENT_MODE_IGNORE_TO_GENERATOR", 3},
    };
    return m;
}

// hkbExpressionData::ExpressionEventMode (sbyte) — used by hkbEvaluateExpressionModifier's
// expression data (and hkbEventRangeData). COMPLETELY DISTINCT from EventMode() above —
// neither names nor values overlap. Resolving an ExpressionEventMode string against the
// EventMode() table silently yields 0 (SEND_ONCE), collapsing BFCO's edge-triggered
// AttackWinStart / attack-start expressions to fire-once and breaking light-attack combos.
inline const std::unordered_map<std::string, long>& ExpressionEventMode() {
    static const std::unordered_map<std::string, long> m = {
        {"EVENT_MODE_SEND_ONCE", 0}, {"EVENT_MODE_SEND_ON_TRUE", 1},
        {"EVENT_MODE_SEND_ON_FALSE_TO_TRUE", 2}, {"EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE", 3},
    };
    return m;
}

// EndMode.cs (sbyte)
inline const std::unordered_map<std::string, long>& EndMode() {
    static const std::unordered_map<std::string, long> m = {
        {"END_MODE_NONE", 0},
        {"END_MODE_TRANSITION_UNTIL_END_OF_FROM_GENERATOR", 1},
        {"END_MODE_CAP_DURATION_AT_END_OF_FROM_GENERATOR", 2},
    };
    return m;
}

// BlendCurve.cs (sbyte)
inline const std::unordered_map<std::string, long>& BlendCurve() {
    static const std::unordered_map<std::string, long> m = {
        {"BLEND_CURVE_SMOOTH", 0}, {"BLEND_CURVE_LINEAR", 1},
        {"BLEND_CURVE_LINEAR_TO_SMOOTH", 2}, {"BLEND_CURVE_SMOOTH_TO_LINEAR", 3},
    };
    return m;
}

// BlendModeFunction (i8) — BGSGamebryoSequenceGenerator::m_eBlendModeFunction
inline const std::unordered_map<std::string, long>& BlendModeFunction() {
    static const std::unordered_map<std::string, long> m = {
        {"BMF_NONE", 0}, {"BMF_PERCENT", 1}, {"BMF_ONE_MINUS_PERCENT", 2},
    };
    return m;
}

// VariableType.cs (uint) — note INVALID = 0xFFFFFFFF
inline const std::unordered_map<std::string, long>& VariableType() {
    static const std::unordered_map<std::string, long> m = {
        {"VARIABLE_TYPE_INVALID", -1}, {"VARIABLE_TYPE_BOOL", 0}, {"VARIABLE_TYPE_INT8", 1},
        {"VARIABLE_TYPE_INT16", 2}, {"VARIABLE_TYPE_INT32", 3}, {"VARIABLE_TYPE_REAL", 4},
        {"VARIABLE_TYPE_POINTER", 5}, {"VARIABLE_TYPE_VECTOR3", 6}, {"VARIABLE_TYPE_VECTOR4", 7},
        {"VARIABLE_TYPE_QUATERNION", 8},
    };
    return m;
}

// Role.cs (short)
inline const std::unordered_map<std::string, long>& Role() {
    static const std::unordered_map<std::string, long> m = {
        {"ROLE_DEFAULT", 0}, {"ROLE_FILE_NAME", 1}, {"ROLE_BONE_INDEX", 2},
        {"ROLE_BONE_INDEX_MAP", 3}, {"ROLE_EVENT_ID", 4}, {"ROLE_VARIABLE_INDEX", 5},
        {"ROLE_ATTRIBUTE_INDEX", 6}, {"ROLE_TIME", 7},
    };
    return m;
}

// RoleFlags.cs (short) — flag bits, OR-able
inline const std::unordered_map<std::string, long>& RoleFlags() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_NONE", 0}, {"FLAG_RAGDOLL", 1}, {"FLAG_NORMALIZED", 2}, {"FLAG_NOT_VARIABLE", 4},
        {"FLAG_HIDDEN", 8}, {"FLAG_OUTPUT", 16}, {"FLAG_NOT_CHARACTER_PROPERTY", 32},
    };
    return m;
}

// TransitionFlags.cs (short) — flag bits, OR-able
inline const std::unordered_map<std::string, long>& TransitionFlags() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_USE_TRIGGER_INTERVAL", 1}, {"FLAG_USE_INITIATE_INTERVAL", 2},
        {"FLAG_UNINTERRUPTIBLE_WHILE_PLAYING", 4}, {"FLAG_UNINTERRUPTIBLE_WHILE_DELAYED", 8},
        {"FLAG_DELAY_STATE_CHANGE", 16}, {"FLAG_DISABLED", 32},
        {"FLAG_DISALLOW_RETURN_TO_PREVIOUS_STATE", 64}, {"FLAG_DISALLOW_RANDOM_TRANSITION", 128},
        {"FLAG_DISABLE_CONDITION", 256},
        {"FLAG_ALLOW_SELF_TRANSITION_BY_TRANSITION_FROM_ANY_STATE", 512},
        {"FLAG_IS_GLOBAL_WILDCARD", 1024}, {"FLAG_IS_LOCAL_WILDCARD", 2048},
        {"FLAG_FROM_NESTED_STATE_ID_IS_VALID", 4096}, {"FLAG_TO_NESTED_STATE_ID_IS_VALID", 8192},
        {"FLAG_ABUT_AT_END_OF_FROM_GENERATOR", 16384},
    };
    return m;
}

// FlagBits.cs (ushort) — hkbBlendingTransitionEffect flags, OR-able
inline const std::unordered_map<std::string, long>& FlagBits() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_NONE", 0}, {"FLAG_IGNORE_FROM_WORLD_FROM_MODEL", 1},
        {"FLAG_SYNC", 2}, {"FLAG_IGNORE_TO_WORLD_FROM_MODEL", 4},
    };
    return m;
}

// hkbBlenderGenerator::BlenderFlags (int16) — OR-able. The flags field of
// hkbBlenderGenerator AND its subclass hkbPoseMatchingGenerator. NOTE these differ from
// FlagBits above (transition-effect flags): here FLAG_SYNC is 1, not 2 — using the wrong
// table would mis-resolve a named flag. (Decompile currently emits these numerically, so
// this table backstops the named path.) e.g. MT_ForwardBlend flags 17 = FLAG_SYNC|FLAG_IS_PARAMETRIC.
inline const std::unordered_map<std::string, long>& BlenderFlags() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_SYNC", 1}, {"FLAG_SMOOTH_GENERATOR_WEIGHTS", 2},
        {"FLAG_DONT_DEACTIVATE_CHILDREN_WITH_ZERO_WEIGHTS", 4}, {"FLAG_PARAMETRIC_BLEND", 8},
        {"FLAG_IS_PARAMETRIC", 16}, {"FLAG_FORCE_DENSE_POSE", 32},
        {"FLAG_BLEND_MOTION_OF_ADDITIVE_ANIMATIONS", 64}, {"FLAG_USE_VELOCITY_SYNCHRONIZATION", 128},
    };
    return m;
}

// hkbClipGenerator flags (int8) — OR-able playback flags. 16 (FLAG_DONT_CONVERT_
// ANNOTATIONS_TO_TRIGGERS) is the common one across vanilla clips.
inline const std::unordered_map<std::string, long>& ClipGeneratorFlags() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_CONTINUE_MOTION_AT_END", 1},
        {"FLAG_SYNC_HALF_CYCLE_IN_PING_PONG_MODE", 2},
        {"FLAG_MIRROR", 4},
        {"FLAG_FORCE_DENSE_POSE", 8},
        {"FLAG_DONT_CONVERT_ANNOTATIONS_TO_TRIGGERS", 16},
        {"FLAG_IGNORE_MOTION", 32},
    };
    return m;
}

// SetAngleMethod.cs (sbyte)
inline const std::unordered_map<std::string, long>& SetAngleMethod() {
    static const std::unordered_map<std::string, long> m = {
        {"LINEAR", 0}, {"RAMPED", 1},
    };
    return m;
}

// RotationAxisCoordinates.cs (sbyte)
inline const std::unordered_map<std::string, long>& RotationAxisCoordinates() {
    static const std::unordered_map<std::string, long> m = {
        {"ROTATION_AXIS_IN_MODEL_COORDINATES", 0}, {"ROTATION_AXIS_IN_LOCAL_COORDINATES", 1},
    };
    return m;
}

// BindingType.cs (sbyte)
inline const std::unordered_map<std::string, long>& BindingType() {
    static const std::unordered_map<std::string, long> m = {
        {"BINDING_TYPE_VARIABLE", 0}, {"BINDING_TYPE_CHARACTER_PROPERTY", 1},
    };
    return m;
}

// EventInfo `flags` (hkbEventInfo.Flags). The only named bit used in vanilla
// behavior data is FLAG_SYNC_POINT (1). Others are passed through numerically.
inline const std::unordered_map<std::string, long>& EventInfoFlags() {
    static const std::unordered_map<std::string, long> m = {
        {"FLAG_NONE", 0}, {"FLAG_SILENT", 1}, {"FLAG_SYNC_POINT", 2},
    };
    return m;
}

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
