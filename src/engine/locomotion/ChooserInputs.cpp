#include <locomotion/ChooserInputs.h>

#include <cmath>
#include <cstring>

namespace CB::engine::locomotion {

    namespace {
        constexpr float kFutureFrom = 0.4f, kFutureTo = 0.5f, kFutureStep = 1.0f / 60.0f;
    }

    bool ChooserVariables::Read(const char* name, relay::Value& out) const
    {
        for (int i = 0; i < kChooserInputs; ++i)
            if (std::strcmp(name, kChooserInputNames[i]) == 0) {
                out = IsBoolInput(i) ? relay::Value::Bool(m_in[i] != 0) : relay::Value::Int(m_in[i]);
                return true;
            }
        for (int i = 0; i < kChooserRealInputs; ++i)
            if (std::strcmp(name, kChooserRealInputNames[i]) == 0) {
                out = relay::Value::Float(m_in.real[i]);
                return true;
            }
        return false;
    }

    void FutureVelocity(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float& vx, float& vy)
    {
        float     ax = px, ay = py;
        const int from = static_cast<int>(std::lround(kFutureFrom / kFutureStep));
        const int to   = static_cast<int>(std::lround(kFutureTo / kFutureStep));
        for (int i = 0; i < to; ++i) {
            if (i == from) ax = px, ay = py;
            Step(s, in, caps, profile, px, py, kFutureStep);
            px += s.vx * kFutureStep, py += s.vy * kFutureStep;
        }
        vx = (px - ax) / (kFutureTo - kFutureFrom);
        vy = (py - ay) / (kFutureTo - kFutureFrom);
    }

    float FutureSpeed(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py)
    {
        float vx = 0.0f, vy = 0.0f;
        FutureVelocity(s, in, caps, profile, px, py, vx, vy);
        return std::hypot(vx, vy);
    }

    void MovementInputs(const State& s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, [[maybe_unused]] float stickYaw,
                        bool playingPivots, ChooserInputs& io)
    {
        const float speed  = std::hypot(s.vx, s.vy);
        const bool  moving = speed > kMovingEpsilon && in.haveStick && !in.turnInPlace;
        const bool  strafe = in.mode == Mode::Strafe;
        const float velYaw = std::atan2(s.vx, s.vy);

        io[kMovementMode]  = in.airborne ? 1 : 0;
        io[kRotationMode]  = strafe ? 1 : 0;
        io[kMovementState] = moving ? 1 : 0;
        if (in.gait != Gait::None) io[kGait] = static_cast<int>(in.gait) - 1;

        float futureX = 0.0f, futureY = 0.0f;
        if (moving) FutureVelocity(s, in, caps, profile, px, py, futureX, futureY);
        const float futureSpeed = std::hypot(futureX, futureY);
        io[kIsStarting] = moving && !playingPivots && futureSpeed >= speed + kStartGain;
        /// @note GASP's IsPivoting: Get_TrajectoryTurnAngle, the yaw from the velocity to the trajectory's future velocity.
        ///       The stick's own angle off the velocity read every quick camera turn as a pivot (chained 90-180 degree
        ///       pivot clips, abandoned every half second); the future velocity has turned only as far as the model will.
        io[kIsPivoting] = moving && futureSpeed > kMovingEpsilon &&
                          std::fabs(WrapPi(std::atan2(futureX, futureY) - velYaw)) >= (strafe ? kPivotStrafe : kPivotOrient);
        io[kShouldSpinTransition] = !playingPivots && speed >= kSpinSpeed && std::fabs(WrapPi(velYaw - s.yaw)) >= kSpinAngle;
        io.real[kSpeed2D]         = speed;
    }

}  // namespace CB::engine::locomotion
