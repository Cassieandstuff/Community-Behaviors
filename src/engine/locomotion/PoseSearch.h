#pragma once

#include <compile/MotionFeatures.h>
#include <compile/MotionMatchingCache.h>
#include <locomotion/DistanceMatch.h>
#include <locomotion/Movement.h>
#include <relay/Relay.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/// @file
/// @brief The motion-matching database and its scoring, game-free: a database built from a CBPoseSearchGenerator and
///        its clips' measurements, the chooser and biases over it, the brute-force nearest-frame search, and the Matcher
///        one character runs them with. The ONE scoring the plugin's node and an editor's match-test panel share.

namespace CB::engine::codec::io { class SchemaObject; }
namespace CB::engine::decompile { struct ParsedHky; }

namespace CB::engine::locomotion {

    /// @brief Frames: a candidate this close in the current clip is the current. 1/3 s at 30 fps, as Holden's 20 at 60.
    inline constexpr int kIgnoreSurrounding = 10;
    /// @brief The least a clip's end no search cuts into (Holden's ignore_range_end); the database widens it to its furthest
    ///        future sample (FutureHorizon).
    inline constexpr double kIgnoreRangeEnd = 1.0 / 3.0;
    /// @brief Game units/s per second: a request changing slower than this has settled, and a jump in it searches then.
    /// @note  Was 5 units/s per update, which at a high frame rate read a stick still ramping as settled.
    inline constexpr float kSettledSpeed = 300.0f;
    inline constexpr float kForceMoving     = 10.0f;   ///< game units/s: a direction counts only above this speed; a request below it stands
    inline constexpr double kSearchInterval = 0.1;     ///< seconds between searches
    /// @brief A jump in the request since the last search searches at once, at most this soon after any search.
    inline constexpr double kForcedSpacing  = 0.05;
    inline constexpr float  kForceSpeed     = 100.0f;      ///< game units/s change of the desired speed
    inline constexpr float  kForceCos       = 0.8660254f;  ///< cos 30 deg: a turn of the desired direction past it
    inline constexpr double kEndMargin      = 0.1;         ///< seconds before a clip's end that force a search
    /// @brief The share of the playing row's cost a candidate must beat it by to replace it.
    /// @note  Without it, query noise alone flips between near-equal frames each search (FluidMotion switches at 12%; UE's
    ///        pose search biases the continuing pose for the same reason).
    inline constexpr float  kSwitchMargin   = 0.12f;
    /// @brief Seconds a pick plays before another may replace it, and the shorter hold after a jump in the request.
    /// @note  A clip's end and the chooser leaving its database replace it at once.
    inline constexpr double kMinDwell = 0.3, kForcedDwell = 0.1;
    /// @brief Seconds after any switch before another of any kind (an interrupt, a clip's end, a forced search) may search:
    ///        a dead blend restarts at every switch, and switches faster than this kept it from ever showing the live pose.
    /// @note  Waived only when the playing clip has run out.
    inline constexpr double kSwitchFloor = 0.1;

    /// @brief The default weights: Holden's demo weights on the samples CB matched on first (1/3, 2/3 and 1 s, ahead and
    ///        back), 0 on every part added since, so a database stating no weights matches as it did.
    /// @note  Applied at search time, never stored in the rows, so a weight change needs no rebuild.
    inline constexpr float kWeightTrajPos = 1.0f, kWeightTrajDir = 1.5f, kWeightFootPos = 0.75f, kWeightFootVel = 1.0f,
                           kWeightHipVel = 1.0f;

    /// @brief The parts a database weighs (CBPoseSearchWeight): per trajectory sample its position, facing, velocity and
    ///        height, then the pose's feet, foot velocity, hip velocity, foot spacing and pelvis facing.
    inline constexpr int kChannels     = 4;
    inline constexpr int kWeightGroups = anim::features::kSamples * kChannels + 5;
    /// @return the weight group of dimension `d`
    int WeightGroupOf(int d);
    /// @return weight group `g`'s name: a channel and its sample's time ("position+0.2", "facing-1"), or a pose part
    ///         ("feet", "feetVelocity", "hipVelocity", "footSpacing", "pelvisFacing")
    std::string WeightGroupName(int g);
    /// @return every dimension's default weight
    std::array<float, anim::features::kDims> DefaultWeights();
    /// @brief `weights` (a weight group's name, its weight) over `out`. A name may also be a channel alone (every
    ///        sample) or a channel with "+" or "-" (every sample ahead or back); broader names apply first, so a
    ///        narrower one wins. Names are byte-exact.
    /// @return the names that name nothing
    std::vector<std::string> ApplyWeights(const std::vector<std::pair<std::string, float>>& weights,
                                          std::array<float, anim::features::kDims>& out);

