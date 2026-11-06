/// @brief Animation Relay's core offline, and the motion-matching database and search over a synthetic bundle: built through
///        the bundle generator's own unit, so the selector is the shape the plugin reads.

#include "TestSupport.h"

#include <codec/format/AnimationYamlLoader.h>
#include <codec/serialization/HavokIo.h>
#include <compile/MotionFeatures.h>
#include <compile/MotionMatchingBundle.h>
#include <compile/MotionMatchingCache.h>
#include <decompile/BehaviorDecompile.h>
#include <disk/Disk.h>
#include <locomotion/PoseSearch.h>
#include <relay/Relay.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cmath>
#include <locomotion/ChooserInputs.h>
#include <locomotion/DeadBlend.h>
#include <locomotion/SpeedProfile.h>
#include <filesystem>
#include <map>
#include <process.h>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace CB::engine::tests {

namespace {

namespace fs  = std::filesystem;
namespace dec = decompile;
namespace F   = anim::features;
namespace ps  = locomotion;
namespace cmp = compile;

/// @brief A variable source over a name -> Value map.
class Vars final : public relay::VariableSource {
public:
    std::map<std::string, relay::Value> values;
    bool Read(const char* name, relay::Value& out) const override {
        const auto it = values.find(name);
        if (it == values.end()) return false;
        out = it->second;
        return true;
    }
};

relay::CompiledExpr Compiled(const std::string& text) {
    std::string err;
    relay::CompiledExpr e = relay::Compile(text, err);
    Check(e.Valid(), "'" + text + "' compiles: " + err);
    return e;
}

void RelayChecks() {
    relay::RegisterCoreBuiltins();
    relay::RegisterCoreBuiltins();

    Vars vars;
    vars.values["Speed"]   = relay::Value::Float(120.5f);
    vars.values["MM_Gait"] = relay::Value::Int(2);
    vars.values["MM_Flag"] = relay::Value::Bool(true);
    relay::EvalContext ctx;
    ctx.Reset(nullptr);
    ctx.variables = &vars;

    const auto value = [&](const std::string& text) { return Compiled(text).Evaluate(ctx); };
    Check(value("GraphVar(\"Speed\")").type == relay::VType::Float && value("GraphVar(\"Speed\")").AsFloat() == 120.5f, "GraphVar reads a float");
    Check(value("GraphVar(\"MM_Gait\")").type == relay::VType::Int && value("GraphVar(\"MM_Gait\")").AsInt() == 2, "GraphVar keeps an int's type");
    Check(value("GraphVar(\"MM_Flag\")").type == relay::VType::Bool && value("GraphVar(\"MM_Flag\")").AsBool(), "GraphVar keeps a bool's type");
    Check(value("GraphVar('absent')").type == relay::VType::Float && value("GraphVar('absent')").AsFloat() == 0.0f, "a variable the source lacks reads 0.0");
    Check(value("GraphVar(\"speed\")").AsFloat() == 0.0f, "names are compared exactly: a miscased name reads as absent");
    Check(value("GraphVar(\"MM_Gait\") == 2 && GraphVar(\"Speed\") > 100").AsBool(), "the grammar composes over GraphVar");
    Check(value("GraphVar(\"MM_Gait\") == 1 || GraphVar(\"Speed\") < 100").AsBool() == false, "and its false case");
    Check(value("-0.05").AsFloat() == -0.05f, "a literal is an expression");
    Check(value("(1 + 2) * 3 / 2").AsFloat() == 4.5f, "arithmetic precedence");
    Check(relay::Intern("MM_Gait") == relay::Intern(std::string("MM_") + "Gait"), "equal text interns to one pointer");
    Check(relay::Intern("mm_gait") != relay::Intern("MM_Gait"), "interning keeps case");

    ctx.Reset(nullptr);
    Check(!ctx.variables && Compiled("GraphVar(\"Speed\")").Evaluate(ctx).AsFloat() == 0.0f, "Reset clears the variables; no source reads 0.0");

    std::string err;
    Check(!relay::Compile("IsSneaking()", err).Valid() && err.find("IsSneaking") != std::string::npos, "a game-state function does not compile offline: " + err);
    err.clear();
    Check(!relay::Compile("1 +", err).Valid() && !err.empty(), "a broken expression reports why");
    Check(!relay::Compile(".", err).Valid() && !err.empty(), "a lone '.' reports why instead of throwing");
    Check(!relay::Compile("0x", err).Valid() && !err.empty(), "a bare 0x reports why instead of throwing");
    Check(!relay::Compile(std::string(400, '9'), err).Valid() && !err.empty(), "a number past double's range reports why instead of throwing");
    Check(!relay::Compile("GraphVar(1)", err).Valid() && err.find("GraphVar") != std::string::npos, "a number where a string belongs is refused");
    Check(!relay::Compile("GraphVar(1 + 1)", err).Valid(), "an expression where a string belongs is refused");
    Check(relay::Compile("GraphVar('Speed') + 1", err).Valid(), "the right type compiles");

    static std::vector<std::string> warned;
    relay::SetWarnSink([](std::string_view m) { warned.emplace_back(m); });
    static constexpr std::array kString{ relay::VType::String };
    relay::RegisterFunction("GraphVar", relay::VType::Float, kString, nullptr);
    relay::SetWarnSink(nullptr);
    Check(warned.size() == 1 && warned[0].find("GraphVar") != std::string::npos, "a repeated registration warns through the sink");
    Check(Compiled("GraphVar(\"MM_Gait\")").Valid(), "and the first registration stays");
}

const std::string kRoot = "meshes/actors/character/";

/// @brief The dimension the line clips move along: the 1/3 s sample's x, weighed by default.
constexpr int kLineDim = F::SampleAt(1) + F::kSamplePos;

/// @brief A clip of `frames` raw rows, every dimension 0 but the first trajectory x (`xBase` + the frame); its Speed curve
///        reads 0 until `standUntil`, then 100.
anim::MmCachedClip LineClip(const std::string& name, int frames, float xBase, float standUntil) {
    anim::MmCachedClip c;
    c.name          = name;
    c.frames        = frames;
    c.frameDuration = 1.0f / 30.0f;
    c.duration      = static_cast<float>(frames - 1) / 30.0f;
    c.features.assign(static_cast<std::size_t>(frames) * F::kDims, 0.0f);
    for (int f = 0; f < frames; ++f) c.features[static_cast<std::size_t>(f) * F::kDims + kLineDim] = xBase + static_cast<float>(f);
    /// @note The pelvis ends at another speed than it starts: a one-shot clip, not a loop (anim::FitLoop reads the pose's
    ///       ends). Balanced about 0, so the group's normalization leaves every other row at 0.
    c.features[F::kHipVel + 1]                                                    = -150.0f;
    c.features[static_cast<std::size_t>(frames - 1) * F::kDims + F::kHipVel + 1] = 150.0f;
    c.rootPositions.assign(static_cast<std::size_t>(frames) * 3, 0.0f);
    anim::AnimCurveDef speed;
    speed.name = "Speed";
    speed.keys.push_back({ 0.0f, 0.0f, anim::AnimCurveKey::Interp::Constant });
    speed.keys.push_back({ standUntil, 100.0f, anim::AnimCurveKey::Interp::Constant });
    c.curves.push_back(speed);
    return c;
}

/// @brief A unit's selector, parsed back from the bundle generator's files.
struct Unit {
    dec::ParsedHky graph;
    const codec::io::SchemaObject* selector = nullptr;
};

void ParseUnit(const cmp::MmBundleSpec& spec, const schema::SchemaRegistry& reg, const fs::path& dir, Unit& out) {
    cmp::MmFiles files;
    std::string  err;
    Check(cmp::BuildMotionMatchingBehavior(spec, reg, files, err), "the bundle generator: " + err);
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const std::string prefix = spec.unitPath + "/";
    for (const auto& [path, text] : files) {
        Check(path.starts_with(prefix), "a unit file below the unit: " + path);
        Check(disk::WriteFile(dir / path.substr(prefix.size()), text), "write " + path);
    }
    Check(dec::ParseHkyReflective(common::DiskUnitSource(dir), reg, out.graph, err), "the unit parses: " + err);
    out.selector = ps::RootSelector(out.graph);
    Check(out.selector != nullptr, "the unit's root is a CBPoseSearchGenerator");
}

using Clips = std::map<std::string, anim::MmCachedClip>;

std::unique_ptr<ps::Database> Built(const Unit& unit, const Clips& clips, std::vector<std::string>* warned = nullptr, ps::BuildReport* report = nullptr) {
    std::map<std::string, int> asked;
    auto db = ps::BuildDatabase(
        *unit.selector, nullptr,
        [&](const std::string& name, const ps::ClipNeeds&) -> const anim::MmCachedClip& {
            ++asked[name];
            const auto it = clips.find(name);
            if (it == clips.end()) throw std::runtime_error("no such clip");
            return it->second;
        },
        [&](const std::string& m) {
            if (warned) warned->push_back(m);
        },
        report);
    for (const auto& [name, count] : asked) Check(count == 1, "each distinct clip is asked for once: " + name);
    return db;
}

/// @brief Walk (a standing/moving gated clip A, a disableReselection clip C, an unmeasurable child, a child in no gate
///        trouble) and Run (A again with a range, looping B).
cmp::MmBundleSpec LineSpec() {
    cmp::MmBundleSpec spec;
    cmp::MmDatabase   walk;
    walk.name              = "Walk";
    walk.baseCostBias      = "GraphVar(\"Bias\")";
    walk.standingCandidate = "GraphVar(\"Speed\") < 15";
    walk.movingCandidate   = "GraphVar(\"Speed\") >= 15";
    walk.tags              = { "Pivots" };
    walk.clips.push_back({ kRoot + "animations/T/a.hkx" });
    walk.clips.push_back({ kRoot + "animations/T/c.hkx", "", cmp::kMmDisableReselection });
    walk.clips.push_back({ kRoot + "animations/T/missing.hkx" });
    cmp::MmDatabase run;
    run.name            = "Run";
    run.loopingCostBias = "IsSneaking()";
    run.clips.push_back({ kRoot + "animations/T/a.hkx", "", 0, 0.49f, 0.81f });
    run.clips.push_back({ kRoot + "animations/T/b.hkx", "", cmp::kMmLooping });
    spec.databases = { walk, run };
    spec.rows.push_back({ "GraphVar(\"MM_Gait\") == 1", { "Walk" } });
    spec.rows.push_back({ "GraphVar(\"MM_Gait\") == 2", { "Run" } });
    spec.rows.push_back({ "IsSneaking()", { "Run" } });
    spec.fallback = { "Walk" };
    return spec;
}

Clips LineClips() {
    Clips c;
    c["animations\\T\\a.hkx"] = LineClip("animations\\T\\a.hkx", 60, 1000.0f, 0.49f);
    c["animations\\T\\b.hkx"] = LineClip("animations\\T\\b.hkx", 60, 2000.0f, 0.0f);
    c["animations\\T\\c.hkx"] = LineClip("animations\\T\\c.hkx", 60, 3000.0f, 0.0f);
    return c;
}

/// @brief A query at raw trajectory x `x`, moving unless `standing`.
ps::Query At(float x, bool standing = false) {
    ps::Query q;
    /// @note The line clips hold 0 in every dimension but one, facing included.
    for (ps::TrajectorySample& s : q.samples) s.dirY = 0.0f;
    q.samples[1].x = x;
    q.desiredX = standing ? 0.0f : 100.0f;
    return q;
}

void DatabaseChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    Unit unit;
    ParseUnit(LineSpec(), reg, tmp / "line", unit);
    std::vector<std::string> warned;
    ps::BuildReport          report;
    const auto dbp = Built(unit, LineClips(), &warned, &report);
    Check(dbp != nullptr, "the database builds");
    if (!dbp) return;
    const ps::Database& db = *dbp;

