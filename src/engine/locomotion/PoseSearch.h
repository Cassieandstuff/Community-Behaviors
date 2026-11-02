#pragma once

#include <compile/MotionFeatures.h>
#include <compile/MotionMatchingCache.h>
#include <locomotion/Movement.h>
#include <relay/Relay.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief The motion-matching database and its scoring, game-free: a database built from a CBPoseSearchGenerator and
///        its clips' measurements, the chooser and biases over it, and the brute-force nearest-frame search. The ONE
///        scoring the plugin's controller and an editor's match-test panel share.

namespace CB::engine::codec::io { class SchemaObject; }
namespace CB::engine::decompile { struct ParsedHky; }

namespace CB::engine::locomotion {

    inline constexpr int kIgnoreSurrounding = 20;   ///< frames: a candidate this close in the current clip is the current
    inline constexpr float kForceMoving     = 10.0f;   ///< game units/s: a direction counts only above this speed; a request below it stands

    /// @brief Each feature group's weight (Holden's demo defaults).
    /// @note  Applied at search time, never stored in the rows, so a weight change needs no rebuild.
    /// @note  The past groups start at the future's weights.
    inline constexpr float kWeightTrajPos = 1.0f, kWeightTrajDir = 1.5f, kWeightPastPos = 1.0f, kWeightPastDir = 1.5f,
                           kWeightFootPos = 0.75f, kWeightFootVel = 1.0f, kWeightHipVel = 1.0f;
    inline constexpr int   kGroups = 7;

    /// @return the feature group of dimension `d`: trajectory position, trajectory direction, past position, past
    ///         direction, foot position, foot velocity, hip velocity
    int GroupOf(int d);

    inline constexpr std::uint8_t kFlagDisableReselection = 1, kFlagLooping = 2;   ///< Clip::flags bits
    /// @brief The per-row bits the search gates on: its database's candidate gates, evaluated at the row's frame.
    inline constexpr std::uint8_t kRowStanding = 1, kRowMoving = 2;

    /// @brief The speed bands a frame's Speed curve falls in (game units/s): standing below kStandSpeed, then Walk,
    ///        Run from kWalkRun, Sprint from kRunSprint. Midway between the clips' measured plateaus (walk 92, run 284,
    ///        sprint 426), as hky-tool's tagger set them.
    inline constexpr float kStandSpeed = 15.0f, kWalkRun = 188.0f, kRunSprint = 355.0f;
    /// @brief A steady forward frame (the clips' own gait speeds): travel within kForwardDeg of facing, speed changing
    ///        by less than kSteadyAccel (game units/s²).
    inline constexpr float kForwardDeg = 45.0f, kSteadyAccel = 100.0f;

    /// @brief The motion curves' names, byte-exact, as hky-tool's motionCurves generator writes them.
    inline constexpr const char* kSpeedCurve     = "Speed";
    inline constexpr const char* kDirectionCurve = "MoveDirection";
    inline constexpr const char* kAccelCurve     = "Acceleration";

    /// @brief One child of the selector (MM_Clip indexes these).
    struct Clip {
        std::string  name;            ///< the clip generator's animationName
        int          first    = 0;    ///< its first row
        int          frames   = 0;    ///< 0 = the clip could not be measured (its index stays, never chosen)
        double       dt       = 1.0 / 30.0;
        double       duration = 0;    ///< what the user-controlled time fraction spans
        int          database = -1;   ///< its PoseDatabase, -1 = in none (never searched)
        std::uint8_t flags    = 0;    ///< kFlag* bits
        float        rangeStart = 0;  ///< seconds: the search cuts only into [rangeStart, rangeEnd]
        float        rangeEnd   = 0;  ///< <= 0 = the clip's end
    };

    /// @brief A condition expression read from the graph, compiled once; `text` kept for the logs.
    struct Expression {
        std::string         text;
        relay::CompiledExpr expr;   ///< invalid when `text` is empty or does not compile
    };

    /// @brief Unreal's per-database cost biases, added to the feature cost (negative = preferred), as evaluated for
    ///        one frame.
    struct Biases {
        float base       = 0;   ///< every candidate from the database
        float continuing = 0;   ///< the playing pose while it is from the database
        float looping    = 0;   ///< candidates from its looping children
    };

    /// @brief One of the graph's databases (CBPoseSearchDatabase).
    struct PoseDatabase {
        std::string name;
        /// @brief The biases' expressions, evaluated per frame (an invalid one is 0).
        Expression  base, continuing, looping;
        /// @brief The candidate gates over each row's curves, evaluated once per row at build (empty: every row; one
        ///        that does not compile: no row).
        Expression  standing, moving;
        bool        pivots = false;   ///< its tags hold "Pivots" (byte-exact): IsStarting and spins stay false
    };

