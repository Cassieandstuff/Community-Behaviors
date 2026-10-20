/// @file
/// @brief hkbClosestLocalFrameCollector: keeps the nearest local frame (matching a name and group) as a handle.
#pragma once

#include <Behavior/Utils/hkbHandle.h>
#include <Common/Base/Types/Geometry/LocalFrame/hkLocalFrame.h>

namespace CB { namespace havok
{
    /// @note 0x90 bytes; the methods are out of line in vanilla. Member and method names are provisional except the
    ///       class's: the constructor fills both handles' vtables and starts the closest distance at HK_REAL_MAX.
    class hkbClosestLocalFrameCollector : public hkLocalFrameCollector
    {
    public:
        hkbHandle                m_closestLocalFrame;   ///< +0x10 — the best frame found so far (m_frame null if none)
        hkReal                   m_closestDistance;     ///< +0x40
        hkbHandle                m_handle;              ///< +0x48 — what each found frame is attached to
        hkReal                   m_minDistance;         ///< +0x78
        const char*              m_localFrameName;      ///< +0x80
        const hkLocalFrameGroup* m_localFrameGroup;     ///< +0x88

        hkbClosestLocalFrameCollector(const char* localFrameName);

        /// @brief Frames found next belong to this rigid body.
        void setRigidBody(hkpRigidBody* rigidBody);
        /// @brief Frames found next belong to this character's bone.
        void setCharacterAndBone(hkbCharacter* character, hkInt16 animationBoneIndex);
        void setMinDistance(hkReal minDistance);
        void setLocalFrameGroup(const hkLocalFrameGroup* group);

        virtual void addFrame(const hkLocalFrame* localFrame, hkReal distance);   ///< slot 3
    };
} }
