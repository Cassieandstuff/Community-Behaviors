/// @file
/// @brief hkbHandIkControlData: where one hand should reach, as written into the generator output's hand IK tracks.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkbHandle;

    /// @note 0x60 bytes; members per the exe's reflected class (hkbHandIkControlData, 12 members).
    struct hkbHandIkControlData
    {
        enum HandleChangeMode
        {
            HANDLE_CHANGE_MODE_ABRUPT            = 0,
            HANDLE_CHANGE_MODE_CONSTANT_VELOCITY = 1
        };

        hkVector4    m_targetPosition;          ///< +0x00
        hkQuaternion m_targetRotation;          ///< +0x10
        hkVector4    m_targetNormal;            ///< +0x20
        /// @note A raw pointer: copies don't add a reference, yet hkbHandIkControlsModifier::deactivate releases it.
        hkbHandle*   m_targetHandle;            ///< +0x30
        hkReal       m_transformOnFraction;     ///< +0x38
        hkReal       m_normalOnFraction;        ///< +0x3c
        hkReal       m_fadeInDuration;          ///< +0x40
        hkReal       m_fadeOutDuration;         ///< +0x44
        hkReal       m_extrapolationTimeStep;   ///< +0x48
        hkReal       m_handleChangeSpeed;       ///< +0x4c
        /// @note A HandleChangeMode.
        hkInt8       m_handleChangeMode;        ///< +0x50
        hkBool       m_fixUp;                   ///< +0x51

        HK_FORCE_INLINE hkbHandIkControlData() {}
        /// @note Memberwise and force-inlined (vanilla inlines every copy; /Ob1 would outline the implicit ones).
        HK_FORCE_INLINE hkbHandIkControlData(const hkbHandIkControlData& d)
        :   m_targetPosition(d.m_targetPosition),
            m_targetRotation(d.m_targetRotation),
            m_targetNormal(d.m_targetNormal),
            m_targetHandle(d.m_targetHandle),
            m_transformOnFraction(d.m_transformOnFraction),
            m_normalOnFraction(d.m_normalOnFraction),
            m_fadeInDuration(d.m_fadeInDuration),
            m_fadeOutDuration(d.m_fadeOutDuration),
            m_extrapolationTimeStep(d.m_extrapolationTimeStep),
            m_handleChangeSpeed(d.m_handleChangeSpeed),
            m_handleChangeMode(d.m_handleChangeMode),
            m_fixUp(d.m_fixUp)
        {
        }
        HK_FORCE_INLINE void operator=(const hkbHandIkControlData& d)
        {
            m_targetPosition        = d.m_targetPosition;
            m_targetRotation        = d.m_targetRotation;
            m_targetNormal          = d.m_targetNormal;
            m_targetHandle          = d.m_targetHandle;
            m_transformOnFraction   = d.m_transformOnFraction;
            m_normalOnFraction      = d.m_normalOnFraction;
            m_fadeInDuration        = d.m_fadeInDuration;
            m_fadeOutDuration       = d.m_fadeOutDuration;
            m_extrapolationTimeStep = d.m_extrapolationTimeStep;
            m_handleChangeSpeed     = d.m_handleChangeSpeed;
            m_handleChangeMode      = d.m_handleChangeMode;
            m_fixUp                 = d.m_fixUp;
        }
    };
} }