    /// @brief One chooser row (CBPoseSearchChooserRow).
    struct ChooserRow {
        Expression       when;     ///< empty text: always holds; text that does not compile: never holds
        std::vector<int> databases;
    };

    /// @brief The database: one row of kDims floats per child frame, normalized (mean, per-group spread) but unweighted.
    struct Database {
        std::vector<Clip>         clips;
        std::vector<float>        rows;
        std::vector<int>          clipOf;       ///< row -> clip
        std::vector<char>         searchable;   ///< row's whole future lies inside its clip, and the row inside its child's range
        std::vector<std::uint8_t> rowGates;     ///< the frame's kRow* bits (the search's gates)
        std::vector<std::uint8_t> rowGait;      ///< the frame's Gait from its Speed curve (None standing)
        std::array<float, anim::features::kDims> mean{}, scale{}, weight{};
        /// @brief The clips' own speed per gait (game units/s, indexed by Gait), 0 where no clip has one: the median
        ///        root speed of the steady forward frames in that gait's Speed band (CollectGaitSpeeds).
        std::array<float, 4>      gaitSpeed{};
        std::vector<PoseDatabase> databases;
        /// @brief Empty without a chooser: every database is searched.
        std::vector<ChooserRow>   chooser;
        std::vector<int>          fallback;     ///< none of the rows holds: these; empty = every database
        bool                      haveChooser = false;
    };

    /// @brief One distinct clip's measurement (raw, unnormalized features), shared by every child that plays it.
    struct Measured {
        std::vector<float>            rows;
        std::vector<char>             searchable;
        std::vector<std::uint8_t>     gait;     ///< per frame, a Gait from the Speed curve's band
        std::vector<anim::AnimCurveDef> curves;   ///< the clip's curves, which its databases' gates read per row
        int                           frames   = 0;
        double                        dt       = 1.0 / 30.0;
        double                        duration = 0;
    };

    /// @brief A clip's curves at one frame, as the variables a candidate gate reads (GraphVar("Speed")).
    /// @note  A curve the clip lacks is not this source's: GraphVar then reads 0 (the build has no actor).
    class CurveVariables final : public relay::VariableSource {
    public:
        explicit CurveVariables(const std::vector<anim::AnimCurveDef>& curves) : m_curves(curves) {}
        float time = 0;   ///< seconds into the clip
        bool  Read(const char* name, relay::Value& out) const override;

    private:
        const std::vector<anim::AnimCurveDef>& m_curves;
    };

    /// @brief Receives what a build would warn of (a bad expression, an unknown database, a clip not measured).
    using WarnFn = std::function<void(const std::string& message)>;
    /// @brief The clip an animationName (byte-exact) names, measured or from a cache.
    /// @warning Throws std::exception when it cannot be had: that child is warned of and never chosen.
    /// @note  The reference need only hold until the next call; each distinct name is asked for once.
    using ClipSource = std::function<const anim::MmCachedClip&(const std::string& animationName)>;

    /// @brief The behavior graph's root selector, or null when the graph is not the contract's shape (a
    ///        CBPoseSearchGenerator as its hkbBehaviorGraph's rootGenerator).
    codec::io::SchemaObject* RootSelector(const decompile::ParsedHky& g);

    /// @brief Each frame's gait from `c`'s Speed curve (None while standing, and for a clip without one).
    /// @pre   `out.dt` is set.
    void AppendRowGaits(const anim::MmCachedClip& c, Measured& out);
    /// @brief Add `c`'s steady forward frames' horizontal root speeds to their gait's list (see Database::gaitSpeed).
    /// @note  A clip without the Speed, MoveDirection and Acceleration curves adds none.
    void CollectGaitSpeeds(const anim::MmCachedClip& c, double dt, const std::vector<std::uint8_t>& gait,
                           std::array<std::vector<float>, 4>& out);
    /// @brief `c` as the database's per-clip measurement.
    /// @note  Built from the cached form on a miss too, so a cache hit and a fresh measurement build the same rows.
    std::unique_ptr<Measured> MeasuredFrom(const anim::MmCachedClip& c, std::array<std::vector<float>, 4>& gaitSpeeds);

    /// @brief `text` compiled as a condition expression; a non-empty one that fails is warned of, naming `what` and
    ///        `fallback` (what it counts as instead).
    Expression CompileExpression(std::string text, std::string_view what, std::string_view fallback, const WarnFn& warn);
    /// @brief Read the selector's databases into `db` and each row's columns into `db.clips` (sized to the rows).
    /// @note  A graph without `databases` is one implicit database over every child, unbiased. A row naming a
    ///        database the graph lacks is in none, so never searched.
    void ReadDatabases(const codec::io::SchemaObject& sel, Database& db, const WarnFn& warn);
    /// @brief The databases named `names` (byte-exact), warning of any the graph lacks.
    std::vector<int> DatabasesNamed(const Database& db, const std::vector<std::string>& names, std::string_view where,
                                    const WarnFn& warn);
    /// @brief Read the selector's chooser into `db` (none: every database is searched).
    void ReadChooser(const codec::io::SchemaObject& sel, Database& db, const WarnFn& warn);