    /// @brief How many groups Candidate::groupCost reports: path, path facing, path velocity, past, past facing, height,
    ///        foot position, foot velocity, hips.
    inline constexpr int kGroups = 9;
    /// @return the reported group of dimension `d`
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
    /// @brief Seconds either side of a frame its clip speed averages over: a stride's speed, not a step's.
    inline constexpr double kClipSpeedWindow = 0.5;

    /// @brief Each frame's horizontal root speed (game units/s) from `rootPositions` (x, y, z per frame), averaged over
    ///        kClipSpeedWindow either side.
    std::vector<float> WindowedRootSpeeds(const std::vector<float>& rootPositions, int frames, double dt);

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
        float        takeoff    = -1; ///< seconds: when the root first rises kTakeoffRise above its start (ClipTakeoff), -1 = never
        double       loopPeriod = 0;  ///< seconds a looping clip wraps at (its measured period, at most `duration`), 0 = not looping
        /// @brief Its distance curve, for a clip in a Stops database (from its stop) or a Pivots one (from its turn);
        ///        invalid otherwise. The node plays it by distance (DistanceMatch) while the movement model predicts one.
        DistanceCurve distance;
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
        bool        stops  = false;   ///< its tags hold "Stops" (byte-exact): its clips play by distance to the stop
        int         space  = 0;       ///< the row space its candidates are measured and weighed in (Database::spaces)
    };

    /// @brief One way of measuring and weighing a frame: a typed schema (CBPoseSearchSchema), or the classic layout
    ///        (anim::AppendClipFeatures weighed by DefaultWeights) for a database stating none.
    /// @note  Every row of the database has a row in every space a database uses, so the playing frame's pose can be read
    ///        in whichever space the candidate is weighed in.
    struct SchemaSpace {
        bool             classic = true;
        anim::SchemaSpec spec;        ///< a typed schema's
        std::uint64_t    key  = 0;    ///< anim::SchemaKey, typed only
        int              dims = anim::features::kDims;
        std::vector<float> rows;      ///< every row of the database, normalized, `dims` wide; empty when no database uses it
        std::vector<float> mean, scale, weight;   ///< per dim; a candidate's cost is the weighted sum of squared differences
        std::vector<int>   active;    ///< the dims weighted above 0, which alone the search reads
        /// @brief Per dim: the query trajectory sample it is read from and the part of it (x, y, facing x, facing y,
        ///        velocity x, velocity y, height); -1 = the playing frame's own row (the pose).
        std::vector<int>   querySample, queryPart;
        /// @brief Per dim: a second query sample (-1 = none) and how far toward it (0..1) the value lies: a schema sample
        ///        between two query sample times reads them interpolated (QuerySampleAt).
        /// @note  Why: GASP's schemas sample at 0, 0.35 and 0.7 s, the query at -0.05, 1/3 and 2/3 s. Read from the nearest,
        ///        every position was off by the speed times the gap (17 units at 0.7 s of a 518 sprint), which made clips
        ///        about 5% slower than the character the best match: at a steady sprint the accelerating run-to-sprint
        ///        transitions beat the sprint loop, and the search flipped between them every half second.
        std::vector<int>   queryNext;
        std::vector<float> queryBlend;
        std::vector<int>   group;     ///< per dim: what Candidate's costs sum it into (GroupOf classic, the feature typed)
    };

    /// @brief One chooser row (CBPoseSearchChooserRow).
    struct ChooserRow {
        Expression       when;     ///< empty text: always holds; text that does not compile: never holds
        std::vector<int> databases;
    };

    /// @brief The database: one row of kDims floats per child frame, normalized (mean, per-group spread) but unweighted.
    struct Database {
        std::vector<Clip>         clips;
        std::vector<SchemaSpace>  spaces;       ///< [0] is the classic layout
        std::vector<int>          spaceOfSchema;   ///< per DatabaseSpec::schemas entry: its space (0 when it fell back)
        std::vector<int>          clipOf;       ///< row -> clip
        std::vector<char>         searchable;   ///< row's whole future lies inside its clip, and the row inside its child's range
        std::vector<std::uint8_t> rowGates;     ///< the frame's kRow* bits (the search's gates)
        std::vector<std::uint8_t> rowGait;      ///< the frame's Gait from its Speed curve (None standing); the logs' label only
        std::vector<float>        rowSpeed;     ///< the frame's clip root speed, game units/s, averaged over kClipSpeedWindow
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
        std::vector<float>            speed;    ///< per frame, the root's horizontal speed averaged over kClipSpeedWindow
        bool                          looping = false;   ///< the clip loops (anim::FitLoop): its features wrap, its playback too
        std::vector<anim::MmCachedClip::SchemaRows> schemaRows;   ///< the clip under each typed schema it was measured for
        std::vector<anim::AnimCurveDef> curves;   ///< the clip's curves, which its databases' gates read per row
        int                           frames   = 0;
        double                        dt       = 1.0 / 30.0;
        double                        duration = 0;
        float                         takeoff  = -1;   ///< ClipTakeoff's
        DistanceCurve                 stopCurve, pivotCurve;   ///< BuildDistanceCurve from its stop and from its turn
        double                        loopPeriod = 0;  ///< the measured loop's period in seconds, 0 = none
    };

    /// @brief How far (game units) the root must rise above its first frame for the clip to have taken off.
    constexpr float kTakeoffRise = 1.0f;

    /// @brief When a clip leaves the ground: the time of the first frame whose root height is kTakeoffRise above frame 0's.
    /// @return -1 when it never does (or `rootPositions` holds under `frames` x (x, y, z)).
    /// @note  Why: a jump start spends its first beat on the ground (GASP's run starts 0.27-0.37 s, its stand and walk
    ///        starts up to 0.7 s), and the controller launches when the playing clip reaches this (the animation-led
    ///        jump wind-up).
    float ClipTakeoff(const std::vector<float>& rootPositions, int frames, double dt);

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
    /// @brief What a build needs of each clip besides its classic measurement: its rows under these typed schemas.
    struct ClipNeeds {
        const anim::Rig*              rig = nullptr;
        std::vector<anim::SchemaSpec> schemas;
        std::vector<std::uint64_t>    keys;   ///< anim::SchemaKey of each, through `rig`
    };
    /// @brief The clip an animationName (byte-exact) names, measured or from a cache, with rows for every one of `needs`'
    ///        keys it can be measured under (anim::MeasureMmClip).
    /// @warning Throws std::exception when it cannot be had: that child is warned of and never chosen.
    /// @note  The reference need only hold until the next call; each distinct name is asked for once.
    using ClipSource = std::function<const anim::MmCachedClip&(const std::string& animationName, const ClipNeeds& needs)>;

    /// @brief One child of a selector, as a database reads it.
    struct SpecChild {
        std::string  clip;   ///< the clip generator's animationName; empty when the child is no clip (never chosen)
        std::string  database;
        float        rangeStart = 0, rangeEnd = 0;
        std::uint8_t flags = 0;   ///< kFlag* bits
    };
    /// @brief One CBPoseSearchDatabase, its expressions as text.
    struct SpecDatabase {
        std::string name, baseCostBias, continuingPoseCostBias, loopingCostBias, standingCandidate, movingCandidate;
        bool        pivots = false;   ///< its tags hold "Pivots" (byte-exact)
        bool        stops  = false;   ///< its tags hold "Stops" (byte-exact)
        int         schema = -1;      ///< its DatabaseSpec::schemas entry; -1 = none stated, the classic layout
    };
    /// @brief One CBPoseSearchChooserRow.
    struct SpecChooserRow {
        std::string              when;
        std::vector<std::string> databases;
    };
    /// @brief What a database is built from, whoever holds the selector: a parsed unit (SpecOf) or a node loaded from a
    ///        compiled graph, which state the same thing.
    struct DatabaseSpec {
        std::vector<SpecChild>        children;
        std::vector<SpecDatabase>     databases;   ///< empty: one implicit database over every child
        std::vector<anim::SchemaSpec> schemas;     ///< the schemas the databases point at, each once
        bool                        haveChooser = false;
        std::vector<SpecChooserRow> rows;
        std::vector<std::string>    fallback;

        /// @return text equal for two specs exactly when they state the same selector
        [[nodiscard]] std::string Key() const;
    };

    /// @brief The behavior graph's root selector, or null when the graph is not the contract's shape (a
    ///        CBPoseSearchGenerator as its hkbBehaviorGraph's rootGenerator).
    codec::io::SchemaObject* RootSelector(const decompile::ParsedHky& g);
    /// @brief What `selector`, a parsed CBPoseSearchGenerator, states.
    DatabaseSpec SpecOf(const codec::io::SchemaObject& selector);

    /// @brief Each frame's gait from `c`'s Speed curve (None while standing, and for a clip without one).
    /// @pre   `out.dt` is set.
    void AppendRowGaits(const anim::MmCachedClip& c, Measured& out);
    /// @brief Add `c`'s steady forward frames' horizontal root speeds to their gait's list (see Database::gaitSpeed).
    /// @note  A clip without the Speed, MoveDirection and Acceleration curves adds none.
    void CollectGaitSpeeds(const anim::MmCachedClip& c, double dt, const std::vector<std::uint8_t>& gait,
                           std::array<std::vector<float>, 4>& out);
    /// @brief `c` as the database's per-clip measurement.
    /// @note  Built from the cached form on a miss too, so a cache hit and a fresh measurement build the same rows.
    /// @param horizon seconds at a non-looping clip's end that are no candidate (FutureHorizon), at most half the clip and
    ///        at least kIgnoreRangeEnd, so a short clip (a one-second landing) keeps its first half
    std::unique_ptr<Measured> MeasuredFrom(const anim::MmCachedClip& c, std::array<std::vector<float>, 4>& gaitSpeeds,
                                           double horizon = kIgnoreRangeEnd);

    /// @brief Seconds of future the database's rows look ahead: the furthest of the classic layout's samples and every
    ///        schema's trajectory samples, at least kIgnoreRangeEnd.
    /// @note  Why: a sample past a clip's end holds at its last frame, so a frame closer to the end than this has a cut-short
    ///        future that can beat the real loop (late frames of a sprint transition picked, then run out a moment later:
    ///        the switch churn that kept restarting the blend).
    double FutureHorizon(const DatabaseSpec& spec);

    /// @brief `text` compiled as a condition expression; a non-empty one that fails is warned of, naming `what` and
    ///        `fallback` (what it counts as instead).
    Expression CompileExpression(std::string text, std::string_view what, std::string_view fallback, const WarnFn& warn);
    /// @brief Read the selector's databases into `db` and each row's columns into `db.clips` (sized to the rows).
    /// @note  A graph without `databases` is one implicit database over every child, unbiased. A row naming a
    ///        database the graph lacks is in none, so never searched.
    void ReadDatabases(const DatabaseSpec& spec, Database& db, const WarnFn& warn);
    /// @brief The databases named `names` (byte-exact), warning of any the graph lacks.
    std::vector<int> DatabasesNamed(const Database& db, const std::vector<std::string>& names, std::string_view where,
                                    const WarnFn& warn);
    /// @brief Read the selector's chooser into `db` (none: every database is searched).
    void ReadChooser(const DatabaseSpec& spec, Database& db, const WarnFn& warn);

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
    /// @param rig what typed schemas are measured through; null: a database pointing at one is warned of and weighed in
    ///        the classic layout instead
    std::unique_ptr<Database> BuildDatabase(const DatabaseSpec& spec, const anim::Rig* rig, const ClipSource& clips, const WarnFn& warn,
                                            BuildReport* report = nullptr);
    /// @brief BuildDatabase over a parsed selector.
    std::unique_ptr<Database> BuildDatabase(const codec::io::SchemaObject& selector, const anim::Rig* rig, const ClipSource& clips,
                                            const WarnFn& warn, BuildReport* report = nullptr);
    /// @brief A parsed skeleton unit's animation skeleton (its animation container's first) as a rig: bone names, parents,
    ///        reference pose; nothing when it holds none.
    /// @note  The same skeleton a character loads, so a database built from it keys as the node's built from the live one.
    std::optional<anim::Rig> RigFromSkeleton(const decompile::ParsedHky& skeleton);
    /// @brief The schema a parsed CBPoseSearchSchema states, its features read by class (names byte-exact).
    anim::SchemaSpec SchemaSpecOf(const codec::io::SchemaObject& schema, const WarnFn& warn = {});

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

    /// @brief One trajectory sample of a question, in the current root space (x right, y forward), game units.
    struct TrajectorySample {
        float x = 0, y = 0;         ///< where the character is
        float dirX = 0, dirY = 1;   ///< and faces, a unit vector
        float vx = 0, vy = 0;       ///< its velocity, game units/s
        float z = 0;                ///< its height
    };
    /// @brief At anim::features::kAhead's times, then kBack's.
    using QueryTrajectory = std::array<TrajectorySample, anim::features::kSamples>;

    /// @brief The query samples a trajectory sample `time` seconds from now is read from: `lo`, and `hi` (-1 = none) a share
    ///        `blend` of the way, the query samples bracketing the time (anim::features::SampleTime's), the nearest end past
    ///        them. Time 0 lies between the nearest back and ahead samples.
    struct QuerySampleAt {
        int   lo = 0, hi = -1;
        float blend = 0;
    };
    QuerySampleAt QuerySampleOf(double time);

    /// @brief One search's question.
    struct Query {
        QueryTrajectory samples{};
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
        bool  hold   = false;           ///< the playing pick is within its dwell: no candidate replaces it (bar clipEnd, interrupt)
        float margin = kSwitchMargin;   ///< see kSwitchMargin
        /// @brief Per space, raw query values given in place of what the space would read (the trajectory sample, the
        ///        playing row): its `dims` long, NaN where nothing is given; empty = nothing for that space.
        /// @note  A feature's bound query (a CBPoseSearch* binding) arrives here.
        std::vector<std::vector<float>> given;
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
        std::array<float, kGroups> groupCost{};   ///< a classic candidate's cost per GroupOf, weighted
        std::vector<float>         featureCost;   ///< a typed candidate's cost per feature of its schema, weighted
    };

    /// @brief The `n` best candidates, best first (ties to the lower row), through the same gates and bans as Search.
    /// @note  Slower than Search, no bound. Its first is Search's bestRow with total == best: a row Search's bound cuts
    ///        short can only differ if its partial sum plus the bias rounds below the best, past float precision.
    std::vector<Candidate> TopCandidates(const Database& db, const Query& q, std::size_t n);

    /// @brief Row `row` scored against `q` as TopCandidates scores a candidate, whatever the gates and bans; the playing row
    ///        (q's current frame) also carries its database's continuing bias, as Search's `playing` does.
    /// @pre   `row` is a row of `db`.
    Candidate ScoreRow(const Database& db, const Query& q, int row);

    /// @brief What one character's graph asks of its Matcher this update.
    struct MatchRequest {
        /// @brief As Query's. @note Without one no search runs; what plays goes on.
        QueryTrajectory      samples{};
        float                desiredX = 0, desiredY = 0;
        bool                 haveQuery = false;
        float                rate = 1;              ///< clip seconds per second
        std::vector<char>    active;                ///< as Query's
        std::vector<Biases>  biases;                ///< as Query's
        /// @brief An input whose change may interrupt changed since the last update: with the playing database out of the
        ///        pick, the next search runs at once and the playing row cannot go on (GASP's InterruptOnDatabaseChange).
        bool                 inputsChanged = false;
        float                reselectTime  = 1;     ///< seconds a pick's neighbourhood stays banned
        std::vector<std::vector<float>> given;      ///< as Query's
    };

    /// @brief What one update did.
    struct MatchStep {
        int          clip = -1;   ///< the child playing now, -1 = none yet
        double       time = 0;    ///< seconds into it
        int          nextClip = -1;   ///< the child the graph shows next frame
        /// @brief Seconds into it, as the graph shows it next frame.
        /// @note  The graph updates children before their parent, so what this update writes shows one frame later. Writing
        ///        `time` plus this update's timestep stepped by 2*dt(N) - dt(N-1) a frame (backwards after a hitch); `time`
        ///        itself lags a steady frame. A switch shows its pick as is, and its next update plays on from there.
        double       nextTime = 0;
        bool         searched = false;
        bool         switched = false;   ///< the search chose a new pick: `nextClip` and `nextTime` are it
        const char*  trigger  = "";      ///< why it searched: "interrupt", "clip end", "forced" or "timer"
        SearchResult result;             ///< its answer, when `searched`
        float        searchMs = 0;
        std::vector<char> searchedActive;   ///< the pick it searched
        /// @brief The question it searched with, when `searched`: its bans, dwell and margin included, so a debugger can
        ///        score any row against what the search saw (ScoreRow, TopCandidates).
        Query        query;
    };

    /// @brief The playback rate's range: a clip plays at most this much slower or faster than authored; past it the feet
    ///        slide rather than the animation visibly slowing or fast-forwarding.
    /// @note  Narrow (FluidMotion's 0.85-1.2): the search picks the clip whose speed fits, the rate only trims what is left;
    ///        a wide range let a wrong clip play on at a skating rate.
    inline constexpr float kMinRate = 0.85f, kMaxRate = 1.2f;
    inline constexpr float kRateClipStill = 25.0f;   ///< game units/s: a playing frame slower than this plays at rate 1
    inline constexpr float kRateHalflife  = 0.15f;   ///< seconds for the rate to close half its gap to the target
    inline constexpr float kRateStill     = 10.0f;   ///< game units/s asked for below which the rate goes back to 1

    /// @brief A node's own playback rate while its speed is bound: `speed` over the playing frame's clip speed, held to
    ///        [kMinRate, kMaxRate] and eased toward it (`rate`, `rateVelocity` carried frame to frame).
    float NodeRate(const Database& db, int playingClip, double playingTime, float speed, float& rate, float& rateVelocity, float dt);

    /// @brief The movement model's distances distance matching plays by.
    struct Distances {
        float toStop  = -1.0f;   ///< game units along the path to where it comes to rest if the stick lets go, -1 = not moving
        float toPivot = -1.0f;   ///< game units along the path to where it turns round, -1 = not pivoting
        float speed   = 0.0f;    ///< game units/s, horizontal: what a pivot past its turn advances by
    };
    /// @brief A pivot's play rate past its turn, advancing by the distance moved (Lyra's start and pivot clamps).
    inline constexpr double kDistanceMinRate = 0.6, kDistanceMaxRate = 5.0;

    /// @brief The time distance matching plays clip `clip` at instead of the clock's: a clip of a Stops database at the
    ///        frame whose distance to its stop is d.toStop, of a Pivots one at its turn's (d.toPivot), past the turn by
    ///        the distance moved.
    /// @return nothing to play by the clock: not a distance clip, the prediction ran out or falls outside its curve, or it
    ///         would play backwards (before `wasTime`) or past its end
    /// @param wasTime the time `clip` played at last update; only a clip that went on playing (no switch) is matched
    std::optional<double> DistanceMatchedTime(const Database& db, int clip, double wasTime, const Distances& d, float dt);

    /// @brief One character's motion matching, game-free: what plays, when a search is due, and the reselect ban.
    /// @note  Time is the timestep it is given, so a paused graph neither plays nor searches on a clock of its own.
    class Matcher {
    public:
        /// @brief Play on by `dt`, then search if one is due.
        /// @pre   `in.active` and `in.biases` are `db`'s, and every update of this Matcher takes the same `db`.
        MatchStep Update(const Database& db, const MatchRequest& in, float dt);
        /// @brief Forget what plays: the next update with a query searches at once, with no pose to continue.
        void Restart();
        /// @brief Play the current clip from `time` instead of the clock's (distance matching); ignored while a switch is
        ///        pending, since the pick shows first.
        void SetTime(double time)
        {
            if (m_clip >= 0 && m_pendingClip < 0) m_time = time;
        }

    private:
        /// @brief A pick and when: the ban follows where it would have played to.
        struct Pick {
            int    clip  = 0;
            double frame = 0;
            double at    = 0;
        };
        int               m_clip = -1;
        double            m_time = 0;
        int               m_pendingClip = -1;
        double            m_pendingTime = 0;
        bool              m_searchNow = true;
        bool              m_interrupt = false;
        std::vector<char> m_interruptActive;   ///< the pick the interrupt saw, which its search uses
        double            m_clock = 0;
        double            m_lastSearch = -1.0e9;
        float             m_lastWantX = 0, m_lastWantY = 0;
        float             m_prevWantX = 0, m_prevWantY = 0;   ///< the request one update ago
        double            m_lastSwitch = -1.0e9;   ///< m_clock at the last pick
        std::vector<Pick> m_picks;
    };

}  // namespace CB::engine::locomotion