    const auto warnedOf = [&](const std::string& what) {
        return std::any_of(warned.begin(), warned.end(), [&](const std::string& m) { return m.find(what) != std::string::npos; });
    };
    Check(warnedOf("missing.hkx") && warnedOf("not measured"), "an unmeasurable child is warned of");
    Check(warnedOf("loopingCostBias") && warnedOf("does not compile"), "a bias calling a game-state function is warned of offline");
    Check(warnedOf("`when`"), "so is such a chooser row");

    Check(db.clips.size() == 5 && db.databases.size() == 2, "one clip per child, one database each");
    Check(db.databases[0].pivots && !db.databases[1].pivots, "the Pivots tag");
    Check(report.distinctClips == 4 && report.measuredClips == 3 && report.frames == 240, "the report's counts");
    Check(db.clips[0].first == 0 && db.clips[1].first == 60 && db.clips[2].frames == 0 && db.clips[3].first == 120 && db.clips[4].first == 180,
          "children lay out in order; the unmeasured one takes no rows");
    Check(db.spaces.size() == 1 && db.spaces[0].rows.size() == 240u * F::kDims && db.clipOf.size() == 240 && db.searchable.size() == 240 && db.rowGates.size() == 240 && db.rowGait.size() == 240,
          "every per-row array covers every row");
    Check(db.clipOf[0] == 0 && db.clipOf[60] == 1 && db.clipOf[120] == 3 && db.clipOf[239] == 4, "row -> clip");
    Check(db.clips[1].flags == ps::kFlagDisableReselection && db.clips[4].flags == ps::kFlagLooping, "the row's flags");

    Check(db.searchable[0] && db.searchable[49] && !db.searchable[50] && !db.searchable[59], "a clip's last third of a second is no candidate");
    const int ranged = db.clips[3].first;
    Check(!db.searchable[ranged + 14] && db.searchable[ranged + 15] && db.searchable[ranged + 24] && !db.searchable[ranged + 25],
          "the range cuts its child");
    Check(db.searchable[14] && db.searchable[25], "and not the other child sharing the clip");

    Check((db.rowGates[14] & ps::kRowStanding) && !(db.rowGates[14] & ps::kRowMoving), "a row under the gate's speed stands");
    Check(!(db.rowGates[15] & ps::kRowStanding) && (db.rowGates[15] & ps::kRowMoving), "and past it moves");
    Check((db.rowGates[ranged] & ps::kRowStanding) && (db.rowGates[ranged] & ps::kRowMoving), "an empty gate passes every row");
    Check(db.rowGait[14] == static_cast<std::uint8_t>(ps::Gait::None) && db.rowGait[15] == static_cast<std::uint8_t>(ps::Gait::Walk), "the row's gait from its Speed curve");
    Check(ps::FrameOf(db.clips[0], 0.5) == 15 && ps::FrameOf(db.clips[0], -1.0) == 0 && ps::FrameOf(db.clips[0], 99.0) == 59, "FrameOf rounds and clamps");

    Vars vars;
    vars.values["MM_Gait"] = relay::Value::Int(2);
    vars.values["Bias"]    = relay::Value::Float(-3.5f);
    relay::EvalContext ctx;
    ctx.Reset(nullptr);
    ctx.variables = &vars;
    Check(ps::Choose(db, ctx) == std::vector<char>{ 0, 1 }, "a chooser row picks its database");
    const std::vector<ps::Biases> biases = ps::EvaluateBiases(db, ctx);
    Check(biases[0].base == -3.5f && biases[1].looping == 0.0f, "biases evaluate; one that did not compile is 0");
    vars.values["MM_Gait"] = relay::Value::Int(0);
    ctx.Reset(nullptr);
    ctx.variables = &vars;
    Check(ps::Choose(db, ctx) == std::vector<char>{ 1, 0 }, "no row holds: the fallback");
    Check(ps::IsActive({}, 1) && !ps::IsActive({}, -1) && !ps::IsActive({ 1, 0 }, 1) && !ps::IsActive({ 1 }, 1), "IsActive");