    /// @brief What a build covered, for its report.
    struct BuildReport {
        std::size_t                distinctClips = 0;   ///< distinct animationNames the children play
        std::size_t                measuredClips = 0;   ///< of those, the ones measured
        std::size_t                frames        = 0;   ///< rows in the database
        std::array<std::size_t, 4> gaitFrames{};        ///< per Gait: the frames gaitSpeed took its median over
    };

    /// @brief The database of `selector`'s children: each distinct animationName taken once from `clips`, its rows
    ///        appended once per child that plays it, normalized, cut by each child's range, gated by each database's
    ///        candidate gates, and the clips' own gait speeds.
    /// @return null when no clip could be measured.
    /// @pre    The relay roster is registered: expressions compile against it, and one calling a function it lacks
    ///         counts as not compiling (RegisterCoreBuiltins offline: only GraphVar).
    std::unique_ptr<Database> BuildDatabase(const codec::io::SchemaObject& selector, const ClipSource& clips, const WarnFn& warn,
                                            BuildReport* report = nullptr);

    /// @return the frame of `c` nearest `time`, clamped to the clip
    /// @pre    `c.frames > 0`.
    int FrameOf(const Clip& c, double time);

    /// @return `e` evaluated to a float, 0 when it is empty or did not compile
    float ValueOf(const Expression& e, const relay::EvalContext& ctx);
    /// @brief Every database's biases for this pass.
    std::vector<Biases> EvaluateBiases(const Database& db, const relay::EvalContext& ctx);
    /// @brief The chooser's pick, per database: the union of every row whose `when` holds; an empty union is the
    ///        fallback, an empty fallback (or no chooser) every database.
    std::vector<char> Choose(const Database& db, const relay::EvalContext& ctx);
    /// @return whether `database` is in the pick `active` (empty = every database; -1, in none, never is)
    bool IsActive(const std::vector<char>& active, int database);

    /// @brief One search's question.
    struct Query {
        /// @brief Where the character will be and face at the database's horizons (anim::features::kHorizons), and where
        ///        they were and faced that long ago, in the current root space (x right, y forward), game units.
        std::array<float, 3> x{}, y{}, dirX{}, dirY{};
        std::array<float, 3> pastX{}, pastY{}, pastDirX{}, pastDirY{};
        /// @brief The request, game units/s: below kForceMoving it stands, gating on kRowStanding, else on kRowMoving.
        float desiredX = 0, desiredY = 0;
        int    currentClip = -1;   ///< the playing child, -1 = none (no pose to continue or match)
        double currentTime = 0;    ///< seconds into it
        std::vector<char>   active;   ///< per database: searched (Choose); empty = every database
        std::vector<Biases> biases;   ///< per database (EvaluateBiases); empty or short = 0 beyond it
        /// @brief Row ranges no candidate comes from, inclusive.
        /// @pre   Sorted ascending: the search's walk over them is monotonic.
        std::vector<std::pair<int, int>> banned;
        bool clipEnd   = false;   ///< the playing clip is ending: it cannot go on, so the best candidate wins outright
        bool interrupt = false;   ///< likewise: the chooser left the playing database
    };

    /// @brief One search's answer.
    struct SearchResult {
        int         bestRow  = -1;        ///< the best candidate, -1 = none
        float       best     = 1.0e30f;   ///< its biased cost
        float       playing  = 1.0e30f;   ///< the playing row's biased cost, with the continuing bias
        int         playingRow = -1;      ///< -1 without a measured playing clip
        std::size_t searched = 0;         ///< candidates scored past the gates
        bool        wins     = false;     ///< the best candidate replaces what plays
    };

    /// @brief The brute-force search: the query's normalized trajectory plus the playing frame's pose against every
    ///        gated candidate, the cost bound cutting each row short.
    /// @pre   `q.currentClip` is -1 or a child of `db`.
    SearchResult Search(const Database& db, const Query& q);

    /// @brief One row as TopCandidates scored it.
    struct Candidate {
        int   row   = -1;
        int   clip  = -1;
        int   frame = 0;
        float total = 0;   ///< the biased cost
        float bias  = 0;   ///< the part of `total` from the biases
        std::array<float, kGroups> groupCost{};   ///< the feature cost per GroupOf, weighted
    };

    /// @brief The `n` best candidates, best first (ties to the lower row), through the same gates and bans as Search.
    /// @note  Slower than Search, no bound. Its first is Search's bestRow with total == best: a row Search's bound cuts
    ///        short can only differ if its partial sum plus the bias rounds below the best, past float precision.
    std::vector<Candidate> TopCandidates(const Database& db, const Query& q, std::size_t n);

}  // namespace CB::engine::locomotion
