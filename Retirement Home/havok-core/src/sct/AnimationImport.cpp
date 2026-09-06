#include "havok/sct/AnimationImport.h"

#include "havok/classes/Animation.h"
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/PackFileDeserializer.h"

#include <exception>

namespace havok::sct {

namespace {

void CopyCommon(const hkaAnimation& src, AnimationData& out)
{
    out.duration           = src.m_duration;
    out.numTransformTracks = src.m_numberOfTransformTracks;
    out.numFloatTracks     = src.m_numberOfFloatTracks;

    out.annotationTracks.reserve(src.m_annotationTracks.size());
    for (const hkaAnnotationTrack& t : src.m_annotationTracks) {
        AnimationAnnotationTrack dst;
        dst.trackName = t.m_trackName;
        dst.annotations.reserve(t.m_annotations.size());
        for (const hkaAnnotationTrackAnnotation& a : t.m_annotations)
            dst.annotations.push_back({ a.m_time, a.m_text });
        out.annotationTracks.push_back(std::move(dst));
    }
}

} // namespace

bool LoadAnimationFromHkx(const std::uint8_t* data, std::size_t size,
                          AnimationData& out,
                          std::string* err)
{
    out = AnimationData{};

    if (data == nullptr || size == 0) {
        if (err) *err = "empty HKX buffer";
        return false;
    }

    try {
        std::vector<std::uint8_t> bytes(data, data + size);

        PackFileDeserializer des;
        BinaryReaderEx br(/*bigEndian*/ false, /*uSizeLong*/ true, bytes);
        des.DeserializePartially(br);

        BinaryReaderEx dataReader(des._header.Endian == 0,
                                  des._header.PointerSize == 8,
                                  des.DataSectionBytes());

        // Spline first: it is what Skyrim actually ships, and a file carrying
        // both would want the compressed one.
        const auto splines = des.ConstructAllOfClass(dataReader,
                                                     "hkaSplineCompressedAnimation");
        const auto interleaved = des.ConstructAllOfClass(
            dataReader, "hkaInterleavedUncompressedAnimation");

        if (!splines.empty()) {
            const auto a = std::dynamic_pointer_cast<hkaSplineCompressedAnimation>(splines.front());
            if (!a) { if (err) *err = "spline animation failed to cast"; return false; }

            out.isSpline                = true;
            CopyCommon(*a, out);
            out.numFrames               = a->m_numFrames;
            out.numBlocks               = a->m_numBlocks;
            out.maxFramesPerBlock       = a->m_maxFramesPerBlock;
            out.maskAndQuantizationSize = a->m_maskAndQuantizationSize;
            out.blockOffsets            = a->m_blockOffsets;
            out.transformOffsets        = a->m_transformOffsets;
            out.data                    = a->m_data;
        }
        else if (!interleaved.empty()) {
            const auto a = std::dynamic_pointer_cast<hkaInterleavedUncompressedAnimation>(
                interleaved.front());
            if (!a) { if (err) *err = "interleaved animation failed to cast"; return false; }

            out.isSpline   = false;
            CopyCommon(*a, out);
            out.transforms = a->m_transforms;

            // Frame count is implied by the array length rather than stored.
            if (out.numTransformTracks > 0)
                out.numFrames = static_cast<int>(out.transforms.size()) / out.numTransformTracks;
        }
        else {
            if (err) *err = "no hkaSplineCompressedAnimation or "
                            "hkaInterleavedUncompressedAnimation in file";
            return false;
        }

        // Binding is optional; identity is the documented fallback.
        const auto bindings = des.ConstructAllOfClass(dataReader, "hkaAnimationBinding");
        if (!bindings.empty()) {
            if (const auto b = std::dynamic_pointer_cast<hkaAnimationBinding>(bindings.front()))
                out.transformTrackToBoneIndices = b->m_transformTrackToBoneIndices;
        }

        return true;
    }
    catch (const std::exception& ex) {
        if (err) *err = ex.what();
        out = AnimationData{};
        return false;
    }
}

} // namespace havok::sct
