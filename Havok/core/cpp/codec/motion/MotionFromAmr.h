#pragma once
// MotionFromAmr.h — AMR (Animation Motion Revolution) annotations -> a CB MotionRecord.
// codec/motion's first tenant (org-pass firesale phase 3f-2): the reference-frame <-> motion transform
// concern, carved out of AnimDataYaml. AMR encodes root motion as animation annotations
// ("animmotion <x> <y> <z>", "animrotation <yawDeg>"); this translates them to the motion field so the
// compiler bakes motion into the adsf and the game's NATIVE (unhooked) motion read serves it.
#include <interface/AnimationData.h>   // MotionRecord

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace havok::animdata {

std::optional<MotionRecord> MotionFromAmrAnnotations(
    const std::vector<std::pair<float, std::string>>& annotations, const std::string& duration);

}  // namespace havok::animdata
