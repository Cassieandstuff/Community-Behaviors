// hky-tool — the standalone .hky pack/unpack driver over codec/serialization (HkyArchive): the
// base-master diff-strategy tooling, linking only the engine.
//
//   hky-tool pack        <bundleDir> -o <out.hky>        YAML tree  -> single-file .hky (round-trip-checked)
//   hky-tool unpack      <in.hky>    -o <outDir>          .hky       -> YAML tree on disk (original case kept)
//   hky-tool bsa-list    <in.bsa>    [prefix]             list an SSE/AE (v105) archive's files
//   hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix] extract them (archive-stored lowercase paths)
//   hky-tool autotag     <clips.hky|dir> -o <injectDir> [reportDir]
//                                                         seed one motion-matching tag config per animation
//   hky-tool assemble    <featureDir> <injectables.json> -o <out.hky>
//                                                         partitions + loose files + generators + inject configs
//   hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>
//                                                         merge + serialize every unit as the game's compile does
//
// Case note (Havok is CASE-SENSITIVE): unpack writes each entry at its ORIGINAL-CASE path, never the
// normalized lookup key — a miscased path binds to nothing/wrong at load. Unpack to a SHORT dir (MAX_PATH).

#include <codec/bsa/BsaArchive.h>
#include <codec/format/AnimationYamlLoader.h>
#include <compile/MotionFeatures.h>
#include <codec/resource/PathKey.h>
#include <codec/serialization/HkyArchive.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <interface/MasterTable.h>
#include <interface/reflection/HavokSchema.h>
#include <exec/Parallel.h>
#include <text/Text.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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

