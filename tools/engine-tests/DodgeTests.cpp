/// @file
/// @brief Advanced Dodge's pure decisions: direction pick, tap/hold, double tap, stamina.

#include "TestSupport.h"

#include <locomotion/Dodge.h>
#include <locomotion/Movement.h>

#include <cmath>
#include <string>

namespace CB::engine::tests {

namespace {

namespace loco = locomotion;
using Dir      = loco::DodgeDirection;

constexpr float kPi  = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

std::string Name(Dir d) {
    static const char* names[] = { "N", "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
    return names[static_cast<int>(d)];
}

void CheckPick(float worldDeg, float bodyDeg, Dir want) {
    const loco::DodgeDirectionPick p = loco::PickDodgeDirection(true, worldDeg * kDeg, bodyDeg * kDeg);
    Check(p.direction == want, "dodge heading " + std::to_string(worldDeg) + " from body " + std::to_string(bodyDeg) + " -> " + Name(want) +
                                   " (got " + Name(p.direction) + ")");
}

void DirectionChecks() {
    Check(loco::PickDodgeDirection(false, 1.0f, 0.0f).direction == Dir::N, "no direction -> N");
    Check(loco::PickDodgeDirection(false, 1.0f, 0.0f).angle == 0.0f, "N has angle 0");

    const Dir ways[] = { Dir::F, Dir::FR, Dir::R, Dir::BR, Dir::B, Dir::BL, Dir::L, Dir::FL };
    for (int i = 0; i < 8; ++i) {
        CheckPick(45.0f * i, 0.0f, ways[i]);
        CheckPick(45.0f * i + 20.0f, 0.0f, ways[i]);
        CheckPick(45.0f * i - 20.0f, 0.0f, ways[i]);
        CheckPick(45.0f * i + 130.0f, 130.0f, ways[i]);   // relative to a turned body
        CheckPick(45.0f * i - 300.0f, -300.0f, ways[i]);  // across the wrap
    }
    CheckPick(22.0f, 0.0f, Dir::F);
    CheckPick(23.0f, 0.0f, Dir::FR);
    CheckPick(180.0f, 0.0f, Dir::B);
    CheckPick(-180.0f, 0.0f, Dir::B);

    const loco::DodgeDirectionPick p = loco::PickDodgeDirection(true, 100.0f * kDeg, 0.0f);
    Check(std::abs(p.angle - 100.0f * kDeg) < 1e-4f, "the pick keeps the exact angle, not the snapped way");
    const loco::DodgeDirectionPick q = loco::PickDodgeDirection(true, -170.0f * kDeg, 175.0f * kDeg);
    Check(std::abs(q.angle - 15.0f * kDeg) < 1e-4f, "the angle is wrapped into (-pi, pi]");
}

void TapHoldChecks() {
    loco::TapHold t;
    Check(!t.Step(false, 0.0, 0.25f).tapped, "idle: no tap");
    Check(!t.Step(true, 1.0, 0.25f).holding, "just pressed: not holding");
    Check(!t.Step(true, 1.2, 0.25f).holding, "within the tap time: not holding");
    const auto up = t.Step(false, 1.24, 0.25f);
    Check(up.tapped && !up.holding, "released within the tap time: a tap");

    Check(!t.Step(true, 2.0, 0.25f).tapped, "press: no tap");
    Check(t.Step(true, 2.3, 0.25f).holding, "held past the tap time: holding");
    const auto late = t.Step(false, 2.5, 0.25f);
    Check(!late.tapped && !late.holding, "released after a hold: no tap, no longer holding");

    Check(!t.Step(false, 3.0, 0.25f).tapped, "a second release without a press is nothing");
}

void DoubleTapChecks() {
    loco::DoubleTap d;
    const float w = 0.3f;
    Check(!d.Step(0, 1, 0.00, w), "first push forward: not yet");
    Check(!d.Step(0, 1, 0.05, w), "held: nothing");
    Check(!d.Step(0, 0, 0.10, w), "release");
    Check(d.Step(0, 1, 0.20, w), "second push forward within the window: double tap");
    Check(!d.Step(0, 0, 0.25, w), "release");
    Check(!d.Step(0, 1, 0.30, w), "a third push starts over");

    loco::DoubleTap late;
    late.Step(1, 0, 0.0, w);
    late.Step(0, 0, 0.1, w);
    Check(!late.Step(1, 0, 0.5, w), "second push after the window: not a double tap");
    late.Step(0, 0, 0.6, w);
    Check(late.Step(1, 0, 0.7, w), "but it starts a new pair");

    loco::DoubleTap turn;
    turn.Step(1, 0, 0.0, w);
    turn.Step(0, 0, 0.1, w);
    Check(!turn.Step(-1, 0, 0.2, w), "right then left: not a double tap");

    loco::DoubleTap noise;
    noise.Step(0, 1, 0.0, w);
    noise.Step(0, 0.3f, 0.05, w);   // dips but stays above the release
    Check(!noise.Step(0, 1, 0.1, w), "a dip above the release is still one push");

    loco::DoubleTap held;
    held.Step(0, 1, 0.0, w);
    Check(!held.Step(0.7f, 0.7f, 0.1, w), "sliding into a diagonal without release is no push");
    Check(!held.Step(0, 1, 0.15, w), "nor is sliding back");
}

void StaminaChecks() {
    loco::DodgeStamina flat;
    Check(loco::DodgeStaminaCost(flat, 300.0f, 1.0f) == 20.0f, "flat: the base");
    Check(loco::DodgeStaminaCost(flat, 300.0f, 1.5f) == 30.0f, "flat: times the dodge's multiplier");
    Check(loco::DodgeStaminaCost(flat, 300.0f, -1.0f) == 0.0f, "never negative");

    loco::DodgeStamina pct{ loco::DodgeCostMode::PercentOfMax, 10.0f, true };
    Check(std::abs(loco::DodgeStaminaCost(pct, 250.0f, 1.0f) - 25.0f) < 1e-4f, "percent: of the maximum");

    Check(loco::DodgeAffordable(flat, 20.0f, 20.0f), "full cost: exactly enough");
    Check(!loco::DodgeAffordable(flat, 19.0f, 20.0f), "full cost: short");
    loco::DodgeStamina souls{ loco::DodgeCostMode::Flat, 20.0f, false };
    Check(loco::DodgeAffordable(souls, 1.0f, 20.0f), "any stamina: a little is enough");
    Check(!loco::DodgeAffordable(souls, 0.0f, 20.0f), "any stamina: empty is not");
    Check(loco::DodgeAffordable(souls, 0.0f, 0.0f), "a free dodge always goes");
}

void StickChecks() {
    const auto none = loco::StickInputOf(0.05f, 0.0f, 0.0f, 0.0f, 123);
    Check(none.intensity == 0 && none.direction == 123, "stick at rest: intensity 0, the direction held");

    Check(loco::StickInputOf(0, 1, 0, 0, 0).direction == 0, "stick forward, camera and body ahead: 0");
    Check(loco::StickInputOf(1, 0, 0, 0, 0).direction == 90, "stick right: 90");
    Check(loco::StickInputOf(-1, 0, 0, 0, 0).direction == 270, "stick left: 270, never negative");
    Check(loco::StickInputOf(0, -1, 0, 0, 0).direction == 180, "stick back: 180");
    Check(loco::StickInputOf(0, 1, 90 * kDeg, 0, 0).direction == 90, "camera turned right: forward is the body's right");
    Check(loco::StickInputOf(0, 1, 0, 90 * kDeg, 0).direction == 270, "body turned right: forward is the body's left");
    Check(loco::StickInputOf(0, 1, 0, 0.1f * kDeg, 0).direction == 0, "just under 360 rounds to 0, not 360");

    Check(loco::StickInputOf(0, 0.5f, 0, 0, 0).intensity == 50, "half push: 50");
    Check(loco::StickInputOf(1, 1, 0, 0, 0).intensity == 100, "a keyboard diagonal is capped at 100");
}

}  // namespace

void RunDodgeTests() {
    DirectionChecks();
    TapHoldChecks();
    DoubleTapChecks();
    StaminaChecks();
    StickChecks();
}

}  // namespace CB::engine::tests
