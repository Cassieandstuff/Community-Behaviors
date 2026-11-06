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

    float FutureSpeed(State s, const Input& in, const Caps& caps, const Profile& profile, float px, float py)
    {
        float     ax = px, ay = py;
        const int from = static_cast<int>(std::lround(kFutureFrom / kFutureStep));
        const int to   = static_cast<int>(std::lround(kFutureTo / kFutureStep));
        for (int i = 0; i < to; ++i) {
            if (i == from) ax = px, ay = py;
            Step(s, in, caps, profile, px, py, kFutureStep);
            px += s.vx * kFutureStep, py += s.vy * kFutureStep;
        }
        return std::hypot(px - ax, py - ay) / (kFutureTo - kFutureFrom);
    }

    void MovementInputs(const State& s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float stickYaw,
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

        io[kIsStarting] = moving && !playingPivots && FutureSpeed(s, in, caps, profile, px, py) >= speed + kStartGain;
        io[kIsPivoting] = moving && std::fabs(WrapPi(stickYaw - velYaw)) >= (strafe ? kPivotStrafe : kPivotOrient);
        io[kShouldSpinTransition] = !playingPivots && speed >= kSpinSpeed && std::fabs(WrapPi(velYaw - s.yaw)) >= kSpinAngle;
        io.real[kSpeed2D]         = speed;
    }

}  // namespace CB::engine::locomotion