    /// @brief Rows of database Walk only, so the gates decide.
    const std::vector<char> walkOnly{ 1, 0 }, runOnly{ 0, 1 };

    {
        ps::Query q = At(1020.0f, true);
        q.active    = walkOnly;
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.bestRow == 14, "standing, the nearest row past the standing gate: " + std::to_string(r.bestRow));
        q.desiredX = 100.0f;
        const ps::SearchResult moving = ps::Search(db, q);
        Check(moving.bestRow == 20 && moving.best == 0.0f, "moving, the exact row: " + std::to_string(moving.bestRow));
    }
    {
        ps::Query q = At(1020.0f);
        q.active    = runOnly;
        Check(ps::Search(db, q).bestRow == ranged + 20, "an in-range row of the ranged child");
        q.samples[1].x = 1028.0f;
        Check(ps::Search(db, q).bestRow == ranged + 24, "past the range, its last row");
        Check(ps::Search(db, At(1055.0f)).bestRow == 49, "a frame in the clip's end is no candidate: the last searchable row");
    }
    {
        ps::Query q = At(1020.0f);
        q.active    = walkOnly;
        q.banned    = { { 15, 18 }, { 19, 22 }, { 200, 210 } };
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.bestRow == 23, "banned rows are no candidates: " + std::to_string(r.bestRow));
        const auto top = ps::TopCandidates(db, q, 1000);
        Check(std::none_of(top.begin(), top.end(), [](const ps::Candidate& c) { return (c.row >= 15 && c.row <= 22) || (c.row >= 200 && c.row <= 210); }),
              "TopCandidates respects the bans");
    }
    {
        ps::Query q = At(2010.0f);
        q.active    = runOnly;
        q.currentClip = 4;
        q.currentTime = 0.0;
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.playingRow == db.clips[4].first, "the playing row");
        Check(r.bestRow == db.clips[4].first + ps::kIgnoreSurrounding, "rows beside the playing one are the playing one: " + std::to_string(r.bestRow));
    }
    {
        ps::Query q   = At(3025.0f);
        q.active      = walkOnly;
        q.currentClip = 1;
        q.currentTime = 0.0;
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.bestRow >= 0 && db.clipOf[r.bestRow] != 1, "disableReselection: the playing child offers no candidate");
        const auto top = ps::TopCandidates(db, q, 1000);
        Check(std::none_of(top.begin(), top.end(), [](const ps::Candidate& c) { return c.clip == 1; }), "nor through TopCandidates");
        q.currentClip = -1;
        Check(db.clipOf[ps::Search(db, q).bestRow] == 1, "another clip's flag does not stop it being picked");
    }
    {
        ps::Query q   = At(1000.0f);
        q.active      = walkOnly;
        q.currentClip = 0;
        q.currentTime = 0.5;   // frame 15
        q.samples[1].x = 1015.0f;
        ps::SearchResult r = ps::Search(db, q);
        Check(r.playing == 0.0f && !r.wins, "the playing row at cost 0 holds");
        q.clipEnd = true;
        Check(ps::Search(db, q).wins, "unless the clip is ending");
        q.clipEnd   = false;
        q.interrupt = true;
        Check(ps::Search(db, q).wins, "or interrupted");
        q.interrupt = false;
        q.biases    = { { 0.0f, 5000.0f, 0.0f }, {} };
        r           = ps::Search(db, q);
        Check(r.playing == 5000.0f && r.wins, "the continuing bias counts on the playing row alone");
        q.hold = true;
        Check(!ps::Search(db, q).wins, "a pick within its dwell holds against a far better candidate");
        q.clipEnd = true;
        Check(ps::Search(db, q).wins, "but not past its clip's end");
        q.clipEnd = false;
        q.hold    = false;
        const float best = r.best;
        Check(best > 0.0f, "the margin case needs a candidate above cost 0: " + std::to_string(best));
        q.biases = { { 0.0f, best * 1.05f, 0.0f }, {} };
        Check(!ps::Search(db, q).wins, "a candidate 5% better than the playing row does not replace it");
        q.margin = 0.0f;
        Check(ps::Search(db, q).wins, "without the margin it does");
        q.margin = ps::kSwitchMargin;
        q.biases = { { 0.0f, best * 1.2f, 0.0f }, {} };
        Check(ps::Search(db, q).wins, "20% better clears the margin");
    }
    {
        ps::Query q = At(2005.0f);
        q.biases    = { {}, { 7.0f, 0.0f, -100.0f } };
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.bestRow == db.clips[4].first + 5 && r.best == 7.0f - 100.0f, "base and looping biases add to a looping child's rows");
        const auto top = ps::TopCandidates(db, q, 1);
        Check(top.size() == 1 && top[0].bias == -93.0f && top[0].total == -93.0f && top[0].clip == 4 && top[0].frame == 5, "TopCandidates reports the bias");
    }
    {
        ps::Query q = At(0.0f);
        q.active    = { 0, 0 };
        const ps::SearchResult r = ps::Search(db, q);
        Check(r.bestRow == -1 && !r.wins && r.searched == 0 && ps::TopCandidates(db, q, 3).empty(), "no database searched: no candidate");
    }

    Clips empty;
    std::vector<std::string> ignored;
    Check(Built(unit, empty, &ignored) == nullptr, "no clip measurable: no database");
}

