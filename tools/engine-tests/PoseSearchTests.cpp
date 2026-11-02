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
#include <array>
#include <cmath>
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

    static std::vector<std::string> warned;
    relay::SetWarnSink([](std::string_view m) { warned.emplace_back(m); });
    static constexpr std::array kString{ relay::VType::String };
    relay::RegisterFunction("GraphVar", relay::VType::Float, kString, nullptr);
    relay::SetWarnSink(nullptr);
    Check(warned.size() == 1 && warned[0].find("GraphVar") != std::string::npos, "a repeated registration warns through the sink");
    Check(Compiled("GraphVar(\"MM_Gait\")").Valid(), "and the first registration stays");
}

const std::string kRoot = "meshes/actors/character/";

/// @brief A clip of `frames` raw rows, every dimension 0 but the first trajectory x (`xBase` + the frame); its Speed curve
///        reads 0 until `standUntil`, then 100.
anim::MmCachedClip LineClip(const std::string& name, int frames, float xBase, float standUntil) {
    anim::MmCachedClip c;
    c.name          = name;
    c.frames        = frames;
    c.frameDuration = 1.0f / 30.0f;
    c.duration      = static_cast<float>(frames - 1) / 30.0f;
    c.features.assign(static_cast<std::size_t>(frames) * F::kDims, 0.0f);
    for (int f = 0; f < frames; ++f) c.features[static_cast<std::size_t>(f) * F::kDims + F::kTrajPos] = xBase + static_cast<float>(f);
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
        *unit.selector,
        [&](const std::string& name) -> const anim::MmCachedClip& {
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
    q.x[0]     = x;
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
    Check(db.rows.size() == 240u * F::kDims && db.clipOf.size() == 240 && db.searchable.size() == 240 && db.rowGates.size() == 240 && db.rowGait.size() == 240,
          "every per-row array covers every row");
    Check(db.clipOf[0] == 0 && db.clipOf[60] == 1 && db.clipOf[120] == 3 && db.clipOf[239] == 4, "row -> clip");
    Check(db.clips[1].flags == ps::kFlagDisableReselection && db.clips[4].flags == ps::kFlagLooping, "the row's flags");

    Check(db.searchable[0] && db.searchable[29] && !db.searchable[30] && !db.searchable[59], "a frame's future must lie inside its clip");
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
        q.x[0] = 1028.0f;
        Check(ps::Search(db, q).bestRow == ranged + 24, "past the range, its last row");
        Check(ps::Search(db, At(1035.0f)).bestRow == 29, "a horizon-cut frame is no candidate: the last searchable row");
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
        q.x[0]        = 1015.0f;
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

    const auto randomArray = [&](std::array<float, 3>& a) { for (float& v : a) v = normal(rng); };
    int moved = 0;
    for (int i = 0; i < 400; ++i) {
        ps::Query q;
        randomArray(q.x), randomArray(q.y), randomArray(q.dirX), randomArray(q.dirY);
        randomArray(q.pastX), randomArray(q.pastY), randomArray(q.pastDirX), randomArray(q.pastDirY);
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
        const int g = ps::GroupOf(d);
        Check(g >= 0 && g < ps::kGroups, "GroupOf is in range");
        const float weights[ps::kGroups] = { ps::kWeightTrajPos, ps::kWeightTrajDir, ps::kWeightPastPos, ps::kWeightPastDir,
                                             ps::kWeightFootPos, ps::kWeightFootVel, ps::kWeightHipVel };
        Check(db.weight[d] == weights[g], "a dimension weighs its group's weight");
    }
}

/// @brief A clip through the same text-to-cached-clip path the plugin measures with.
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
    MeasureChecks();
    std::error_code ec;
    fs::remove_all(tmp, ec);
}

}  // namespace CB::engine::tests
