// AnimationDecompiler — the import direction for animations: a compiled
// hkaSplineCompressedAnimation .hkx -> animation.yaml that AnimationYamlLoader
// reads back. Emits one keyframe per frame per channel (the spline is degree-1,
// so per-frame samples ARE the control points); the compressor reclassifies and
// re-quantizes them on recompile.
//
// SCHEMA-NATIVE: the packfile is deserialized through the generic io::SchemaObject path
// (MakeSchemaFactory over the shared registry), so a real game animation's second variant
// (hkMemoryResourceContainer) deserializes cleanly (it has a schema descriptor). Every field is read off
// the SchemaObject's tagged FieldValue store; the spline blob feeds the shared DecodeSpline codec.
//
// ROUND-TRIP FIDELITY (an animation produced by this toolchain, recompiled):
//   * packfile structure, block layout, and TRANSLATION (BITS16) are recovered byte-for-byte.
//   * ROTATION (THREECOMP40) round-trips to within <=1 quantization step per component (imperceptible).
//   * SCALE (BITS16) and static FLOAT tracks round-trip byte-for-byte; ANNOTATION tracks are re-emitted
//     verbatim and round-trip byte-for-byte.

#include "decompile/AnimationDecompiler.h"

#include <codec/format/AnimDataYaml.h>      // MotionFromAmrAnnotations / EmitMotionSidecar (AMR -> motion field)
#include "codec/spline/SplineDecompressor.h"
#include <codec/motion/RootMotion.h>

#include <codec/serialization/HavokIo.h>          // io::SchemaObject + DeserializeSchema
#include <interface/reflection/HavokSchema.h>  // schema::SharedRegistry
#include <disk/Disk.h>
#include <text/Text.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace CB::engine::anim {
namespace fs = std::filesystem;

namespace {

using CB::engine::codec::io::AsSchema;
using CB::engine::codec::io::FindVariant;
using CB::engine::codec::io::SchemaObject;

} // namespace

