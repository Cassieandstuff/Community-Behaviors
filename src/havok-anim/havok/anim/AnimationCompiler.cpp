#include "havok/anim/AnimationCompiler.h"

#include "havok/anim/SplineCompressor.h"      // anim::CompressAnimation (the shared spline codec)
#include "havok/core/PackFileSerializer.h"    // havok-framing — the ONE serializer (io::SchemaObject)
#include "havok/cross/Cross.h"                // havok::cross::trackBoneRef (the inverse membrane)

#include <havok-io/HavokIo.h>                  // io::SchemaObject
#include <havok-schema/HavokSchema.h>          // schema::SchemaRegistry + SharedRegistry()

#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace havok::anim {

namespace {

// Data-driven (schema) animation emit — builds an io::SchemaObject graph (hkaSplineCompressedAnimation
// + annotation tracks + hkaAnimationBinding + hkaAnimationContainer, wrapped in hkRootLevelContainer)
// via the Havok/ descriptors. The spline codec (anim::CompressAnimation) is shared and runs here; only
// the object assembly is schema-driven. Byte-gated == the retired typed emitter (animation-schema-check).
std::shared_ptr<io::SchemaObject> mkA(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<io::SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

std::shared_ptr<io::SchemaObject> AssembleAnimation(const AnimationDef& anim, int fps,
                                                    const schema::SchemaRegistry& reg,
                                                    const std::vector<std::string>* boneNames) {
    CompressedResult r = CompressAnimation(anim, fps);
    auto i32b = [](std::int32_t v) { std::uint8_t b[4]; std::memcpy(b, &v, 4); return std::vector<std::uint8_t>(b, b + 4); };
    auto f32b = [](float v)        { std::uint8_t b[4]; std::memcpy(b, &v, 4); return std::vector<std::uint8_t>(b, b + 4); };
    auto u32arr = [](const std::vector<std::uint32_t>& a) { std::vector<std::uint8_t> o; o.reserve(a.size() * 4);
        for (std::uint32_t v : a) { std::uint8_t b[4]; std::memcpy(b, &v, 4); o.insert(o.end(), b, b + 4); } return o; };

    auto spline = mkA(reg, "hkaSplineCompressedAnimation"); if (!spline) return nullptr;
    spline->FieldRef("type").raw                    = i32b(5);   // HK_SPLINE_COMPRESSED_ANIMATION (hkaAnimation base)
    spline->FieldRef("duration").raw                = f32b(anim.duration);
    spline->FieldRef("numberOfTransformTracks").raw = i32b(static_cast<std::int32_t>(anim.tracks.size()));
    spline->FieldRef("numberOfFloatTracks").raw     = i32b(static_cast<std::int32_t>(anim.floatTracks.size()));
    spline->FieldRef("numFrames").raw               = i32b(r.numFrames);
    spline->FieldRef("numBlocks").raw               = i32b(r.numBlocks);
    spline->FieldRef("maxFramesPerBlock").raw       = i32b(r.maxFramesPerBlock);
    spline->FieldRef("maskAndQuantizationSize").raw = i32b(r.maskAndQuantizationSize);
    spline->FieldRef("blockDuration").raw           = f32b(r.blockDuration);
    spline->FieldRef("blockInverseDuration").raw    = f32b(r.blockInverseDuration);
    spline->FieldRef("frameDuration").raw           = f32b(r.frameDuration);
    spline->FieldRef("blockOffsets").raw            = u32arr(r.blockOffsets);
    spline->FieldRef("data").raw                    = std::move(r.data);   // the compressed spline blob (byte array)
    spline->FieldRef("endian").raw                  = i32b(0);
    // floatBlockOffsets / transformOffsets / floatOffsets intentionally empty; extractedMotion null.
    { auto& tracks = spline->FieldRef("annotationTracks").objs;
      for (const auto& at : anim.annotationTracks) {
          auto track = mkA(reg, "hkaAnnotationTrack"); if (!track) return nullptr;
          track->FieldRef("trackName").str = at.trackName;
          auto& anns = track->FieldRef("annotations").objs;
          for (const auto& a : at.annotations) {
              auto ann = mkA(reg, "hkaAnnotationTrackAnnotation"); if (!ann) return nullptr;
              ann->FieldRef("time").raw = f32b(a.time);
              ann->FieldRef("text").str = a.text;
              anns.push_back(ann);
          }
          tracks.push_back(track);
      }
    }

    auto binding = mkA(reg, "hkaAnimationBinding"); if (!binding) return nullptr;
    binding->FieldRef("originalSkeletonName").str = anim.skeleton;
    binding->FieldRef("animation").obj            = spline;
    binding->FieldRef("blendHint").raw            = { 0 };

    // transformTrackToBoneIndices — the INVERSE membrane. Resolve every track's authored bone
    // reference to a skeleton bone index (havok::cross::trackBoneRef: a "track<N>" placeholder ->
    // identity N, a bone NAME -> its served-skeleton index). Emit the int16 map only when it is NOT
    // pure identity; a pure-identity map stays an empty array, which is exactly what vanilla ships for
    // a clip whose tracks are its skeleton's bones in order (keeps that case byte-identical).
    // floatTrackToFloatSlotIndices left empty == identity.
    {
        static const std::vector<std::string> kNoBones;
        const auto& bones = boneNames ? *boneNames : kNoBones;
        bool identity = true;
        std::vector<std::uint8_t> ttb; ttb.reserve(anim.tracks.size() * 2);
        for (std::size_t t = 0; t < anim.tracks.size(); ++t) {
            const int bi = havok::cross::trackBoneRef(anim.tracks[t].bone, static_cast<int>(t), bones);
            if (bi != static_cast<int>(t)) identity = false;
            const std::int16_t v = static_cast<std::int16_t>(bi);
            std::uint8_t b[2]; std::memcpy(b, &v, 2); ttb.insert(ttb.end(), b, b + 2);
        }
        if (!identity) binding->FieldRef("transformTrackToBoneIndices").raw = std::move(ttb);
    }

    auto container = mkA(reg, "hkaAnimationContainer"); if (!container) return nullptr;
    container->FieldRef("animations").objs.push_back(spline);
    container->FieldRef("bindings").objs.push_back(binding);

    auto root = mkA(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto nv   = mkA(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str      = "Merged Animation Container";
    nv->FieldRef("className").str  = "hkaAnimationContainer";
    nv->FieldRef("variant").obj    = container;
    root->FieldRef("namedVariants").objs.push_back(nv);
    return root;
}

}  // namespace

AnimCompileResult CompileAnimation(const AnimationDef& anim, int fps, const HKXHeader& header,
                                   const std::vector<std::string>* boneNames) {
    AnimCompileResult r;
    try {
        schema::SchemaRegistry* reg = schema::SharedRegistry();
        if (!reg) {
            r.error = "schema registry unavailable (" + schema::SharedRegistryError() + ")";
            return r;
        }
        auto sroot = AssembleAnimation(anim, fps, *reg, boneNames);
        if (!sroot) { r.error = "animation assembly failed (missing schema class descriptor)"; return r; }
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(sroot, bw, header);
        r.bytes = bw.Data();
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

AnimCompileResult CompileAnimationToFile(const AnimationDef& anim,
                                         const std::filesystem::path& outPath,
                                         int fps, const HKXHeader& header,
                                         const std::vector<std::string>* boneNames) {
    AnimCompileResult r = CompileAnimation(anim, fps, header, boneNames);
    if (!r.ok) return r;
    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f) { r.ok = false; r.error = "cannot open for writing: " + outPath.string(); return r; }
    if (!r.bytes.empty())
        f.write(reinterpret_cast<const char*>(r.bytes.data()), static_cast<std::streamsize>(r.bytes.size()));
    if (!f.good()) { r.ok = false; r.error = "write failed: " + outPath.string(); }
    return r;
}

} // namespace havok::anim
