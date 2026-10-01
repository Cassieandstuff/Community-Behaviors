#include <codec/motion/RootMotion.h>

#include <codec/format/AnimDataYaml.h>         // EmitMotionSidecar
#include <codec/format/AnimationYamlLoader.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace CB::core::anim {
namespace {

struct Xf {
    double t[3] = { 0, 0, 0 };
    double q[4] = { 0, 0, 0, 1 };
};

void qmul(const double a[4], const double b[4], double o[4]) {
    const double r[4] = { a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
                          a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
                          a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
                          a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2] };
    std::memcpy(o, r, sizeof r);
}

void qrot(const double q[4], const double v[3], double o[3]) {
    const double tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]),
                 tz = 2 * (q[0] * v[1] - q[1] * v[0]);
    const double r[3] = { v[0] + q[3] * tx + (q[1] * tz - q[2] * ty), v[1] + q[3] * ty + (q[2] * tx - q[0] * tz),
                          v[2] + q[3] * tz + (q[0] * ty - q[1] * tx) };
    std::memcpy(o, r, sizeof r);
}

void qnorm(double q[4]) {
    const double m = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (m < 1e-12) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
    for (int i = 0; i < 4; ++i) q[i] /= m;
}

double qdot(const double a[4], const double b[4]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3]; }

/// @brief a · b (b applied first, in a's frame).
Xf compose(const Xf& a, const Xf& b) {
    Xf r;
    qrot(a.q, b.t, r.t);
    for (int i = 0; i < 3; ++i) r.t[i] += a.t[i];
    qmul(a.q, b.q, r.q);
    return r;
}

Xf inverse(const Xf& a) {
    Xf r;
    r.q[0] = -a.q[0]; r.q[1] = -a.q[1]; r.q[2] = -a.q[2]; r.q[3] = a.q[3];
    const double n[3] = { -a.t[0], -a.t[1], -a.t[2] };
    qrot(r.q, n, r.t);
    return r;
}

/// @brief Bit-exact port of the engine's FloatToHalf (140e19360, round-to-nearest-even).
std::uint16_t FloatToHalf(float f) {
    std::uint32_t b; std::memcpy(&b, &f, 4);
    std::uint32_t u = b & 0x7fffffffu;
    const std::uint16_t sign = static_cast<std::uint16_t>((b >> 16) & 0x8000u);
    if (u > 0x477fe000u) return static_cast<std::uint16_t>((((b & 0x7f800000u) != 0x7f800000u || (b & 0x7fffffu) == 0) ? 0x7c00u : 0x7fffu) | sign);
    if (u == 0) return sign;
    if (u < 0x38800000u) u = ((b & 0x7fffffu) | 0x800000u) >> ((0x71u - (u >> 23)) & 0x1fu);
    else u += 0xc8000000u;
    return static_cast<std::uint16_t>((((u + 0xfffu + ((u >> 13) & 1u)) >> 13) & 0x7fffu) | sign);
}

float HalfToFloat(std::uint16_t h) {
    std::uint32_t mant = h & 0x3ffu, exp = (h >> 10) & 0x1fu, out;
    const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16;
    if (exp == 0x1f) out = sign | 0x7f800000u | (mant << 13);
    else if (exp == 0) {
        if (mant == 0) out = sign;
        else {
            int e = -1;
            do { ++e; mant <<= 1; } while ((mant & 0x400u) == 0);
            out = sign | (static_cast<std::uint32_t>(112 - e) << 23) | ((mant & 0x3ffu) << 13);
        }
    } else out = sign | ((exp + 112) << 23) | (mant << 13);
    float f; std::memcpy(&f, &out, 4);
    return f;
}

double EngineTime(double t) { return HalfToFloat(FloatToHalf(static_cast<float>(t))); }

/// @brief NiQuaternion_FastSlerp (140d340e0): nlerp with a polynomial correction of the blend factor.
void FastSlerp(double t, const double a[4], const double b[4], double o[4]) {
    double k = 1.0 - qdot(a, b) * 0.8227969;
    k = k * k * 0.5854922;
    if (t > 0.5) {
        const double u = 1.0 - t;
        t = 1.0 - (((u + u) - 3.0) * k * u + 1.0 + k) * u;
    } else {
        t = (((t + t) - 3.0) * k * t + 1.0 + k) * t;
    }
    for (int i = 0; i < 4; ++i) o[i] = (b[i] - a[i]) * t + a[i];
    qnorm(o);
}

