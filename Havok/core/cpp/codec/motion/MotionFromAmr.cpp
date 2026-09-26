// MotionFromAmr.cpp — AMR (Animation Motion Revolution) annotations -> a CB MotionRecord.
// Carved out of AnimDataYaml in org-pass firesale phase 3f-2 as the first tenant of codec/motion
// (the reference-frame <-> motion transform concern). See MotionFromAmr.h.
#include <codec/motion/MotionFromAmr.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace havok::animdata {

std::optional<MotionRecord> MotionFromAmrAnnotations(
    const std::vector<std::pair<float, std::string>>& annotations, const std::string& duration)
{
    auto starts = [](const std::string& s, const char* p) {
        const std::size_t n = std::strlen(p);
        return s.size() >= n && std::equal(p, p + n, s.begin());
    };
    auto num = [](float v) { char b[32]; std::snprintf(b, sizeof b, "%g", v); return std::string(b); };

    std::vector<std::pair<float, std::array<float, 3>>> tr;   // (time, x y z)
    std::vector<std::pair<float, float>>                rot;  // (time, yawDegrees)
    for (const auto& [t, raw] : annotations) {
        std::size_t i = 0;
        while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;   // ltrim
        const std::string text = raw.substr(i);
        if (starts(text, "animmotion ")) {
            float x = 0, y = 0, z = 0;
            if (std::sscanf(text.c_str() + 11, "%f %f %f", &x, &y, &z) == 3) tr.push_back({ t, { x, y, z } });
        } else if (starts(text, "animrotation ")) {
            float yaw = 0;
            if (std::sscanf(text.c_str() + 13, "%f", &yaw) == 1) rot.push_back({ t, yaw });
        }
    }
    if (tr.empty() && rot.empty()) return std::nullopt;

    std::stable_sort(tr.begin(),  tr.end(),  [](const auto& a, const auto& b) { return a.first < b.first; });
    std::stable_sort(rot.begin(), rot.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    MotionRecord mr;
    mr.duration = duration;
    for (const auto& [t, xyz] : tr)
        mr.translations.push_back(num(t) + " " + num(xyz[0]) + " " + num(xyz[1]) + " " + num(xyz[2]));
    for (const auto& [t, yawDeg] : rot) {
        const float y = yawDeg * 3.14159265358979323846f / 180.0f;   // AMR: roll=pitch=0
        const float w = std::cos(y / 2.0f), z = std::sin(y / 2.0f);  // quat (x=y=0)
        mr.rotations.push_back(num(t) + " 0 0 " + num(z) + " " + num(w));   // "t x y z w"
    }
    return mr;
}


}  // namespace havok::animdata
