/// @file
/// @brief hkbRigidBodyRagdollControlsModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/RigidBodyRagdollControls/hkbRigidBodyRagdollControlsModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>
#include <Common/Base/Container/String/hkString.h>

namespace CB { namespace havok
{
    void hkbRigidBodyRagdollControlsModifierUtils::modify(hkbRigidBodyRagdollControlsModifier& mod, hkbGeneratorOutput& inOut)
    {
        if (inOut.isValid(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_CONTROLS)) {
            hkbGeneratorOutput::Track track = inOut.accessTrack(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_CONTROLS);
            const int numBones       = track.m_header->m_capacity;
            int       numBoneIndices = 0;
            bool      bonesFit;
            if (mod.m_bones) {
                numBoneIndices = mod.m_bones->m_boneIndices.getSize();
                bonesFit       = numBones >= numBoneIndices;
            } else {
                bonesFit = true;
            }

            if (numBones > 0 && bonesFit) {
                const int dataSize = HK_NEXT_MULTIPLE_OF(16, track.m_header->m_elementSizeBytes * numBones);
                hkLocalBuffer<hkUint8> buffer(numBones + dataSize);
                hkbRigidBodyRagdollControlData* controlData = reinterpret_cast<hkbRigidBodyRagdollControlData*>(&buffer[0]);
                *controlData = mod.m_controlData;

                hkInt8* palette = reinterpret_cast<hkInt8*>(&buffer[dataSize]);
                hkString::memSet(palette, numBoneIndices ? -1 : 0, numBones);
                for (int i = 0; i < numBoneIndices; i++) palette[mod.m_bones->m_boneIndices[i]] = 0;

                hkbGeneratorOutput::TrackHeader header;
                header.m_capacity         = hkInt16(numBones);
                header.m_elementSizeBytes = sizeof(hkbRigidBodyRagdollControlData);
                header.m_numData          = 1;
                header.m_dataOffset       = 0;
                header.m_onFraction       = 1.0f;
                header.m_flags            = hkbGeneratorOutput::TRACK_FLAG_PALETTE;
                header.m_type             = 0;
                hkbGeneratorOutput::ConstTrack source(&header, controlData);
                hkbGeneratorOutputUtils::copyPaletteTrack(source, track);
                track.m_header->m_onFraction = 1.0f;
            }
        }

        if (inOut.isValid(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_BLEND_TIME)) {
            hkbGeneratorOutput::Track track = inOut.accessTrack(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_BLEND_TIME);
            if (track.m_header->m_capacity > 0) {
                *static_cast<hkReal*>(track.m_data) = mod.m_controlData.m_durationToBlend;
                track.m_header->m_onFraction        = 1.0f;
            }
        }
    }
} }