std::vector<double> Numbers(const std::string& s) {
    std::vector<double> v;
    const char* p = s.c_str();
    char* end = nullptr;
    for (;;) {
        const double d = std::strtod(p, &end);
        if (end == p) break;
        v.push_back(d);
        p = end;
    }
    return v;
}

std::string Num(double d) {
    char buf[32];
    const auto r = std::to_chars(buf, buf + sizeof buf, static_cast<float>(d));
    return std::string(buf, r.ptr);
}

/// @brief The root track sampled at frame `f` (dense tracks index directly; sparse ones interpolate by time).
Xf SampleTrack(const AnimTrackDef& tr, int f, double time) {
    Xf x;
    if (!tr.translation.empty()) {
        const auto& k = tr.translation;
        if (static_cast<std::size_t>(f) < k.size() && std::abs(k[f].time - time) < 1e-4) {
            for (int i = 0; i < 3; ++i) x.t[i] = k[f].value[i];
        } else {
            std::size_t j = 0;
            while (j + 1 < k.size() && k[j + 1].time <= time) ++j;
            const std::size_t n = std::min(j + 1, k.size() - 1);
            const double span = k[n].time - k[j].time, a = span > 1e-9 ? std::clamp((time - k[j].time) / span, 0.0, 1.0) : 0.0;
            for (int i = 0; i < 3; ++i) x.t[i] = k[j].value[i] + (k[n].value[i] - k[j].value[i]) * a;
        }
    }
    if (!tr.rotation.empty()) {
        const auto& k = tr.rotation;
        if (static_cast<std::size_t>(f) < k.size() && std::abs(k[f].time - time) < 1e-4) {
            for (int i = 0; i < 4; ++i) x.q[i] = k[f].value[i];
        } else {
            std::size_t j = 0;
            while (j + 1 < k.size() && k[j + 1].time <= time) ++j;
            const std::size_t n = std::min(j + 1, k.size() - 1);
            const double span = k[n].time - k[j].time, a = span > 1e-9 ? std::clamp((time - k[j].time) / span, 0.0, 1.0) : 0.0;
            double b[4]; for (int i = 0; i < 4; ++i) b[i] = k[n].value[i];
            double qa[4]; for (int i = 0; i < 4; ++i) qa[i] = k[j].value[i];
            if (qdot(qa, b) < 0) for (double& c : b) c = -c;
            for (int i = 0; i < 4; ++i) x.q[i] = qa[i] + (b[i] - qa[i]) * a;
        }
        qnorm(x.q);
    }
    return x;
}

double FrameTime(const AnimationDef& def, int f) { return static_cast<double>(static_cast<float>(f) * def.frameDuration); }

} // namespace

EngineMotionCurve::EngineMotionCurve(const CB::core::animdata::MotionRecord& m) {
    for (const auto& s : m.translations) {
        const auto v = Numbers(s);
        if (v.size() >= 4) m_translation.push_back({ EngineTime(v[0]), { v[1], v[2], v[3], 0 } });
    }
    for (const auto& s : m.rotations) {
        const auto v = Numbers(s);
        if (v.size() >= 5) m_rotation.push_back({ EngineTime(v[0]), { v[1], v[2], v[3], v[4] } });
    }
    if (m_translation.size() == 1 && m_translation[0].v[0] == 0 && m_translation[0].v[1] == 0 && m_translation[0].v[2] == 0)
        m_translation.clear();
    if (m_rotation.size() == 1 && m_rotation[0].v[0] == 0 && m_rotation[0].v[1] == 0 && m_rotation[0].v[2] == 0 && m_rotation[0].v[3] == 1)
        m_rotation.clear();
}

