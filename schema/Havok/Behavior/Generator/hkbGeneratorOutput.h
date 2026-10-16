/// @file
/// @brief hkbGeneratorOutput: the track buffer a generator writes its pose, motion and float slots into.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaSkeleton;

    struct hkbGeneratorOutput
    {
        /// @note Tracks 4-8 named per the Havok SDK; hkbBlendingTransitionEffectUtils reads 4 and 6 as the ragdoll controls.
        enum StandardTracks
        {
            TRACK_WORLD_FROM_MODEL                      = 0,
            TRACK_EXTRACTED_MOTION                      = 1,
            TRACK_POSE                                  = 2,
            TRACK_FLOAT_SLOTS                           = 3,
            TRACK_RIGID_BODY_RAGDOLL_CONTROLS           = 4,
            TRACK_RIGID_BODY_RAGDOLL_BLEND_TIME         = 5,
            TRACK_POWERED_RAGDOLL_CONTROLS              = 6,
            TRACK_POWERED_RAGDOLL_WORLD_FROM_MODEL_MODE = 7,
            TRACK_KEYFRAMED_RAGDOLL_BONES               = 8,
            /// @brief One float per attribute id (the character's root graph's ids), written by hkbAttributeModifier.
            /// @note The index is vanilla's (hkbAttributeModifierUtils::modify tests `numTracks > 10`); later Havok SDKs
            ///       number the attribute track 11.
            TRACK_ATTRIBUTES                            = 10,
            /// @brief One hkbHandIkControlData per hand (four hands): the blendable hand IK controls.
            /// @note 13-16 and 17-20 are vanilla's (hkbHandIkControlsModifier writes both families at track + hand index);
            ///       21 tracks in all, the count hkbPoseMatchingGenerator asks for.
            TRACK_HAND_IK_CONTROLS_0                    = 13,
            /// @brief The same controls again, copied rather than blended.
            TRACK_HAND_IK_CONTROLS_NON_BLENDABLE_0      = 17
        };
        /// @note PALETTE and SPARSE per the Havok SDK (activate of hkbPoseMatchingGenerator empties those tracks).
        enum TrackFlags { TRACK_FLAG_ADDITIVE_POSE = 1, TRACK_FLAG_PALETTE = 2, TRACK_FLAG_SPARSE = 4 };

        struct TrackHeader
        {
            hkInt16 m_capacity;
            hkInt16 m_numData;
            hkInt16 m_dataOffset;
            hkInt16 m_elementSizeBytes;
            hkReal  m_onFraction;
            hkInt8  m_flags;
            hkInt8  m_type;
            hkInt16 m_pad;
        };
        struct TrackMasterHeader
        {
            hkInt32 m_numBytes;
            hkInt32 m_numTracks;
            hkInt8  m_unused[8];
        };
        struct Tracks
        {
            TrackMasterHeader m_masterHeader;
            TrackHeader       m_trackHeaders[1];
        };

        Tracks* m_tracks;
        bool    m_deleteTracks;

        /// @brief An output owning a fresh track buffer laid out for these counts.
        /// @note Out of line in vanilla (0x140abdf40). Parameter names provisional: hkbPoseMatchingGenerator passes
        ///       (numBones, 0, 0, 0, 0x15, 0).
        hkbGeneratorOutput(int numBones, int numFloatSlots, int numAttributes, int numRagdollBones, int numTracks, int flags);
        /// @brief Frees the track buffer when the output owns it.
        HK_FORCE_INLINE ~hkbGeneratorOutput()
        {
            if (m_deleteTracks) hkDeallocate(m_tracks);
        }

        HK_FORCE_INLINE int            getNumTracks() const { return m_tracks->m_masterHeader.m_numTracks; }
        HK_FORCE_INLINE bool           isValid(int track) const { return getNumTracks() > track; }
        /// @brief Whether the output has `track` and it contributes (m_onFraction > 0).
        /// @note Name provisional. Spelled as a flag set under the isValid test: vanilla materializes the fraction test
        ///       (setb; test) and branches on it, which `isValid() && fraction > 0` does not reproduce.
        HK_FORCE_INLINE bool           isTrackOn(int track) const
        {
            bool on = false;
            if (isValid(track)) on = m_tracks->m_trackHeaders[track].m_onFraction > 0.0f;
            return on;
        }
        HK_FORCE_INLINE TrackHeader&   getTrackHeader(int track) { return m_tracks->m_trackHeaders[track]; }
        HK_FORCE_INLINE void*          getTrackData(int track) { return reinterpret_cast<char*>(m_tracks) + m_tracks->m_trackHeaders[track].m_dataOffset; }
        HK_FORCE_INLINE int            getNumPoseLocal() const { return isValid(TRACK_POSE) ? m_tracks->m_trackHeaders[TRACK_POSE].m_numData : 0; }
        HK_FORCE_INLINE hkQsTransform* accessPoseLocal() { return static_cast<hkQsTransform*>(getTrackData(TRACK_POSE)); }
        HK_FORCE_INLINE hkQsTransform& accessWorldFromModel() { return *static_cast<hkQsTransform*>(getTrackData(TRACK_WORLD_FROM_MODEL)); }
    };
} }