AnimDecompileResult DecompileAnimation(const std::vector<std::uint8_t>& hkx, const fs::path& dir,
                                       const std::vector<std::string>* boneNames) {
    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) return { false, "schema registry unavailable (" + schema::SharedRegistryError() + ")" };

    std::shared_ptr<SchemaObject> spline, binding;
    try {
        auto root = CB::engine::codec::io::DeserializeSchema(hkx, *reg);
        if (!root) return { false, "not a valid packfile root" };

        // Walk hkRootLevelContainer.namedVariants for the hkaAnimationContainer, then pull its first
        // animation (the spline) + first binding (skeleton name / track->bone map).
        auto container = FindVariant(*root, "hkaAnimationContainer");
        if (!container) return { false, "no hkaAnimationContainer variant" };

        const auto& anims = container->Children("animations");
        if (anims.empty()) return { false, "animation container has no animations" };
        spline = AsSchema(anims[0]);
        if (!spline) return { false, "first animation object is not a SchemaObject" };

        const auto& binds = container->Children("bindings");
        if (!binds.empty()) binding = AsSchema(binds[0]);
    } catch (const std::exception& e) {
        return { false, std::string("deserialize failed: ") + e.what() };
    }

    // Decode the pose stream. TWO source formats are supported — the game loads both, so we must
    // decompile both: hkaSplineCompressedAnimation (the common authored form) and
    // hkaInterleavedUncompressedAnimation (raw per-frame transforms; some clips ship uncompressed, e.g.
    // a few SkyParkour climbs). Each fills `poses` (frame-major, index = frame*numTracks + track) +
    // `floatVals`; the YAML emit below is shared. Other classes (delta/quantized/reference-pose) remain
    // out of scope — the caller falls back to the loose vanilla .hkx.
    const std::string animClass = spline->ClassName();
    const int   numTracks = static_cast<int>(spline->GetInt("numberOfTransformTracks"));
    const int   numFloat  = static_cast<int>(spline->GetInt("numberOfFloatTracks"));
    const float duration  = spline->GetFloat("duration");
    if (numTracks <= 0) return { false, "animation has no transform tracks" };

    int   numFrames = 0;
    float fd        = 0.0f;
    std::vector<CB::engine::codec::spline::DecodedPose> poses;
    std::vector<float>       floatVals;
    std::string              warn;

    if (animClass == "hkaSplineCompressedAnimation") {
        numFrames = static_cast<int>(spline->GetInt("numFrames"));
        fd        = spline->GetFloat("frameDuration");
        if (numFrames <= 0) return { false, "animation has no frames" };
        const int   numBlocks         = static_cast<int>(spline->GetInt("numBlocks"));
        const int   maxFramesPerBlock = static_cast<int>(spline->GetInt("maxFramesPerBlock"));
        const int   maskAndQuant      = static_cast<int>(spline->GetInt("maskAndQuantizationSize"));
        const auto& dataRaw = spline->FieldRef("data").raw;
        const std::vector<std::uint32_t> blockOffsets = spline->GetArray<std::uint32_t>("blockOffsets");
        const bool ok = CB::engine::codec::spline::DecodeSpline(dataRaw.data(), dataRaw.size(), numFrames, numBlocks, maxFramesPerBlock,
                                     maskAndQuant, blockOffsets.data(), static_cast<int>(blockOffsets.size()),
                                     numTracks, numFloat, poses, &warn, numFloat > 0 ? &floatVals : nullptr);
        if (!ok) return { false, "spline decode failed (bounds/format mismatch)" };
    } else if (animClass == "hkaInterleavedUncompressedAnimation") {
        // transforms: hkArray<hkQsTransform>, 48 B each (translation vec4 | rotation quat xyzw | scale
        // vec4), frame-major. floats: hkArray<float>, frame-major. numFrames = transforms / numTracks;
        // frames span [0, duration] so frameDuration = duration/(numFrames-1).
        const auto&       tr     = spline->FieldRef("transforms").raw;
        const std::size_t nTrans = tr.size() / 48;
        if (nTrans == 0 || (nTrans % static_cast<std::size_t>(numTracks)) != 0)
            return { false, "interleaved: transform count not a multiple of the track count" };
        numFrames = static_cast<int>(nTrans / static_cast<std::size_t>(numTracks));
        fd        = (numFrames > 1) ? duration / static_cast<float>(numFrames - 1) : 0.0f;
        poses.resize(nTrans);
        for (std::size_t i = 0; i < nTrans; ++i) {
            float f12[12];
            std::memcpy(f12, tr.data() + i * 48, 48);
            CB::engine::codec::spline::DecodedPose& dp = poses[i];
            dp.t[0] = f12[0]; dp.t[1] = f12[1]; dp.t[2] = f12[2];                     // translation (drop w)
            dp.q[0] = f12[4]; dp.q[1] = f12[5]; dp.q[2] = f12[6]; dp.q[3] = f12[7];   // rotation x,y,z,w
            dp.s[0] = f12[8]; dp.s[1] = f12[9]; dp.s[2] = f12[10];                    // scale (drop w)
        }
        if (numFloat > 0) {
            const auto& fl = spline->FieldRef("floats").raw;
            floatVals.resize(fl.size() / 4);
            if (!floatVals.empty()) std::memcpy(floatVals.data(), fl.data(), floatVals.size() * 4);
        }
    } else {
        return { false, "unsupported animation class '" + animClass + "' (need spline or interleaved)" };
    }

    std::string skeleton;
    if (binding) skeleton = binding->Str("originalSkeletonName");

    // Phase 2 (inverse membrane): read hkaAnimationBinding.transformTrackToBoneIndices and, when a
    // skeleton bone roster is supplied, resolve each track to its bone NAME via the cross membrane.
    // Until then, tracks decompile to track<N> placeholders (identity binding).
    std::vector<int> trackToBone;
    if (binding) {
        const auto ttb = binding->GetArray<std::int16_t>("transformTrackToBoneIndices");
        trackToBone.assign(ttb.begin(), ttb.end());
    }
    auto trackName = [&](int t) -> std::string {
        if (boneNames) {
            // An EMPTY transformTrackToBoneIndices is the identity binding (track t -> bone t), so with
            // a skeleton present, fall back to the ordinal as the bone index. A NON-empty map is honored.
            const int bi = (t < static_cast<int>(trackToBone.size())) ? trackToBone[t] : t;
            if (bi >= 0 && bi < static_cast<int>(boneNames->size())) return (*boneNames)[bi];
        }
        return "track" + std::to_string(t);
    };

    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string y;
    y += "animation:\n";
    y += "  name: " + dir.filename().string() + "\n";
    y += "  duration: " + text::FormatFloat(duration) + "\n";
    // Native frame timing, so a recompile reproduces the source's exact frame count/spacing instead of
    // resampling to a fixed fps (a 60fps clip stays 60fps; a sparse 2-keyframe pose stays 2 frames).
    y += "  numFrames: " + std::to_string(numFrames) + "\n";
    y += "  frameDuration: " + text::FormatFloat(fd) + "\n";
    y += "  skeleton: " + skeleton + "\n";
    if (!warn.empty()) y += "  # NOTE: " + warn + ".\n";
    y += "  tracks:\n";

    for (int t = 0; t < numTracks; ++t) {
        y += "    - bone: " + trackName(t) + "\n";
        y += "      translation:\n";
        for (int f = 0; f < numFrames; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            y += "        - { time: " + text::FormatFloat(f * fd) + ", value: ["
               + text::FormatFloat(p.t[0]) + ", " + text::FormatFloat(p.t[1]) + ", " + text::FormatFloat(p.t[2]) + "] }\n";
        }
        y += "      rotation:\n";
        for (int f = 0; f < numFrames; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            y += "        - { time: " + text::FormatFloat(f * fd) + ", value: ["
               + text::FormatFloat(p.q[0]) + ", " + text::FormatFloat(p.q[1]) + ", " + text::FormatFloat(p.q[2]) + ", " + text::FormatFloat(p.q[3]) + "] }\n";
        }
        bool anyScale = false;
        for (int f = 0; f < numFrames && !anyScale; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            if (p.s[0] != 1.f || p.s[1] != 1.f || p.s[2] != 1.f) anyScale = true;
        }
        if (anyScale) {
            y += "      scale:\n";
            for (int f = 0; f < numFrames; ++f) {
                const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
                y += "        - { time: " + text::FormatFloat(f * fd) + ", value: ["
                   + text::FormatFloat(p.s[0]) + ", " + text::FormatFloat(p.s[1]) + ", " + text::FormatFloat(p.s[2]) + "] }\n";
            }
        }
    }

    for (int ft = 0; ft < numFloat; ++ft) {
        if (ft == 0) y += "  floatTracks:\n";
        y += "    - name: float" + std::to_string(ft) + "\n";
        bool varying = false;
        for (int f = 1; f < numFrames && !varying; ++f)
            if (floatVals[static_cast<std::size_t>(f) * numFloat + ft] !=
                floatVals[static_cast<std::size_t>(ft)]) varying = true;
        y += "      keyframes:\n";
        const int last = varying ? numFrames : 1;
        for (int f = 0; f < last; ++f)
            y += "        - { time: " + text::FormatFloat(f * fd) + ", value: "
               + text::FormatFloat(floatVals[static_cast<std::size_t>(f) * numFloat + ft]) + " }\n";
    }

    std::optional<CB::engine::animdata::MotionRecord> amrMotion;
    // Annotation tracks (AMR root motion / MorphFace payloads) — one lane per entry, verbatim.
    /// @note Always double-quoted: vanilla annotations carry control bytes (e.g. "FootBack\r\n") a single-quoted
    ///       scalar can't hold on one line, and the escapes keep the exact bytes round-tripping.
    const auto& annTracks = spline->Children("annotationTracks");
    if (!annTracks.empty()) {
        y += "  annotationTracks:\n";
        for (auto& atObj : annTracks) {
            auto at = AsSchema(atObj); if (!at) continue;
            y += "    - trackName: " + text::QuoteDouble(at->Str("trackName")) + "\n";
            const auto& anns = at->Children("annotations");
            if (anns.empty()) {
                y += "      annotations: []\n";
            } else {
                y += "      annotations:\n";
                for (auto& aObj : anns) {
                    auto a = AsSchema(aObj); if (!a) continue;
                    y += "        - { time: " + text::FormatFloat(a->GetFloat("time")) + ", text: "
                       + text::QuoteDouble(a->Str("text")) + " }\n";
                }
            }
        }

        /// @note AMR annotations (animmotion/animrotation) are the clip's actor motion; vanilla clips carry none (the
        ///       base reunion supplies theirs).
        std::vector<std::pair<float, std::string>> allAnns;
        for (auto& atObj : annTracks) {
            auto at = AsSchema(atObj); if (!at) continue;
            for (auto& aObj : at->Children("annotations")) {
                auto a = AsSchema(aObj); if (!a) continue;
                allAnns.emplace_back(a->GetFloat("time"), a->Str("text"));
            }
        }
        amrMotion = CB::engine::animdata::MotionFromAmrAnnotations(allAnns, text::FormatFloat(duration));
    }

    /// @note The unit's motion form: AMR motion folds onto the root where it round-trips (else stays explicit); a
    ///       moving root with no motion is marked visual-only.
    CB::engine::anim::FinalizeUnitMotion(y, amrMotion);
    std::string err;
    if (!disk::WriteFile(dir / "animation.yaml", y, {}, &err)) return { false, err };
    return { true, "" };
}

} // namespace CB::engine::anim
