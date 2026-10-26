#include "PCH.h"

#include "CharDebugHud.h"
#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharMovementModel.h"
#include "CharPoseSearch.h"
#include "CharSkeletonWatch.h"

#include <core/debug/Settings.h>
#include <core/resolve/LoadOrderReader.h>
#include <platform/relay/Relay.h>
#include <codec/format/AnimationYamlLoader.h>
#include <compile/MotionFeatures.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>
#include <platform/profile/Profile.h>

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/// @file
/// @brief The player's motion matching (stock-node prototype): a database of every add-on clip's per-frame features,
///        built off-thread once the load order is read; a chooser picking which of the graph's databases are searched;
///        a brute-force nearest-frame search over those; and the winner played through the add-on's graph.
/// @note  THE ADD-ON CONTRACT (Motion Matching.hky, shipped apart from CB): the behavior `kGraphKey` whose root is a
///        CBPoseSearchGenerator over clip generators (MM_Clip = the child index; a clip listed in several databases is
///        one child per database), its `databases` contiguous child ranges with Unreal's cost biases (condition
///        expressions), `childFlags` per child and `chooser` rows each holding a `when` condition expression; the
///        selected child bound to `kClipVar` and every child's
///        user-controlled time to `kTimeVar`; `kEnableVar`, declared in 0_master and mt_behavior, switches
///        mt_behavior's LocomotionDefault to that graph; and the chooser's inputs (kInputNames) declared on it, which CB
///        writes each frame. Without the add-on the graph is not served, nothing is built, and the controller runs
///        exactly as before.

namespace CB::feature {

    namespace {

        namespace hooks = platform::hooks;
        namespace ch    = CB::feature::controller;
        namespace relay = CB::platform::relay;
        namespace mv    = CB::feature::controller::move;
        namespace game  = CB::platform::game;
        namespace ea    = CB::engine::anim;
        namespace F     = CB::engine::anim::features;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;
        using Clock     = std::chrono::steady_clock;
        using CB::engine::codec::io::SchemaObject;

        constexpr std::string_view kGraphKey   = "meshes/actors/character/behaviors/motionmatching.hkx";
        constexpr std::string_view kProjectDir = "meshes/actors/character/";   ///< a clip's animationName is below this
        constexpr const char*      kEnableVar  = "MM_Enable";
        constexpr const char*      kClipVar    = "MM_Clip";
        constexpr const char*      kTimeVar    = "MM_Time";

        constexpr double kSearchInterval    = 0.1;   ///< seconds between searches
        /// @brief A jump in the request since the last search searches at once, at most this soon after any search.
        constexpr double kForcedSpacing     = 0.05;
        constexpr float  kForceSpeed        = 100.0f;      ///< game units/s change of the desired speed
        constexpr float  kForceCos          = 0.8660254f;  ///< cos 30 deg: a turn of the desired direction past it
        constexpr float  kForceMoving       = 10.0f;       ///< game units/s: a direction counts only above this speed

        /// @brief fMMReselectTime (settings.ini [Character], read once): seconds a pick's neighbourhood stays banned.
        /// @note  Why: 12% of switches once went A -> B -> A within a second. A forced search keeps the ban, so input
        ///        noise cannot flip.
        float ReselectTime()
        {
            static const float t = std::clamp(ch::SettingFloat("fMMReselectTime", 1.0f), 0.0f, 5.0f);
            return t;
        }
        /// @brief iMMDatabaseLOD (settings.ini [Character], read once): the MM_DatabaseLOD the chooser sees.
        int DatabaseLod()
        {
            static const int lod = static_cast<int>(std::lround(ch::SettingFloat("iMMDatabaseLOD", 0.0f)));
            return lod;
        }

        constexpr int    kIgnoreSurrounding = 20;    ///< frames: a candidate this close in the current clip is the current
        constexpr double kEndMargin         = 0.1;   ///< seconds before a clip's end that force a search

        /// @brief Each feature group's weight (Holden's demo defaults).
        /// @note  Applied at search time, never stored in the database, so a weight change needs no rebuild.
        /// @note  The past groups start at the future's weights.
        constexpr float kWeightTrajPos = 1.0f, kWeightTrajDir = 1.5f, kWeightPastPos = 1.0f, kWeightPastDir = 1.5f,
                        kWeightFootPos = 0.75f, kWeightFootVel = 1.0f, kWeightHipVel = 1.0f;
        constexpr int   kGroups = 7;

        /// @brief The playback rate's range: a clip plays at most this much slower or faster than authored; past it the
        ///        feet slide rather than the animation visibly slowing or fast-forwarding.
        constexpr float kMinRate = 0.7f, kMaxRate = 1.5f;
        /// @brief Below this asked-for speed (game units/s) the gait's rate stays as it was: standing has no speed to match.
        constexpr float kRateStill = 10.0f;

        /// @brief The chooser's inputs, GASP's (its AnimBP's chooser columns), as the MotionMatching graph declares them.
        /// @note  Values: MovementMode 0 OnGround / 1 InAir; RotationMode 0 OrientToMovement / 1 Strafe / 2 Aim (never
        ///        set: GASP's CMC character); MovementState 0 Idle / 1 Moving; Gait 0 Walk / 1 Run / 2 Sprint; Stance
        ///        0 Stand / 1 Crouch; kIsStarting..kJustTraversed are BOOL, the rest INT32.
        enum Input : int {
            kMovementMode,
            kRotationMode,
            kMovementState,
            kGait,
            kStance,
            kIsStarting,
            kIsPivoting,
            kJustLandedLight,
            kJustLandedHeavy,
            kShouldTurnInPlace,
            kShouldSpinTransition,
            kJustTraversed,
            kDatabaseLOD,
            kInputs
        };
        /// @brief The graph variables' names, byte-exact (the generator that writes the add-on declares these).
        constexpr std::array<const char*, kInputs> kInputNames{ "MM_MovementMode",      "MM_RotationMode",      "MM_MovementState",
                                                                "MM_Gait",              "MM_Stance",            "MM_IsStarting",
                                                                "MM_IsPivoting",        "MM_JustLandedLight",   "MM_JustLandedHeavy",
                                                                "MM_ShouldTurnInPlace", "MM_ShouldSpinTransition", "MM_JustTraversed",
                                                                "MM_DatabaseLOD" };
        constexpr bool IsBoolInput(int i) { return i >= kIsStarting && i <= kJustTraversed; }

        /// @brief The REAL chooser inputs. Speed2D: the horizontal speed, game units/s (GASP's is cm/s; an importer scales
        ///        its bounds by 0.7). TimeToLand: seconds until the predicted path lands (GASP's
        ///        TrajectoryCollision.TimeToLand), 0 on the ground, kNoLanding when no landing is within the horizon.
        enum RealInput : int { kSpeed2D, kTimeToLand, kRealInputs };
        constexpr std::array<const char*, kRealInputs> kRealInputNames{ "MM_Speed2D", "MM_TimeToLand" };

        /// @brief One frame's chooser inputs: the INT32 / BOOL ones by Input (`in[kGait]`), the REAL ones in `real`.
        struct Inputs {
            std::array<int, kInputs>       word{};
            std::array<float, kRealInputs> real{};
            constexpr int&       operator[](int i) { return word[i]; }
            constexpr const int& operator[](int i) const { return word[i]; }
            bool                 operator==(const Inputs&) const = default;
        };
        /// @brief Before the first frame: on the ground, orienting, idle, standing, Run (GASP's starting gait), LOD 0.
        /// @note  Settings-free: it initializes a global, before settings.ini is read.
        constexpr Inputs InitialInputs()
        {
            Inputs in{};
            in[kGait] = 1;
            return in;
        }