constexpr double      kPi       = 3.14159265358979323846;

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
    namespace ea = CB::engine::anim;
    ClipResult r;
    const auto  def = ea::AnimationYamlLoader::LoadFromString(yaml, path);
    std::string err;
    const auto  m = ea::MeasureClip(def, err);
    if (!m) { r.err = err; return r; }
    const int    n  = m->frames;
    const double dt = m->dt;
    r.frames        = n;
    const std::array<bool, 2> hasFoot{ m->hasFeet, m->hasFeet };
    if (!m->hasFeet) r.note = "no foot chain: plants not measured";
    const auto& foot = m->foot;

    std::vector<double> px(n), py(n), yaw(n);
    for (int i = 0; i < n; ++i) {
        px[i] = m->rootPos[i].x;
        py[i] = m->rootPos[i].y;
        const ea::MotionVec f = ea::Rotate(m->rootRot[i], { 0, 1, 0 });
        yaw[i] = std::atan2(-f.x, f.y);
        if (i > 0) {
            while (yaw[i] - yaw[i - 1] > kPi) yaw[i] -= 2 * kPi;
            while (yaw[i] - yaw[i - 1] < -kPi) yaw[i] += 2 * kPi;
        }
    }

    const auto vx = Smooth(Diff(px, dt), th.smoothHalf), vy = Smooth(Diff(py, dt), th.smoothHalf);
    std::vector<double> speed(n), heading(n);
    for (int i = 0; i < n; ++i) {
        speed[i] = std::hypot(vx[i], vy[i]);
        const ea::MotionVec lv = ea::ToLocal(m->rootRot[i], { vx[i], vy[i], 0 });
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
        for (int i = 0; i < n; ++i) v[i] = std::sqrt(dx[i] * dx[i] + dy[i] * dy[i] + dz[i] * dz[i]);
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

/// @brief The build's config-driven injection: `inject/**/*.json` configs write values into named fields of files in a
///        feature's partitions, as allowed by the injectables table (tools/hky/injectables.json: file kind -> field ->
///        value type).
namespace assemble {

using Json = nlohmann::ordered_json;

/// @brief `text` parsed as JSON, with a key repeated inside one object rejected.
/// @note  nlohmann keeps the last of two equal keys silently, which would drop a tag's first definition unseen.
Json ParseStrict(const std::string& text) {
    std::vector<std::vector<std::string>> open;
    const Json::parser_callback_t cb = [&](int, Json::parse_event_t ev, Json& parsed) {
        if (ev == Json::parse_event_t::object_start) open.emplace_back();
        else if (ev == Json::parse_event_t::object_end && !open.empty()) open.pop_back();
        else if (ev == Json::parse_event_t::key && !open.empty()) {
            const auto key = parsed.get<std::string>();
            if (std::find(open.back().begin(), open.back().end(), key) != open.back().end())
                throw std::runtime_error("duplicate key \"" + key + "\"");
            open.back().push_back(key);
        }
        return true;
    };
    return Json::parse(text, cb);
}

/// @brief The file kinds the injector knows; a kind in the injectables table must be one of these.
/// @note  Recognizing a kind is format knowledge, so it lives here; the table only says what each kind may receive.
enum class Kind { Animation };
/// @brief The value a field takes; a type in the injectables table must be one of these shapes.
/// @note  rangeMap = {name: [[start, end], ...]}; oneOf = one of the listed names, as a string.
struct FieldType {
    enum class Shape { RangeMap, OneOf } shape = Shape::RangeMap;
    std::vector<std::string>             oneOf;
};

using Table = std::map<Kind, std::map<std::string, FieldType>>;

/// @brief The fields whose injected value this tool reads back through the animation loader.
/// @note  A field the loader does not read would inject cleanly and then do nothing (the loader skips unknown keys),
///        so the table may name only fields with a read-back (InjectAnimation).
constexpr std::array<std::string_view, 2> kAnimationReadBack{ "tags", "motionTarget" };

/// @brief A path that can name an animation: a bare `.hkx` FILE, not inside a unit (a unit is a `.hkx/` folder).
bool IsAnimationPath(const std::string& path) {
    using CB::engine::codec::resource::PathKey;
    return CB::engine::codec::resource::HasExt(path, ".hkx") && PathKey(path).find(".hkx/") == std::string::npos;
}

/// @brief An animation: an animation path whose YAML root key is `animation`.
bool IsAnimation(const std::string& path, std::string_view text) {
    if (!IsAnimationPath(path)) return false;
    text = CB::engine::text::StripBom(text);
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end  = (std::min)(text.find('\n', at), text.size());
        std::string_view  line = text.substr(at, end - at);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') return line == "animation:";
        at = end + 1;
    }
    return false;
}

/// @brief Empty when `v` fits `type`, else why not.
/// @note  rangeMap ranges are integers, 0 <= start < end. Whether they suit the file (sorted, inside the clip) is the
///        consumer's check: the injector checks shape only.
std::string CheckType(const FieldType& type, const Json& v) {
    switch (type.shape) {
    case FieldType::Shape::OneOf: {
        if (v.is_string() && std::find(type.oneOf.begin(), type.oneOf.end(), v.get<std::string>()) != type.oneOf.end()) return {};
        std::string names;
        for (const auto& n : type.oneOf) names += (names.empty() ? "" : ", ") + n;
        return "not one of " + names + ": " + v.dump();
    }
    case FieldType::Shape::RangeMap:
        if (!v.is_object()) return "a rangeMap is {name: [[start, end], ...]}";
        for (const auto& [name, ranges] : v.items()) {
            if (name.empty()) return "an empty name";
            if (!ranges.is_array()) return "'" + name + "' is not a list of ranges";
            for (const auto& r : ranges) {
                if (!r.is_array() || r.size() != 2 || !r[0].is_number_integer() || !r[1].is_number_integer())
                    return "'" + name + "' has a range that is not [start, end] in whole frames";
                if (r[0].get<long long>() < 0 || r[0].get<long long>() >= r[1].get<long long>())
                    return "'" + name + "' has a range that does not run 0 <= start < end: " + r.dump();
            }
        }
        return {};
    }
    return "unknown type";
}

/// @brief `v` as YAML flow text (JSON's own escaping; YAML's double-quoted strings read it the same).
std::string Flow(const Json& v) {
    std::string s;
    if (v.is_object()) {
        for (const auto& [k, x] : v.items()) s += (s.empty() ? "" : ", ") + Json(k).dump() + ": " + Flow(x);
        return "{" + s + "}";
    }
    if (v.is_array()) {
        for (const auto& x : v) s += (s.empty() ? "" : ", ") + Flow(x);
        return "[" + s + "]";
    }
    return v.dump();
}

struct Injection {
    std::string field;
    Json        values;
    std::string config;   ///< the config it came from, for messages
};

/// @brief `text` with every injection written in, each as a child of the root `animation:` mapping.
/// @throws std::runtime_error with why, when a field is already present or does not read back as written.
/// @note  Appended as text, so every other byte of the clip is kept. That relies on `animation:` being the file's last
///        top-level key with 2-space children; the read-back through the loader proves it for each file.
std::string InjectAnimation(const std::string& path, std::string text, const std::vector<Injection>& injections) {
    for (const auto& in : injections) {
        const std::string key = "  " + in.field + ":";
        for (std::size_t at = 0; at < text.size();) {
            const std::size_t end = (std::min)(text.find('\n', at), text.size());
            if (std::string_view(text).substr(at, end - at).starts_with(key))
                throw std::runtime_error("already has '" + in.field + "' (" + in.config + ")");
            at = end + 1;
        }
        if (!text.empty() && text.back() != '\n') text += '\n';
        text += key + " " + Flow(in.values) + "\n";
    }
    using MotionTarget = CB::engine::anim::AnimationDef::MotionTarget;
    const auto def = CB::engine::anim::AnimationYamlLoader::LoadFromString(text, path);
    for (const auto& in : injections) {
        bool same = false;
        if (in.field == "tags") {
            std::map<std::string, std::vector<std::pair<int, int>>> want;
            for (const auto& [name, ranges] : in.values.items())
                for (const auto& r : ranges) want[name].emplace_back(r[0].get<int>(), r[1].get<int>());
            same = def.tags && *def.tags == want;
        } else if (in.field == "motionTarget") {
            same = def.motionTarget == (in.values.get<std::string>() == "motionMatching" ? MotionTarget::MotionMatching
                                                                                          : MotionTarget::AnimationData);
        }
        if (!same)
            throw std::runtime_error("'" + in.field + "' did not read back as written (is `animation:` the last top-level key?)");
    }
    return text;
}

/// @brief One file's injections applied, after checking its kind and the table.
std::string Apply(const std::string& path, std::string text, const std::vector<Injection>& injections, const Table& table) {
    if (!IsAnimation(path, text))
        throw std::runtime_error("not a file kind the injector knows (animation: a bare .hkx file whose root key is "
                                 "`animation:`)");
    static const std::map<std::string, FieldType> kNone;
    const auto  row    = table.find(Kind::Animation);
    const auto& fields = row == table.end() ? kNone : row->second;
    for (const auto& in : injections) {
        const auto f = fields.find(in.field);
        if (f == fields.end())
            throw std::runtime_error("animation files accept no '" + in.field + "' (injectables.json; " + in.config + ")");
        if (const auto why = CheckType(f->second, in.values); !why.empty())
            throw std::runtime_error("'" + in.field + "': " + why + " (" + in.config + ")");
    }
    return InjectAnimation(path, std::move(text), injections);
}

/// @brief The injectables table: {kind: {field: type}}, every kind and type one the injector knows.
Table LoadTable(const std::string& tablePath) {
    const auto text = CB::engine::disk::ReadFile(tablePath, { .stripBom = true });
    if (!text) throw std::runtime_error("cannot read " + tablePath);
    const Json j = ParseStrict(*text);
    if (!j.is_object()) throw std::runtime_error(tablePath + ": not {kind: {field: type}}");
    Table table;
    for (const auto& [kind, fields] : j.items()) {
        if (kind != "animation") throw std::runtime_error(tablePath + ": unknown file kind '" + kind + "'");
        if (!fields.is_object()) throw std::runtime_error(tablePath + ": '" + kind + "' is not {field: type}");
        auto& row = table[Kind::Animation];
        for (const auto& [field, type] : fields.items()) {
            const bool plain = !field.empty() && std::all_of(field.begin(), field.end(), [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            });
            if (!plain) throw std::runtime_error(tablePath + ": '" + field + "' is not a plain key name");
            if (std::find(kAnimationReadBack.begin(), kAnimationReadBack.end(), field) == kAnimationReadBack.end())
                throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' has no read-back in hky-tool, so a "
                                         "value the loader ignores would pass unseen");
            FieldType ft;
            if (type.is_string() && type.get<std::string>() == "rangeMap") {
                ft.shape = FieldType::Shape::RangeMap;
            } else if (type.is_object() && type.size() == 1 && type.contains("oneOf") && type["oneOf"].is_array() &&
                       !type["oneOf"].empty()) {
                ft.shape = FieldType::Shape::OneOf;
                for (const auto& n : type["oneOf"]) {
                    if (!n.is_string() || n.get<std::string>().empty())
                        throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' oneOf holds a non-name " + n.dump());
                    ft.oneOf.push_back(n.get<std::string>());
                }
            } else {
                throw std::runtime_error(tablePath + ": '" + kind + "." + field + "' has an unknown type " + type.dump() +
                                         " (known: \"rangeMap\", {\"oneOf\": [names]})");
            }
            row[field] = std::move(ft);
        }
    }
    return table;
}

}  // namespace assemble

