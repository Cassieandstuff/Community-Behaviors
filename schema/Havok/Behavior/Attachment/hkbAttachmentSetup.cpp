/// @file
/// @brief hkbAttachmentSetup, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Attachment/hkbAttachmentSetup.h>

namespace CB { namespace schema
{
    /// @note VARIANT: vanilla stores the m_gain qword after the zeroes, 24247 after m_attachmentType (schedule only).
    hkbAttachmentSetup::hkbAttachmentSetup()
    :   m_blendInTime(0.0f),
        m_moveAttacherFraction(0.0f),
        m_gain(0.3f),
        m_extrapolationTimeStep(0.0f),
        m_fixUpGain(0.0f),
        m_maxLinearDistance(0.0f),
        m_maxAngularDistance(0.0f),
        m_attachmentType(ATTACHMENT_TYPE_KEYFRAME_RIGID_BODY)
    {
    }

    hkbAttachmentSetup::hkbAttachmentSetup(const hkbAttachmentSetup& setup)
    :   hkReferencedObject(setup)
    {
        hkString::memCpy(&m_blendInTime, &setup.m_blendInTime,
                         int(reinterpret_cast<const char*>(&setup.m_attachmentType + 1) - reinterpret_cast<const char*>(&setup.m_blendInTime)));
    }
} }
