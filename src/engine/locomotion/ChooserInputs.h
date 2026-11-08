#pragma once

#include <locomotion/Movement.h>
#include <relay/Relay.h>

#include <array>
#include <span>

/// @file
/// @brief What a pose search chooser reads: GASP's AnimBP chooser columns as graph variables, what their values mean, and
///        how a frame's movement sets them. The game's frame hook and an editor's match test share this, so a chooser
///        tested in the editor reads the values it reads in game.

namespace CB::engine::locomotion {

    /// @brief The INT32 and BOOL inputs.
    /// @note  kIsStarting..kJustTraversed are BOOL, the rest INT32.
    enum ChooserInput : int {
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
        kChooserInputs
    };
    /// @brief The graph variables' names, byte-exact (the generator that writes the add-on declares these).
    inline constexpr std::array<const char*, kChooserInputs> kChooserInputNames{
        "MM_MovementMode",      "MM_RotationMode",         "MM_MovementState", "MM_Gait",            "MM_Stance",
        "MM_IsStarting",        "MM_IsPivoting",           "MM_JustLandedLight", "MM_JustLandedHeavy",
        "MM_ShouldTurnInPlace", "MM_ShouldSpinTransition", "MM_JustTraversed", "MM_DatabaseLOD"
    };
    constexpr bool IsBoolInput(int i) { return i >= kIsStarting && i <= kJustTraversed; }

    /// @brief The REAL inputs. Speed2D: the horizontal speed, game units/s (GASP's is cm/s; an importer scales its bounds by
    ///        0.7). TimeToLand: seconds until the predicted path lands (GASP's TrajectoryCollision.TimeToLand), 0 on the
    ///        ground.
    enum ChooserRealInput : int { kSpeed2D, kTimeToLand, kChooserRealInputs };
    inline constexpr std::array<const char*, kChooserRealInputs> kChooserRealInputNames{ "MM_Speed2D", "MM_TimeToLand" };

    /// @brief One frame's inputs: the INT32 and BOOL ones by ChooserInput (`in[kGait]`), the REAL ones in `real`.
    struct ChooserInputs {
        std::array<int, kChooserInputs>       word{};
        std::array<float, kChooserRealInputs> real{};
        constexpr int&       operator[](int i) { return word[i]; }
        constexpr const int& operator[](int i) const { return word[i]; }
        bool                 operator==(const ChooserInputs&) const = default;
    };
    /// @brief Before the first frame: on the ground, orienting, idle, standing, Run (GASP's starting gait), LOD 0.
    constexpr ChooserInputs InitialChooserInputs()
    {
        ChooserInputs in{};
        in[kGait] = 1;
        return in;
    }

    /// @brief What a chooser column holds: which labels name an INT32's values, or a BOOL or REAL.
    enum class ChooserColumnKind : std::uint8_t { Choice, Bool, Real };
    struct ChooserColumn {
        const char*                  variable;   ///< byte-exact
        const char*                  label;
        ChooserColumnKind            kind;
        std::span<const char* const> choices;    ///< a Choice's labels, by value
    };
    namespace detail {
        inline constexpr const char* kModes[]     = { "OnGround", "InAir" };
        inline constexpr const char* kRotations[] = { "Orient", "Strafe", "Aim" };
        inline constexpr const char* kStates[]    = { "Idle", "Moving" };
        inline constexpr const char* kGaits[]     = { "Walk", "Run", "Sprint" };
        inline constexpr const char* kStances[]   = { "Stand", "Crouch" };
        inline constexpr const char* kLods[]      = { "Dense", "Sparse", "ExtremeSparse" };
    }
    /// @brief Every input, in the order an editor lays them out: the broad choices first, as GASP's top tables do.
    inline constexpr std::array<ChooserColumn, kChooserInputs + kChooserRealInputs> kChooserColumns{ {
        { "MM_MovementMode", "Mode", ChooserColumnKind::Choice, detail::kModes },
        { "MM_Stance", "Stance", ChooserColumnKind::Choice, detail::kStances },
        { "MM_MovementState", "State", ChooserColumnKind::Choice, detail::kStates },
        { "MM_Gait", "Gait", ChooserColumnKind::Choice, detail::kGaits },
        { "MM_RotationMode", "Rotation", ChooserColumnKind::Choice, detail::kRotations },
        { "MM_IsStarting", "Starting", ChooserColumnKind::Bool, {} },
        { "MM_IsPivoting", "Pivoting", ChooserColumnKind::Bool, {} },
        { "MM_ShouldSpinTransition", "Spin", ChooserColumnKind::Bool, {} },
        { "MM_ShouldTurnInPlace", "Turn in place", ChooserColumnKind::Bool, {} },
        { "MM_JustLandedLight", "Landed", ChooserColumnKind::Bool, {} },
        { "MM_JustLandedHeavy", "Landed heavy", ChooserColumnKind::Bool, {} },
        { "MM_JustTraversed", "Traversed", ChooserColumnKind::Bool, {} },
        { "MM_Speed2D", "Speed", ChooserColumnKind::Real, {} },
        { "MM_TimeToLand", "Time to land", ChooserColumnKind::Real, {} },
        { "MM_DatabaseLOD", "LOD", ChooserColumnKind::Choice, detail::kLods },
    } };

