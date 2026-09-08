// AnimationDecompiler — the import direction for animations: a compiled
// hkaSplineCompressedAnimation .hkx -> animation.yaml that AnimationYamlLoader
// reads back. Emits one keyframe per frame per channel (the spline is degree-1,
// so per-frame samples ARE the control points); the compressor reclassifies and
// re-quantizes them on recompile.
//
// SCHEMA-NATIVE: the packfile is deserialized through havok-io's generic SchemaObject path
// (MakeSchemaFactory over the shared registry) — NOT the typed hka* classes — so a real game
// animation's unported second variant (hkMemoryResourceContainer) deserializes cleanly (it has a
// schema descriptor) instead of throwing the way the typed graph walk did. Every field is read off
// the SchemaObject's tagged FieldValue store; the spline blob feeds the shared DecodeSpline codec.
//
// ROUND-TRIP FIDELITY (an animation produced by this toolchain, recompiled):
//   * packfile structure, block layout, and TRANSLATION (BITS16) are recovered byte-for-byte.
//   * ROTATION (THREECOMP40) round-trips to within <=1 quantization step per component (imperceptible).
//   * SCALE (BITS16) and static FLOAT tracks round-trip byte-for-byte; ANNOTATION tracks are re-emitted
//     verbatim and round-trip byte-for-byte.

#include "havok/anim/AnimationDecompiler.h"

#include "havok/anim/SplineDecompressor.h"
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/PackFileDeserializer.h"

#include <havok-io/HavokIo.h>          // io::SchemaObject + MakeSchemaFactory
#include <havok-schema/HavokSchema.h>  // schema::SharedRegistry

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace havok::anim {
namespace fs = std::filesystem;

namespace {

using havok::io::SchemaObject;

std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }

// Double-quoted YAML scalar with escapes. Annotation text / track names are raw Havok strings that can
// carry embedded control bytes — some vanilla clips store annotations like "FootBack\r\n". A single-
// quoted scalar cannot represent a newline on one line (it splits the line -> "bad indentation", or a
// worse ryml fault), so use the double-quoted form, which escapes \, ", and every control byte and
// stays on ONE line — preserving the exact bytes so the annotation round-trips faithfully.
std::string dq(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\x%02X", c); out += b; }
                else out += static_cast<char>(c);
        }
    }
    out += "\"";
    return out;
}

void writeText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::int32_t rdI32(SchemaObject& o, const char* n) {
    const auto& r = o.FieldRef(n).raw; std::int32_t v = 0;
    if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v;
}
float rdF32(SchemaObject& o, const char* n) {
    const auto& r = o.FieldRef(n).raw; float v = 0.f;
    if (r.size() >= 4) std::memcpy(&v, r.data(), 4); return v;
}

std::shared_ptr<SchemaObject> asSO(const std::shared_ptr<IHavokObject>& o) {
    return std::dynamic_pointer_cast<SchemaObject>(o);
}

} // namespace