MotionPose EngineMotionCurve::Sample(double time) const {
    MotionPose p;
    /// @note Both channels share the engine's search: clamp to [0, last], first key at/after t, blend from the previous
    ///       key or the implicit origin.
    const auto locate = [&](const std::vector<Key>& keys, std::size_t& k, double& alpha) {
        const double last = keys.back().time;
        const double t = time > last ? last : (time < 0 ? 0 : time);
        k = keys.size();
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (t <= keys[i].time) { k = i; break; }
        if (k == keys.size()) return false;
        const double prev = k == 0 ? 0.0 : keys[k - 1].time;
        alpha = (keys[k].time - prev) > 1.1920929e-07 ? (t - prev) / (keys[k].time - prev) : 1.0;
        return true;
    };
    if (!m_translation.empty()) {
        std::size_t k; double a;
        if (!locate(m_translation, k, a)) for (int i = 0; i < 3; ++i) p.t[i] = m_translation.back().v[i];
        else {
            const double zero[3] = { 0, 0, 0 };
            const double* prev = k == 0 ? zero : m_translation[k - 1].v;
            for (int i = 0; i < 3; ++i) p.t[i] = prev[i] * (1 - a) + m_translation[k].v[i] * a;
        }
    }
    if (!m_rotation.empty()) {
        std::size_t k; double a;
        if (!locate(m_rotation, k, a)) std::memcpy(p.q, m_rotation.back().v, sizeof p.q);
        else {
            const double ident[4] = { 0, 0, 0, 1 };
            FastSlerp(a, k == 0 ? ident : m_rotation[k - 1].v, m_rotation[k].v, p.q);
        }
    }
    return p;
}

bool FoldMotionOntoRoot(AnimationDef& def, std::size_t rootTrack, std::string* err) {
    if (!def.motion) return false;
    if (rootTrack >= def.tracks.size() || def.numFrames <= 0 || def.frameDuration <= 0) {
        if (err) *err = "no root track or frame timing";
        return false;
    }
    const EngineMotionCurve curve(*def.motion);
    AnimTrackDef& root = def.tracks[rootTrack];
    std::vector<Vec3Keyframe> tr(def.numFrames);
    std::vector<QuatKeyframe> ro(def.numFrames);
    for (int f = 0; f < def.numFrames; ++f) {
        const double time = FrameTime(def, f);
        const MotionPose m = curve.Sample(time);
        Xf mx; std::memcpy(mx.t, m.t, sizeof mx.t); std::memcpy(mx.q, m.q, sizeof mx.q);
        const Xf r = compose(mx, SampleTrack(root, f, time));
        tr[f].time = ro[f].time = static_cast<float>(time);
        for (int i = 0; i < 3; ++i) tr[f].value[i] = static_cast<float>(r.t[i]);
        for (int i = 0; i < 4; ++i) ro[f].value[i] = static_cast<float>(r.q[i]);
    }
    root.translation = std::move(tr);
    root.rotation    = std::move(ro);
    def.motion.reset();
    return true;
}

std::optional<CB::core::animdata::MotionRecord> ExtractRootMotion(AnimationDef& def, std::size_t rootTrack) {
    if (rootTrack >= def.tracks.size() || def.numFrames <= 1 || def.frameDuration <= 0) return std::nullopt;
    AnimTrackDef& root = def.tracks[rootTrack];
    const int n = def.numFrames;
    std::vector<Xf> pose(n), motion(n);
    for (int f = 0; f < n; ++f) pose[f] = SampleTrack(root, f, FrameTime(def, f));
    const Xf inv0 = inverse(pose[0]);
    bool moves = false;
    double prevQ[4] = { 0, 0, 0, 1 };
    for (int f = 0; f < n; ++f) {
        const Xf d = compose(pose[f], inv0);
        Xf m;
        std::memcpy(m.t, d.t, sizeof m.t);
        /// @note Yaw = the twist of Δ about Z (swing-twist); the engine's motion carries no pitch/roll (vanilla: 0 keys).
        m.q[0] = 0; m.q[1] = 0; m.q[2] = d.q[2]; m.q[3] = d.q[3];
        qnorm(m.q);
        if (qdot(m.q, prevQ) < 0) for (double& c : m.q) c = -c;
        std::memcpy(prevQ, m.q, sizeof prevQ);
        motion[f] = m;
        if (std::abs(m.t[0]) + std::abs(m.t[1]) + std::abs(m.t[2]) > 1e-4 || std::abs(m.q[2]) > 1e-6) moves = true;
    }
    if (!moves) return std::nullopt;

    CB::core::animdata::MotionRecord rec;
    rec.duration = Num(def.duration);
    for (int f = 1; f < n; ++f) {
        /// @note The engine reads key times as IEEE halves: a key at frame time t lands at half(t), up to 1/256 s off at
        ///       4-8 s — at dragon-takeoff speed that is several units. So each key sits AT half(t), its value the
        ///       motion interpolated to that time; the engine then reproduces the frame curve exactly at its keys.
        ///       The LAST key keeps the full end value at the half at-or-below the clip end: the engine clamps to it, so
        ///       the clip still delivers its whole motion (vanilla's own last key lands early the same way).
        const double exact = FrameTime(def, f);
        double keyTime = EngineTime(exact);
        const bool last = f == n - 1;
        if (last && keyTime > exact) {
            std::uint16_t h = FloatToHalf(static_cast<float>(exact));
            keyTime = HalfToFloat(--h);
        }
        const int g = keyTime < exact ? f - 1 : std::min(f + 1, n - 1) == f ? f - 1 : f + 1;
        const double a = last ? 0.0 : (keyTime - exact) / (FrameTime(def, g) - exact);
        Xf m = motion[f];
        double qg[4]; std::memcpy(qg, motion[g].q, sizeof qg);
        if (qdot(qg, m.q) < 0) for (double& c : qg) c = -c;
        for (int i = 0; i < 3; ++i) m.t[i] += (motion[g].t[i] - m.t[i]) * a;
        for (int i = 0; i < 4; ++i) m.q[i] += (qg[i] - m.q[i]) * a;
        qnorm(m.q);
        const std::string t = Num(keyTime);
        rec.translations.push_back(t + " " + Num(m.t[0]) + " " + Num(m.t[1]) + " " + Num(m.t[2]));
        rec.rotations.push_back(t + " " + Num(m.q[0]) + " " + Num(m.q[1]) + " " + Num(m.q[2]) + " " + Num(m.q[3]));
        const Xf r = compose(inverse(motion[f]), pose[f]);
        root.translation.resize(n); root.rotation.resize(n);
        root.translation[f].time = root.rotation[f].time = static_cast<float>(FrameTime(def, f));
        for (int i = 0; i < 3; ++i) root.translation[f].value[i] = static_cast<float>(r.t[i]);
        for (int i = 0; i < 4; ++i) root.rotation[f].value[i] = static_cast<float>(r.q[i]);
    }
    root.translation[0].time = root.rotation[0].time = 0.f;
    for (int i = 0; i < 3; ++i) root.translation[0].value[i] = static_cast<float>(pose[0].t[i]);
    for (int i = 0; i < 4; ++i) root.rotation[0].value[i] = static_cast<float>(pose[0].q[i]);
    return rec;
}