/// @brief Search and TopCandidates over random rows, biases, bans, picks and playing clips: the same winner, every time.
void AgreementChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    std::mt19937                    rng(0x5EA7C4);
    std::normal_distribution<float> normal(0.0f, 50.0f);
    std::uniform_real_distribution<float> unit01(0.0f, 1.0f);

    cmp::MmBundleSpec spec;
    Clips             clips;
    for (int d = 0; d < 3; ++d) {
        cmp::MmDatabase db;
        db.name = "D" + std::to_string(d);
        for (int c = 0; c < 4; ++c) {
            const std::string file = "animations/R/d" + std::to_string(d) + "c" + std::to_string(c) + ".hkx";
            std::uint8_t      flags = 0;
            if (c == 1) flags = cmp::kMmLooping;
            if (c == 2) flags = cmp::kMmDisableReselection;
            db.clips.push_back({ kRoot + file, "", flags, c == 3 ? 0.2f : 0.0f, c == 3 ? 1.0f : 0.0f });
            anim::MmCachedClip m;
            std::string name = file;
            std::replace(name.begin(), name.end(), '/', '\\');
            m.name          = name;
            m.frames        = 45 + 10 * c;
            m.frameDuration = 1.0f / 30.0f;
            m.duration      = static_cast<float>(m.frames - 1) / 30.0f;
            m.features.resize(static_cast<std::size_t>(m.frames) * F::kDims);
            for (float& v : m.features) v = normal(rng);
            m.rootPositions.assign(static_cast<std::size_t>(m.frames) * 3, 0.0f);
            clips[name] = std::move(m);
        }
        spec.databases.push_back(std::move(db));
    }
    Unit unit;
    ParseUnit(spec, reg, tmp / "random", unit);
    const auto dbp = Built(unit, clips);
    Check(dbp != nullptr, "the random database builds");
    if (!dbp) return;
    const ps::Database& db = *dbp;

    const auto randomTrajectory = [&](ps::QueryTrajectory& t) {
        for (ps::TrajectorySample& v : t) v = { normal(rng), normal(rng), normal(rng), normal(rng), normal(rng), normal(rng), normal(rng) };
    };
    int moved = 0;
    for (int i = 0; i < 400; ++i) {
        ps::Query q;
        randomTrajectory(q.samples);
        q.desiredX = unit01(rng) < 0.5f ? 0.0f : 200.0f;
        if (unit01(rng) < 0.7f) {
            q.currentClip = static_cast<int>(unit01(rng) * db.clips.size());
            q.currentTime = unit01(rng) * db.clips[q.currentClip].duration;
        }
        if (unit01(rng) < 0.5f) q.active = { static_cast<char>(unit01(rng) < 0.5f), 1, static_cast<char>(unit01(rng) < 0.5f) };
        if (unit01(rng) < 0.5f) {
            const auto b = [&] { return (unit01(rng) - 0.5f) * 40.0f; };
            q.biases = { { b(), b(), b() }, { b(), b(), b() }, { b(), b(), b() } };
        }
        for (int k = static_cast<int>(unit01(rng) * 4); k > 0; --k) {
            const int lo = static_cast<int>(unit01(rng) * (db.clipOf.size() - 1));
            q.banned.emplace_back(lo, lo + static_cast<int>(unit01(rng) * 40));
        }
        std::sort(q.banned.begin(), q.banned.end());
        q.clipEnd = unit01(rng) < 0.2f;

        const ps::SearchResult r     = ps::Search(db, q);
        const auto             top   = ps::TopCandidates(db, q, 5);
        const auto             every = ps::TopCandidates(db, q, db.clipOf.size());
        const std::string      at    = "query " + std::to_string(i);
        Check(r.searched == every.size(), at + ": the same candidates");
        Check(top.size() == (std::min<std::size_t>)(5, every.size()), at + ": n candidates");
        if (r.bestRow < 0) {
            Check(top.empty(), at + ": no winner, no candidates");
            continue;
        }
        Check(!top.empty() && top[0].row == r.bestRow && top[0].total == r.best, at + ": the same winner and cost");
        if (r.wins) ++moved;
        for (std::size_t k = 0; k + 1 < every.size(); ++k)
            Check(every[k].total < every[k + 1].total || (every[k].total == every[k + 1].total && every[k].row < every[k + 1].row), at + ": best first");
        for (std::size_t k = 0; k < top.size(); ++k) Check(top[k].row == every[k].row, at + ": the top n are the first n");
        for (const ps::Candidate& c : top) {
            float sum = c.bias;
            for (const float g : c.groupCost) sum += g;
            Check(std::fabs(sum - c.total) <= 1.0e-3f * (std::fabs(c.total) + 1.0f), at + ": the groups and the bias sum to the total");
            Check(db.clipOf[c.row] == c.clip && db.clips[c.clip].first + c.frame == c.row, at + ": row, clip and frame agree");
        }
    }
    Check(moved > 0, "some searches found a winner");

    for (int d = 0; d < F::kDims; ++d) {
        Check(ps::GroupOf(d) >= 0 && ps::GroupOf(d) < ps::kGroups, "GroupOf is in range");
        Check(ps::WeightGroupOf(d) >= 0 && ps::WeightGroupOf(d) < ps::kWeightGroups, "WeightGroupOf is in range");
    }
    for (const ps::PoseDatabase& p : db.databases) Check(p.space == 0, "a database stating no schema weighs the classic layout");
    const auto defaults = ps::DefaultWeights();
    Check(std::equal(defaults.begin(), defaults.end(), db.spaces[0].weight.begin()), "the classic layout weighs the defaults");
}

/// @brief The per-character Matcher over a small random database: when it searches, and what it plays.
void MatcherChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    std::mt19937                    rng(0x4A7C4);
    std::normal_distribution<float> normal(0.0f, 50.0f);

    cmp::MmBundleSpec spec;
    Clips             clips;
    for (int d = 0; d < 2; ++d) {
        cmp::MmDatabase db;
        db.name = "D" + std::to_string(d);
        for (int c = 0; c < 3; ++c) {
            const std::string file = "animations/M/d" + std::to_string(d) + "c" + std::to_string(c) + ".hkx";
            db.clips.push_back({ kRoot + file, "", 0, 0.0f, 0.0f });
            anim::MmCachedClip m;
            std::string name = file;
            std::replace(name.begin(), name.end(), '/', '\\');
            m.name          = name;
            m.frames        = 90;
            m.frameDuration = 1.0f / 30.0f;
            m.duration      = static_cast<float>(m.frames - 1) / 30.0f;
            m.features.resize(static_cast<std::size_t>(m.frames) * F::kDims);
            for (float& v : m.features) v = normal(rng);
            m.rootPositions.assign(static_cast<std::size_t>(m.frames) * 3, 0.0f);
            clips[name] = std::move(m);
        }
        spec.databases.push_back(std::move(db));
    }
    Unit unit;
    ParseUnit(spec, reg, tmp / "matcher", unit);
    const auto dbp = Built(unit, clips);
    Check(dbp != nullptr, "the matcher's database builds");
    if (!dbp) return;
    const ps::Database& db = *dbp;

    const ps::DatabaseSpec described = ps::SpecOf(*unit.selector);
    Check(described.children.size() == db.clips.size() && described.databases.size() == db.databases.size(), "a spec states the selector's rows");
    ps::DatabaseSpec moved = described;
    Check(moved.Key() == described.Key(), "the same selector, the same key");
    moved.children[0].rangeEnd = 0.5f;
    Check(moved.Key() != described.Key(), "a child's range changes the key");

    const auto request = [&] {
        ps::MatchRequest in;
        for (ps::TrajectorySample& v : in.samples) v = { normal(rng), normal(rng), normal(rng), normal(rng), normal(rng), normal(rng), normal(rng) };
        in.desiredX  = 200.0f;
        in.haveQuery = true;
        return in;
    };
    constexpr float kDt = 1.0f / 60.0f;

    {
        ps::Matcher m;
        ps::MatchRequest none;
        const ps::MatchStep waits = m.Update(db, none, kDt);
        Check(!waits.searched && waits.clip == -1 && waits.nextClip == -1, "no query, nothing plays");

        const ps::MatchRequest in = request();
        const ps::MatchStep first = m.Update(db, in, kDt);
        Check(first.searched && first.switched && first.clip == -1, "the first query searches, with nothing playing");
        Check(first.nextClip >= 0, "and names what plays next");
        const ps::MatchStep second = m.Update(db, in, kDt);
        Check(second.clip == first.nextClip && second.time == first.nextTime, "the next update plays what the last one named");
        Check(!second.searched, "a search is not due yet");
        Check(second.nextClip == second.clip && std::fabs(second.nextTime - (second.time + kDt)) < 1.0e-9, "the time goes on by the timestep");
    }
    {
        ps::Matcher m;
        ps::MatchRequest in = request();
        m.Update(db, in, kDt);
        double waited = 0;
        ps::MatchStep s;
        do {
            s = m.Update(db, in, kDt);
            waited += kDt;
        } while (!s.searched && waited < 1.0);
        Check(s.searched && waited >= ps::kSearchInterval - kDt, "the timer waits its interval");
        Check(std::string_view(s.trigger) == "timer", "and says so");

        ps::MatchStep again = m.Update(db, in, kDt);
        Check(!again.searched, "no search right after one");
        in.desiredX = 0.0f;
        int updates = 1;
        do {
            again = m.Update(db, in, kDt);
            ++updates;
        } while (!again.searched && updates < 60);
        Check(again.searched && std::string_view(again.trigger) == "forced", "a jump in the request searches early");
        Check(updates * kDt >= ps::kForcedSpacing - 1.0e-4f && updates * kDt < ps::kSearchInterval, "at the spacing, before the timer");
    }
    {
        /// @note Without a query nothing else can move it: it plays the first pick up to the clip's end.
        ps::Matcher      m;
        ps::MatchRequest in = request();
        in.rate = 3.0f;
        m.Update(db, in, kDt);
        ps::MatchRequest quiet;
        quiet.rate = 3.0f;
        ps::MatchStep s = m.Update(db, quiet, kDt);
        const double  duration = db.clips[s.clip].duration;
        for (int i = 0; i < 2000 && s.time < duration; ++i) {
            s = m.Update(db, quiet, kDt);
            Check(s.time <= duration, "the time stops at the clip's end");
            Check(!s.searched, "no query, no search");
        }
        Check(s.time == duration, "it plays to the clip's end");
        const ps::MatchStep ended = m.Update(db, in, kDt);
        Check(ended.searched && std::string_view(ended.trigger) == "clip end", "a clip's end searches at once");
    }
    {
        ps::Matcher m;
        ps::MatchRequest in = request();
        m.Update(db, in, kDt);
        const ps::MatchStep playing = m.Update(db, in, kDt);
        const int           from    = db.clips[playing.clip].database;
        Check(from >= 0, "the pick is in a database");

        ps::MatchRequest change = in;
        change.active = { 0, 0 };
        change.active[1 - from] = 1;
        ps::MatchStep unchanged = m.Update(db, change, kDt);
        Check(!unchanged.searched || std::string_view(unchanged.trigger) != "interrupt", "out of the pick without a change of input does not interrupt");
        change.inputsChanged = true;
        const ps::MatchStep cut = m.Update(db, change, kDt);
        Check(cut.searched && std::string_view(cut.trigger) == "interrupt", "out of the pick with a change of input interrupts");
        if (cut.switched) Check(db.clips[cut.nextClip].database == 1 - from, "an interrupt plays from the new pick");
    }
    {
        /// @note The ban: no pick lands within reach of where an earlier pick of the last reselectTime would be now.
        ps::Matcher m;
        struct Seen { int clip; double frame; double at; };
        std::vector<Seen> seen;
        double            clock = 0;
        int               picks = 0;
        for (int i = 0; i < 1200; ++i) {
            const ps::MatchStep s = m.Update(db, request(), kDt);
            clock += kDt;
            if (!s.switched) continue;
            ++picks;
            const int row = s.result.bestRow;
            for (const Seen& p : seen) {
                if (clock - p.at > 1.0 || p.clip != s.nextClip) continue;
                const double at = p.frame + (clock - p.at) * 1.0 / db.clips[p.clip].dt;
                Check(std::fabs((row - db.clips[p.clip].first) - at) >= ps::kIgnoreSurrounding - 1, "a recent pick's neighbourhood stays banned");
            }
            seen.push_back({ s.nextClip, static_cast<double>(row - db.clips[s.nextClip].first), clock });
        }
        Check(picks > 1, "random queries keep picking");
    }
    {
        ps::Matcher m;
        const ps::MatchRequest in = request();
        m.Update(db, in, kDt);
        m.Update(db, in, kDt);
        m.Restart();
        ps::MatchRequest none;
        Check(m.Update(db, none, kDt).clip == -1, "a restart forgets what plays");
        Check(m.Update(db, in, kDt).searched, "and the next query searches at once");
    }
}

