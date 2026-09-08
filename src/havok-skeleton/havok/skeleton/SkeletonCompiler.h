#pragma once
// SkeletonCompiler — SkeletonData → validated skeleton .hkx, SCHEMA-NATIVE. The write half of the
// skeleton codec and the mirror of SkeletonImport: scatter the neutral per-bone data (and derive the
// full ragdoll from the authored physics knobs) into an io::SchemaObject graph via the Havok/ class
// descriptors, then serialize with the shared PackFileSerializer. No typed hka*/hkp* classes, no
// havok-core — the derivation math (SkeletonMath) and the field values are ported from havok-core's
// byte-exact compiler, so the emit stays byte-identical (gated by havok-core-cli skeleton-full-parity).

#include "havok/skeleton/SkeletonData.h"
#include "havok/core/PackFileTypes.h"   // HKXHeader (havok-framing)

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace havok::skeleton {

// Result of a skeleton compile. Same shape as havok::anim::AnimCompileResult (kept local so the module
// carries no havok-core dependency); callers read .ok / .error / .bytes.
struct SkeletonCompileResult {
    bool                      ok = false;
    std::string               error;   // populated when !ok
    std::vector<std::uint8_t> bytes;   // the packfile (when ok)
};

// Anim-skeleton only: the hkaSkeleton (bones + parent indices + reference pose) inside a
// hkaAnimationContainer. `data.bones` must be in final index order (parent < child).
SkeletonCompileResult CompileSkeleton(const SkeletonData& data,
                                      const HKXHeader& header = HKXHeader::SkyrimSE());

SkeletonCompileResult CompileSkeletonToFile(const SkeletonData&          data,
                                            const std::filesystem::path& outPath,
                                            bool                         validate = true,
                                            const HKXHeader&             header = HKXHeader::SkyrimSE());

// FUSED compile: the full 6-variant skeleton.hkx from one SkeletonData carrying per-bone physics
// (+ optional bumper) — anim + derived ragdoll skeletons, rigid bodies, constraints, ragdoll instance,
// physics system, the 2 anim↔ragdoll mappers, and the resource tree. Everything but the authored knobs
// (mass / capsule / joint) derives from the bone geometry. No physics bones → anim-skeleton-only
// (the first-person rig), matching vanilla.
SkeletonCompileResult CompileSkeletonFull(const SkeletonData& data,
                                          const HKXHeader& header = HKXHeader::SkyrimSE());

// Compile-over-base: read `baseBytes` (a complete skeleton.hkx) as its SchemaObject graph, rebuild ONLY
// the animation skeleton's bone arrays from `animBones`, and carry everything else (ragdoll skeleton,
// physics, mappers, resource tree, float slots, local frames) untouched. Re-serialized with the base's
// own header, so an IDENTITY `animBones` reproduces the base byte-for-byte. The serve path for existing
// content (vanilla + XPMSSE + bone-add plugins): physics carried, bone list authored/merged.
SkeletonCompileResult CompileSkeletonOverBase(const SkeletonData&              animBones,
                                              const std::vector<std::uint8_t>& baseBytes);

} // namespace havok::skeleton
