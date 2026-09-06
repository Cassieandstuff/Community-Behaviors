#include "havok/sct/AnimationCompiler.h"

#include "havok/anim/AnimationEmitter.h"
#include "havok/anim/SplineCompressor.h"      // anim::CompressAnimation (the shared spline codec)
#include "havok/core/PackFileSerializer.h"
#include "havok/sct/HavokFile.h"

#include "SchemaCompilerState.h"              // shared data-driven-compiler toggle + schema registry

#include <havok-io/HavokIo.h>                 // io::SchemaObject
#include <havok-schema/HavokSchema.h>         // schema::SchemaRegistry

#include <cstring>
#include <exception>
#include <memory>

namespace havok::sct {

namespace {

// Data-driven (schema) animation emit — the equivalent of anim::EmitAnimationHkx, building an
// io::SchemaObject graph (hkaSplineCompressedAnimation + annotation tracks + hkaAnimationBinding +
// hkaAnimationContainer, wrapped in hkRootLevelContainer) via the Havok/ descriptors instead of the
// typed hka* classes. The spline codec (anim::CompressAnimation) is shared and runs here exactly as in
// the typed path — only the object assembly differs. Byte-gated == typed. Lives in havok-core beside
// the codec it must call; the whole anim/ subsystem can migrate to havok-model as a unit later.
std::shared_ptr<io::SchemaObject> mkA(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<io::SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

std::shared_ptr<io::SchemaObject> AssembleAnimation(const anim::AnimationDef& anim, int fps,
                                                    const schema::SchemaRegistry& reg) {
    anim::CompressedResult r = anim::CompressAnimation(anim, fps);
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
    // transformTrackToBoneIndices / floatTrackToFloatSlotIndices empty == identity.

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

CompileResult CompileAnimation(const anim::AnimationDef& anim, int fps, const HKXHeader& header) {
    CompileResult r;
    try {
        // Data-driven path (opt-in): assemble via the Havok/ descriptors, proven byte-identical to the
        // typed EmitAnimationHkx below. Any failure falls through to typed.
        if (SchemaCompileEnabled()) {
            if (schema::SchemaRegistry* reg = SchemaCompileRegistry()) {
                try {
                    if (auto sroot = AssembleAnimation(anim, fps, *reg)) {
                        PackFileSerializer ser;
                        BinaryWriterEx bw;
                        ser.Serialize(sroot, bw, header);
                        r.bytes = bw.Data();
                        r.ok    = true;
                        return r;
                    }
                } catch (const std::exception&) { /* fall through to the typed emitter */ }
            }
        }
        r.bytes = anim::EmitAnimationHkx(anim, fps, header);
        r.ok    = true;
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = e.what();
        r.bytes.clear();
    }
    return r;
}

CompileResult CompileAnimationToFile(const anim::AnimationDef& anim,
                                     const std::filesystem::path& outPath,
                                     int fps, const HKXHeader& header) {
    CompileResult r = CompileAnimation(anim, fps, header);
    if (!r.ok) return r;
    std::string err;
    if (!WriteHavokFile(outPath, r.bytes, &err)) { r.ok = false; r.error = err; }
    return r;
}

} // namespace havok::sct