/// @brief A clip through the same text-to-cached-clip path the plugin measures with.
void ChooserInputChecks() {
    const ps::Profile& profile = ps::kPlayer;
    ps::Caps           caps;
    caps.forward = 300.0f;
    const auto inputs = [&](float vy, float stickDegrees, bool stick, bool pivots = false) {
        ps::State s;
        s.vy = vy;
        ps::Input in;
        in.gait      = ps::Gait::Sprint;
        in.haveStick = stick;
        const float yaw = stickDegrees * 3.14159265f / 180.0f;
        in.stickX = std::sin(yaw), in.stickY = std::cos(yaw);
        ps::ChooserInputs io = ps::InitialChooserInputs();
        ps::MovementInputs(s, in, caps, profile, 0.0f, 0.0f, yaw, pivots, io);
        return io;
    };
    const ps::ChooserInputs cruising = inputs(300.0f, 0.0f, true);
    Check(cruising[ps::kMovementState] == 1 && cruising[ps::kGait] == 2 && cruising.real[ps::kSpeed2D] == 300.0f, "moving at speed, the gait as the input names it");
    Check(!cruising[ps::kIsStarting] && !cruising[ps::kIsPivoting], "a steady run neither starts nor pivots");
    Check(inputs(30.0f, 0.0f, true)[ps::kIsStarting] == 1, "speeding up well past its speed starts");
    Check(inputs(30.0f, 0.0f, true, true)[ps::kIsStarting] == 0, "not from a pivot");
    Check(inputs(300.0f, 90.0f, true)[ps::kIsPivoting] == 1, "a stick 90 degrees off the velocity pivots");
    Check(inputs(300.0f, 30.0f, true)[ps::kIsPivoting] == 0, "30 does not");
    Check(inputs(300.0f, 0.0f, false)[ps::kMovementState] == 0, "no stick is idle at any speed");

    {
        ps::State s;
        ps::Input in;
        in.gait        = ps::Gait::Run;
        in.haveStick   = true;
        in.turnInPlace = true;
        in.stickX = 1.0f, in.stickY = 0.0f;   // 90 degrees right
        ps::ChooserInputs io = ps::InitialChooserInputs();
        ps::MovementInputs(s, in, caps, profile, 0.0f, 0.0f, 1.5707963f, false, io);
        Check(io[ps::kMovementState] == 0, "a light stick is not moving");
        for (int i = 0; i < 15; ++i) ps::Step(s, in, caps, profile, 0.0f, 0.0f, 1.0f / 60.0f);
        Check(std::hypot(s.vx, s.vy) < 1.0e-3f, "it never gains a velocity");
        Check(s.yaw > 0.0f && s.yaw <= profile.turnInPlaceRate * 0.25f + 1.0e-4f, "it turns toward the stick, no faster than the cap");
        for (int i = 0; i < 120; ++i) ps::Step(s, in, caps, profile, 0.0f, 0.0f, 1.0f / 60.0f);
        Check(std::fabs(s.yaw - 1.5707963f) < 0.02f, "and comes to face it");

        ps::State run;
        run.vy = 300.0f;
        in.stickX = 0.0f, in.stickY = 1.0f;
        for (int i = 0; i < 60; ++i) ps::Step(run, in, caps, profile, 0.0f, 0.0f, 1.0f / 60.0f);
        Check(run.vy < 1.0f, "from a run, a light stick brakes to a stop");
    }

    ps::ChooserInputs     spinning = ps::InitialChooserInputs();
    ps::State             backward;
    backward.vy = -200.0f;
    ps::MovementInputs(backward, ps::Input{}, caps, profile, 0.0f, 0.0f, 0.0f, false, spinning);
    Check(spinning[ps::kShouldSpinTransition] == 1, "moving against the facing spins");
    Check(spinning[ps::kGait] == 1, "an input naming no gait keeps the last");

    ps::ChooserInputs values = ps::InitialChooserInputs();
    values[ps::kGait]        = 2;
    values[ps::kIsPivoting]  = 1;
    values.real[ps::kSpeed2D] = 12.5f;
    const ps::ChooserVariables vars(values);
    relay::Value               v;
    Check(vars.Read("MM_Gait", v) && v.type == relay::VType::Int && v.AsInt() == 2, "an INT32 reads as one");
    Check(vars.Read("MM_IsPivoting", v) && v.type == relay::VType::Bool && v.AsBool(), "a BOOL reads as one");
    Check(vars.Read("MM_Speed2D", v) && v.AsFloat() == 12.5f, "a REAL reads as one");
    Check(!vars.Read("iState", v) && !vars.Read("mm_gait", v), "any other name is the graph's, and case counts");

    std::size_t named = 0;
    for (const ps::ChooserColumn& c : ps::kChooserColumns) {
        relay::Value read;
        if (vars.Read(c.variable, read)) ++named;
    }
    Check(named == ps::kChooserColumns.size(), "every column names an input");
}

