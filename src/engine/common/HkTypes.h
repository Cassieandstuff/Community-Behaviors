#pragma once
#include <cstdint>

// The engine's own minimal value types. Deliberately NOT RE:: analogues — the engine
// is CommonLib-free (configure fails on any RE/ include).

namespace CB::engine::common {

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

} // namespace CB::engine::common