    /// @brief The inputs by their exact names, as GraphVar reads them: this frame's values, typed as declared.
    /// @note  Why ahead of the graph: the inputs are declared on the add-on's graph, not the root one GraphVar takes types
    ///        from, and the frame knows them before the graph is evaluated.
    class ChooserVariables final : public relay::VariableSource {
    public:
        explicit ChooserVariables(const ChooserInputs& in) : m_in(in) {}
        bool Read(const char* name, relay::Value& out) const override;

    private:
        const ChooserInputs& m_in;
    };

    /// @brief GASP's thresholds (its AnimBP's), centimetres at 0.7 game units each.
    inline constexpr float kMovingEpsilon = 1.0f;          ///< game units/s: IsMoving's "velocity != 0"
    inline constexpr float kStartGain     = 70.0f;         ///< IsStarting: the future speed exceeds the speed by 100 cm/s
    inline constexpr float kPivotOrient   = 0.78539816f;   ///< IsPivoting: 45 deg orienting to movement ...
    inline constexpr float kPivotStrafe   = 0.52359878f;   ///< ... 30 deg strafing
    inline constexpr float kSpinAngle     = 2.2689280f;    ///< ShouldSpinTransition: 130 deg ...
    inline constexpr float kSpinSpeed     = 105.0f;        ///< ... at 150 cm/s or more

    /// @brief GASP's future velocity's speed: the displacement over 0.4 -> 0.5 s ahead, game units/s.
    /// @note  An approximation of GASP's trajectory sample: the model stepped on with its input frozen (the predictor's own
    ///        step), integrated directly, without the predictor's velocity spring, collisions, floors or falls.
    float FutureSpeed(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py);

    /// @brief The future velocity FutureSpeed measures (GASP's Trj_FutureVelocity: the displacement from 0.4 to 0.5 s
    ///        ahead), world x and y, game units/s.
    void FutureVelocity(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float& vx, float& vy);

    /// @brief Seconds the stop and pivot predictions step at most.
    inline constexpr float kPredictHorizon = 3.0f;

    /// @brief How far (game units, along the path) the character slides before it stops if the stick lets go now: the
    ///        model stepped on with no input, the body (moving at bodyVx, bodyVy) following it through its ground velocity
    ///        spring (GroundVelocityStep, its rate from 0 as the predictor starts it), until both are still, at most
    ///        kPredictHorizon.
    /// @return -1 when neither is moving
    /// @note  The model's own integrator, not a closed form: Unreal's PredictGroundMovementStopLocation assumes constant
    ///        friction and drifts with the frame rate.
    /// @note  Why the body: the model alone brakes a run to rest in about 10 units, while the character, which moves at
    ///        the body's velocity, slides about 57; a stop clip played by the model's distance jumped ahead and ran out
    ///        before the character stopped.
    float PredictStopDistance(State s, Input in, const Caps& caps, const Profile& profile, float px, float py, float bodyVx, float bodyVy);

    /// @brief How far (game units, along the body's path) the character goes before it turns round, when the stick points
    ///        more than 90 degrees from the model's velocity: the model stepped on with the input held and the body
    ///        following it (as PredictStopDistance), up to the body's slowest point.
    /// @return -1 when it is not pivoting (moving with the stick, or no turn within kPredictHorizon)
    float PredictPivotDistance(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float bodyVx, float bodyVy);

    /// @brief The inputs a frame's movement decides: mode, rotation, state, gait, starting, pivoting, spin and speed.
    /// @param playingPivots the playing child's database is tagged "Pivots": IsStarting and spins stay false
    /// @note  `io` keeps its gait while the input names none. The rest (stance, landings, turning in place, time to land)
    ///        need what only a running character knows.
    void MovementInputs(const State& s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, bool playingPivots,
                        ChooserInputs& io);

    /// @brief GASP's Get_MMInterruptMode: the movement mode changed, or on the ground the movement state, the gait while
    ///        moving, or the stance did.
    bool InputsInterrupt(const ChooserInputs& before, const ChooserInputs& now);

    /// @brief ShouldTurnInPlace: 50 deg (GASP's) between the facing and the orientation intent.
    inline constexpr float kTurnInPlace = 0.87266463f;

    /// @brief The orientation intent: the desired facing (State::desiredYaw: the stick's latest heading, the strafe
    ///        target's while strafing), else the facing.
    float TurnIntent(const State& s);

    /// @brief ShouldTurnInPlace for this frame, raised for `holdFor` seconds once asked.
    /// @param hold seconds it stays raised, carried frame to frame
    /// @param moving this frame's MovementState
    /// @note  Asked whenever the character stands kTurnInPlace or more off its intent. GASP asks only on the stop or
    ///        while aiming, its root lagging a capsule that snapped; CB's facing is still turning toward the intent then
    ///        (Step), so a released flick's turn plays a turn clip to its end rather than an idle sliding its feet.
    /// @note  Held kSearchInterval by the caller (`holdFor`): GASP searches every tick, CB's search runs at most that often,
    ///        and a one-frame input would mostly miss it.
    bool ShouldTurnInPlace(float& hold, bool moving, float intent, float yaw, float holdFor, float dt);

}  // namespace CB::engine::locomotion
