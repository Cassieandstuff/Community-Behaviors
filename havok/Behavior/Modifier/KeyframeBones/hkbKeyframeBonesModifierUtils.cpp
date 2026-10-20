/// @file
/// @brief hkbKeyframeBonesModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Algorithm/Sort/hkSort.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>
#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace havok
{
    /// @note Only the first `capacity` keyframe infos are considered. The targets go out as hkQsTransforms with unit scale,
    ///       each tagged with its bone (truncated to a byte) in the sparse track's index bytes.
    void hkbKeyframeBonesModifierUtils::modify(hkbKeyframeBonesModifier& mod, hkbGeneratorOutput& inOut)
    {
        if (inOut.isValid(hkbGeneratorOutput::TRACK_KEYFRAME_TARGETS)) {
            hkbGeneratorOutput::Track track = inOut.accessTrack(hkbGeneratorOutput::TRACK_KEYFRAME_TARGETS);
            const int capacity     = track.m_header->m_capacity;
            const int numKeyframes = (mod.m_keyframeInfo.getSize() < capacity) ? mod.m_keyframeInfo.getSize() : capacity;
            if (numKeyframes > 0) {
                const int dataSize = HK_NEXT_MULTIPLE_OF(16, track.m_header->m_elementSizeBytes * capacity);
                hkLocalBuffer<hkUint8> buffer(dataSize + capacity);
                hkQsTransform* targets     = reinterpret_cast<hkQsTransform*>(&buffer[0]);
                hkInt8*        boneIndices = reinterpret_cast<hkInt8*>(targets + capacity);

                const int           mapSize = hkMap<int, int>::getSizeInBytesFor(capacity);
                hkLocalBuffer<char> mapStorage(mapSize);
                hkMap<int, int>     boneToKeyframe(&mapStorage[0], mapSize);

                hkLocalArray<int> bones(capacity);
                for (int i = 0; i < numKeyframes; i++) {
                    const hkbKeyframeBonesModifier::KeyframeInfo& info = mod.m_keyframeInfo[i];
                    if (info.m_isValid) {
                        bones.pushBackUnchecked(info.m_boneIndex);
                        boneToKeyframe.insert(info.m_boneIndex, i);
                    }
                }

                const int numBones = bones.getSize();
                if (numBones > 0) {
                    hkAlgorithm::quickSort(bones.begin(), numBones);
                    for (int i = 0; i < numBones; i++) {
                        const int                                     keyframe = boneToKeyframe.getWithDefault(bones[i], -1);
                        const hkbKeyframeBonesModifier::KeyframeInfo& info     = mod.m_keyframeInfo[keyframe];
                        hkQsTransform& target = targets[i];
                        target.m_rotation     = info.m_keyframedRotation;
                        target.m_translation  = info.m_keyframedPosition;
                        target.m_scale        = hkVector4(1.0f, 1.0f, 1.0f);
                        boneIndices[i]        = hkInt8(info.m_boneIndex);
                    }

                    hkbGeneratorOutput::TrackHeader header;
                    header.m_capacity         = hkInt16(capacity);
                    header.m_numData          = hkInt16(numBones);
                    header.m_dataOffset       = 0;
                    header.m_elementSizeBytes = sizeof(hkQsTransform);
                    header.m_onFraction       = 1.0f;
                    header.m_flags            = hkbGeneratorOutput::TRACK_FLAG_SPARSE;
                    header.m_type             = 1;
                    hkbGeneratorOutput::ConstTrack source(&header, targets);
                    hkbGeneratorOutputUtils::copySparseTrack(source, track);
                    track.m_header->m_onFraction = 1.0f;
                }
            }
        }

        if (inOut.isValid(hkbGeneratorOutput::TRACK_KEYFRAMED_RAGDOLL_BONES)) {
            hkbGeneratorOutput::Track track = inOut.accessTrack(hkbGeneratorOutput::TRACK_KEYFRAMED_RAGDOLL_BONES);
            if (track.m_header->m_numData > 0) {
                hkReal*   weights  = static_cast<hkReal*>(track.m_data);
                const int numBones = track.m_header->m_capacity;
                hkString::memSet4(weights, 0, numBones);

                const int numKeyframedBones = mod.m_keyframedBonesList ? mod.m_keyframedBonesList->m_boneIndices.getSize() : 0;
                for (int i = 0; i < numKeyframedBones; i++) {
                    const int boneIndex = mod.m_keyframedBonesList->m_boneIndices[i];
                    if (boneIndex >= 0 && boneIndex < numBones) weights[boneIndex] = 1.0f;
                }
                track.m_header->m_onFraction = 1.0f;
            }
        }
    }
} }
