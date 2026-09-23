#pragma once
// Vec4Text — the ONE hkVector4 / Quaternion TEXT parser (text -> 4 floats).
//
// Accepts BOTH the decompiler's "(x y z w)" and a vanilla/Nemesis bare, possibly multi-line
// "x\n y\n z\n w" (no parens). Single source that every caller shares (havok-model parseVec4Raw ->
// 16 bytes, havok-core BehaviorBuilder::pv4 -> Vector4), killing the divergent-twin hazard (B4/BR-28,
// where a paren-only parser silently zeroed a bare vec4).
//
// NOTE on the reverse: there is deliberately NO single formatVec4 here. floats -> text is
// CONTEXT-SPECIFIC — the tagfile XML emitter, the .hky YAML emitter, and the adsf emitter each format
// the floats at different precision by design (tff / %.9g / %g). Those emitters stay where they are;
// only the PARSE direction is uniform, so only it is a shared leaf. std-only, header-only.

#include <array>
#include <sstream>
#include <string>
#include <string_view>

namespace havok::vec4 {

inline std::array<float, 4> parseVec4(std::string_view t) {
    std::string s(t);
    for (char& c : s) if (c == '(' || c == ')' || c == ',') c = ' ';
    std::array<float, 4> q{ 0.f, 0.f, 0.f, 0.f };
    std::istringstream ss(s);
    ss >> q[0] >> q[1] >> q[2] >> q[3];   // >> skips whitespace incl. newlines
    return q;
}

}  // namespace havok::vec4
