#pragma once
#include <cstdint>

// havok-core's own minimal value types. Deliberately NOT RE::/System.Numerics
// analogues (plan §1.3) — havok-core is a standalone, dependency-free library.

namespace havok {

struct Vector4 {
    float x{};
    float y{};
    float z{};
    float w{};
    friend bool operator==(const Vector4&, const Vector4&) = default;
};

struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{};
    friend bool operator==(const Quaternion&, const Quaternion&) = default;
};

// hkQsTransform — translation + rotation (quaternion) + scale; 48 bytes serialized.
struct QSTransform {
    Vector4    translation{};
    Quaternion rotation{};
    Vector4    scale{};
    friend bool operator==(const QSTransform&, const QSTransform&) = default;
};

// IEEE-754 binary16 stored as raw 16 bits. havok-core round-trips the bits; it
// does not (yet) interpret/convert them. Promote to a real float16 type only if
// a downstream consumer needs arithmetic on Half values.
using Half = std::uint16_t;

} // namespace havok