/// @brief The build's config-driven GENERATORS: `generate/**/*.json` configs name a generator kind this tool knows and
///        its parameters, and the generator writes new files into the bundle from a partition's content, so what is
///        derived from the clips (a behavior over them, the rosters naming them) can never drift from them.
namespace generate {

using Json = assemble::Json;

/// @brief One animation a generator works over: its path in the bundle and its roster name.
struct Clip {
    std::string path;     ///< original-case path in the bundle
    std::string roster;   ///< the path below `rosterRoot`, backslashed: the roster entry and the clip's animationName
    std::string stem;     ///< the file name without `.hkx`
};

std::string Quote(std::string_view s) {
    std::string out = "'";
    for (const char c : s) out += c == '\'' ? std::string("''") : std::string(1, c);
    return out + "'";
}

/// @brief A self-owned FormID (`0000x…`), the id a node a bundle adds carries.
std::string OwnId(unsigned local) {
    char b[16];
    std::snprintf(b, sizeof(b), "0000x%04x", local);
    return b;
}

/// @brief The config's parameters, with exactly `keys` present and every one a string unless listed in `other`.
void ExpectKeys(const Json& j, std::initializer_list<const char*> keys, std::initializer_list<const char*> other = {}) {
    if (!j.is_object()) throw std::runtime_error("not an object");
    for (const auto& [k, v] : j.items()) {
        const bool known = std::any_of(keys.begin(), keys.end(), [&](const char* x) { return k == x; }) ||
                           std::any_of(other.begin(), other.end(), [&](const char* x) { return k == x; });
        if (!known) throw std::runtime_error("unknown parameter '" + k + "'");
    }
    for (const char* k : keys) {
        if (!j.contains(k)) throw std::runtime_error(std::string("missing parameter '") + k + "'");
        if (!j[k].is_string() || j[k].get<std::string>().empty()) throw std::runtime_error(std::string("'") + k + "' is not a non-empty string");
    }
    for (const char* k : other)
        if (!j.contains(k)) throw std::runtime_error(std::string("missing parameter '") + k + "'");
}

/// @brief The partition's animations under `prefix`, sorted by path: the ORDER every generator over the same clips
///        shares (a selector's child index is this index).
/// @note  `prefix` and `rosterRoot` match by path identity; the roster names keep the bundle's original case.
std::vector<Clip> ClipsUnder(const CB::engine::codec::HkyArchive& arc, const std::string& prefix, const std::string& rosterRoot) {
    using CB::engine::codec::resource::HasExt;
    using CB::engine::codec::resource::PathKey;
    const std::string want = PathKey(prefix), root = PathKey(rosterRoot);
    if (!want.starts_with(root)) throw std::runtime_error("'clips' is not below 'rosterRoot'");
    std::vector<Clip> clips;
    for (const auto& path : arc.filesUnderOrig("")) {
        if (!HasExt(path, ".hkx") || !PathKey(path).starts_with(want)) continue;
        std::string roster = path.substr(root.size());
        std::replace(roster.begin(), roster.end(), '/', '\\');
        const auto slash = path.rfind('/');
        clips.push_back({ path, std::move(roster), path.substr(slash + 1, path.size() - slash - 1 - 4) });
    }
    std::sort(clips.begin(), clips.end(), [](const Clip& a, const Clip& b) { return a.path < b.path; });
    if (clips.empty()) throw std::runtime_error("no animations under '" + prefix + "'");
    return clips;
}

using Files = std::vector<std::pair<std::string, std::string>>;

/// @brief A selector class the root may be: its index fields and how many children its index can address.
struct SelectorClass {
    const char* name;
    const char* selectedField;
    const char* currentField;
    std::size_t maxChildren;
};
/// @note hkbManualSelectorGenerator's indices are int8: it clamps to (int8)(children - 1), so past 128 children
///       every selection reads outside them. CBPoseSearchGenerator (CB's own) has int32 indices.
constexpr std::array<SelectorClass, 2> kSelectorClasses{ {
    { "hkbManualSelectorGenerator", "selectedGeneratorIndex", "currentGeneratorIndex", 128 },
    { "CBPoseSearchGenerator", "selectedIndex", "currentIndex", 0x7fffffff },
} };

/// @brief `clipSelectorBehavior`: a complete behavior unit whose root is a selector (`rootClass`) over one clip per
///        animation, the selected child bound to `clipVariable` and every child's user-controlled time to
///        `timeVariable` (only the selected child plays, so one shared time is safe).
/// @note  The unit's own ids: 0000x0001 the root container, 0000x0002 the selector, 0000x0003+ the clips in clip
///        order. Both variables are declared here; a graph that references this one shares them by name.
Files ClipSelectorBehavior(const Json& j, const std::vector<Clip>& clips) {
    const auto out = j["out"].get<std::string>(), selector = j["selectorName"].get<std::string>();
    const auto clipVar = j["clipVariable"].get<std::string>(), timeVar = j["timeVariable"].get<std::string>();
    const auto rootClass = j["rootClass"].get<std::string>();
    const auto sc = std::find_if(kSelectorClasses.begin(), kSelectorClasses.end(), [&](const SelectorClass& c) { return rootClass == c.name; });
    if (sc == kSelectorClasses.end())
        throw std::runtime_error("'rootClass' " + rootClass + " is neither hkbManualSelectorGenerator nor CBPoseSearchGenerator");
    if (clips.size() > sc->maxChildren)
        throw std::runtime_error(std::to_string(clips.size()) + " clips is past what " + rootClass + "'s index addresses (" +
                                 std::to_string(sc->maxChildren) + ")");
    const auto slash = out.rfind('/');
    std::string graphName = out.substr(slash == std::string::npos ? 0 : slash + 1);
    if (!CB::engine::codec::resource::HasExt(graphName, ".hkx")) throw std::runtime_error("'out' is not a .hkx unit path");
    graphName = graphName.substr(0, graphName.size() - 4) + ".hkb";

    Files files;
    files.emplace_back(out + "/graph.yaml",
        "id: " + OwnId(1) + "\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbBehaviorGraph':\n"
        "    className: 'hkbBehaviorGraph'\n    variant:\n      class: hkbBehaviorGraph\n      userData: 0\n"
        "      name: " + Quote(graphName) + "\n      variableMode: VARIABLE_MODE_DISCARD_WHEN_INACTIVE\n"
        "      rootGenerator: " + OwnId(2) + "\n      data:\n        variables:\n"
        "          " + Quote(clipVar) + ": {type: VARIABLE_TYPE_INT32, value: 0}\n"
        "          " + Quote(timeVar) + ": {type: VARIABLE_TYPE_REAL, value: 0}\n"
        "        characterProperties: {}\n        events: {}\n        attributes: {}\n");

    const std::string sel = out + "/selectors/" + OwnId(2) + "/";
    files.emplace_back(sel + OwnId(2) + ".yaml",
        "id: " + OwnId(2) + "\nclass: " + rootClass + "\nvariableBindingSet:\n  indexOfBindingToEnable: -1\n"
        "userData: 0\nname: " + Quote(selector) + "\n" + sc->selectedField + ": 0\n" + sc->currentField + ": 0\n");
    std::string children;
    for (std::size_t i = 0; i < clips.size(); ++i)
        children += Quote(OwnId(static_cast<unsigned>(i))) + ": " + OwnId(static_cast<unsigned>(i + 3)) + "\n";
    files.emplace_back(sel + "generators.yaml", children);
    files.emplace_back(sel + "variableBindingSet.bindings.yaml",
        "'" + std::string(sc->selectedField) + "':\n  variableIndex: 0\n  variable: " + Quote(clipVar) +
        "\n  bitIndex: -1\n  bindingType: BINDING_TYPE_VARIABLE\n");

    for (std::size_t i = 0; i < clips.size(); ++i) {
        const std::string id = OwnId(static_cast<unsigned>(i + 3)), dir = out + "/clips/" + id + "/";
        files.emplace_back(dir + id + ".yaml",
            "id: " + id + "\nclass: hkbClipGenerator\nvariableBindingSet:\n  indexOfBindingToEnable: -1\nuserData: 0\n"
            "name: " + Quote(clips[i].stem) + "\nanimationName: " + Quote(clips[i].roster) + "\n"
            "cropStartAmountLocalTime: 0\ncropEndAmountLocalTime: 0\nstartTime: 0\nplaybackSpeed: 1\nenforcedDuration: 0\n"
            "userControlledTimeFraction: 0\nanimationBindingIndex: -1\nmode: MODE_USER_CONTROLLED\nflags: 0\n");
        files.emplace_back(dir + "variableBindingSet.bindings.yaml",
            "'userControlledTimeFraction':\n  variableIndex: 1\n  variable: " + Quote(timeVar) +
            "\n  bitIndex: -1\n  bindingType: BINDING_TYPE_VARIABLE\n");
    }
    return files;
}

/// @brief `rosterPatch`: one patch unit per listed character adding every animation to its roster.
/// @note  A roster is a union: the patch names only what it adds. The same list goes to every character, since
///        characters sharing a graph need identical slot sets (a slot is keyed by file name).
Files RosterPatch(const Json& j, const std::vector<Clip>& clips) {
    const Json& chars = j["characters"];
    if (!chars.is_array() || chars.empty()) throw std::runtime_error("'characters' is not a non-empty list");
    std::string names;
    for (const auto& c : clips) names += (names.empty() ? "" : ", ") + Quote(c.roster);
    Files files;
    for (const auto& c : chars) {
        ExpectKeys(c, { "path", "root" });
        const auto root = c["root"].get<std::string>();
        const bool formId = root.size() == 9 && root[4] == 'x' &&
                            std::all_of(root.begin(), root.end(), [](char ch) { return ch == 'x' || std::isxdigit(static_cast<unsigned char>(ch)); });
        if (!formId) throw std::runtime_error("'" + root + "' is not a FormID like 0001x000b");
        files.emplace_back(c["path"].get<std::string>() + "/graph.yaml",
            "id: " + root + "\nclass: hkRootLevelContainer\nnamedVariants:\n  'hkbCharacterData':\n    variant:\n"
            "      stringData:\n        animationNames: [" + names + "]\n");
    }
    return files;
}

/// @brief One config run: its files, from the partition it names.
/// @throws std::runtime_error with why, on an unknown generator or a bad parameter.
Files Run(const Json& j, const std::map<std::string, std::shared_ptr<CB::engine::codec::HkyArchive>>& partitions) {
    if (!j.is_object() || !j.contains("generator") || !j["generator"].is_string())
        throw std::runtime_error("not {\"generator\": <kind>, ...}");
    const auto kind = j["generator"].get<std::string>();
    Json params = j;
    params.erase("generator");
    if (kind == "clipSelectorBehavior")
        ExpectKeys(params, { "from", "clips", "rosterRoot", "out", "rootClass", "selectorName", "clipVariable", "timeVariable" });
    else if (kind == "rosterPatch")
        ExpectKeys(params, { "from", "clips", "rosterRoot" }, { "characters" });
    else
        throw std::runtime_error("unknown generator '" + kind + "' (known: clipSelectorBehavior, rosterPatch)");
    const auto from = params["from"].get<std::string>();
    const auto p    = partitions.find(from);
    if (p == partitions.end()) throw std::runtime_error("no partition '" + from + "' in the feature");
    const auto clips = ClipsUnder(*p->second, params["clips"].get<std::string>(), params["rosterRoot"].get<std::string>());
    return kind == "clipSelectorBehavior" ? ClipSelectorBehavior(params, clips) : RosterPatch(params, clips);
}

}  // namespace generate