        /// @brief GASP's thresholds (its AnimBP's), centimetres at 0.7 game units each.
        constexpr float kMovingEpsilon   = 1.0f;          ///< game units/s: IsMoving's "velocity != 0"
        constexpr float kStartGain       = 70.0f;         ///< IsStarting: the future speed exceeds the speed by 100 cm/s
        constexpr float kPivotOrient     = 0.78539816f;   ///< IsPivoting: 45 deg orienting to movement ...
        constexpr float kPivotStrafe     = 0.52359878f;   ///< ... 30 deg strafing
        constexpr float kTurnInPlace     = 0.87266463f;   ///< ShouldTurnInPlace: 50 deg
        constexpr float kSpinAngle       = 2.2689280f;    ///< ShouldSpinTransition: 130 deg ...
        constexpr float kSpinSpeed       = 105.0f;        ///< ... at 150 cm/s or more
        constexpr float kLandHold        = 0.2f;          ///< seconds JustLanded* stays true after touching down
        /// @brief JustLandedHeavy's vertical speed (700 cm/s). @warning Assumed: GASP's HeavyLandSpeedThreshold was not
        ///        exported with the sample's assets.
        constexpr float kHeavyLandSpeed  = 490.0f;
        /// @brief GASP's future velocity spans the trajectory's 0.4 -> 0.5 s; the model is stepped at kFutureStep.
        constexpr float kFutureFrom = 0.4f, kFutureTo = 0.5f, kFutureStep = 1.0f / 60.0f;

        constexpr std::uint8_t kFlagDisableReselection = 1, kFlagLooping = 2;   ///< CBPoseSearchGenerator childFlags bits

        /// @brief One child of the add-on's selector (MM_Clip indexes these).
        struct Clip {
            std::string  name;            ///< the clip generator's animationName
            int          first    = 0;    ///< its first row
            int          frames   = 0;    ///< 0 = the clip could not be measured (its index stays, never chosen)
            double       dt       = 1.0 / 30.0;
            double       duration = 0;    ///< what the user-controlled time fraction spans
            int          database = -1;   ///< its PoseDatabase, -1 = in none (never searched)
            std::uint8_t flags    = 0;    ///< kFlag* bits
        };

        /// @brief A condition expression read from the graph, compiled once; `text` kept for the logs.
        struct Expression {
            std::string        text;
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
            /// @brief The biases' expressions, evaluated each frame by the frame hook (an invalid one is 0).
            Expression  base, continuing, looping;
            /// @brief The candidate gates over each row's curves, evaluated once per row at build (empty: every row; one
            ///        that does not compile: no row).
            Expression  standing, moving;
            bool        pivots     = false;   ///< its tags hold "Pivots" (byte-exact): IsStarting and spins stay false
        };

        /// @brief One chooser row (CBPoseSearchChooserRow).
        struct ChooserRow {
            Expression       when;     ///< empty text: always holds; text that does not compile: never holds
            std::vector<int> databases;
        };

        /// @brief The database: one row of kDims floats per child frame, normalized (mean, per-group spread) but unweighted.
        struct Database {
            std::vector<Clip>               clips;
            std::vector<float>              rows;
            std::vector<int>                clipOf;       ///< row -> clip
            std::vector<char>               searchable;   ///< row's whole future lies inside its clip
            std::vector<std::uint8_t>       rowGates;     ///< the frame's kRow* bits (the search's gates)
            std::vector<std::uint8_t>       rowGait;      ///< the frame's ch::Gait from its Speed curve (None standing)
            std::array<float, F::kDims>     mean{}, scale{}, weight{};
            /// @brief The clips' own speed per gait (game units/s, indexed by ch::Gait), 0 where no clip has one: the
            ///        median root speed of the steady forward frames in that gait's Speed band (CollectGaitSpeeds).
            std::array<float, 4>            gaitSpeed{};
            std::vector<PoseDatabase>       databases;
            /// @brief Empty without a chooser: every database is searched.
            std::vector<ChooserRow>         chooser;
            std::vector<int>                fallback;     ///< none of the rows holds: these; empty = every database
            bool                            haveChooser = false;
        };
        /// @note Published once, never freed (process lifetime): the search and the frame hook read it lock-free.
        std::atomic<const Database*> g_db{ nullptr };

        /// @brief What plays: a clip and the time in it.
        struct Playback {
            int    clip = -1;
            double time = 0;
        };
        struct Shared {
            std::mutex        lock;
            Playback          now;
            Playback          pending;
            bool              havePending = false;
            bool              searchNow   = true;
            /// @brief GASP's InterruptOnDatabaseChange fired: the next search runs at once and the playing row cannot go on.
            bool              interrupt   = false;
            /// @brief Per database: searched (the chooser's latest pick; empty until the first frame = every database).
            std::vector<char> active;
            /// @brief Per database: its biases as of the latest frame (empty until the first frame = every bias 0).
            std::vector<Biases> biases;
            std::vector<char> interruptActive;   ///< the pick the interrupt saw, which its search uses
            Inputs            inputs = InitialInputs();
        };
        Shared             g_shared;
        /// @brief The playback rate (clip seconds per second): written by the predictor's Rate, read by the frame hook.
        std::atomic<float> g_rate{ 1.0f };
        /// @brief The predictor's landing time (SetTimeToLand), read by the frame hook's MM_TimeToLand while airborne.
        std::atomic<float> g_timeToLand{ controller::motion::kNoLanding };
        /// @brief When the last switch was chosen (steady_clock ticks; 0 = none yet), for Describe.
        std::atomic<Clock::rep> g_lastSwitch{ 0 };
        std::atomic<int>   g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 400; }

        /// @brief The searches' biased costs since the last report: the best candidate's and the playing row's.
        struct CostLog {
            std::mutex         lock;
            std::vector<float> winning, playing;
        };
        CostLog g_costs;
        constexpr std::size_t kCostLogCap = 4096;

        int GroupOf(int d)
        {
            if (d < F::kTrajDir) return 0;
            if (d < F::kPastPos) return 1;
            if (d < F::kPastDir) return 2;
            if (d < F::kFootPos) return 3;
            if (d < F::kFootVel) return 4;
            if (d < F::kHipVel) return 5;
            return 6;
        }

        /// @brief The add-on graph's root selector, or null when the graph is not the contract's shape.
        SchemaObject* RootSelector(const CB::engine::decompile::ParsedHky& g)
        {
            if (!g.root) return nullptr;
            for (const auto& nv : g.root->Children("namedVariants")) {
                const auto* v     = dynamic_cast<const SchemaObject*>(nv.get());
                auto*       graph = v ? v->Child("variant") : nullptr;
                if (!graph || std::string_view(graph->ClassName()) != "hkbBehaviorGraph") continue;
                auto* gen = graph->Child("rootGenerator");
                return gen && std::string_view(gen->ClassName()) == "CBPoseSearchGenerator" ? gen : nullptr;
            }
            return nullptr;
        }

        /// @brief The per-row bits the search gates on: its database's candidate gates, evaluated at the row's frame.
        constexpr std::uint8_t kRowStanding = 1, kRowMoving = 2;

        /// @brief The speed bands a frame's Speed curve falls in (game units/s): standing below kStandSpeed, then Walk,
        ///        Run from kWalkRun, Sprint from kRunSprint. Midway between the clips' measured plateaus (walk 92, run 284,
        ///        sprint 426), as hky-tool's tagger set them.
        constexpr float kStandSpeed = 15.0f, kWalkRun = 188.0f, kRunSprint = 355.0f;
        /// @brief A steady forward frame (the clips' own gait speeds): travel within kForwardDeg of facing, speed changing
        ///        by less than kSteadyAccel (game units/s²).
        constexpr float kForwardDeg = 45.0f, kSteadyAccel = 100.0f;

        /// @brief The motion curves' names, byte-exact, as hky-tool's motionCurves generator writes them.
        constexpr const char* kSpeedCurve = "Speed";
        constexpr const char* kDirectionCurve = "MoveDirection";
        constexpr const char* kAccelCurve = "Acceleration";

        /// @brief One distinct clip's measurement (raw, unnormalized features), shared by every child that plays it.
        struct Measured {
            std::vector<float>        rows;
            std::vector<char>         searchable;
            std::vector<std::uint8_t> gait;     ///< per frame, a ch::Gait from the Speed curve's band
            std::vector<ea::AnimCurveDef> curves;   ///< the clip's curves, which its databases' gates read per row
            int                       frames   = 0;
            double                    dt       = 1.0 / 30.0;
            double                    duration = 0;
        };

        /// @return the curve named `name` (byte-exact) in `curves`, or null
        const ea::AnimCurveDef* CurveNamed(const std::vector<ea::AnimCurveDef>& curves, std::string_view name)
        {
            const auto it = std::find_if(curves.begin(), curves.end(), [&](const ea::AnimCurveDef& c) { return c.name == name; });
            return it == curves.end() ? nullptr : &*it;
        }

