/// @file
/// @brief hkbAttachmentSetup: how an attachment between two handles is formed and enforced.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Layout from the exe's reflected class (0x30 bytes, 8 members). The destructor is implicit: vanilla's
    ///       scalar deleting destructor stamps only hkBaseObject's vtable.
    class hkbAttachmentSetup : public hkReferencedObject
    {
    public:
        /// @note Values in the reflected enum's order.
        enum AttachmentType
        {
            ATTACHMENT_TYPE_KEYFRAME_RIGID_BODY    = 0,
            ATTACHMENT_TYPE_BALL_SOCKET_CONSTRAINT = 1,
            ATTACHMENT_TYPE_RAGDOLL_CONSTRAINT     = 2,
            ATTACHMENT_TYPE_SET_WORLD_FROM_MODEL   = 3,
            ATTACHMENT_TYPE_NONE                   = 4,
        };

        hkReal m_blendInTime;             ///< +0x10
        hkReal m_moveAttacherFraction;    ///< +0x14
        hkReal m_gain;                    ///< +0x18
        hkReal m_extrapolationTimeStep;   ///< +0x1c
        hkReal m_fixUpGain;               ///< +0x20
        hkReal m_maxLinearDistance;       ///< +0x24
        hkReal m_maxAngularDistance;      ///< +0x28
        hkInt8 m_attachmentType;          ///< +0x2c — AttachmentType

        hkbAttachmentSetup();
        /// @brief Copies the settings (one block, m_blendInTime through m_attachmentType).
        hkbAttachmentSetup(const hkbAttachmentSetup& setup);
    };
    static_assert(sizeof(hkbAttachmentSetup) == 0x30, "hkbAttachmentSetup size");
} }