/// @brief `assemble`: a feature folder in the repo -> its shipped .hky. Every `*.hky` at the folder's top is a
///        partition, `inject/` holds the configs, and everything else (manifest.json, loose trees) ships at its path.
/// @note  Every failure is a hard error, never a skip: a config naming a missing path, two configs for one file's
///        field, a field the file's kind does not accept, a value of the wrong shape, two sources supplying one path.
int doAssemble(const std::string& dir, const std::string& tablePath, const std::string& out) {
    if (dir.empty() || tablePath.empty() || out.empty()) {
        std::printf("usage: hky-tool assemble <featureDir> <injectables.json> -o <out.hky>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using CB::engine::codec::HkyArchive;
    using CB::engine::codec::resource::HasExt;
    using CB::engine::codec::resource::PathKey;

    std::vector<std::string> errors;
    assemble::Table          table;
    try {
        table = assemble::LoadTable(tablePath);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }

    struct Source {
        HkyArchive::PackEntry           entry;
        std::string                     from;   ///< a partition's name or "loose", for messages
        std::vector<assemble::Injection> injections;
    };
    std::vector<Source>                          sources;
    std::unordered_map<std::string, std::size_t> byKey;
    const auto add = [&](std::string name, std::function<std::optional<std::string>()> read, const std::string& from) {
        const auto [it, fresh] = byKey.emplace(PathKey(name), sources.size());
        if (!fresh) {
            errors.push_back("'" + name + "' is supplied by both " + sources[it->second].from + " and " + from);
            return;
        }
        sources.push_back({ { std::move(name), std::move(read) }, from, {} });
    };

    const fs::path root = fs::absolute(fs::path(dir));
    fs::path       injectRoot, generateRoot;
    std::map<std::string, std::shared_ptr<HkyArchive>> partitions;
    std::size_t    loose = 0;
    std::error_code ec;
    std::vector<fs::directory_entry> top;
    for (fs::directory_iterator it(CB::engine::disk::LongPath(root), ec), end; !ec && it != end; it.increment(ec)) top.push_back(*it);
    if (ec) { std::printf("FAIL: cannot list '%s': %s\n", dir.c_str(), ec.message().c_str()); return 1; }
    std::sort(top.begin(), top.end(), [](const auto& a, const auto& b) { return a.path().filename() < b.path().filename(); });
    for (const auto& e : top) {
        const fs::path    child = root / e.path().filename();
        const std::string fname = e.path().filename().generic_string();
        if (e.is_directory() && fname == "inject") { injectRoot = child; continue; }
        if (e.is_directory() && fname == "generate") { generateRoot = child; continue; }
        if (e.is_regular_file() && HasExt(fname, ".hky")) {
            std::string err;
            auto arc = HkyArchive::LoadFromFile(child.string(), err);
            if (!arc) { std::printf("FAIL: partition %s: %s\n", fname.c_str(), err.c_str()); return 1; }
            const auto keys = arc->filesUnder(""), orig = arc->filesUnderOrig("");
            for (std::size_t i = 0; i < keys.size(); ++i)
                add(orig[i], [arc, k = keys[i]] { return arc->file(k); }, fname);
            partitions.emplace(fname, std::move(arc));
            continue;
        }
        const auto addLoose = [&](const fs::path& p) {
            add(p.lexically_relative(root).generic_string(), [p] { return CB::engine::disk::ReadFile(p); }, "loose files");
            ++loose;
        };
        if (e.is_regular_file()) { addLoose(child); continue; }
        if (e.is_directory())
            for (fs::recursive_directory_iterator it(CB::engine::disk::LongPath(child), ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file()) addLoose(child / it->path().lexically_relative(CB::engine::disk::LongPath(child)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", child.string().c_str(), ec.message().c_str()); return 1; }
    }

    const auto jsonUnder = [&](const fs::path& folder, std::vector<fs::path>& found) {
        if (folder.empty()) return true;
        for (fs::recursive_directory_iterator it(CB::engine::disk::LongPath(folder), ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && HasExt(it->path().generic_string(), ".json"))
                found.push_back(folder / it->path().lexically_relative(CB::engine::disk::LongPath(folder)));
        if (ec) { std::printf("FAIL: cannot walk '%s': %s\n", folder.string().c_str(), ec.message().c_str()); return false; }
        std::sort(found.begin(), found.end());
        return true;
    };
    std::vector<fs::path> configs, generators;
    if (!jsonUnder(injectRoot, configs) || !jsonUnder(generateRoot, generators)) return 1;

    /// @note Generators run before injection, so an inject config may target a generated file like any other.
    std::size_t generated = 0;
    for (const auto& g : generators) {
        const std::string name = g.lexically_relative(generateRoot).generic_string();
        try {
            const auto text = CB::engine::disk::ReadFile(g, { .stripBom = true });
            if (!text) throw std::runtime_error("unreadable");
            for (auto& [path, content] : generate::Run(assemble::ParseStrict(*text), partitions)) {
                add(path, [c = std::make_shared<const std::string>(std::move(content))] { return std::optional<std::string>(*c); },
                    "generator " + name);
                ++generated;
            }
        } catch (const std::exception& e) {
            errors.push_back("generate/" + name + ": " + e.what());
        }
    }

    std::map<std::pair<std::size_t, std::string>, std::string> slots;
    std::size_t injected = 0;
    for (const auto& c : configs) {
        const std::string name = c.lexically_relative(injectRoot).generic_string();
        try {
            const auto text = CB::engine::disk::ReadFile(c, { .stripBom = true });
            if (!text) throw std::runtime_error("unreadable");
            const auto j = assemble::ParseStrict(*text);
            if (!j.is_object() || j.size() != 1 || !j.contains("targets") || !j["targets"].is_array())
                throw std::runtime_error("not {\"targets\": [...]}");
            for (const auto& t : j["targets"]) {
                /// @note `path` names one file; `under` names every animation below a path prefix (path identity), so
                ///       one target covers a whole clip set and a clip added under it gets the value too.
                const bool one = t.is_object() && t.contains("path"), many = t.is_object() && t.contains("under");
                const char* where = one ? "path" : "under";
                if (!t.is_object() || t.size() != 3 || one == many || !t.contains("field") || !t.contains("values") ||
                    !t[where].is_string() || !t["field"].is_string())
                    throw std::runtime_error("a target is not {\"path\" or \"under\", \"field\", \"values\"}");
                const auto at = t[where].get<std::string>(), field = t["field"].get<std::string>();
                std::vector<std::size_t> hits;
                if (one) {
                    const auto it = byKey.find(PathKey(at));
                    if (it == byKey.end()) throw std::runtime_error("no file '" + at + "' in the feature");
                    hits.push_back(it->second);
                } else {
                    const std::string prefix = PathKey(at);
                    for (std::size_t s = 0; s < sources.size(); ++s)
                        if (assemble::IsAnimationPath(sources[s].entry.name) && PathKey(sources[s].entry.name).starts_with(prefix))
                            hits.push_back(s);
                    if (hits.empty()) throw std::runtime_error("no animation under '" + at + "' in the feature");
                }
                for (const std::size_t s : hits) {
                    const auto [slot, fresh] = slots.emplace(std::pair{ s, field }, name);
                    if (!fresh)
                        throw std::runtime_error("'" + field + "' of '" + sources[s].entry.name + "' is also set by " + slot->second);
                    sources[s].injections.push_back({ field, t["values"], name });
                    ++injected;
                }
            }
        } catch (const std::exception& e) {
            errors.push_back(name + ": " + e.what());
        }
    }
    if (!errors.empty()) {
        for (const auto& e : errors) std::printf("  FAIL %s\n", e.c_str());
        std::printf("FAIL: assemble '%s': %zu error(s), nothing packed.\n", dir.c_str(), errors.size());
        return 1;
    }

    std::mutex               failMutex;
    std::vector<std::string> failures;
    std::vector<HkyArchive::PackEntry> entries;
    std::size_t targetsTouched = 0;
    for (auto& s : sources) {
        if (!s.injections.empty()) {
            ++targetsTouched;
            s.entry.read = [base = std::move(s.entry.read), name = s.entry.name, inj = std::move(s.injections), &table,
                            &failMutex, &failures]() -> std::optional<std::string> {
                auto text = base();
                if (!text) return std::nullopt;
                try {
                    return assemble::Apply(name, std::move(*text), inj, table);
                } catch (const std::exception& e) {
                    const std::lock_guard lock(failMutex);
                    failures.push_back(name + ": " + e.what());
                    return std::nullopt;
                }
            };
        }
        entries.push_back(std::move(s.entry));
    }

    const std::size_t total = entries.size();
    std::string       err;
    if (!HkyArchive::PackEntries(std::move(entries), out, err)) {
        /// @note An injection failure surfaces from the pack as an unreadable source; its own message says why.
        for (const auto& f : failures) std::printf("  FAIL %s\n", f.c_str());
        if (failures.empty()) std::printf("FAIL: %s\n", err.c_str());
        else std::printf("FAIL: assemble '%s': %zu error(s), nothing packed.\n", dir.c_str(), failures.size());
        return 1;
    }
    if (!HkyArchive::LoadFromFile(out, err)) { std::printf("FAIL: assembled but unreadable: %s\n", err.c_str()); return 1; }
    std::printf("OK: assemble '%s' -> %s: %zu file(s) from %zu partition(s) + %zu loose + %zu generated by %zu "
                "config(s); %zu injection(s) from %zu config(s) into %zu file(s).\n",
                dir.c_str(), out.c_str(), total, partitions.size(), loose, generated, generators.size(), injected,
                configs.size(), targetsTouched);
    return 0;
}

/// @brief `merge-check`: every unit in `bundle` merged onto its base in `base` (a patch unit) or alone (a unit the
///        bundle owns), then serialized: the compile the game's warm-up runs, offline. Each result is written to
///        `outDir/<unit>` (the .hkx) and decompiled back beside it as `<unit>.merged/` for inspection.
/// @note  Bones come from the base's humanoid skeleton, so the check covers graphs on that rig.
int doMergeCheck(const std::string& basePath, const std::string& bundlePath, const std::string& schemaDir,
                 const std::string& outDir) {
    if (basePath.empty() || bundlePath.empty() || schemaDir.empty() || outDir.empty()) {
        std::printf("usage: hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n");
        return 2;
    }
    namespace fs = std::filesystem;
    using CB::engine::codec::HkyArchive;
    using CB::engine::codec::resource::PathKey;
    CB::engine::schema::SetSharedSchemaDir(schemaDir);
    auto* reg = CB::engine::schema::SharedRegistry();
    if (!reg) { std::printf("FAIL: schema: %s\n", CB::engine::schema::SharedRegistryError().c_str()); return 1; }

    std::string err;
    const auto base = HkyArchive::LoadFromFile(basePath, err);
    if (!base) { std::printf("FAIL: base: %s\n", err.c_str()); return 1; }
    const auto bundle = HkyArchive::LoadFromFile(bundlePath, err);
    if (!bundle) { std::printf("FAIL: bundle: %s\n", err.c_str()); return 1; }

    std::vector<std::string> bones;
    if (const auto list = base->file(PathKey("meshes/actors/character/character assets/skeleton.hkx/bonelist.yaml"))) {
        for (std::size_t at = 0; at < list->size();) {
            const std::size_t end  = (std::min)(list->find('\n', at), list->size());
            const std::string line = list->substr(at, end - at);
            const auto a = line.find('"'), b = line.rfind('"');
            if (line.find("- ") != std::string::npos && a != std::string::npos && b > a) bones.push_back(line.substr(a + 1, b - a - 1));
            at = end + 1;
        }
    }
    if (bones.empty()) { std::printf("FAIL: no humanoid skeleton bone list in the base\n"); return 1; }

    std::string stem = fs::path(bundlePath).stem().string();
    stem = PathKey(stem);
    std::vector<std::string> baseUnits;
    for (const auto& u : base->units()) baseUnits.push_back(u.prefix);
    std::sort(baseUnits.begin(), baseUnits.end());

    std::size_t ok = 0, failed = 0;
    for (const auto& u : bundle->units()) {
        std::vector<CB::engine::common::LayerSource> layers;
        const bool patch = std::binary_search(baseUnits.begin(), baseUnits.end(), u.prefix);
        if (patch) layers.push_back({ base->source(u.prefix), std::string(CB::engine::codec::formid::BASE_GAME_STEM),
                                      CB::engine::codec::formid::BuildMasterTable(CB::engine::codec::formid::BASE_GAME_STEM, {}, true) });
        layers.push_back({ bundle->source(u.prefix), stem, CB::engine::codec::formid::BuildMasterTable(stem, {}, true) });

        CB::engine::decompile::ParsedHky graph;
        std::vector<std::uint8_t>        bytes;
        const fs::path                   dst = fs::path(outDir) / u.prefix;
        if (!CB::engine::decompile::MergeLayersReflective(layers, *reg, &bones, graph, err) ||
            !CB::engine::decompile::SerializeGraph(graph, bytes, err) ||
            !CB::engine::disk::WriteFile(dst, std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), {}, &err) ||
            !CB::engine::decompile::DecompileBehaviorReflective(bytes, *reg, dst.string() + ".merged", err, &bones)) {
            std::printf("  FAIL %s (%s): %s\n", u.prefix.c_str(), patch ? "patch" : "own unit", err.c_str());
            ++failed;
            continue;
        }
        std::printf("  OK   %s (%s): %zu object(s), %zu bytes\n", u.prefix.c_str(), patch ? "patch onto the base" : "own unit",
                    graph.objects.size(), bytes.size());
        ++ok;
    }
    std::printf("%s: merge-check '%s': %zu unit(s) merged and serialized, %zu failed.\n", failed ? "FAIL" : "OK",
                bundlePath.c_str(), ok, failed);
    return failed ? 1 : 0;
}

void usage() {
    std::printf("usage:\n"
                "  hky-tool pack        <bundleDir> -o <out.hky>\n"
                "  hky-tool unpack      <in.hky>    -o <outDir>\n"
                "  hky-tool bsa-list    <in.bsa>    [prefix]\n"
                "  hky-tool bsa-extract <in.bsa>    -o <outDir> [prefix]\n"
                "  hky-tool autotag     <clips.hky|dir> -o <injectDir> [reportDir]\n"
                "  hky-tool assemble    <featureDir> <injectables.json> -o <out.hky>\n"
                "  hky-tool merge-check <Skyrim.hky> <bundle.hky> <schemaDir> -o <outDir>\n");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 2; }
    const std::string verb = argv[1];

    std::string arg, arg2, arg3, out;
    for (int i = 2; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) { out = argv[++i]; }
        else if (arg.empty())         { arg = a; }
        else if (arg2.empty())        { arg2 = a; }
        else if (arg3.empty())        { arg3 = a; }
    }

    if (verb == "pack")        return doHkyPack(arg, out);
    if (verb == "unpack")      return doHkyUnpack(arg, out);
    if (verb == "bsa-list")    return doBsaList(arg, arg2);
    if (verb == "bsa-extract") return doBsaExtract(arg, out, arg2);
    if (verb == "autotag")     return doAutoTag(arg, out, arg2);
    if (verb == "assemble")    return doAssemble(arg, arg2, out);
    if (verb == "merge-check") return doMergeCheck(arg, arg2, arg3, out);
    usage();
    return 2;
}
