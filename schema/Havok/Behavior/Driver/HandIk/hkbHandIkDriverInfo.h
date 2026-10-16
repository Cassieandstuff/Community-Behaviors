/// @file
/// @brief hkbHandIkDriverInfo: the character data's hand IK setup (arms and the fade curve).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x28 bytes; members per the reflected schema (schema/gen/hkbHandIkDriverInfo.yaml).
    class hkbHandIkDriverInfo : public hkReferencedObject
    {
    public:
        /// @note 0x60 bytes (schema/gen/hkbHandIkDriverInfoHand.yaml); `m_handOrienationOffsetLS` is Havok's spelling.
        struct Hand
        {
            hkVector4    m_elbowAxisLS;              ///< +0x00
            hkVector4    m_backHandNormalLS;         ///< +0x10
            hkVector4    m_handOffsetLS;             ///< +0x20
            hkQuaternion m_handOrienationOffsetLS;   ///< +0x30
            hkReal       m_maxElbowAngleDegrees;     ///< +0x40
            hkReal       m_minElbowAngleDegrees;     ///< +0x44
            hkInt16      m_shoulderIndex;            ///< +0x48
            hkInt16      m_shoulderSiblingIndex;     ///< +0x4a
            hkInt16      m_elbowIndex;               ///< +0x4c
            hkInt16      m_elbowSiblingIndex;        ///< +0x4e
            hkInt16      m_wristIndex;               ///< +0x50
            hkBool       m_enforceEndPosition;       ///< +0x52
            hkBool       m_enforceEndRotation;       ///< +0x53
            hkStringPtr  m_localFrameName;           ///< +0x58
        };

        hkArray<Hand> m_hands;            ///< +0x10
        /// @note An hkbBlendCurveUtils::BlendCurve.
        hkInt8        m_fadeInOutCurve;   ///< +0x20

        /// @note Vanilla inlines it into the type registry's finish function; out of line here
        ///       (Behavior/Driver/HandIk/hkbHandIkDriverInfo.cpp), it also brings the class's vtable and scalar deleting
        ///       destructor into that unit.
        hkbHandIkDriverInfo(hkFinishLoadedObjectFlag flag);
    };
} }