/// @brief A jump to another time of the playing clip blends from what was shown, as a change of clip does.
/// @brief The speed profile: built-ins for the replaced locomotion types, the ini's keys over them, nothing for the rest.
void SpeedProfileChecks() {
    namespace loco = CB::engine::locomotion;
    const auto none = [](std::string_view) { return std::string(); };
    const auto def  = loco::ResolveSpeedProfile("NPCDefault", none);
    Check(def && def->walk.forward == 148.0f && def->run.side == 259.0f, "NPCDefault resolves to GASP's walk and run");
    Check(!loco::ResolveSpeedProfile("NPCBleedout", none), "a type without built-ins or keys keeps its record");
    const auto keyed = loco::ResolveSpeedProfile("NPCDefault", [](std::string_view k) { return std::string(k == "fWalkForward" ? "100" : k == "fRunBack" ? "junk" : ""); });
    Check(keyed && keyed->walk.forward == 100.0f && keyed->run.back == 222.0f, "an ini key overrides, an unreadable one leaves the built-in");
    const auto added = loco::ResolveSpeedProfile("MyState", [](std::string_view k) { return std::string(k == "fRunForward" ? "400" : ""); });
    Check(added && added->run.forward == 400.0f && added->walk.forward == 0.0f, "a new type needs only its section");
    Check(loco::SpeedSection("NPCDefault") == "Speed.NPCDefault", "the section is Speed.<MNAM>, byte-exact");
    Check(loco::SpeedFactor(148.0f, 80.0f) == 1.85f && loco::SpeedFactor(0.0f, 80.0f) == 1.0f && loco::SpeedFactor(148.0f, 0.0f) == 1.0f,
          "the factor is wanted over record, 1 when either is unset");
}

/// @brief The clip speed the rate trims against: a stride's average, not a step's.
void ClipSpeedChecks() {
    const int          frames = 60;
    const double       dt     = 1.0 / 30.0;
    std::vector<float> p;
    for (int i = 0; i < frames; ++i) {
        const float x = i < 30 ? 0.0f : static_cast<float>((i - 30) * 10);   // stands for a second, then 300 units/s
        p.insert(p.end(), { x, 0.0f, 0.0f });
    }
    const auto v = ps::WindowedRootSpeeds(p, frames, dt);
    Check(v.size() == static_cast<std::size_t>(frames), "a speed per frame");
    Check(v[0] == 0.0f && std::fabs(v[59] - 300.0f) < 0.01f, "standing reads 0, steady reads its speed: " + std::to_string(v[59]));
    Check(v[30] > 100.0f && v[30] < 200.0f, "the start averages across the window: " + std::to_string(v[30]));
    Check(ps::WindowedRootSpeeds({}, 0, dt).empty(), "no frames, no speeds");
}

/// @brief A clip whose last frame meets its first loops; one that ends elsewhere does not.
void LoopFitChecks() {
    namespace F = anim::features;
    const int          frames = 30;
    const double       dt     = 1.0 / 30.0;
    std::vector<float> rows(static_cast<std::size_t>(frames) * F::kDims, 0.0f);
    const auto         pose = [&](int f, float footY, float speed) {
        float* r       = rows.data() + static_cast<std::size_t>(f) * F::kDims;
        r[F::kFootPos + 1] = footY;
        r[F::kFootPos + 4] = -footY;
        r[F::kFootVel + 1] = speed;
        r[F::kHipVel + 1]  = speed;
    };
    for (int f = 0; f < frames; ++f) pose(f, 20.0f * std::sin(f * 0.2f), 300.0f);
    pose(frames - 1, 0.0f, 300.0f);   // the last frame repeats the first
    Check(anim::FitLoop(rows.data(), frames, dt).period == frames - 1, "a repeated first frame loops, its cycle one frame short");
    pose(frames - 1, 8.0f, 300.0f);   // about a frame's travel (10) short of the first: it runs on into it
    Check(anim::FitLoop(rows.data(), frames, dt).period == frames, "a last frame one step from the first loops over every frame");
    pose(frames - 1, 0.0f, 0.0f);     // the same feet, but stopped: a start or a stop, not a loop
    Check(anim::FitLoop(rows.data(), frames, dt).period == 0, "a clip that ends stopped does not loop");
    pose(frames - 1, 40.0f, 300.0f);  // the feet elsewhere
    Check(anim::FitLoop(rows.data(), frames, dt).period == 0, "a clip that ends in another pose does not loop");
    Check(anim::FitLoop(rows.data(), 4, dt).period == 0, "too short to tell");
}

/// @brief Opt-in (CB_TEST_MM_CLIPS = a folder of unpacked clip units): every clip's loop fit, to tune FitLoop on real data.
void LoopFitReport() {
    const char* dir = std::getenv("CB_TEST_MM_CLIPS");
    if (!dir || !*dir) return;
    std::error_code ec;
    int             clips = 0, loops = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file() || e.path().extension() != ".hkx") continue;
        const auto read = disk::ReadFile(e.path());
        if (!read) continue;
        const std::string& text = *read;
        try {
            const anim::MmCachedClip c   = anim::MeasureMmClip(e.path().filename().string(), e.path().string(), text, 0);
            const anim::LoopFit      fit = anim::FitLoop(c.features.data(), c.frames, c.frameDuration);
            ++clips;
            if (c.loopFrames > 0) ++loops;
            std::printf("loopfit %-52s frames %4d period %4d fit %4d footPos %7.2f step %6.2f footVel %7.1f hipVel %7.1f\n",
                        e.path().filename().string().c_str(), c.frames, c.loopFrames, fit.period, fit.footPos, fit.step, fit.footVel, fit.hipVel);
        } catch (const std::exception& x) {
            std::printf("loopfit %-52s not measured: %s\n", e.path().filename().string().c_str(), x.what());
        }
    }
    std::printf("loopfit: %d of %d clip(s) loop\n", loops, clips);
}

/// @brief A schema's features measured through a rig: every kind, its weights, a missing bone, a loop's wrap.
void SchemaMeasureChecks() {
    anim::Rig rig;
    rig.bones.push_back({ "Root", -1, {}, {} });
    rig.bones.push_back({ "Pelvis", 0, { 0, 0, 50 }, {} });
    rig.bones.push_back({ "Foot", 1, { 10, 0, -50 }, {} });

    anim::AnimationDef def;   // the root walks +y at 100 units/s for a second; nothing else moves
    def.numFrames     = 31;
    def.frameDuration = 1.0f / 30.0f;
    def.duration      = 1.0f;
    anim::AnimTrackDef root;
    root.bone = "Root";
    root.translation.push_back({ 0.0f, { 0, 0, 0 } });
    root.translation.push_back({ 1.0f, { 0, 100, 0 } });
    root.rotation.push_back({ 0.0f, { 0, 0, 0, 1 } });
    def.tracks.push_back(root);
    anim::AnimCurveDef speed;
    speed.name = "Speed";
    speed.keys.push_back({ 0.0f, 100.0f, anim::AnimCurveKey::Interp::Constant });
    def.curves.push_back(speed);

    anim::SchemaSpec schema;
    anim::FeatureSpec traj;
    traj.kind    = anim::FeatureKind::Trajectory;
    traj.weight  = 2;
    traj.samples = { { 0.5f, 1, 1, 1, 0 }, { -0.2f, 0.5f, 0, 0, 0 } };
    anim::FeatureSpec pos;
    pos.kind = anim::FeatureKind::Position;
    pos.bone = "Foot";
    anim::FeatureSpec vel;
    vel.kind = anim::FeatureKind::Velocity;
    vel.bone = "Foot";
    anim::FeatureSpec head;
    head.kind = anim::FeatureKind::Heading;
    head.bone = "Pelvis";
    anim::FeatureSpec curve;
    curve.kind  = anim::FeatureKind::Curve;
    curve.curve = "Speed";
    schema.features = { traj, pos, vel, head, curve };

    Check(anim::SchemaDims(schema) == 23, "dims: two trajectory samples, a position, a velocity, a heading, a curve");
    const auto w = anim::SchemaWeights(schema);
    Check(w.size() == 23 && w[0] == 2.0f && w[6] == 0.0f && w[7] == 1.0f && w[9] == 0.0f && w[14] == 1.0f,
          "a sample part's weight times its feature's");

    std::string err;
    const auto  rows = anim::MeasureSchema(def, rig, schema, 0, err);
    Check(rows.has_value(), "the schema measures: " + err);
    if (!rows) return;
    const float* r0 = rows->data();
    Check(std::fabs(r0[1] - 50.0f) < 0.01f && std::fabs(r0[5] - 100.0f) < 0.5f, "half a second ahead: 50 forward, at 100 units/s");
    Check(r0[7] == 0.0f && r0[8] == 0.0f, "back before the first frame holds at it");
    Check(std::fabs(r0[14] - 10.0f) < 0.01f && std::fabs(r0[16]) < 0.01f, "the foot from the root, through the pelvis's reference pose");
    Check(std::fabs(r0[17]) < 0.01f && std::fabs(r0[18]) < 0.01f, "the foot moves with the root: no velocity of its own");
    Check(std::fabs(r0[20]) < 0.01f && std::fabs(r0[21] - 1.0f) < 0.01f, "the pelvis faces forward");
    Check(r0[22] == 100.0f, "the curve's value");
    const float* last = rows->data() + 30 * 23;
    Check(std::fabs(last[1]) < 0.01f, "the last frame's future holds at the clip's end");

    const auto looped = anim::MeasureSchema(def, rig, schema, 30, err);
    Check(looped && std::fabs((*looped)[30 * 23 + 1] - 50.0f) < 0.01f, "a loop's last frame sees its next cycle ahead");

    schema.features[1].bone = "Hand";
    Check(!anim::MeasureSchema(def, rig, schema, 0, err) && err.find("Hand") != std::string::npos, "a bone the rig lacks is refused by name");
}