        /// @brief A clip's curves at one frame, as the variables a candidate gate reads (GraphVar("Speed")).
        /// @note  A curve the clip lacks is not this source's: GraphVar then reads 0 (the build has no actor).
        class CurveVariables final : public relay::VariableSource {
        public:
            explicit CurveVariables(const std::vector<ea::AnimCurveDef>& curves) : m_curves(curves) {}
            float time = 0;   ///< seconds into the clip
            bool  Read(const char* name, relay::Value& out) const override
            {
                const ea::AnimCurveDef* c = CurveNamed(m_curves, name);
                if (!c) return false;
                out = relay::Value::Float(ea::SampleCurve(*c, time));
                return true;
            }

        private:
            const std::vector<ea::AnimCurveDef>& m_curves;
        };

        /// @brief Each frame's gait from `def`'s Speed curve (None while standing, and for a clip without one).
        void AppendRowGaits(const ea::ClipMotion& m, const ea::AnimationDef& def, Measured& out)
        {
            const ea::AnimCurveDef* speed = CurveNamed(def.curves, kSpeedCurve);
            for (int i = 0; i < m.frames; i++) {
                const float s = speed ? ea::SampleCurve(*speed, static_cast<float>(i * m.dt)) : 0.0f;
                const ch::Gait gait = s >= kRunSprint ? ch::Gait::Sprint
                                    : s >= kWalkRun   ? ch::Gait::Run
                                    : s >= kStandSpeed ? ch::Gait::Walk
                                                       : ch::Gait::None;
                out.gait.push_back(static_cast<std::uint8_t>(gait));
            }
        }

