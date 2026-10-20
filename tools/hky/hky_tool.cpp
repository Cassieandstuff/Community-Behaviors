// hky-tool — the standalone .hky pack/unpack driver over codec/serialization (HkyArchive): the
// base-master diff-strategy tooling, linking only the engine.
//
//   hky-tool pack        <bundleDir> -o <out.hky>        YAML tree  -> single-file .hky (round-trip-checked)
//   hky-tool unpack      <in.hky>    -o <outDir>          .hky       -> YAML tree on disk (original case kept)
//   hky-tool bsa-list    <in.bsa>    [prefix]             list an SSE/AE (v105) archive's files
//   hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix] extract them (archive-stored lowercase paths)
//   hky-tool autotag     <clips.hky|dir> -o <injectDir> [reportDir]
//                                                         seed one motion-matching tag config per animation
//
// Case note (Havok is CASE-SENSITIVE): unpack writes each entry at its ORIGINAL-CASE path, never the
// normalized lookup key — a miscased path binds to nothing/wrong at load. Unpack to a SHORT dir (MAX_PATH).

#include <codec/bsa/BsaArchive.h>
#include <codec/format/AnimationYamlLoader.h>
#include <codec/resource/PathKey.h>
#include <codec/serialization/HkyArchive.h>
#include <disk/Disk.h>
#include <exec/Parallel.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// hky pack: compress an uncompressed bundle YAML tree into a single-file .hky, then re-read it
// (LoadFromFile) to confirm the archive is valid — the pack<->load round-trip sanity.
int doHkyPack(const std::string& dir, const std::string& out) {
    if (out.empty()) { std::printf("usage: hky-tool pack <bundleDir> -o <out.hky>\n"); return 2; }
    std::string err;
    if (!CB::engine::codec::HkyArchive::PackDirectory(dir, out, err)) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(out, err);   // round-trip sanity
    if (!arc) { std::printf("FAIL: packed but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: hky pack '%s' -> %s (%zu unit(s)).\n", dir.c_str(), out.c_str(), arc->units().size());
    return 0;
}

// hky unpack: decompress a single-file .hky into its uncompressed YAML tree on disk, ORIGINAL path
// case preserved. For inspecting/diffing a packed master (Skyrim.hky) or any bundle.
int doHkyUnpack(const std::string& hkyPath, const std::string& outDir) {
    if (outDir.empty()) { std::printf("usage: hky-tool unpack <in.hky> -o <outDir>\n"); return 2; }
    std::string err;
    auto arc = CB::engine::codec::HkyArchive::LoadFromFile(hkyPath, err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }

    // Same ordered range, two views: normalized keys drive file() lookup; original-case paths give the
    // on-disk layout (index-aligned — both iterate the archive's file map in order).
    const auto keys  = arc->filesUnder("");
    const auto paths = arc->filesUnderOrig("");
    namespace fs = std::filesystem;
    std::atomic<std::size_t> written{ 0 };
    try {
        /// @note Each file is read + inflated on the worker that writes it (HkyArchive reads are thread-safe).
        CB::engine::exec::ParallelFor(keys.size(), [&](std::size_t i) {
            const auto content = arc->file(keys[i]);
            if (!content) return;   // keys come from the same map — should always resolve
            const fs::path dst = fs::path(outDir) / fs::path(paths[i]);
            if (std::string werr; !CB::engine::disk::WriteFile(dst, *content, {}, &werr)) throw std::runtime_error(werr);
            ++written;
        });
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("OK: hky unpack '%s' -> %s (%zu file(s), %zu unit(s)).\n",
                hkyPath.c_str(), outDir.c_str(), written.load(), arc->units().size());
    return 0;
}

/// @note Path identity (PathKey) on both sides, so "Meshes/Actors" matches the archive's "meshes\actors\…".
bool BsaPrefixMatch(const std::string& path, const std::string& prefix) {
    using CB::engine::codec::resource::PathKey;
    return PathKey(path).rfind(PathKey(prefix), 0) == 0;
}

int doBsaList(const std::string& bsaPath, const std::string& prefix) {
    std::string err;
    const auto arc = CB::engine::codec::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    std::size_t shown = 0;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        std::printf("%s\t%u%s\n", e.path.c_str(), e.size, e.compressed ? "\tlz4" : "");
        ++shown;
    }
    std::printf("OK: v%u, %zu file(s), %zu listed.\n", arc->Version(), arc->Entries().size(), shown);
    return 0;
}

int doBsaExtract(const std::string& bsaPath, const std::string& outDir, const std::string& prefix) {
    if (outDir.empty()) { std::printf("usage: hky-tool bsa-extract <in.bsa> -o <outDir> [prefix]\n"); return 2; }
    std::string err;
    const auto arc = CB::engine::codec::bsa::Archive::Open(bsaPath, &err);
    if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
    namespace fs = std::filesystem;
    std::size_t written = 0, failed = 0;
    std::vector<std::uint8_t> bytes;
    for (const auto& e : arc->Entries()) {
        if (!prefix.empty() && !BsaPrefixMatch(e.path, prefix)) continue;
        if (!arc->Extract(e, bytes, &err)) { std::printf("  FAIL %s: %s\n", e.path.c_str(), err.c_str()); ++failed; continue; }
        std::string rel = e.path;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        const fs::path dst = fs::path(outDir) / fs::path(rel);
        if (std::string werr; !CB::engine::disk::WriteFile(dst, bytes, {}, &werr)) { std::printf("  FAIL %s\n", werr.c_str()); ++failed; continue; }
        ++written;
    }
    std::printf("%s: bsa extract '%s' -> %s (%zu written, %zu failed).\n", failed ? "FAIL" : "OK",
                bsaPath.c_str(), outDir.c_str(), written, failed);
    return failed ? 1 : 0;
}

/// @brief The motion-matching auto-tagger: measures every animation's root and feet and seeds one inject config per
///        clip (`{"targets": [{"path", "field": "tags", "values": {Tag: [[start, end), ...]}}]}`, frames end-exclusive).
/// @note  A SEED, not a build step: an existing config is never overwritten, so hand corrections survive a re-run and
///        only clips without a config get one.
namespace autotag {

struct V3 { double x = 0, y = 0, z = 0; };
struct Qt { double x = 0, y = 0, z = 0, w = 1; };
struct Xf { V3 p; Qt q; };

V3     operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
V3     operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
V3     operator*(V3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
V3     Cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
double Length(V3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }

Qt Mul(Qt a, Qt b) {
    return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
             a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
             a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
             a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
Qt Conj(Qt q) { return { -q.x, -q.y, -q.z, q.w }; }
Qt Normalize(Qt q) {
    const double n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return n > 0 ? Qt{ q.x / n, q.y / n, q.z / n, q.w / n } : Qt{};
}
V3 Rotate(Qt q, V3 v) {
    const V3 u{ q.x, q.y, q.z };
    const V3 t = Cross(u, v) * 2.0;
    return v + t * q.w + Cross(u, t);
}
/// @brief `local` placed under `parent` (Havok's parent-relative bone transforms).
Xf Compose(const Xf& parent, const Xf& local) { return { parent.p + Rotate(parent.q, local.p), Normalize(Mul(parent.q, local.q)) }; }

/// @brief The key interval holding `t`: the keys around it and the blend between them (held past either end).
template <class K>
std::pair<const K*, double> Bracket(const std::vector<K>& keys, double t, const K*& next) {
    const auto it = std::upper_bound(keys.begin(), keys.end(), t, [](double v, const K& k) { return v < k.time; });
    if (it == keys.begin()) { next = &keys.front(); return { &keys.front(), 0.0 }; }
    if (it == keys.end())   { next = &keys.back();  return { &keys.back(), 0.0 }; }
    const K& a = *(it - 1);
    next = &*it;
    const double span = static_cast<double>(it->time) - a.time;
    return { &a, span > 0 ? (t - a.time) / span : 0.0 };
}
V3 SampleV3(const std::vector<CB::engine::anim::Vec3Keyframe>& keys, double t) {
    const CB::engine::anim::Vec3Keyframe* b = nullptr;
    const auto [a, f] = Bracket(keys, t, b);
    const V3 va{ a->value[0], a->value[1], a->value[2] }, vb{ b->value[0], b->value[1], b->value[2] };
    return va + (vb - va) * f;
}
Qt SampleQt(const std::vector<CB::engine::anim::QuatKeyframe>& keys, double t) {
    const CB::engine::anim::QuatKeyframe* b = nullptr;
    const auto [a, f] = Bracket(keys, t, b);
    const Qt qa{ a->value[0], a->value[1], a->value[2], a->value[3] };
    Qt       qb{ b->value[0], b->value[1], b->value[2], b->value[3] };
    if (qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w < 0) qb = { -qb.x, -qb.y, -qb.z, -qb.w };
    return Normalize({ qa.x + (qb.x - qa.x) * f, qa.y + (qb.y - qa.y) * f, qa.z + (qb.z - qa.z) * f, qa.w + (qb.w - qa.w) * f });
}

constexpr double      kPi       = 3.14159265358979323846;
constexpr const char* kRootBone = "NPC Root [Root]";
/// @note The humanoid leg chains below the root, verified against Skyrim.hky's skeleton.hkx (each bone's `parent:`).
constexpr std::array<std::array<const char*, 5>, 2> kLegs{ {
    { "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]" },
    { "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]" },
} };

/// @brief The measuring thresholds (Skyrim units, seconds, degrees).
/// @note  SEEDS to tune against the report, not settled values. The speed bands sit midway between the clips' measured
///        plateaus (walk 92, run 284, sprint 426 units/s in the retarget report). Actors face +Y, Z is up.
struct Thresholds {
    double idleSpeed       = 15.0;    ///< below this the root is standing (units/s)
    double walkRun         = 188.0;   ///< Walk below, Run from here (units/s)
    double runSprint       = 355.0;   ///< Sprint from here (units/s)
    double strafeDeg       = 45.0;    ///< travel this far off facing is a strafe
    double backwardDeg     = 135.0;   ///< and this far is backward
    double turnRate        = 45.0;    ///< facing turn rate that counts as turning (deg/s)
    double accel           = 100.0;   ///< speed change that counts as accelerating (units/s^2)
    double plantHeight     = 3.0;     ///< foot at most this far above its lowest point in the clip (units)
    double plantSpeed      = 25.0;    ///< and moving at most this fast (units/s) ...
    double plantSpeedScale = 0.25;    ///< ... or this fraction of the root's speed, whichever is more
    int    plantMinFrames  = 2;       ///< plant runs shorter than this are dropped (a sprint contact is ~3 frames)
    int    smoothHalf      = 3;       ///< root signals: moving average over this many frames each side
    int    footSmoothHalf  = 1;       ///< foot signals: the same, kept short so short plants survive
    int    gapFrames       = 2;       ///< gaps up to this long inside a tag's run are filled
    int    minFrames       = 4;       ///< runs shorter than this are dropped
};

enum Tag { Idle, Walk, Run, Sprint, Forward, StrafeLeft, StrafeRight, Backward, TurnLeft, TurnRight, Accelerate,
           Decelerate, LeftPlant, RightPlant, kTagCount };
/// @note Tag names are compared byte-exact downstream, like every name CB carries.
constexpr std::array<const char*, kTagCount> kTagNames{ "Idle", "Walk", "Run", "Sprint", "Forward", "StrafeLeft",
    "StrafeRight", "Backward", "TurnLeft", "TurnRight", "Accelerate", "Decelerate", "LeftPlant", "RightPlant" };

using Ranges = std::vector<std::pair<int, int>>;

struct ClipResult {
    std::string                      err;   ///< empty = measured
    int                              frames   = 0;
    double                           maxSpeed = 0;
    std::array<Ranges, kTagCount>    ranges;
    std::string                      note;
    std::string                      frameRows;
};

/// @brief Central differences per frame (one-sided at the ends), over `dt` seconds per frame.
std::vector<double> Diff(const std::vector<double>& v, double dt) {
    const std::size_t n = v.size();
    std::vector<double> d(n, 0.0);
    if (n < 2) return d;
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t a = i == 0 ? 0 : i - 1, b = i + 1 == n ? n - 1 : i + 1;
        d[i] = (v[b] - v[a]) / (static_cast<double>(b - a) * dt);
    }
    return d;
}
std::vector<double> Smooth(const std::vector<double>& v, int half) {
    const int n = static_cast<int>(v.size());
    std::vector<double> s(v.size(), 0.0);
    for (int i = 0; i < n; ++i) {
        const int a = (std::max)(0, i - half), b = (std::min)(n - 1, i + half);
        double sum = 0;
        for (int j = a; j <= b; ++j) sum += v[j];
        s[i] = sum / (b - a + 1);
    }
    return s;
}
/// @brief A tag's per-frame verdicts as `[start, end)` runs: short gaps filled, then short runs dropped.
Ranges BuildRanges(const std::vector<char>& on, int gap, int minLen) {
    Ranges runs;
    const int n = static_cast<int>(on.size());
    for (int i = 0; i < n;) {
        if (!on[i]) { ++i; continue; }
        int j = i;
        while (j < n && on[j]) ++j;
        if (!runs.empty() && i - runs.back().second <= gap) runs.back().second = j;
        else runs.emplace_back(i, j);
        i = j;
    }
    std::erase_if(runs, [&](const auto& r) { return r.second - r.first < minLen; });
    return runs;
}

ClipResult Measure(const std::string& path, const std::string& yaml, const Thresholds& th) {
    ClipResult r;
    const auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(yaml, path);
    const double dt = def.frameDuration > 0 ? def.frameDuration : 1.0 / 30.0;
    const int    n  = def.numFrames > 0 ? def.numFrames : static_cast<int>(std::lround(def.duration / dt)) + 1;
    if (n < 2) { r.err = "fewer than 2 frames"; return r; }
    r.frames = n;

    std::unordered_map<std::string, const CB::engine::anim::AnimTrackDef*> tracks;
    for (const auto& t : def.tracks) tracks.emplace(t.bone, &t);
    const auto bone = [&](const char* name) -> const CB::engine::anim::AnimTrackDef* {
        const auto it = tracks.find(name);
        return it == tracks.end() || it->second->translation.empty() || it->second->rotation.empty() ? nullptr : it->second;
    };
    const auto* root = bone(kRootBone);
    if (!root) { r.err = std::string("no translation + rotation track on ") + kRootBone; return r; }
    std::array<std::array<const CB::engine::anim::AnimTrackDef*, 5>, 2> legs{};
    std::array<bool, 2> hasFoot{ true, true };
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < 5; ++b) {
            legs[s][b] = bone(kLegs[s][b]);
            if (!legs[s][b]) hasFoot[s] = false;
        }
    if (!hasFoot[0] || !hasFoot[1]) r.note = "no foot chain: plants not measured";

    std::vector<double> px(n), py(n), yaw(n);
    std::vector<Qt>     rq(n);
    std::array<std::vector<V3>, 2> foot{ std::vector<V3>(n), std::vector<V3>(n) };
    for (int i = 0; i < n; ++i) {
        const double t = i * dt;
        const Xf rootX{ SampleV3(root->translation, t), SampleQt(root->rotation, t) };
        px[i] = rootX.p.x;
        py[i] = rootX.p.y;
        rq[i] = rootX.q;
        const V3 f = Rotate(rootX.q, { 0, 1, 0 });
        yaw[i] = std::atan2(-f.x, f.y);
        if (i > 0) {
            while (yaw[i] - yaw[i - 1] > kPi) yaw[i] -= 2 * kPi;
            while (yaw[i] - yaw[i - 1] < -kPi) yaw[i] += 2 * kPi;
        }
        for (int s = 0; s < 2; ++s) {
            if (!hasFoot[s]) continue;
            Xf w = rootX;
            for (const auto* tr : legs[s]) w = Compose(w, { SampleV3(tr->translation, t), SampleQt(tr->rotation, t) });
            foot[s][i] = w.p;
        }
    }

    const auto vx = Smooth(Diff(px, dt), th.smoothHalf), vy = Smooth(Diff(py, dt), th.smoothHalf);
    std::vector<double> speed(n), heading(n);
    for (int i = 0; i < n; ++i) {
        speed[i] = std::hypot(vx[i], vy[i]);
        const V3 lv = Rotate(Conj(rq[i]), { vx[i], vy[i], 0 });
        heading[i] = std::atan2(lv.x, lv.y) * 180.0 / kPi;
        r.maxSpeed = (std::max)(r.maxSpeed, speed[i]);
    }
    auto yawRate = Smooth(Diff(yaw, dt), th.smoothHalf);
    for (auto& y : yawRate) y *= 180.0 / kPi;
    const auto accel = Smooth(Diff(speed, dt), th.smoothHalf);

    std::array<std::vector<double>, 2> footH, footV;
    for (int s = 0; s < 2; ++s) {
        if (!hasFoot[s]) continue;
        std::vector<double> x(n), y(n), z(n);
        double minZ = foot[s][0].z;
        for (int i = 0; i < n; ++i) { x[i] = foot[s][i].x; y[i] = foot[s][i].y; z[i] = foot[s][i].z; minZ = (std::min)(minZ, z[i]); }
        const auto dx = Diff(x, dt), dy = Diff(y, dt), dz = Diff(z, dt);
        std::vector<double> v(n);
        for (int i = 0; i < n; ++i) v[i] = Length({ dx[i], dy[i], dz[i] });
        footV[s] = Smooth(v, th.footSmoothHalf);
        footH[s].resize(n);
        for (int i = 0; i < n; ++i) footH[s][i] = z[i] - minZ;
    }

    std::array<std::vector<char>, kTagCount> on;
    for (auto& o : on) o.assign(n, 0);
    for (int i = 0; i < n; ++i) {
        const double s = speed[i], h = heading[i], ah = std::fabs(h);
        const bool   moving = s >= th.idleSpeed;
        on[Idle][i]        = !moving;
        on[Walk][i]        = moving && s < th.walkRun;
        on[Run][i]         = s >= th.walkRun && s < th.runSprint;
        on[Sprint][i]      = s >= th.runSprint;
        on[Forward][i]     = moving && ah < th.strafeDeg;
        on[StrafeLeft][i]  = moving && h <= -th.strafeDeg && h >= -th.backwardDeg;
        on[StrafeRight][i] = moving && h >= th.strafeDeg && h <= th.backwardDeg;
        on[Backward][i]    = moving && ah > th.backwardDeg;
        on[TurnLeft][i]    = yawRate[i] >= th.turnRate;
        on[TurnRight][i]   = yawRate[i] <= -th.turnRate;
        on[Accelerate][i]  = accel[i] >= th.accel;
        on[Decelerate][i]  = accel[i] <= -th.accel;
        /// @note The speed limit grows with the root's speed: at a run or sprint the ankle keeps rolling through a
        ///       contact (10-90 units/s measured), so a fixed walk-tuned limit missed nearly every fast contact.
        const double plantLimit = (std::max)(th.plantSpeed, th.plantSpeedScale * s);
        for (int f = 0; f < 2; ++f)
            on[f == 0 ? LeftPlant : RightPlant][i] =
                hasFoot[f] && footH[f][i] <= th.plantHeight && footV[f][i] <= plantLimit;
    }
    for (int t = 0; t < kTagCount; ++t)
        r.ranges[t] = BuildRanges(on[t], th.gapFrames, t == LeftPlant || t == RightPlant ? th.plantMinFrames : th.minFrames);

    char row[256];
    for (int i = 0; i < n; ++i) {
        std::snprintf(row, sizeof(row), "\t%d\t%.2f\t%.1f\t%.1f\t%.1f", i, speed[i], heading[i], yawRate[i], accel[i]);
        r.frameRows += path + row;
        for (int f = 0; f < 2; ++f) {
            if (hasFoot[f]) std::snprintf(row, sizeof(row), "\t%.2f\t%.2f", footH[f][i], footV[f][i]);
            else            std::snprintf(row, sizeof(row), "\t\t");
            r.frameRows += row;
        }
        r.frameRows += '\n';
    }
    return r;
}

std::string JsonString(std::string_view s) {
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (static_cast<unsigned char>(c) < 0x20) { char u[8]; std::snprintf(u, sizeof(u), "\\u%04x", c); out += u; }
        else out += c;
    }
    return out + '"';
}

std::string ConfigJson(const std::string& path, const ClipResult& r) {
    std::string j = "{\n  \"targets\": [\n    {\n      \"path\": " + JsonString(path) +
                    ",\n      \"field\": \"tags\",\n      \"values\": {";
    bool any = false;
    for (int t = 0; t < kTagCount; ++t) {
        if (r.ranges[t].empty()) continue;
        j += any ? ",\n" : "\n";
        any = true;
        j += std::string("        ") + JsonString(kTagNames[t]) + ": [";
        for (std::size_t k = 0; k < r.ranges[t].size(); ++k)
            j += (k ? ", [" : "[") + std::to_string(r.ranges[t][k].first) + ", " + std::to_string(r.ranges[t][k].second) + "]";
        j += "]";
    }
    j += any ? "\n      }" : "}";
    return j + "\n    }\n  ]\n}\n";
}

}  // namespace autotag