/// @brief A database pointing at a typed schema: its candidates are weighed in that schema's rows, a database stating none
///        in the classic layout, and the playing frame's pose is read in whichever space a candidate is weighed in.
void TypedSpaceChecks() {
    anim::Rig rig;
    rig.bones.push_back({ "Root", -1, {}, {} });
    anim::SchemaSpec schema;
    schema.name = "Curve only";
    anim::FeatureSpec curve;
    curve.kind  = anim::FeatureKind::Curve;
    curve.curve = "Lean";
    schema.features.push_back(curve);
    const std::uint64_t key = anim::SchemaKey(schema, rig);
    Check(key == anim::SchemaKey(schema, rig), "a schema's key is stable");
    anim::SchemaSpec heavier = schema;
    heavier.features[0].weight = 5;
    Check(anim::SchemaKey(heavier, rig) == key, "a weight is no part of the key: it never changes the rows");
    anim::Rig moved = rig;
    moved.bones[0].translation.z = 1;
    Check(anim::SchemaKey(schema, moved) != key, "the rig is");

    ps::DatabaseSpec spec;
    spec.schemas.push_back(schema);
    ps::SpecDatabase typed, classic;
    typed.name     = "Typed";
    typed.schema   = 0;
    classic.name   = "Classic";
    spec.databases = { typed, classic };
    for (int i = 0; i < 2; ++i) {
        ps::SpecChild c;
        c.clip     = "c" + std::to_string(i);
        c.database = i == 0 ? "Typed" : "Classic";
        spec.children.push_back(c);
    }
    std::map<std::string, anim::MmCachedClip> clips;
    for (int i = 0; i < 2; ++i) {
        anim::MmCachedClip c = LineClip("c" + std::to_string(i), 30, 1000.0f * (i + 1), 0.0f);
        anim::MmCachedClip::SchemaRows block;
        block.key = key;
        for (int f = 0; f < 30; ++f) block.rows.push_back(static_cast<float>(f + 100 * i));
        c.schemaRows.push_back(block);
        clips[c.name] = c;
    }
    std::vector<std::string> warned;
    ps::ClipNeeds            seen;
    const auto source = [&](const std::string& name, const ps::ClipNeeds& needs) -> const anim::MmCachedClip& {
        seen = needs;
        return clips.at(name);
    };
    const auto db = ps::BuildDatabase(spec, &rig, source, [&](const std::string& m) { warned.push_back(m); });
    Check(db != nullptr, "the database builds");
    if (!db) return;
    Check(seen.keys.size() == 1 && seen.keys[0] == key && seen.rig == &rig, "the clip source is asked for the typed schema's rows");
    Check(db->spaces.size() == 2 && !db->spaces[1].classic && db->databases[0].space == 1 && db->databases[1].space == 0,
          "the typed database weighs its schema, the other the classic layout");
    Check(db->spaces[1].rows.size() == 60 && db->spaces[0].rows.size() == 60u * F::kDims, "every row has a row in every space in use");

    const auto noRig = ps::BuildDatabase(spec, nullptr, source, [&](const std::string& m) { warned.push_back(m); });
    Check(noRig && noRig->databases[0].space == 0, "without a rig the typed database falls back to the classic layout");
    Check(std::any_of(warned.begin(), warned.end(), [](const std::string& m) { return m.find("rig") != std::string::npos; }), "and says why");

    clips["c0"].schemaRows.clear();
    warned.clear();
    const auto missing = ps::BuildDatabase(spec, &rig, source, [&](const std::string& m) { warned.push_back(m); });
    Check(missing && missing->clips[0].frames == 0, "a clip without the schema's rows is never chosen");
    Check(std::any_of(warned.begin(), warned.end(), [](const std::string& m) { return m.find("Curve only") != std::string::npos; }),
          "and is warned of by the schema's name");
}

/// @brief The bundle generator writes a database's schema with its features, and binds the selector's speed: parsed back,
///        the spec states them as written.
void SchemaBundleChecks(const schema::SchemaRegistry& reg, const fs::path& tmp) {
    cmp::MmBundleSpec spec = LineSpec();
    anim::SchemaSpec  schema;
    schema.name     = "Run";
    schema.rootBone = "NPC Root [Root]";
    anim::FeatureSpec traj;
    traj.kind    = anim::FeatureKind::Trajectory;
    traj.samples = { { 0.35f, 1, 1, 1, 0 }, { -0.1f, 0.3f, 0, 1, 0 } };
    anim::FeatureSpec foot;
    foot.kind   = anim::FeatureKind::Position;
    foot.bone   = "NPC L Foot [Lft ]";
    foot.origin = "NPC R Foot [Rft ]";
    foot.weight = 0.5f;
    schema.features    = { traj, foot };
    spec.schemas       = { schema };
    spec.databases[1].schema = 0;
    Unit unit;
    ParseUnit(spec, reg, tmp / "schema-bundle", unit);
    if (!unit.selector) return;
    const ps::DatabaseSpec back = ps::SpecOf(*unit.selector);
    Check(back.schemas.size() == 1 && back.databases.size() == 2 && back.databases[0].schema == -1 && back.databases[1].schema == 0,
          "the schema is written once and the database points at it");
    if (back.schemas.size() != 1) return;
    Check(anim::SchemaText(back.schemas[0]) == anim::SchemaText(schema) && back.schemas[0].name == "Run",
          "its features read back as written, byte-exact");
    bool speed = false;
    if (const auto* set = unit.selector->Child("variableBindingSet"))
        for (const auto& o : set->Children("bindings"))
            if (const auto* bd = dynamic_cast<const codec::io::SchemaObject*>(o.get()); bd && bd->Str("memberPath") == "speed") speed = true;
    Check(speed, "the selector's speed is bound");

    spec.databases[1].schema = 3;
    cmp::MmFiles files;
    std::string  err;
    Check(!cmp::BuildMotionMatchingBehavior(spec, reg, files, err) && err.find("schema 3") != std::string::npos, "a schema index past the spec is refused");
}

void DeadBlendChecks() {
    const ps::DeadBlendTuning tuning{ 0.2f, 0.1f };
    const auto                run = [&](bool jumped, int child) {
        ps::DeadBlend b;
        b.dt = 1.0f / 60.0f;
        std::vector<ps::Bone> pose(1);
        for (int f = 0; f < 3; ++f) {
            pose[0].t[0] = static_cast<float>(f);
            b.Step(pose.data(), 1, 0, false, tuning);
        }
        pose[0].t[0] = 100.0f;
        b.Step(pose.data(), 1, child, jumped, tuning);
        return pose[0].t[0];
    };
    Check(run(false, 0) == 100.0f, "playing on in the same clip shows the live pose");
    Check(run(true, 0) < 10.0f, "a jump within the clip starts from the pose shown: " + std::to_string(run(true, 0)));
    Check(run(false, 1) < 10.0f, "and so does a change of clip");
}

