/// @file
/// @brief The compile-time root strip: what a stripped root holds per motion target.

#include "TestSupport.h"

#include <codec/motion/RootMotion.h>
#include <compile/AnimationCompiler.h>
#include <decompile/AnimationDecompiler.h>

#include <cmath>
#include <string>

namespace CB::engine::tests {

namespace {

/// @brief A jump that starts mid-air and turned: the root starts 100 up, yawed 90 degrees, and travels forward and down.
anim::AnimationDef MidAirJump(anim::AnimationDef::MotionTarget target) {
    anim::AnimationDef def;
    def.name          = "jump";
    def.skeleton      = "NPC Root [Root]";
    def.numFrames     = 5;
    def.frameDuration = 1.0f / 30.0f;
    def.duration      = 4.0f / 30.0f;
    def.motionTarget  = target;
    const float yaw90[4] = { 0, 0, 0.70710678f, 0.70710678f };
    anim::AnimTrackDef root{ "NPC Root [Root]", {}, {}, {} }, child{ "NPC COM [COM ]", {}, {}, {} };
    for (int f = 0; f < def.numFrames; ++f) {
        const float t = f * def.frameDuration;
        root.translation.push_back({ t, { 0, 10.0f * f, 100.0f - 5.0f * f } });
        root.rotation.push_back({ t, { yaw90[0], yaw90[1], yaw90[2], yaw90[3] } });
        child.translation.push_back({ t, { 0, 0, 60 } });
        child.rotation.push_back({ t, { 0, 0, 0, 1 } });
    }
    def.tracks = { root, child };
    return def;
}

bool Near(double a, double b) { return std::abs(a - b) < 1e-4; }

bool RootIs(const anim::AnimTrackDef& r, const float t[3], const float q[4]) {
    for (const auto& k : r.translation)
        if (!Near(k.value[0], t[0]) || !Near(k.value[1], t[1]) || !Near(k.value[2], t[2])) return false;
    for (const auto& k : r.rotation)
        if (!Near(std::abs(k.value[0] * q[0] + k.value[1] * q[1] + k.value[2] * q[2] + k.value[3] * q[3]), 1)) return false;
    return !r.translation.empty() && !r.rotation.empty();
}

void StripChecks() {
    const float origin[3] = { 0, 0, 0 }, ident[4] = { 0, 0, 0, 1 };

    auto mm        = MidAirJump(anim::AnimationDef::MotionTarget::MotionMatching);
    const auto rec = anim::ResolveRootMotion(mm);
    Check(rec.has_value(), "a moving motionMatching root still yields its record");
    Check(mm.tracks[0].translation.size() == 5 && RootIs(mm.tracks[0], origin, ident),
          "a motionMatching clip's stripped root is identity on every frame, though it starts off origin and turned");

    auto adsf = MidAirJump(anim::AnimationDef::MotionTarget::AnimationData);
    (void)anim::ResolveRootMotion(adsf);
    const float start[3] = { 0, 0, 100 }, yaw90[4] = { 0, 0, 0.70710678f, 0.70710678f };
    Check(RootIs(adsf.tracks[0], start, yaw90), "an animationData clip's stripped root keeps its frame-0 pose");

    const auto bin = anim::CompileAnimation(mm, 30);
    anim::DecodedAnimation d;
    std::string            err;
    Check(bin.ok && anim::DecodeAnimation(bin.bytes.data(), bin.bytes.size(), d, err),
          "the stripped motionMatching clip compiles and decodes: " + bin.error + err);
    bool clean = d.numFrames == 5 && d.numTracks == 2;
    for (int f = 0; clean && f < d.numFrames; ++f) {
        const auto& p = d.poses[static_cast<std::size_t>(f) * d.numTracks];
        clean = Near(p.t[0], 0) && Near(p.t[1], 0) && Near(p.t[2], 0) && Near(std::abs(p.q[3]), 1);
    }
    Check(clean, "the compiled motionMatching clip serves an identity root");
}

}  // namespace

void RunRootMotionTests(const schema::SchemaRegistry&) {
    StripChecks();
}

}  // namespace CB::engine::tests
