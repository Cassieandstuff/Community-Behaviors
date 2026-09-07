#pragma once

// AnimDataProbe — a gated diagnostic/prototype for the in-engine animationdata BYPASS
// (see memory br-runtime-is-the-feature). Instead of writing a merged cache text file and
// redirecting the loader's open, the end goal is to hand clip metadata to the engine's
// in-memory AnimationClipDataSingleton directly. This probe is step 1: prove we can READ
// the singleton via the game's own GetClipInformation (validating our vendored struct
// layout against known ground truth), then WRITE a field the engine consumes.
//
// Both stages are OFF unless an explicit marker file is present under Data\community_behaviors\:
//   probe_animdata.txt   -> read-back a known vanilla clip and log its fields
//   inject_animdata.txt  -> also write a test clip's motionSpeed (write proof)
// Nothing runs on a normal launch. Crash-sensitive path — opt-in only.

namespace CB::animprobe {

    // Read (and, if the inject marker is present, write) the AnimationClipDataSingleton via
    // the game's own accessors. Safe no-op if the singleton isn't built yet or no marker.
    // Call on a message where the singleton is populated (kDataLoaded / kPostLoadGame).
    void ProbeAnimClipData();

}  // namespace CB::animprobe