/// @brief `autotag`: every animation (a `.hkx` file, a YAML compile target) in a .hky or a bundle directory, measured
///        and seeded as `<injectDir>/<path below the clips' common folder>.json`; `reportDir` gets summary.tsv (one row
///        per clip) and frames.tsv (every frame's signals) for tuning the thresholds.
int doAutoTag(const std::string& in, const std::string& outDir, const std::string& reportDir) {
    if (in.empty() || outDir.empty()) {
        std::printf("usage: hky-tool autotag <clips.hky|dir> -o <injectDir> [reportDir]\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using CB::engine::codec::resource::HasExt;
    std::vector<std::string> paths, keys;
    std::shared_ptr<CB::engine::codec::HkyArchive> arc;
    std::error_code ec;
    if (fs::is_directory(in, ec)) {
        for (const auto& e : fs::recursive_directory_iterator(in, ec))
            if (e.is_regular_file() && HasExt(e.path().generic_string(), ".hkx"))
                paths.push_back(fs::relative(e.path(), in).generic_string());
        std::sort(paths.begin(), paths.end());
    } else {
        std::string err;
        arc = CB::engine::codec::HkyArchive::LoadFromFile(in, err);
        if (!arc) { std::printf("FAIL: %s\n", err.c_str()); return 1; }
        const auto k = arc->filesUnder(""), o = arc->filesUnderOrig("");
        for (std::size_t i = 0; i < k.size(); ++i)
            if (HasExt(o[i], ".hkx")) { keys.push_back(k[i]); paths.push_back(o[i]); }
    }
    if (paths.empty()) { std::printf("FAIL: no animations (.hkx files) in '%s'\n", in.c_str()); return 1; }

    const auto dirOf = [](const std::string& s) {
        const auto k = s.rfind('/');
        return k == std::string::npos ? std::string() : s.substr(0, k + 1);
    };
    std::string prefix = dirOf(paths.front());
    for (const auto& p : paths)
        while (!prefix.empty() && p.compare(0, prefix.size(), prefix) != 0) prefix = dirOf(prefix.substr(0, prefix.size() - 1));

    const autotag::Thresholds th{};
    std::vector<autotag::ClipResult> res(paths.size());
    CB::engine::exec::ParallelFor(paths.size(), [&](std::size_t i) {
        try {
            const auto yaml = arc ? arc->file(keys[i]) : CB::engine::disk::ReadFile(fs::path(in) / paths[i]);
            if (!yaml) { res[i].err = "unreadable"; return; }
            res[i] = autotag::Measure(paths[i], *yaml, th);
        } catch (const std::exception& e) {
            res[i].err = e.what();
        }
    });

    std::size_t seeded = 0, kept = 0, failed = 0;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (!res[i].err.empty()) { std::printf("  FAIL %s: %s\n", paths[i].c_str(), res[i].err.c_str()); ++failed; continue; }
        const std::string rel = paths[i].substr(prefix.size());
        const fs::path    dst = fs::path(outDir) / (rel.substr(0, rel.size() - 4) + ".json");
        if (fs::exists(dst, ec)) { ++kept; continue; }
        if (std::string werr; !CB::engine::disk::WriteFile(dst, autotag::ConfigJson(paths[i], res[i]), {}, &werr)) {
            std::printf("  FAIL %s\n", werr.c_str());
            ++failed;
            continue;
        }
        ++seeded;
    }

    if (!reportDir.empty()) {
        std::string summary = "path\tframes\tmax_speed";
        for (const char* t : autotag::kTagNames) summary += std::string("\t") + t;
        summary += "\tnote\n";
        std::string frames = "path\tframe\tspeed\theading_deg\tyaw_rate_dps\taccel\tl_foot_h\tl_foot_v\tr_foot_h\tr_foot_v\n";
        for (std::size_t i = 0; i < paths.size(); ++i) {
            const auto& r = res[i];
            if (!r.err.empty()) continue;
            char head[64];
            std::snprintf(head, sizeof(head), "\t%d\t%.1f", r.frames, r.maxSpeed);
            summary += paths[i] + head;
            for (const auto& rg : r.ranges) {
                int covered = 0;
                for (const auto& [a, b] : rg) covered += b - a;
                summary += "\t" + std::to_string(covered);
            }
            summary += "\t" + r.note + "\n";
            frames += r.frameRows;
        }
        std::string werr;
        if (!CB::engine::disk::WriteFile(fs::path(reportDir) / "summary.tsv", summary, {}, &werr) ||
            !CB::engine::disk::WriteFile(fs::path(reportDir) / "frames.tsv", frames, {}, &werr)) {
            std::printf("FAIL: report: %s\n", werr.c_str());
            return 1;
        }
    }

    std::printf("%s: autotag '%s': %zu clip(s), %zu seeded, %zu kept (existing configs are never overwritten), %zu failed.\n",
                failed ? "FAIL" : "OK", in.c_str(), paths.size(), seeded, kept, failed);
    return failed ? 1 : 0;
}

void usage() {
    std::printf("usage:\n"
                "  hky-tool pack        <bundleDir> -o <out.hky>\n"
                "  hky-tool unpack      <in.hky>    -o <outDir>\n"
                "  hky-tool bsa-list    <in.bsa>    [prefix]\n"
                "  hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix]\n"
                "  hky-tool autotag     <clips.hky|dir> -o <injectDir> [reportDir]\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];

    std::string arg, arg2, out;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { out = argv[++i]; }
        else if (arg.empty())         { arg = a; }
        else if (arg2.empty())        { arg2 = a; }
    }

    if (verb == "pack")        return doHkyPack(arg, out);
    if (verb == "unpack")      return doHkyUnpack(arg, out);
    if (verb == "bsa-list")    return doBsaList(arg, arg2);
    if (verb == "bsa-extract") return doBsaExtract(arg, out, arg2);
    if (verb == "autotag")     return doAutoTag(arg, out, arg2);
    usage();
    return 2;
}
