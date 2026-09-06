#include "havok/anim/AnimationEmitter.h"

#include "havok/anim/SplineCompressor.h"
#include "havok/classes/Animation.h"
#include "havok/classes/Graph.h"          // hkRootLevelContainer(+NamedVariant)
#include "havok/core/PackFileSerializer.h"

#include <memory>

namespace havok::anim {

std::vector<std::uint8_t> EmitAnimationHkx(const AnimationDef& anim, int fps,
                                           const HKXHeader& header) {
    CompressedResult r = CompressAnimation(anim, fps);

    auto spline = std::make_shared<hkaSplineCompressedAnimation>();
    spline->m_type                    = 5;   // HK_SPLINE_COMPRESSED_ANIMATION
    spline->m_duration                = anim.duration;
    spline->m_numberOfTransformTracks = static_cast<std::int32_t>(anim.tracks.size());
    spline->m_numberOfFloatTracks     = static_cast<std::int32_t>(anim.floatTracks.size());
    spline->m_numFrames               = r.numFrames;
    spline->m_numBlocks               = r.numBlocks;
    spline->m_maxFramesPerBlock       = r.maxFramesPerBlock;
    spline->m_maskAndQuantizationSize = r.maskAndQuantizationSize;
    spline->m_blockDuration           = r.blockDuration;
    spline->m_blockInverseDuration    = r.blockInverseDuration;
    spline->m_frameDuration           = r.frameDuration;
    spline->m_blockOffsets            = r.blockOffsets;
    spline->m_data                    = std::move(r.data);
    spline->m_endian                  = 0;
    // floatBlockOffsets / transformOffsets / floatOffsets intentionally empty.

    // Annotation tracks (AMR root motion / MorphFace payloads) — written verbatim.
    for (const auto& at : anim.annotationTracks) {
        hkaAnnotationTrack track;
        track.m_trackName = at.trackName;
        for (const auto& a : at.annotations) {
            hkaAnnotationTrackAnnotation ann;
            ann.m_time = a.time;
            ann.m_text = a.text;
            track.m_annotations.push_back(std::move(ann));
        }
        spline->m_annotationTracks.push_back(std::move(track));
    }

    auto binding = std::make_shared<hkaAnimationBinding>();
    binding->m_originalSkeletonName = anim.skeleton;
    binding->m_animation            = spline;
    binding->m_blendHint            = 0;
    // transformTrackToBoneIndices empty == identity (track i -> bone i).

    auto container = std::make_shared<hkaAnimationContainer>();
    container->m_animations.push_back(spline);
    container->m_bindings.push_back(binding);

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "Merged Animation Container";
    nv.m_className = "hkaAnimationContainer";
    nv.m_variant   = container;
    root->m_namedVariants.push_back(nv);

    PackFileSerializer ser;
    BinaryWriterEx bw;
    ser.Serialize(root, bw, header);
    return bw.Data();
}

} // namespace havok::anim