        /// @brief Add `m`'s steady forward frames' horizontal root speeds to their gait's list (see Database::gaitSpeed).
        /// @note  A clip without the Speed, MoveDirection and Acceleration curves adds none.
        void CollectGaitSpeeds(const ea::ClipMotion& m, const std::vector<ea::AnimCurveDef>& curves, const std::vector<std::uint8_t>& gait,
                               std::array<std::vector<float>, 4>& out)
        {
            const ea::AnimCurveDef* direction = CurveNamed(curves, kDirectionCurve);
            const ea::AnimCurveDef* accel     = CurveNamed(curves, kAccelCurve);
            if (!direction || !accel || gait.size() < static_cast<std::size_t>(m.frames)) return;
            for (int i = 0; i + 1 < m.frames; ++i) {
                const float t = static_cast<float>(i * m.dt);
                if (gait[i] == static_cast<std::uint8_t>(ch::Gait::None) || std::fabs(ea::SampleCurve(*direction, t)) >= kForwardDeg ||
                    std::fabs(ea::SampleCurve(*accel, t)) >= kSteadyAccel)
                    continue;
                const double dx = m.rootPos[i + 1].x - m.rootPos[i].x, dy = m.rootPos[i + 1].y - m.rootPos[i].y;
                out[gait[i]].push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy) / m.dt));
            }
        }

        /// @brief Measure the clip `name` (an animationName). @warning Throws when it cannot be measured.
        std::unique_ptr<Measured> Measure(const core::resolve::LoadOrderReader& reader, const std::string& name,
                                          std::array<std::vector<float>, 4>& gaitSpeeds)
        {
            std::string path = std::string(kProjectDir) + name;
            std::replace(path.begin(), path.end(), '\\', '/');
            const auto text = reader.nativeAnimation(path);
            if (!text) throw std::runtime_error("not shipped by any bundle");
            const auto  def = ea::AnimationYamlLoader::LoadFromString(*text, path);
            std::string err;
            const auto  m = ea::MeasureClip(def, err);
            if (!m) throw std::runtime_error(err);
            if (!m->hasFeet || !m->hasHip) throw std::runtime_error("no humanoid leg chains");
            auto out = std::make_unique<Measured>();
            ea::AppendClipFeatures(*m, out->rows);
            out->frames       = m->frames;
            out->dt           = m->dt;
            out->duration     = def.duration > 0 ? def.duration : (m->frames - 1) * m->dt;
            const int horizon = static_cast<int>(std::lround(F::kHorizons.back() / m->dt));
            for (int i = 0; i < m->frames; ++i) out->searchable.push_back(i + horizon <= m->frames - 1);
            AppendRowGaits(*m, def, *out);
            CollectGaitSpeeds(*m, def.curves, out->gait, gaitSpeeds);
            out->curves = def.curves;
            return out;
        }

        /// @return string field `name` of `o`, "" when it holds none (an empty string may be left out of a unit).
        std::string StrOr(const SchemaObject& o, std::string_view name)
        {
            const auto* f = o.FindField(name);
            return f ? f->str : std::string();
        }

        /// @brief `text` compiled as a condition expression; a non-empty one that fails is logged, naming `what` and
        ///        `fallback` (what it counts as instead).
        Expression CompileExpression(std::string text, std::string_view what, std::string_view fallback)
        {
            Expression e{ std::move(text), {} };
            if (e.text.empty()) return e;
            std::string err;
            e.expr = relay::Compile(e.text, err);
            if (!e.expr.Valid()) LOG_WARN("motion matching: {} '{}' does not compile ({}) - {}", what, e.text, err, fallback);
            return e;
        }

        /// @brief Read the selector's databases into `db` and each child's into `clips` (sized to the children).
        /// @note  A graph without `databases` (bundles from before them) is one implicit database over every child,
        ///        unbiased. A child two ranges claim stays in the first.
        void ReadDatabases(const SchemaObject& sel, Database& db)
        {
            const int   count = static_cast<int>(db.clips.size());
            const auto* field = sel.HasField("databases") ? sel.FindField("databases") : nullptr;
            if (!field || field->objs.empty()) {
                db.databases.push_back({ "(every child)" });
                for (Clip& c : db.clips) c.database = 0;
            } else {
                for (const auto& o : field->objs) {
                    const auto* d = dynamic_cast<const SchemaObject*>(o.get());
                    if (!d) continue;
                    PoseDatabase p;
                    p.name       = d->Str("name");
                    p.base       = CompileExpression(StrOr(*d, "baseCostBias"), "database '" + p.name + "' baseCostBias", "0");
                    p.continuing = CompileExpression(StrOr(*d, "continuingPoseCostBias"), "database '" + p.name + "' continuingPoseCostBias", "0");
                    p.looping    = CompileExpression(StrOr(*d, "loopingCostBias"), "database '" + p.name + "' loopingCostBias", "0");
                    p.standing   = CompileExpression(StrOr(*d, "standingCandidate"), "database '" + p.name + "' standingCandidate",
                                                     "no row is a standing candidate");
                    p.moving     = CompileExpression(StrOr(*d, "movingCandidate"), "database '" + p.name + "' movingCandidate",
                                                     "no row is a moving candidate");
                    if (const auto* tags = d->FindField("tags"))
                        p.pivots = std::find(tags->strs.begin(), tags->strs.end(), "Pivots") != tags->strs.end();
                    const auto first = d->GetInt("firstChild"), n = d->GetInt("numChildren");
                    if (first < 0 || n < 0 || first + n > count)
                        LOG_WARN("motion matching: database '{}' spans children {}..{} of {} - clamped", p.name, first, first + n - 1, count);
                    const int index = static_cast<int>(db.databases.size());
                    int       overlap = 0;
                    for (auto i = (std::max)(first, std::int64_t{ 0 }); i < (std::min)(first + n, std::int64_t{ count }); ++i) {
                        if (db.clips[i].database >= 0) ++overlap;
                        else db.clips[i].database = index;
                    }
                    if (overlap)
                        LOG_WARN("motion matching: database '{}' claims {} child(ren) an earlier database holds - left there", p.name, overlap);
                    db.databases.push_back(std::move(p));
                }
            }
            if (!sel.HasField("childFlags")) return;
            std::vector<std::uint8_t> flags;
            try {
                flags = sel.GetArray<std::uint8_t>("childFlags");
            } catch (const std::exception& e) {
                LOG_WARN("motion matching: childFlags unreadable ({}) - every child unflagged", e.what());
            }
            if (!flags.empty() && flags.size() != db.clips.size())
                LOG_WARN("motion matching: {} childFlags for {} children - the rest unflagged", flags.size(), db.clips.size());
            for (std::size_t i = 0; i < flags.size() && i < db.clips.size(); ++i) db.clips[i].flags = flags[i];
        }

        /// @brief The databases named `names` (byte-exact), warning of any the graph lacks.
        std::vector<int> DatabasesNamed(const Database& db, const std::vector<std::string>& names, std::string_view where)
        {
            std::vector<int> out;
            for (const auto& name : names) {
                const auto it = std::find_if(db.databases.begin(), db.databases.end(), [&](const PoseDatabase& d) { return d.name == name; });
                if (it == db.databases.end()) LOG_WARN("motion matching: the chooser's {} names database '{}', which the graph lacks - ignored", where, name);
                else out.push_back(static_cast<int>(it - db.databases.begin()));
            }
            return out;
        }

        /// @brief Read the selector's chooser into `db` (none: every database is searched).
        void ReadChooser(const SchemaObject& sel, Database& db)
        {
            const SchemaObject* chooser = sel.HasField("chooser") ? sel.Child("chooser") : nullptr;
            if (!chooser) return;
            db.haveChooser = true;
            if (const auto* rows = chooser->FindField("rows"))
                for (const auto& o : rows->objs) {
                    const auto* r = dynamic_cast<const SchemaObject*>(o.get());
                    if (!r) continue;
                    ChooserRow row;
                    row.when = CompileExpression(StrOr(*r, "when"), "chooser row " + std::to_string(db.chooser.size()) + " `when`",
                                                 "the row never holds");
                    if (const auto* names = r->FindField("databases")) row.databases = DatabasesNamed(db, names->strs, "row");
                    db.chooser.push_back(std::move(row));
                }
            if (const auto* fallback = chooser->FindField("fallback")) db.fallback = DatabasesNamed(db, fallback->strs, "fallback");
        }

        /// @brief Measure every clip the add-on graph plays and publish the database.
        /// @note Runs on its own below-normal thread: the add-on's authored YAML takes seconds to parse. Each distinct
        ///       animationName is measured once (a clip may be a child of several databases) and its rows are appended
        ///       once per child that plays it.
        void Build(const core::resolve::LoadOrderReader& reader)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto t0 = Clock::now();
            CB::engine::decompile::ParsedHky graph;
            if (!reader.mergedGraph(std::string(kGraphKey), graph)) {
                LOG_INFO("motion matching: '{}' is not in the load order (no Motion Matching.hky database bundle installed) - off", kGraphKey);
                return;
            }
            auto* selector = RootSelector(graph);
            if (!selector) {
                LOG_WARN("motion matching: '{}' does not have a CBPoseSearchGenerator at its root - off", kGraphKey);
                return;
            }

            auto        db       = std::make_unique<Database>();
            const auto& children = selector->Children("generators");
            db->clips.resize(children.size());
            ReadDatabases(*selector, *db);
            ReadChooser(*selector, *db);

            /// @note Keyed by the exact animationName: case is never folded (a differently cased name is its own clip).
            std::map<std::string, std::unique_ptr<Measured>> measured;
            std::vector<const Measured*>                     of(children.size(), nullptr);
            std::array<std::vector<float>, 4>                gaitSpeeds;
            for (std::size_t i = 0; i < children.size(); ++i) {
                const auto* gen = dynamic_cast<const SchemaObject*>(children[i].get());
                if (!gen || std::string_view(gen->ClassName()) != "hkbClipGenerator") {
                    LOG_WARN("motion matching: selector child {} is not a clip generator - never chosen", i);
                    continue;
                }
                Clip& c = db->clips[i];
                c.name  = gen->Str("animationName");
                auto it = measured.find(c.name);
                if (it == measured.end()) {
                    std::unique_ptr<Measured> m;
                    try {
                        m = Measure(reader, c.name, gaitSpeeds);
                    } catch (const std::exception& e) {
                        LOG_WARN("motion matching: clip {} '{}' not measured ({}) - never chosen", i, c.name, e.what());
                    }
                    it = measured.emplace(c.name, std::move(m)).first;
                }
                of[i] = it->second.get();
            }

            /// @note Holden's normalization: each dimension centred on its mean, then each GROUP divided by its mean
            ///       spread, so a group's dimensions keep their relative sizes and every group weighs in comparably.
            ///       Taken over the distinct clips, so a clip listed in several databases does not weigh more.
            std::array<double, F::kDims> mean{}, var{};
            std::size_t                  distinctRows = 0, measuredClips = 0;
            for (const auto& [name, m] : measured) {
                if (!m) continue;
                ++measuredClips;
                distinctRows += static_cast<std::size_t>(m->frames);
                for (std::size_t k = 0; k < m->rows.size(); ++k) mean[k % F::kDims] += m->rows[k];
            }
            if (distinctRows == 0) {
                LOG_WARN("motion matching: no clip of '{}' could be measured - off", kGraphKey);
                return;
            }
            for (auto& m : mean) m /= static_cast<double>(distinctRows);
            for (const auto& [name, m] : measured) {
                if (!m) continue;
                for (std::size_t k = 0; k < m->rows.size(); ++k) {
                    const double x = m->rows[k] - mean[k % F::kDims];
                    var[k % F::kDims] += x * x;
                }
            }
            std::array<double, kGroups> groupSpread{}, groupDims{};
            for (int d = 0; d < F::kDims; ++d) {
                groupSpread[GroupOf(d)] += std::sqrt(var[d] / static_cast<double>(distinctRows));
                groupDims[GroupOf(d)] += 1;
            }
            const std::array<float, kGroups> weights{ kWeightTrajPos, kWeightTrajDir, kWeightPastPos, kWeightPastDir,
                                                      kWeightFootPos, kWeightFootVel, kWeightHipVel };
            for (int d = 0; d < F::kDims; ++d) {
                const double spread = groupSpread[GroupOf(d)] / groupDims[GroupOf(d)];
                db->mean[d]   = static_cast<float>(mean[d]);
                db->scale[d]  = static_cast<float>(spread > 1.0e-6 ? spread : 1.0);
                db->weight[d] = weights[GroupOf(d)];
            }

            std::size_t total = 0;
            for (const Measured* m : of) total += m ? static_cast<std::size_t>(m->frames) : 0;
            db->rows.reserve(total * F::kDims);
            db->clipOf.reserve(total);
            db->searchable.reserve(total);
            db->rowGates.reserve(total);
            db->rowGait.reserve(total);
            /// @note The gates read only the row's curves: the build has no actor, and the context is reset per row so
            ///       the primitive memo never hands one row's curve value to the next.
            relay::EvalContext ctx;
            const auto         gate = [&](const Expression& e) {
                if (e.text.empty()) return true;
                return e.expr.Valid() && e.expr.Evaluate(ctx).AsBool();
            };
            for (std::size_t i = 0; i < db->clips.size(); ++i) {
                Clip& c = db->clips[i];
                c.first = static_cast<int>(db->rows.size() / F::kDims);
                const Measured* m = of[i];
                if (!m) continue;
                c.frames   = m->frames;
                c.dt       = m->dt;
                c.duration = m->duration;
                for (std::size_t k = 0; k < m->rows.size(); ++k) {
                    const int d = static_cast<int>(k % F::kDims);
                    db->rows.push_back((m->rows[k] - db->mean[d]) / db->scale[d]);
                }
                db->clipOf.insert(db->clipOf.end(), static_cast<std::size_t>(m->frames), static_cast<int>(i));
                db->searchable.insert(db->searchable.end(), m->searchable.begin(), m->searchable.end());
                db->rowGait.insert(db->rowGait.end(), m->gait.begin(), m->gait.end());
                const PoseDatabase* pd = c.database >= 0 ? &db->databases[c.database] : nullptr;
                CurveVariables      vars(m->curves);
                for (int f = 0; f < m->frames; ++f) {
                    std::uint8_t bits = 0;
                    if (pd) {
                        ctx.Reset(nullptr);
                        ctx.variables = &vars;
                        vars.time     = static_cast<float>(f * m->dt);
                        if (gate(pd->standing)) bits |= kRowStanding;
                        if (gate(pd->moving)) bits |= kRowMoving;
                    }
                    db->rowGates.push_back(bits);
                }
            }

            for (std::size_t g = 0; g < gaitSpeeds.size(); ++g) {
                auto& v = gaitSpeeds[g];
                if (v.empty()) continue;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                db->gaitSpeed[g] = v[v.size() / 2];
            }

            const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
            LOG_INFO("motion matching: database built in {:.1f} s - {} child(ren) over {} distinct clip(s) ({} measured), {} database(s), "
                     "chooser {}; {} frames x {} features; clip speeds walk {:.0f} ({} frames), run {:.0f} ({}), sprint {:.0f} ({}) "
                     "game units/s",
                     secs, db->clips.size(), measured.size(), measuredClips, db->databases.size(),
                     db->haveChooser ? std::format("{} row(s), {} fallback", db->chooser.size(), db->fallback.size()) : std::string("none"),
                     total, F::kDims, db->gaitSpeed[1], gaitSpeeds[1].size(), db->gaitSpeed[2], gaitSpeeds[2].size(), db->gaitSpeed[3],
                     gaitSpeeds[3].size());
            const auto text = [](const Expression& e) { return e.text.empty() ? std::string("0") : "`" + e.text + "`"; };
            for (const PoseDatabase& d : db->databases)
                LOG_INFO("motion matching: database '{}' - bias base {}, continuing {}, looping {}{}", d.name, text(d.base),
                         text(d.continuing), text(d.looping), d.pivots ? ", Pivots" : "");
            for (std::size_t r = 0; r < db->chooser.size(); ++r)
                LOG_INFO("motion matching: chooser row {} - when {} -> {}", r, db->chooser[r].when.text.empty() ? std::string("always") : text(db->chooser[r].when),
                         db->chooser[r].databases.size());
            g_db.store(db.release(), std::memory_order_release);
        }

        [[maybe_unused]] const bool s_build = [] {
            if (!ch::motion::On()) return false;
            core::resolve::OnLoadOrderReady([](const core::resolve::LoadOrderReader& reader) {
                std::thread([reader] { Build(reader); }).detach();
            });
            return true;
        }();

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && static_cast<void*>(mc.get()) == controller ? player : nullptr;
        }

        int FrameOf(const Clip& c, double time)
        {
            return std::clamp(static_cast<int>(std::lround(time / c.dt)), 0, c.frames - 1);
        }

        /// @brief GASP's future velocity's speed: the displacement over kFutureFrom -> kFutureTo ahead, game units/s.
        /// @note  An approximation of GASP's trajectory sample: the movement model stepped on from the frame's snapshot
        ///        with its input frozen (the predictor's own step), integrated directly, without the predictor's velocity
        ///        spring, collisions, floors or falls.
        float FutureSpeed(const mv::Snapshot& snap)
        {
            mv::State   s    = snap.state;
            float       px   = snap.px, py = snap.py, ax = px, ay = py;
            const int   from = static_cast<int>(std::lround(kFutureFrom / kFutureStep));
            const int   to   = static_cast<int>(std::lround(kFutureTo / kFutureStep));
            for (int i = 0; i < to; ++i) {
                if (i == from) ax = px, ay = py;
                mv::Step(s, snap.input, snap.caps, px, py, kFutureStep);
                px += s.vx * kFutureStep, py += s.vy * kFutureStep;
            }
            return std::hypot(px - ax, py - ay) / (kFutureTo - kFutureFrom);
        }

        /// @brief Frame-to-frame memory of the chooser inputs. @note The frame hook's alone: no lock.
        struct InputMemory {
            bool  haveZ = false;
            float lastZ = 0;
            float airVz = 0;          ///< the vertical speed on the latest airborne frame, game units/s
            bool  wasAirborne = false;
            float landedFor = -1;     ///< seconds since touching down, -1 = not landed since the last air time
            bool  heavy = false;
            bool  haveStickYaw = false;
            float stickYaw = 0;       ///< the stick's latest heading (held after its release)
            float turnHold = 0;       ///< seconds ShouldTurnInPlace stays raised
        };

        /// @brief GASP's chooser inputs for this frame into `io` (its previous values on entry).
        /// @param playingPivots the playing child's database is tagged "Pivots".
        /// @return false without a fresh movement-model frame: `io` keeps its values.
        bool ComputeInputs(RE::Actor* player, float dt, bool playingPivots, Inputs& io)
        {
            static InputMemory m;
            const float z  = player->GetPosition().z;
            const float vz = m.haveZ && dt > 0.0f ? (z - m.lastZ) / dt : 0.0f;
            m.lastZ = z, m.haveZ = true;

            mv::Snapshot snap;
            if (!mv::Current(snap)) return false;
            const mv::State& s      = snap.state;
            const mv::Input& in     = snap.input;
            const float      speed  = std::hypot(s.vx, s.vy);
            const bool       moving = speed > kMovingEpsilon && in.haveStick;
            const bool       strafe = in.mode == mv::Mode::Strafe;
            const bool       wasMoving = io[kMovementState] == 1;
            const float      velYaw = std::atan2(s.vx, s.vy);
            if (in.haveStick) m.stickYaw = std::atan2(in.stickX, in.stickY), m.haveStickYaw = true;

            io[kMovementMode]  = in.airborne ? 1 : 0;
            io[kRotationMode]  = strafe ? 1 : 0;
            io[kMovementState] = moving ? 1 : 0;
            if (in.gait != ch::Gait::None) io[kGait] = static_cast<int>(in.gait) - 1;
            io[kStance]        = player->IsSneaking() ? 1 : 0;

            io[kIsStarting] = moving && !playingPivots && FutureSpeed(snap) >= speed + kStartGain;
            io[kIsPivoting] = moving && std::fabs(ch::WrapPi(m.stickYaw - velYaw)) >= (strafe ? kPivotStrafe : kPivotOrient);

            /// @note The landing's vertical speed is the player's height change over the last airborne frame (the
            ///       touch-down frame's own is already cut short by the floor).
            if (in.airborne) m.airVz = vz, m.landedFor = -1.0f;
            else if (m.wasAirborne) m.landedFor = 0.0f, m.heavy = std::fabs(m.airVz) >= kHeavyLandSpeed;
            else if (m.landedFor >= 0.0f) m.landedFor += dt;
            m.wasAirborne     = in.airborne;
            const bool landed = m.landedFor >= 0.0f && m.landedFor < kLandHold;
            io[kJustLandedLight] = landed && !m.heavy;
            io[kJustLandedHeavy] = landed && m.heavy;

            /// @note The orientation intent: strafing, toward the lock target; orienting, the stick's latest heading,
            ///       since the stop that raises it releases the stick (GASP's capsule likewise keeps the last input's
            ///       rotation while its visual root lags behind).
            /// @note Held kSearchInterval: GASP searches every tick, CB's search runs at most that often, and a one-frame
            ///       input would mostly miss it.
            float intent = s.yaw;
            if (strafe) {
                const float dx = in.targetX - snap.px, dy = in.targetY - snap.py;
                if (in.haveTarget && dx * dx + dy * dy > 1.0f) intent = std::atan2(dx, dy);
            } else if (m.haveStickYaw) {
                intent = m.stickYaw;
            }
            if (wasMoving && !moving && std::fabs(ch::WrapPi(intent - s.yaw)) >= kTurnInPlace) m.turnHold = static_cast<float>(kSearchInterval);
            else m.turnHold = (std::max)(0.0f, m.turnHold - dt);
            io[kShouldTurnInPlace] = m.turnHold > 0.0f;

            io[kShouldSpinTransition] = !playingPivots && speed >= kSpinSpeed && std::fabs(ch::WrapPi(velYaw - s.yaw)) >= kSpinAngle;
            io[kJustTraversed]        = 0;
            io[kDatabaseLOD]          = DatabaseLod();
            io.real[kSpeed2D]         = speed;
            io.real[kTimeToLand]      = in.airborne ? g_timeToLand.load(std::memory_order_relaxed) : 0.0f;
            return true;
        }

        void WriteInputs(RE::Actor* player, const Inputs& in)
        {
            static const std::array<RE::BSFixedString, kInputs> names = [] {
                std::array<RE::BSFixedString, kInputs> a;
                for (int i = 0; i < kInputs; ++i) a[i] = kInputNames[i];
                return a;
            }();
            for (int i = 0; i < kInputs; ++i) {
                if (IsBoolInput(i)) player->SetGraphVariableBool(names[i], in[i] != 0);
                else player->SetGraphVariableInt(names[i], in[i]);
            }
            static const std::array<RE::BSFixedString, kRealInputs> realNames = [] {
                std::array<RE::BSFixedString, kRealInputs> a;
                for (int i = 0; i < kRealInputs; ++i) a[i] = kRealInputNames[i];
                return a;
            }();
            for (int i = 0; i < kRealInputs; ++i) player->SetGraphVariableFloat(realNames[i], in.real[i]);
        }

        /// @brief GASP's Get_MMInterruptMode: the movement mode changed, or on the ground the movement state, the gait
        ///        while moving, or the stance did.
        bool InputsInterrupt(const Inputs& before, const Inputs& now)
        {
            if (before[kMovementMode] != now[kMovementMode]) return true;
            if (now[kMovementMode] != 0) return false;
            return before[kMovementState] != now[kMovementState] || (now[kMovementState] == 1 && before[kGait] != now[kGait]) ||
                   before[kStance] != now[kStance];
        }

        /// @brief The chooser inputs, by their exact names, as GraphVar reads them: this frame's values, typed as declared.
        /// @note  Why ahead of the graph: the inputs are declared on the add-on's graph, not the root one GraphVar takes
        ///        types from, and the frame knows them before the graph is evaluated.
        class InputVariables final : public relay::VariableSource {
        public:
            explicit InputVariables(const Inputs& in) : m_in(in) {}
            bool Read(const char* name, relay::Value& out) const override
            {
                for (int i = 0; i < kInputs; ++i)
                    if (std::strcmp(name, kInputNames[i]) == 0) {
                        out = IsBoolInput(i) ? relay::Value::Bool(m_in[i] != 0) : relay::Value::Int(m_in[i]);
                        return true;
                    }
                for (int i = 0; i < kRealInputs; ++i)
                    if (std::strcmp(name, kRealInputNames[i]) == 0) {
                        out = relay::Value::Float(m_in.real[i]);
                        return true;
                    }
                return false;
            }

        private:
            const Inputs& m_in;
        };

        /// @brief The frame's evaluation pass over the player: the chooser and every bias share its primitive reads.
        /// @note  The frame hook's alone. `vars` must outlive the frame's evaluations.
        const relay::EvalContext& FrameContext(RE::Actor* player, const InputVariables& vars)
        {
            static relay::EvalContext ctx;
            ctx.Reset(player);
            ctx.variables = &vars;
            return ctx;
        }

        /// @return `e` evaluated to a float, 0 when it is empty or did not compile
        float ValueOf(const Expression& e, const relay::EvalContext& ctx)
        {
            return e.expr.Valid() ? e.expr.Evaluate(ctx).AsFloat() : 0.0f;
        }

        /// @brief Every database's biases for this frame.
        std::vector<Biases> EvaluateBiases(const Database& db, const relay::EvalContext& ctx)
        {
            std::vector<Biases> out(db.databases.size());
            for (std::size_t d = 0; d < out.size(); ++d) {
                const PoseDatabase& p = db.databases[d];
                out[d] = { ValueOf(p.base, ctx), ValueOf(p.continuing, ctx), ValueOf(p.looping, ctx) };
            }
            return out;
        }

        /// @brief The chooser's pick, per database: the union of every row whose `when` holds; an empty union is the
        ///        fallback, an empty fallback (or no chooser) every database.
        std::vector<char> Choose(const Database& db, const relay::EvalContext& ctx)
        {
            std::vector<char> on(db.databases.size(), 0);
            bool              any = false;
            for (const ChooserRow& row : db.chooser) {
                const bool holds = row.when.text.empty() || (row.when.expr.Valid() && row.when.expr.Evaluate(ctx).AsBool());
                if (!holds) continue;
                for (const int d : row.databases) on[d] = 1, any = true;
            }
            if (any) return on;
            if (db.fallback.empty()) std::fill(on.begin(), on.end(), 1);
            for (const int d : db.fallback) on[d] = 1;
            return on;
        }

        bool IsActive(const std::vector<char>& active, int database)
        {
            return database >= 0 && (active.empty() || (static_cast<std::size_t>(database) < active.size() && active[database]));
        }

        std::string ActiveNames(const Database& db, const std::vector<char>& active)
        {
            std::string out;
            for (std::size_t d = 0; d < db.databases.size(); ++d)
                if (IsActive(active, static_cast<int>(d))) out += (out.empty() ? "" : ", ") + db.databases[d].name;
            return out.empty() ? std::string("none") : out;
        }

        std::string InputsText(const Inputs& in)
        {
            static constexpr const char* kGaits[3] = { "walk", "run", "sprint" };
            std::string out = std::format("{} {} {} {} {}", in[kMovementMode] ? "air" : "ground", in[kRotationMode] ? "strafe" : "orient",
                                          in[kMovementState] ? "moving" : "idle", kGaits[std::clamp(in[kGait], 0, 2)],
                                          in[kStance] ? "crouch" : "stand");
            static constexpr std::pair<int, const char*> kFlags[] = {
                { kIsStarting, "starting" },    { kIsPivoting, "pivoting" },          { kJustLandedLight, "landed" },
                { kJustLandedHeavy, "landed-heavy" }, { kShouldTurnInPlace, "turn-in-place" }, { kShouldSpinTransition, "spin" },
                { kJustTraversed, "traversed" }
            };
            for (const auto& [i, name] : kFlags)
                if (in[i]) out += std::string(" ") + name;
            if (in[kDatabaseLOD]) out += std::format(" lod{}", in[kDatabaseLOD]);
            out += std::format(" {:.0f}/s", in.real[kSpeed2D]);
            if (in[kMovementMode]) out += in.real[kTimeToLand] >= controller::motion::kNoLanding
                                              ? std::string(" no-landing")
                                              : std::format(" lands-in {:.2f}s", in.real[kTimeToLand]);
            return out;
        }

        void CountFrame();

        /// @brief Plays what the search chose: advances it by the frame's time and hands it to the add-on graph; computes
        ///        the chooser's inputs, writes them to the graph and publishes the chooser's pick and the databases' biases
        ///        for the search.
        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            RE::Actor*       player = out ? PlayerOf(controller) : nullptr;
            const Database*  db     = g_db.load(std::memory_order_acquire);
            if (!player) return;
            ch::motion::QueueSkeletonWatch();
            if (!db) return;
            CountFrame();
            CB_PLOT("MM rate", g_rate.load(std::memory_order_relaxed));

            Playback p;
            {
                std::scoped_lock lock(g_shared.lock);
                Playback& now = g_shared.now;
                if (g_shared.havePending) {
                    now                  = g_shared.pending;
                    g_shared.havePending = false;
                } else if (now.clip >= 0) {
                    now.time += deltaTime * g_rate.load(std::memory_order_relaxed);
                }
                if (now.clip >= 0) {
                    const Clip& c = db->clips[now.clip];
                    if (now.time >= c.duration - kEndMargin) g_shared.searchNow = true;
                    now.time = (std::min)(now.time, c.duration);
                }
                p = now;
            }

            /// @note The frame hook's alone: the inputs as of the previous frame, then this one's.
            static Inputs     inputs = InitialInputs();
            static std::vector<char> lastActive;
            const int         playingDb = p.clip >= 0 ? db->clips[p.clip].database : -1;
            const Inputs      before    = inputs;
            ComputeInputs(player, deltaTime, playingDb >= 0 && db->databases[playingDb].pivots, inputs);
            WriteInputs(player, inputs);
            const InputVariables     vars(inputs);
            const relay::EvalContext& ctx       = FrameContext(player, vars);
            const std::vector<char>  active    = Choose(*db, ctx);
            std::vector<Biases>      biases    = EvaluateBiases(*db, ctx);
            const bool               interrupt = playingDb >= 0 && !IsActive(active, playingDb) && InputsInterrupt(before, inputs);
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.active = active;
                g_shared.biases = std::move(biases);
                g_shared.inputs = inputs;
                if (interrupt) g_shared.interrupt = true, g_shared.interruptActive = active;
            }
            if (active != lastActive) {
                static int changes = 0;
                if (changes++ < 300)
                    LOG_INFO("motion matching: searching {} ({}){}", ActiveNames(*db, active), InputsText(inputs),
                             interrupt ? std::format(" - interrupts '{}'", db->databases[playingDb].name) : std::string());
                lastActive = active;
            }

            /// @note Until the first search chooses a clip, MM_Enable stays at its declared 0: vanilla locomotion plays.
            if (p.clip < 0) return;
            static const RE::BSFixedString enableName(kEnableVar), clipName(kClipVar), timeName(kTimeVar);
            const Clip& c = db->clips[p.clip];
            player->SetGraphVariableInt(enableName, 1);
            player->SetGraphVariableInt(clipName, p.clip);
            player->SetGraphVariableFloat(timeName, static_cast<float>(c.duration > 0 ? p.time / c.duration : 0.0));
        }

        [[maybe_unused]] const bool s_subscribed =
            Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharMotionMatching", "char_motion_matching");

        /// @brief How often clips are sampled: the proof that CBPoseSearchGenerator's skipped children are not.
        /// @note  Kept every child active, so a skip the engine ignored would sample all 252 add-on clips every frame.
        using ClipGenerate = hooks::Override<&CB::havok::hkbClipGenerator::generate>;
        constexpr std::string_view kClipPrefix = "animations\\MotionMatching\\";   ///< the add-on's clips, as authored
        constexpr double           kReportEvery = 5.0;   ///< seconds
        struct GenerateCount {
            std::atomic<std::uint64_t> ours{ 0 }, all{ 0 }, frames{ 0 };
            std::atomic<std::uint64_t> oursThisFrame{ 0 };   ///< add-on clip samples since the last player frame (the plot)
        };
        GenerateCount g_generates;

        void OnClipGenerate(ClipGenerate::Next next, CB::havok::hkbClipGenerator* clip, const CB::havok::hkbContext& context,
                            const CB::havok::hkbGeneratorOutput** activeChildrenOutput, CB::havok::hkbGeneratorOutput& output,
                            CB::havok::hkReal timeOffset)
        {
            g_generates.all.fetch_add(1, std::memory_order_relaxed);
            if (const char* name = clip->m_animationName.cString(); name && std::string_view(name).starts_with(kClipPrefix)) {
                g_generates.ours.fetch_add(1, std::memory_order_relaxed);
                g_generates.oursThisFrame.fetch_add(1, std::memory_order_relaxed);
            }
            next(clip, context, activeChildrenOutput, output, timeOffset);
        }

        [[maybe_unused]] const bool s_countSubscribed =
            ClipGenerate::Subscribe(&OnClipGenerate, hooks::Priority::Normal, "CharMotionMatching", "char_motion_matching");

        /// @brief "min / median / max (count)" of `v`, which it reorders.
        std::string Range(std::vector<float>& v)
        {
            if (v.empty()) return "none";
            std::sort(v.begin(), v.end());
            return std::format("{:.2f} / {:.2f} / {:.2f} ({})", v.front(), v[v.size() / 2], v.back(), v.size());
        }

        /// @brief Count a player frame; every kReportEvery seconds, log the clip samples per frame and the searches'
        ///        biased cost ranges since the last report (the scale the databases' biases act on).
        void CountFrame()
        {
            g_generates.frames.fetch_add(1, std::memory_order_relaxed);
            CB_PLOT("MM add-on clip samples", g_generates.oursThisFrame.exchange(0, std::memory_order_relaxed));
            static Clock::time_point last = Clock::now();
            const auto               now  = Clock::now();
            if (std::chrono::duration<double>(now - last).count() < kReportEvery) return;
            last               = now;
            const auto frames  = g_generates.frames.exchange(0);
            const auto ours    = g_generates.ours.exchange(0);
            const auto all     = g_generates.all.exchange(0);
            std::vector<float> winning, playing;
            {
                std::scoped_lock lock(g_costs.lock);
                winning.swap(g_costs.winning);
                playing.swap(g_costs.playing);
            }
            static int reports = 0;
            if (reports == 0)
                LOG_INFO("motion matching: switching - additive database cost biases, reselect ban {:.2f} s, forced on {:.0f} "
                         "units/s or 30 deg, database LOD {}",
                         ReselectTime(), kForceSpeed, DatabaseLod());
            if (frames > 0 && reports++ < 60)
                LOG_INFO("motion matching: clip samples per frame - add-on clips {:.2f}, every clip {:.1f} (over {} frames); "
                         "biased cost min / median / max - best candidate {}, playing {}",
                         static_cast<double>(ours) / frames, static_cast<double>(all) / frames, frames, Range(winning), Range(playing));
        }

    }  // namespace

    bool controller::motion::On()
    {
        static const bool on = core::debug::FlagOn("char_motion_matching");
        return on;
    }

    void controller::motion::SetTimeToLand(float seconds) { g_timeToLand.store(seconds, std::memory_order_relaxed); }

    std::string controller::motion::Describe()
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return "no database";
        Playback p;
        {
            std::scoped_lock lock(g_shared.lock);
            p = g_shared.now;
        }
        if (p.clip < 0) return "no clip chosen yet";
        const Clip&      c    = db->clips[p.clip];
        const Clock::rep at   = g_lastSwitch.load(std::memory_order_relaxed);
        const double     ago  = at ? std::chrono::duration<double>(Clock::now() - Clock::time_point(Clock::duration(at))).count() : -1.0;
        return std::format("clip {} '{}' ({}) frame {}, blend left {:.2f} s, last switch {:.2f} s ago", p.clip, c.name,
                           c.database >= 0 ? db->databases[c.database].name : std::string("no database"),
                           c.frames > 0 ? FrameOf(c, p.time) : 0, BlendRemaining(), ago);
    }

    float controller::motion::Rate(const FrameSpeed* frame)
    {
        const Database* db   = g_db.load(std::memory_order_acquire);
        const float     last = g_rate.load(std::memory_order_relaxed);
        if (!db || !frame || frame->gait == Gait::None || frame->full < kRateStill) return last;
        const float clipSpeed = db->gaitSpeed[static_cast<std::size_t>(frame->gait)];
        if (clipSpeed <= 0.0f) return last;
        const float rate = std::clamp(frame->full / clipSpeed, kMinRate, kMaxRate);
        g_rate.store(rate, std::memory_order_relaxed);
        return rate;
    }

    void controller::motion::Search(const Query& q)
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return;
        CB_ZONE(zone, "MM search");
        /// @note Only the predictor thread searches, so the search's own history needs no lock.
        static Clock::time_point last{};
        static float             lastWantX = 0, lastWantY = 0;
        const auto               now     = Clock::now();
        const double             elapsed = std::chrono::duration<double>(now - last).count();

        /// @note Forced: the request jumped since the last search, in speed or (while moving) in direction.
        const float speed = std::hypot(q.desiredX, q.desiredY), lastSpeed = std::hypot(lastWantX, lastWantY);
        bool        forced = std::fabs(speed - lastSpeed) > kForceSpeed;
        if (!forced && speed > kForceMoving && lastSpeed > kForceMoving)
            forced = (q.desiredX * lastWantX + q.desiredY * lastWantY) / (speed * lastSpeed) < kForceCos;

        Playback          cur;
        bool              clipEnd = false, interrupt = false;
        std::vector<char>   active;
        std::vector<Biases> biases;
        Inputs              inputs = InitialInputs();
        {
            std::scoped_lock lock(g_shared.lock);
            clipEnd   = g_shared.searchNow;
            interrupt = g_shared.interrupt;
            if (!clipEnd && !interrupt && elapsed < kSearchInterval && !(forced && elapsed >= kForcedSpacing)) return;
            g_shared.searchNow = false;
            g_shared.interrupt = false;
            active             = interrupt ? g_shared.interruptActive : g_shared.active;
            biases             = g_shared.biases;
            inputs             = g_shared.inputs;
            cur                = g_shared.havePending ? g_shared.pending : g_shared.now;
        }
        last = now, lastWantX = q.desiredX, lastWantY = q.desiredY;
        const char* trigger = interrupt ? "interrupt" : clipEnd ? "clip end" : elapsed < kSearchInterval ? "forced" : "timer";

        /// @note The query: the trajectory ahead and back from the predictor, the pose from the frame playing now.
        std::array<float, F::kDims> qv{};
        for (int k = 0; k < 3; ++k) {
            const float raw[8] = { q.x[k], q.y[k], q.dirX[k], q.dirY[k], q.pastX[k], q.pastY[k], q.pastDirX[k], q.pastDirY[k] };
            const int   at[8]  = { F::kTrajPos + 2 * k, F::kTrajPos + 2 * k + 1, F::kTrajDir + 2 * k, F::kTrajDir + 2 * k + 1,
                                   F::kPastPos + 2 * k, F::kPastPos + 2 * k + 1, F::kPastDir + 2 * k, F::kPastDir + 2 * k + 1 };
            for (int j = 0; j < 8; ++j) qv[at[j]] = (raw[j] - db->mean[at[j]]) / db->scale[at[j]];
        }
        int curRow = -1;
        if (cur.clip >= 0 && db->clips[cur.clip].frames > 0) {
            const Clip& c = db->clips[cur.clip];
            curRow        = c.first + FrameOf(c, cur.time);
            std::copy_n(db->rows.data() + static_cast<std::size_t>(curRow) * F::kDims + F::kFootPos, F::kDims - F::kFootPos,
                        qv.data() + F::kFootPos);
        }

        const auto cost = [&](std::size_t r, float bound) {
            const float* row = db->rows.data() + r * F::kDims;
            float        sum = 0;
            for (int d = 0; d < F::kDims && sum < bound; ++d) {
                const float e = db->weight[d] * (qv[d] - row[d]);
                sum += e * e;
            }
            return sum;
        };
        /// @brief The frame hook's latest biases for `database` (all 0 before its first frame or outside every database).
        const auto biasesOf = [&](int database) {
            return database >= 0 && static_cast<std::size_t>(database) < biases.size() ? biases[database] : Biases{};
        };
        /// @brief Unreal's additive biases for any candidate of `clip`: its database's base, plus looping for a looping child.
        const auto bias = [&](int clip) {
            const Clip&   c = db->clips[clip];
            const Biases  b = biasesOf(c.database);
            return b.base + ((c.flags & kFlagLooping) ? b.looping : 0.0f);
        };

        /// @note The reselect ban: each pick of the last fMMReselectTime, advanced by the clip time played since (at the
        ///       current rate), bans the rows within kIgnoreSurrounding of where it would be now, as merged row ranges.
        struct Pick {
            int               clip  = 0;
            double            frame = 0;
            Clock::time_point at;
        };
        static std::vector<Pick> picks;
        const float              reselect = ReselectTime();
        const float              rate     = g_rate.load(std::memory_order_relaxed);
        std::erase_if(picks, [&](const Pick& p) { return std::chrono::duration<double>(now - p.at).count() > reselect; });
        std::vector<std::pair<int, int>> banned;
        for (const Pick& p : picks) {
            const Clip&  c  = db->clips[p.clip];
            const double at = p.frame + std::chrono::duration<double>(now - p.at).count() * rate / c.dt;
            const int    lo = (std::max)(0, static_cast<int>(std::floor(at)) - kIgnoreSurrounding);
            const int    hi = (std::min)(c.frames - 1, static_cast<int>(std::ceil(at)) + kIgnoreSurrounding);
            if (lo <= hi) banned.emplace_back(c.first + lo, c.first + hi);
        }
        std::sort(banned.begin(), banned.end());

        const auto  t0      = Clock::now();
        /// @note The playing row (the continuing pose) carries its database's continuing bias on top of a candidate's.
        const float playing = curRow >= 0 ? cost(static_cast<std::size_t>(curRow), 1.0e30f) + bias(cur.clip) +
                                                biasesOf(db->clips[cur.clip].database).continuing
                                          : 1.0e30f;
        float       best     = 1.0e30f;
        int         bestRow  = -1;
        std::size_t ban      = 0, searched = 0;
        /// @note disableReselection: while such a child plays, no other frame of it is a candidate (it goes on or leaves).
        const bool  noRepick = curRow >= 0 && (db->clips[cur.clip].flags & kFlagDisableReselection);
        /// @note The candidate gates: standing (the request near zero) cuts only into rows its database's
        ///       standingCandidate passed, moving only into rows its movingCandidate passed (both evaluated at build).
        ///       Only the chooser's databases are searched; rows ascend with the children, so the ban walk stays monotonic.
        const bool standing = std::hypot(q.desiredX, q.desiredY) < kForceMoving;
        for (int ci = 0; ci < static_cast<int>(db->clips.size()); ++ci) {
            const Clip& c = db->clips[ci];
            if (c.frames == 0 || !IsActive(active, c.database) || (noRepick && ci == cur.clip)) continue;
            const float b = bias(ci);
            for (int r = c.first; r < c.first + c.frames; ++r) {
                if (!db->searchable[r]) continue;
                if (!(db->rowGates[r] & (standing ? kRowStanding : kRowMoving))) continue;
                if (curRow >= 0 && ci == cur.clip && std::abs(r - curRow) < kIgnoreSurrounding) continue;
                while (ban < banned.size() && banned[ban].second < r) ++ban;
                bool skip = false;
                for (std::size_t k = ban; k < banned.size() && banned[k].first <= r; ++k)
                    if (banned[k].second >= r) { skip = true; break; }
                if (skip) continue;
                ++searched;
                const float f = cost(static_cast<std::size_t>(r), best - b);
                if (f + b < best) best = f + b, bestRow = r;
            }
        }
        const float searchMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        CB_PLOT("MM search ms", searchMs);
        {
            std::scoped_lock lock(g_costs.lock);
            if (bestRow >= 0 && g_costs.winning.size() < kCostLogCap) g_costs.winning.push_back(best);
            if (curRow >= 0 && g_costs.playing.size() < kCostLogCap) g_costs.playing.push_back(playing);
        }

        /// @note At a clip's end or an interrupt the playing row cannot go on, so the best candidate wins outright;
        ///       otherwise its biased cost must be below the playing row's.
        const bool wins = bestRow >= 0 && (curRow < 0 || clipEnd || interrupt || best < playing);

        static int switches = 0;
        if (wins) {
            const int   clip = db->clipOf[bestRow];
            const Clip& c    = db->clips[clip];
            const char* dbName = c.database >= 0 ? db->databases[c.database].name.c_str() : "-";
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.pending     = { clip, (bestRow - c.first) * c.dt };
                g_shared.havePending = true;
            }
            picks.push_back({ clip, static_cast<double>(bestRow - c.first), now });
            ++switches;
            g_lastSwitch.store(now.time_since_epoch().count(), std::memory_order_relaxed);
            CB_PLOT("MM switches", switches);
            CB_MESSAGE("MM -> clip {} '{}' [{}] frame {} ({}, cost {:.2f}, was {:.2f})", clip, c.name, dbName, bestRow - c.first, trigger,
                       best, playing);
            static constexpr const char* kGaitNames[4] = { "none", "walk", "run", "sprint" };
            if (LogBudget())
                LOG_INFO("motion matching: -> clip {} '{}' in '{}' frame {} x{:.2f} (cost {:.2f}, was {:.2f}; {}; gait {} / frame {}; "
                         "{} of {} rows in {:.2f} ms)",
                         clip, c.name, dbName, bestRow - c.first, rate, best, playing, trigger,
                         kGaitNames[static_cast<int>(q.gait) & 3], kGaitNames[db->rowGait[bestRow] & 3], searched, db->clipOf.size(),
                         searchMs);
        } else {
            best    = playing;
            bestRow = curRow;
        }

        if (ch::hud::On()) {
            ch::hud::Matching h;
            const int   shown = bestRow >= 0 ? db->clipOf[bestRow] : -1;
            if (shown >= 0) {
                const Clip&        c     = db->clips[shown];
                const auto         slash = c.name.find_last_of('\\');
                const std::string  stem  = slash == std::string::npos ? c.name : c.name.substr(slash + 1);
                std::snprintf(h.clip, sizeof(h.clip), "%s", stem.c_str());
                std::snprintf(h.database, sizeof(h.database), "%s", c.database >= 0 ? db->databases[c.database].name.c_str() : "-");
                h.frame = bestRow - c.first;
            }
            std::snprintf(h.active, sizeof(h.active), "%s", ActiveNames(*db, active).c_str());
            std::snprintf(h.chooser, sizeof(h.chooser), "%s", InputsText(inputs).c_str());
            h.cost     = best;
            h.searchMs = searchMs;
            h.switches = switches;
            h.rate     = g_rate.load(std::memory_order_relaxed);
            h.blend    = BlendRemaining();
            ch::hud::Publish(h);
        }
    }

}  // namespace CB::feature