std::optional<FoldCheck> CheckFoldRoundTrip(const AnimationDef& def, std::size_t rootTrack) {
    if (!def.motion || rootTrack >= def.tracks.size() || def.numFrames <= 1 || def.frameDuration <= 0) return std::nullopt;
    const EngineMotionCurve vanilla(*def.motion);
    AnimationDef work = def;
    if (!FoldMotionOntoRoot(work, rootTrack)) return std::nullopt;
    const auto extracted = ExtractRootMotion(work, rootTrack);
    const EngineMotionCurve ours(extracted ? *extracted : CB::core::animdata::MotionRecord{});

    const auto angle = [](const double a[4], const double b[4]) {
        return 2.0 * std::acos(std::min(1.0, std::abs(qdot(a, b)))) * 57.29577951308232;
    };
    const auto dist = [](const double a[3], const double b[3]) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
    };
    const auto asXf = [](const MotionPose& m) { Xf x; std::memcpy(x.t, m.t, sizeof x.t); std::memcpy(x.q, m.q, sizeof x.q); return x; };

    FoldCheck c;
    c.extracted = extracted.has_value();
    {
        const std::uint16_t h = FloatToHalf(def.duration);
        c.keyTimeStep = static_cast<double>(HalfToFloat(static_cast<std::uint16_t>(h + 1))) - HalfToFloat(h);
    }
    const AnimTrackDef& src = def.tracks[rootTrack];
    const AnimTrackDef& out = work.tracks[rootTrack];
    const Xf src0Inv = inverse(SampleTrack(src, 0, 0.0));
    for (int f = 0; f < def.numFrames; ++f) {
        const double time = FrameTime(def, f);
        for (const double t : { time, time + 0.5 * def.frameDuration }) {
            if (t > def.duration + 1e-6) continue;
            const MotionPose a = vanilla.Sample(t), b = ours.Sample(t);
            if (dist(a.t, b.t) > c.motionErr) { c.motionErr = dist(a.t, b.t); c.worstTime = t; }
            c.motionDeg = std::max(c.motionDeg, angle(a.q, b.q));
        }
        if (f > 0) {
            const double prevT = FrameTime(def, f - 1);
            for (const EngineMotionCurve* curve : { &vanilla, &ours }) {
                const MotionPose p0 = curve->Sample(prevT), p1 = curve->Sample(time);
                c.maxSpeed = std::max(c.maxSpeed, dist(p0.t, p1.t) / def.frameDuration);
                c.maxTurn  = std::max(c.maxTurn, angle(p0.q, p1.q) / def.frameDuration);
            }
        }
        const Xf rootSrc = SampleTrack(src, f, time);
        const Xf v = compose(asXf(vanilla.Sample(time)), rootSrc);
        const Xf o = compose(asXf(ours.Sample(time)), SampleTrack(out, f, time));
        c.visualErr = std::max(c.visualErr, dist(v.t, o.t));
        c.visualDeg = std::max(c.visualDeg, angle(v.q, o.q));
        const Xf d = compose(rootSrc, src0Inv);
        const double zero[3] = { 0, 0, 0 };
        c.rootDrift = std::max(c.rootDrift, dist(d.t, zero));
        double tw[4] = { 0, 0, d.q[2], d.q[3] }; qnorm(tw);
        const double ident[4] = { 0, 0, 0, 1 };
        c.rootYawDeg = std::max(c.rootYawDeg, angle(tw, ident));
    }
    return c;
}