void WeightChecks() {
    Check(ps::WeightGroupName(0) == "position+0.2", "the first group is the first sample's position");
    Check(ps::WeightGroupName(1 * ps::kChannels + 1) == "facing+0.33", "a sample's label rounds to two places");
    Check(ps::WeightGroupName(4 * ps::kChannels + 2) == "velocity+1", "and drops trailing zeros");
    Check(ps::WeightGroupName(5 * ps::kChannels + 3) == "height-0.05", "the past samples are minus");
    Check(ps::WeightGroupName(F::kSamples * ps::kChannels) == "feet" && ps::WeightGroupName(ps::kWeightGroups - 1) == "pelvisFacing", "then the pose parts");
    Check(ps::WeightGroupName(ps::kWeightGroups).empty() && ps::WeightGroupName(-1).empty(), "past the end, nothing");
    for (int g = 0; g < ps::kWeightGroups; ++g) {
        int dims = 0;
        for (int d = 0; d < F::kDims; ++d) dims += ps::WeightGroupOf(d) == g;
        Check(dims > 0, "every group holds a dimension: " + ps::WeightGroupName(g));
    }

    const auto defaults = ps::DefaultWeights();
    const int  pos13    = F::SampleAt(1) + F::kSamplePos, pos02 = F::SampleAt(0) + F::kSamplePos;
    Check(defaults[pos13] == ps::kWeightTrajPos && defaults[pos02] == 0.0f, "by default the classic samples count and the new ones don't");
    Check(defaults[F::SampleAt(1) + F::kSampleFacing] == ps::kWeightTrajDir && defaults[F::SampleAt(1) + F::kSampleVelocity] == 0.0f, "facing does, velocity doesn't");
    Check(defaults[F::SampleAt(7) + F::kSamplePos] == ps::kWeightTrajPos && defaults[F::SampleAt(5) + F::kSamplePos] == 0.0f, "back too: 1/3 s does, 0.05 s doesn't");
    Check(defaults[F::kFootPos] == ps::kWeightFootPos && defaults[F::kFootSpacing] == 0.0f && defaults[F::kPelvisFacing] == 0.0f, "the pose's classic parts count");

    auto       w       = defaults;
    const auto unknown = ps::ApplyWeights({ { "position+0.2", 10.0f }, { "position", 2.0f }, { "height+", 3.0f }, { "Position", 1.0f }, { "feet", 0.0f } }, w);
    Check(w[pos02] == 10.0f, "a narrower name wins whatever its order");
    Check(w[pos13] == 2.0f && w[F::SampleAt(9) + F::kSamplePos] == 2.0f, "a channel alone covers every sample");
    Check(w[F::SampleAt(2) + F::kSampleHeight] == 3.0f && w[F::SampleAt(6) + F::kSampleHeight] == 0.0f, "a channel with + covers the samples ahead only");
    Check(w[F::kFootPos] == 0.0f && w[F::kFootVel] == ps::kWeightFootVel, "a pose part, and only it");
    Check(unknown == std::vector<std::string>{ "Position" }, "a name in the wrong case names nothing");
}

void MeasureChecks() {
    anim::AnimationDef def;
    def.name          = "t";
    def.duration      = 1.0f;
    def.numFrames     = 31;
    def.frameDuration = 1.0f / 30.0f;
    def.skeleton      = "s";
    const auto track = [&](const char* bone, float z) {
        anim::AnimTrackDef t;
        t.bone = bone;
        anim::Vec3Keyframe a, b;
        a.time = 0.0f, b.time = 1.0f;
        a.value[2] = b.value[2] = z;
        t.translation = { a, b };
        anim::QuatKeyframe q0, q1;
        q0.time = 0.0f, q1.time = 1.0f;
        t.rotation = { q0, q1 };
        def.tracks.push_back(std::move(t));
    };
    track("NPC Root [Root]", 0.0f);
    def.tracks[0].translation[1].value[1] = 100.0f;
    anim::AnimCurveDef curve;
    curve.name = "Speed";
    curve.keys.push_back({ 0.0f, 100.0f, anim::AnimCurveKey::Interp::Linear });
    def.curves.push_back(curve);

    std::string err;
    const std::string rootOnly = anim::EmitAnimationYaml(def);
    bool threw = false;
    try {
        (void)anim::MeasureMmClip("t", "t.hkx", rootOnly, 7);
    } catch (const std::exception& e) {
        threw = std::string(e.what()).find("leg chains") != std::string::npos;
    }
    Check(threw, "a clip without the humanoid chains throws");
    threw = false;
    try {
        (void)anim::MeasureMmClip("t", "t.hkx", "not: a clip", 7);
    } catch (const std::exception&) {
        threw = true;
    }
    Check(threw, "text that is no clip throws");

    for (const char* bone : { "NPC COM [COM ]", "NPC Pelvis [Pelv]", "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]",
                              "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]" })
        track(bone, 10.0f);
    const std::string text = anim::EmitAnimationYaml(def);
    const anim::MmCachedClip c = anim::MeasureMmClip("animations\\T\\t.hkx", "t.hkx", text, 1234);
    Check(c.name == "animations\\T\\t.hkx" && c.fingerprint == 1234 && c.frames == 31, "the measured clip's name, fingerprint and frames");
    Check(c.features.size() == 31u * F::kDims && c.rootPositions.size() == 31u * 3, "its features and root positions");
    Check(std::fabs(c.frameDuration - 1.0f / 30.0f) < 1.0e-6f && std::fabs(c.duration - 1.0f) < 1.0e-6f, "its timing");
    Check(c.curves.size() == 1 && c.curves[0].name == "Speed", "its curves, byte-exact");
    Check(std::fabs(c.rootPositions[30 * 3 + 1] - 100.0f) < 1.0e-3f, "its root travel");

    const auto parsed = anim::AnimationYamlLoader::LoadFromString(text, "t.hkx");
    const auto motion = anim::MeasureClip(parsed, err);
    std::vector<float> direct;
    if (motion) anim::AppendClipFeatures(*motion, direct);
    Check(motion && direct == c.features, "the same features as measuring directly: " + err);

    std::vector<std::uint8_t> bytes;
    std::vector<anim::MmCachedClip> back;
    Check(anim::WriteMmCache({ c }, bytes, err) && anim::ReadMmCache(bytes, back, err), "the cache round trips: " + err);
    Check(back.size() == 1 && back[0].features == c.features && back[0].fingerprint == 1234, "a cached clip's features come back");
    std::array<std::vector<float>, 4> speedsA, speedsB;
    const auto a = ps::MeasuredFrom(c, speedsA);
    const auto b = ps::MeasuredFrom(back[0], speedsB);
    Check(a->rows == b->rows && a->searchable == b->searchable && a->gait == b->gait, "a cache hit and a fresh measurement build the same rows");
}

}  // namespace

void RunPoseSearchTests(const schema::SchemaRegistry& reg) {
    const fs::path tmp = fs::temp_directory_path() / ("cb-pose-search-" + std::to_string(_getpid()));
    RelayChecks();
    DatabaseChecks(reg, tmp);
    AgreementChecks(reg, tmp);
    MatcherChecks(reg, tmp);
    ChooserInputChecks();
    WeightChecks();
    DeadBlendChecks();
    SpeedProfileChecks();
    ClipSpeedChecks();
    LoopFitChecks();
    LoopFitReport();
    SchemaMeasureChecks();
    TypedSpaceChecks();
    SchemaBundleChecks(reg, tmp);
    MeasureChecks();
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

}  // namespace CB::engine::tests
