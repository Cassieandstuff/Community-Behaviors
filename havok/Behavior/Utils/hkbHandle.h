/// @file
/// @brief hkbHandle: a sensed target — a local frame, a rigid body, or a character's bone.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkLocalFrame;
    class hkpRigidBody;
    struct hkbCharacter;

    /// @note 0x30 bytes. The default constructor leaves the members unset: vanilla's constructors store the vtable and
    ///       the reference count, then clear the members in their bodies.
    class hkbHandle : public hkReferencedObject
    {
    public:
        hkLocalFrame* m_frame;                ///< +0x10
        hkpRigidBody* m_rigidBody;            ///< +0x18
        hkbCharacter* m_character;            ///< +0x20
        hkInt16       m_animationBoneIndex;   ///< +0x28

        HK_FORCE_INLINE hkbHandle() {}
        hkbHandle(hkFinishLoadedObjectFlag flag);

        /// @brief Points the handle at nothing.
        /// @note Name provisional.
        HK_FORCE_INLINE void clear()
        {
            m_frame              = HK_NULL;
            m_rigidBody          = HK_NULL;
            m_character          = HK_NULL;
            m_animationBoneIndex = -1;
        }
        /// @brief Copies the target (not the reference count).
        /// @note Name provisional.
        HK_FORCE_INLINE void set(const hkbHandle& h)
        {
            m_frame              = h.m_frame;
            m_rigidBody          = h.m_rigidBody;
            m_character          = h.m_character;
            m_animationBoneIndex = h.m_animationBoneIndex;
        }
    };
} }
