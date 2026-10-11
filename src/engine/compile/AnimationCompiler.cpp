#include "compile/AnimationCompiler.h"

#include "codec/spline/SplineCompressor.h"     // CB::engine::codec::spline::CompressAnimation (the shared spline codec)
#include <codec/serialization/packfile/PackFileSerializer.h>    // the ONE serializer (io::SchemaObject)
#include <interface/linker/Membrane.h>       // BoneMembrane::resolveTrackRef (the one name<->index codec)

#include <codec/serialization/HavokIo.h>                  // io::SchemaObject
#include <interface/reflection/HavokSchema.h>          // schema::SchemaRegistry + SharedRegistry()

#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace CB::engine::anim {
using namespace CB::engine::codec;
using namespace CB::engine::common;

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
    CB::engine::codec::spline::CompressedResult r = CB::engine::codec::spline::CompressAnimation(anim, fps);

    auto spline = mkA(reg, "hkaSplineCompressedAnimation"); if (!spline) return nullptr;
    spline->SetInt("type", 5);   // HK_SPLINE_COMPRESSED_ANIMATION (hkaAnimation base)
    spline->SetFloat("duration", anim.duration);
    spline->SetInt("numberOfTransformTracks", static_cast<std::int32_t>(anim.tracks.size()));
    spline->SetInt("numberOfFloatTracks", static_cast<std::int32_t>(anim.floatTracks.size()));
    spline->SetInt("numFrames", r.numFrames);
    spline->SetInt("numBlocks", r.numBlocks);
    spline->SetInt("maxFramesPerBlock", r.maxFramesPerBlock);
    spline->SetInt("maskAndQuantizationSize", r.maskAndQuantizationSize);
    spline->SetFloat("blockDuration", r.blockDuration);
    spline->SetFloat("blockInverseDuration", r.blockInverseDuration);
    spline->SetFloat("frameDuration", r.frameDuration);
    spline->SetArray("blockOffsets", r.blockOffsets);
    spline->SetArray("floatBlockOffsets", r.floatBlockOffsets);
    spline->FieldRef("data").raw = std::move(r.data);   // the compressed spline blob (byte array)
    spline->SetInt("endian", 0);
    // transformOffsets / floatOffsets intentionally empty; extractedMotion null.
    { auto& tracks = spline->FieldRef("annotationTracks").objs;
      for (const auto& at : anim.annotationTracks) {
          auto track = mkA(reg, "hkaAnnotationTrack"); if (!track) return nullptr;
          track->FieldRef("trackName").str = at.trackName;
          auto& anns = track->FieldRef("annotations").objs;
          for (const auto& a : at.annotations) {
              auto ann = mkA(reg, "hkaAnnotationTrackAnnotation"); if (!ann) return nullptr;
              ann->SetFloat("time", a.time);
              ann->FieldRef("text").str = a.text;
              anns.push_back(ann);
          }
          tracks.push_back(track);
      }
    }

    auto binding = mkA(reg, "hkaAnimationBinding"); if (!binding) return nullptr;
    binding->FieldRef("originalSkeletonName").str = anim.skeleton;
    binding->FieldRef("animation").obj            = spline;
    binding->SetInt("blendHint", 0);

    // transformTrackToBoneIndices — the INVERSE membrane. Resolve every track's authored bone
    // reference to a skeleton bone index (BoneMembrane::resolveTrackRef: a "track<N>" placeholder ->
    // identity N, a bone NAME -> its served-skeleton index). Emit the int16 map only when it is NOT
    // pure identity; a pure-identity map stays an empty array, which is exactly what vanilla ships for
    // a clip whose tracks are its skeleton's bones in order (keeps that case byte-identical).
    // floatTrackToFloatSlotIndices left empty == identity.
    {
        static const std::vector<std::string> kNoBones;
        const auto& bones = boneNames ? *boneNames : kNoBones;
        // Resolve each track's bone by NAME through the one name<->index codec. Bones are matched
        // case-sensitively (Havok is case-sensitive), exactly as the former linear scan did.
        namespace linker = CB::engine::linker;
        const linker::Linker       boneL = linker::Linker::OfOrderedNames(bones);
        const linker::BoneMembrane boneM{ &boneL };
        bool identity = true;
        std::vector<std::int16_t> ttb; ttb.reserve(anim.tracks.size());
        for (std::size_t t = 0; t < anim.tracks.size(); ++t) {
            const int bi = boneM.resolveTrackRef(anim.tracks[t].bone, static_cast<int>(t));
            if (bi != static_cast<int>(t)) identity = false;
            ttb.push_back(static_cast<std::int16_t>(bi));
        }
        if (!identity) binding->SetArray("transformTrackToBoneIndices", ttb);
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

} // namespace CB::engine::anim
