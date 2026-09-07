// AnimationDecompiler — the import direction for animations: a compiled
// hkaSplineCompressedAnimation .hkx -> animation.yaml that AnimationYamlLoader
// reads back. Emits one keyframe per frame per channel (the spline is degree-1,
// so per-frame samples ARE the control points); the compressor reclassifies and
// re-quantizes them on recompile.
//
// ROUND-TRIP FIDELITY (an animation produced by this toolchain, recompiled):
//   * packfile structure, block layout, and TRANSLATION (BITS16) are recovered
//     byte-for-byte — dequant→requant is exactly idempotent for the vector codec.
//   * ROTATION (THREECOMP40) round-trips to within ≤1 quantization step per
//     component: dequantizing, re-normalizing the quaternion, and re-quantizing
//     is not float-exact at the LSB, so a fraction of rotation bytes drift by 1.
//     The pose error is ~1/4095 of a unit component (imperceptible). Making it
//     bit-exact would require changing SplineCompressor, which is deliberately
//     pinned byte-exact to the reference encoder — so it is left as-is.
//
//   * SCALE (BITS16, same codec as translation) and FLOAT TRACKS (static f32 —
//     the only float form in a large real corpus) round-trip byte-for-byte.
//   * ANNOTATION tracks (AMR root-motion / MorphFace payloads) are re-emitted
//     verbatim and round-trip byte-for-byte.
//
// The one non-exact channel is rotation (the ≤1-LSB case above). Dynamic float
// channels (none seen in the wild) decode via the encoder's 1-component spline —
// self-consistent through this toolchain but unvalidated against Havok.

#include "havok/sct/CharacterDecompiler.h"   // DecompileResult

#include "havok/anim/SplineDecompressor.h"
#include "havok/classes/Animation.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace havok::sct {
namespace fs = std::filesystem;

namespace {

std::string fstr(float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

// Single-quoted YAML scalar ('' escapes an embedded quote) — annotation text and
// track names carry '|', '[', ']', '.' and must survive verbatim.
std::string q(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "''"; else out += c; }
    out += "'";
    return out;
}

void writeText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

} // namespace

DecompileResult DecompileAnimation(const std::shared_ptr<hkaAnimationContainer>& c,
                                   const fs::path& dir) {
    if (!c || c->m_animations.empty())
        return { false, "animation container has no animations", "animation" };

    auto spline = std::dynamic_pointer_cast<hkaSplineCompressedAnimation>(c->m_animations[0]);
    if (!spline)
        return { false, "first animation is not spline-compressed (only hkaSplineCompressedAnimation is decompiled)", "animation" };

    const int numTracks = spline->m_numberOfTransformTracks;
    const int numFloat   = spline->m_numberOfFloatTracks;
    const int numFrames  = spline->m_numFrames;
    if (numTracks <= 0 || numFrames <= 0)
        return { false, "animation has no transform tracks / frames", "animation" };

    std::vector<anim::DecodedPose> poses;
    std::vector<float> floatVals;
    std::string warn;
    const bool ok = anim::DecodeSpline(
        spline->m_data.data(), spline->m_data.size(),
        numFrames, spline->m_numBlocks, spline->m_maxFramesPerBlock,
        spline->m_maskAndQuantizationSize,
        spline->m_blockOffsets.data(), static_cast<int>(spline->m_blockOffsets.size()),
        numTracks, numFloat, poses, &warn, numFloat > 0 ? &floatVals : nullptr);
    if (!ok)
        return { false, "spline decode failed (bounds/format mismatch)", "animation" };

    std::string skeleton;
    if (!c->m_bindings.empty() && c->m_bindings[0])
        skeleton = c->m_bindings[0]->m_originalSkeletonName;

    std::error_code ec;
    fs::create_directories(dir, ec);

    std::string y;
    y += "animation:\n";
    // The .hkx does not store the authored animation name (the named-variant is a
    // fixed container label), so it is not recoverable; use the output dir name.
    y += "  name: " + dir.filename().string() + "\n";
    y += "  duration: " + fstr(spline->m_duration) + "\n";
    y += "  skeleton: " + skeleton + "\n";
    if (!warn.empty())
        y += "  # NOTE: " + warn + ".\n";
    y += "  tracks:\n";

    const float fd = spline->m_frameDuration;
    for (int t = 0; t < numTracks; ++t) {
        y += "    - bone: track" + std::to_string(t) + "\n";
        y += "      translation:\n";
        for (int f = 0; f < numFrames; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            y += "        - { time: " + fstr(f * fd) + ", value: ["
               + fstr(p.t[0]) + ", " + fstr(p.t[1]) + ", " + fstr(p.t[2]) + "] }\n";
        }
        y += "      rotation:\n";
        for (int f = 0; f < numFrames; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            y += "        - { time: " + fstr(f * fd) + ", value: ["
               + fstr(p.q[0]) + ", " + fstr(p.q[1]) + ", " + fstr(p.q[2]) + ", " + fstr(p.q[3]) + "] }\n";
        }
        // Scale is emitted only when a track is non-identity: an all-(1,1,1) track
        // decodes to the pose default and the compressor reclassifies it Identity,
        // so omitting it round-trips identically while keeping the file clean.
        bool anyScale = false;
        for (int f = 0; f < numFrames && !anyScale; ++f) {
            const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
            if (p.s[0] != 1.f || p.s[1] != 1.f || p.s[2] != 1.f) anyScale = true;
        }
        if (anyScale) {
            y += "      scale:\n";
            for (int f = 0; f < numFrames; ++f) {
                const auto& p = poses[static_cast<std::size_t>(f) * numTracks + t];
                y += "        - { time: " + fstr(f * fd) + ", value: ["
                   + fstr(p.s[0]) + ", " + fstr(p.s[1]) + ", " + fstr(p.s[2]) + "] }\n";
            }
        }
    }

    // Float tracks — one lane per float slot. Names are not stored in the .hkx
    // (they live on the skeleton/behavior), so placeholder names are used; they
    // don't affect the compiled bytes. A constant track collapses to one keyframe
    // (the compressor reclassifies it Static either way); a varying one is dense.
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
            y += "        - { time: " + fstr(f * fd) + ", value: "
               + fstr(floatVals[static_cast<std::size_t>(f) * numFloat + ft]) + " }\n";
    }

    // Annotation tracks (AMR root motion / MorphFace payloads) — one lane per
    // entry in m_annotationTracks, emitted verbatim so they round-trip.
    if (!spline->m_annotationTracks.empty()) {
        y += "  annotationTracks:\n";
        for (const auto& at : spline->m_annotationTracks) {
            y += "    - trackName: " + q(at.m_trackName) + "\n";
            if (at.m_annotations.empty()) {
                y += "      annotations: []\n";
            } else {
                y += "      annotations:\n";
                for (const auto& a : at.m_annotations)
                    y += "        - { time: " + fstr(a.m_time) + ", text: " + q(a.m_text) + " }\n";
            }
        }
    }

    writeText(dir / "animation.yaml", y);
    return { true, "", "animation" };
}

} // namespace havok::sct