bool FoldRoundTrips(const FoldCheck& c) {
    const auto within = [&](double err, double rate) { return err <= 0.01 + rate * std::max(c.keyTimeStep, 0.001); };
    return c.rootDrift < 0.01 && c.rootYawDeg < 0.1 && within(c.motionErr, c.maxSpeed) && within(c.motionDeg, c.maxTurn) &&
           within(c.visualErr, c.maxSpeed) && within(c.visualDeg, c.maxTurn);
}

namespace {
/// @brief Remove the unit's `motion` key (a block or `motion: none`), up to the next `animation:` key or the end.
void StripMotion(std::string& text) {
    const std::size_t at = text.find("\n  motion:");
    if (at == std::string::npos) return;
    std::size_t end = text.size();
    for (std::size_t p = text.find('\n', at + 1); p != std::string::npos && p + 3 < text.size(); p = text.find('\n', p + 1))
        if (text.compare(p + 1, 2, "  ") == 0 && text[p + 3] != ' ') { end = p; break; }
    text.erase(at + 1, end == text.size() ? std::string::npos : end - at);
}

bool IsStill(const CB::core::animdata::MotionRecord& m) {
    for (const auto& s : m.translations) {
        const auto v = Numbers(s);
        if (v.size() >= 4 && (v[1] != 0 || v[2] != 0 || v[3] != 0)) return false;
    }
    for (const auto& s : m.rotations) {
        const auto v = Numbers(s);
        if (v.size() >= 5 && (v[1] != 0 || v[2] != 0 || v[3] != 0 || std::abs(v[4]) != 1)) return false;
    }
    return true;
}
} // namespace

UnitMotionForm FinalizeUnitMotion(std::string& text, const std::optional<CB::core::animdata::MotionRecord>& motion,
                                  std::string* err) {
    StripMotion(text);
    if (!text.empty() && text.back() != '\n') text += '\n';
    AnimationDef def;
    try { def = AnimationYamlLoader::LoadFromString(text, "<unit>"); }
    catch (const std::exception& e) { if (err) *err = e.what(); return UnitMotionForm::None; }

    if (motion) {
        def.motion = *motion;
        if (!IsStill(*motion)) {
            const auto check = CheckFoldRoundTrip(def, 0);
            if (check && FoldRoundTrips(*check) && FoldMotionOntoRoot(def, 0) && ReplaceTrackYaml(text, 0, def.tracks[0]))
                return UnitMotionForm::Folded;
        }
        const std::string body = CB::core::animdata::EmitMotionSidecar(*motion);
        text += "  motion:\n";
        for (std::size_t p = 0; p < body.size();) {
            const std::size_t nl = body.find('\n', p);
            const std::string line = body.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
            if (!line.empty()) text += "    " + line + "\n";
            p = nl == std::string::npos ? body.size() : nl + 1;
        }
        return UnitMotionForm::Explicit;
    }
    if (ExtractRootMotion(def, 0)) {
        text += "  motion: none\n";
        return UnitMotionForm::Visual;
    }
    return UnitMotionForm::None;
}

std::optional<CB::core::animdata::MotionRecord> ResolveRootMotion(AnimationDef& def, std::size_t rootTrack) {
    if (def.motion) return def.motion;
    if (def.rootMotionVisual) return std::nullopt;
    return ExtractRootMotion(def, rootTrack);
}

} // namespace CB::core::anim