AnimDecompileResult DecompileAnimation(const std::vector<std::uint8_t>& hkx, const fs::path& dir,
                                       const std::vector<std::string>* boneNames) {
    schema::SchemaRegistry* reg = schema::SharedRegistry();
    if (!reg) return { false, "schema registry unavailable (" + schema::SharedRegistryError() + ")" };

    std::shared_ptr<SchemaObject> spline, binding;
    try {
        havok::BinaryReaderEx br(hkx);
        havok::PackFileDeserializer des;
        des.ObjectFactory = havok::io::MakeSchemaFactory(*reg);
        auto root = asSO(des.Deserialize(br));
        if (!root) return { false, "not a valid packfile root" };

        // Walk hkRootLevelContainer.namedVariants for the hkaAnimationContainer, then pull its first
        // animation (the spline) + first binding (skeleton name / track->bone map).
        std::shared_ptr<SchemaObject> container;
        for (auto& nvObj : root->FieldRef("namedVariants").objs) {
            auto nv = asSO(nvObj); if (!nv) continue;
            if (nv->FieldRef("className").str == "hkaAnimationContainer") {
                container = asSO(nv->FieldRef("variant").obj);
                break;
            }
        }
        if (!container) return { false, "no hkaAnimationContainer variant" };

        auto& anims = container->FieldRef("animations").objs;
        if (anims.empty()) return { false, "animation container has no animations" };
        spline = asSO(anims[0]);
        if (!spline) return { false, "first animation object is not a SchemaObject" };

        auto& binds = container->FieldRef("bindings").objs;
        if (!binds.empty()) binding = asSO(binds[0]);
    } catch (const std::exception& e) {
        return { false, std::string("deserialize failed: ") + e.what() };
    }

    // Only hkaSplineCompressedAnimation is decompiled. A container whose first animation is another
    // codec (interleaved/uncompressed) is reported as a skip by the caller via this exact wording.
    if (std::string(spline->ClassName()) != "hkaSplineCompressedAnimation")
        return { false, "first animation is not spline-compressed" };

    const int numTracks = rdI32(*spline, "numberOfTransformTracks");
    const int numFloat  = rdI32(*spline, "numberOfFloatTracks");
    const int numFrames = rdI32(*spline, "numFrames");
    if (numTracks <= 0 || numFrames <= 0)
        return { false, "animation has no transform tracks / frames" };

    const int   numBlocks         = rdI32(*spline, "numBlocks");
    const int   maxFramesPerBlock = rdI32(*spline, "maxFramesPerBlock");
    const int   maskAndQuant      = rdI32(*spline, "maskAndQuantizationSize");
    const float duration          = rdF32(*spline, "duration");
    const float fd                = rdF32(*spline, "frameDuration");

    const auto& dataRaw = spline->FieldRef("data").raw;
    const auto& boRaw   = spline->FieldRef("blockOffsets").raw;
    std::vector<std::uint32_t> blockOffsets(boRaw.size() / 4);
    if (!blockOffsets.empty()) std::memcpy(blockOffsets.data(), boRaw.data(), blockOffsets.size() * 4);

    std::vector<DecodedPose> poses;
    std::vector<float>       floatVals;
    std::string              warn;
    const bool ok = DecodeSpline(dataRaw.data(), dataRaw.size(), numFrames, numBlocks, maxFramesPerBlock,
                                 maskAndQuant, blockOffsets.data(), static_cast<int>(blockOffsets.size()),
                                 numTracks, numFloat, poses, &warn, numFloat > 0 ? &floatVals : nullptr);
    if (!ok) return { false, "spline decode failed (bounds/format mismatch)" };

    std::string skeleton;
    if (binding) skeleton = binding->FieldRef("originalSkeletonName").str;

    // Phase 2 (inverse membrane): read hkaAnimationBinding.transformTrackToBoneIndices and, when a
    // skeleton bone roster is supplied, resolve each track to its bone NAME via the cross membrane.
    // Until then, tracks decompile to track<N> placeholders (identity binding).
    std::vector<int> trackToBone;
    if (binding) {
        const auto& ttbRaw = binding->FieldRef("transformTrackToBoneIndices").raw;   // int16[]
        trackToBone.resize(ttbRaw.size() / 2);
        for (std::size_t i = 0; i < trackToBone.size(); ++i) {
            std::int16_t v = 0; std::memcpy(&v, ttbRaw.data() + i * 2, 2); trackToBone[i] = v;
        }
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
    y += "  duration: " + fstr(duration) + "\n";
    // Native frame timing, so a recompile reproduces the source's exact frame count/spacing instead of
    // resampling to a fixed fps (a 60fps clip stays 60fps; a sparse 2-keyframe pose stays 2 frames).
    y += "  numFrames: " + std::to_string(numFrames) + "\n";
    y += "  frameDuration: " + fstr(fd) + "\n";
    y += "  skeleton: " + skeleton + "\n";
    if (!warn.empty()) y += "  # NOTE: " + warn + ".\n";
    y += "  tracks:\n";

    for (int t = 0; t < numTracks; ++t) {
        y += "    - bone: " + trackName(t) + "\n";
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

    // Annotation tracks (AMR root motion / MorphFace payloads) — one lane per entry, verbatim.
    auto& annTracks = spline->FieldRef("annotationTracks").objs;
    if (!annTracks.empty()) {
        y += "  annotationTracks:\n";
        for (auto& atObj : annTracks) {
            auto at = asSO(atObj); if (!at) continue;
            y += "    - trackName: " + dq(at->FieldRef("trackName").str) + "\n";
            auto& anns = at->FieldRef("annotations").objs;
            if (anns.empty()) {
                y += "      annotations: []\n";
            } else {
                y += "      annotations:\n";
                for (auto& aObj : anns) {
                    auto a = asSO(aObj); if (!a) continue;
                    y += "        - { time: " + fstr(rdF32(*a, "time")) + ", text: "
                       + dq(a->FieldRef("text").str) + " }\n";
                }
            }
        }
    }

    writeText(dir / "animation.yaml", y);
    return { true, "" };
}

} // namespace havok::anim
