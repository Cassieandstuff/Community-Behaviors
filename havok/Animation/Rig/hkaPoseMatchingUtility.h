/// @file
/// @brief hkaPoseMatchingUtility: finds which of a set of candidate poses best matches a pose (declarations only so far).
/// @note The folder is a guess (the exe keeps no __FILE__ string for it); its functions sit at 0x140bd89c0-0x140bd8fxx.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x30 bytes, no vtable; heap-allocated by hkbPoseMatchingGenerator::activate (the allocation is
    ///       null-checked). Member names provisional except the class's.
    class hkaPoseMatchingUtility
    {
    public:
        /// @brief One candidate: the pose's signature (computed from the three matching bones) and what the caller
        ///        attached to it.
        /// @note 0x20 bytes; names provisional.
        struct CandidatePose
        {
            hkQuadReal m_signature;   ///< +0x00
            hkUlong    m_userData;    ///< +0x10
            hkReal     m_time;        ///< +0x18
        };

        hkArray<CandidatePose> m_candidatePoses;     ///< +0x00
        hkVector4              m_upVector;           ///< +0x10
        hkInt16                m_rootBoneIndex;      ///< +0x20
        hkInt16                m_otherBoneIndex;     ///< +0x22
        hkInt16                m_anotherBoneIndex;   ///< +0x24

        /// @brief The class allocator. `throw()`: vanilla null-checks the block.
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        /// @note Out of line in vanilla (0x140bd89c0).
        hkaPoseMatchingUtility(hkInt16 rootBoneIndex, hkInt16 otherBoneIndex, hkInt16 anotherBoneIndex, const hkVector4& upVector);
        /// @brief Adds `poseModelSpace` as a candidate; returns its index.
        /// @note Name provisional; out of line in vanilla (0x140bd8ab0).
        int addCandidatePose(const hkQsTransform* poseModelSpace, hkUlong userData, hkReal time);
        /// @brief The candidate closest to `poseModelSpace` (-1 with none), and its error (1 - |signature dot|).
        /// @note Name provisional; out of line in vanilla (0x140bd8a00).
        int findBestCandidatePoseIndex(const hkQsTransform* poseModelSpace, hkReal& errorOut);
        /// @brief The reference frames the three matching bones span in an animation pose and a ragdoll pose (model
        ///        space): the world-from-models that would line the two up.
        /// @note Name provisional; out of line in vanilla (0x140bd8b30), called by hkbGetUpModifier::modify.
        void computeReferenceFrame(const hkQsTransform* animPoseModelSpace, const hkQsTransform* ragdollPoseModelSpace, hkQsTransform& animWorldFromModelOut,
                                   hkQsTransform& ragdollWorldFromModelOut);
    };
} }
