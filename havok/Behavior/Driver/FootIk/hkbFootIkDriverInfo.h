/// @file
/// @brief hkbFootIkDriverInfo: the character data's foot IK setup (legs and ground raycast parameters).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x48 bytes; members per the reflected schema (schema/project/hkbFootIkDriverInfo.yaml).
    class hkbFootIkDriverInfo : public hkReferencedObject
    {
    public:
        /// @note 0x60 bytes (schema/project/hkbFootIkDriverInfoLeg.yaml).
        struct Leg
        {
            hkQuaternion m_prevAnkleRotLS;             ///< +0x00
            hkVector4    m_kneeAxisLS;                 ///< +0x10
            hkVector4    m_footEndLS;                  ///< +0x20
            hkReal       m_footPlantedAnkleHeightMS;   ///< +0x30
            hkReal       m_footRaisedAnkleHeightMS;    ///< +0x34
            hkReal       m_maxAnkleHeightMS;           ///< +0x38
            hkReal       m_minAnkleHeightMS;           ///< +0x3c
            hkReal       m_maxKneeAngleDegrees;        ///< +0x40
            hkReal       m_minKneeAngleDegrees;        ///< +0x44
            hkReal       m_maxAnkleAngleDegrees;       ///< +0x48
            hkInt16      m_hipIndex;                   ///< +0x4c
            hkInt16      m_kneeIndex;                  ///< +0x4e
            hkInt16      m_ankleIndex;                 ///< +0x50
        };

        hkArray<Leg> m_legs;                     ///< +0x10
        hkReal       m_raycastDistanceUp;        ///< +0x20
        hkReal       m_raycastDistanceDown;      ///< +0x24
        hkReal       m_originalGroundHeightMS;   ///< +0x28
        hkReal       m_verticalOffset;           ///< +0x2c
        hkUint32     m_collisionFilterInfo;      ///< +0x30
        hkReal       m_forwardAlignFraction;     ///< +0x34
        hkReal       m_sidewaysAlignFraction;    ///< +0x38
        hkReal       m_sidewaysSampleWidth;      ///< +0x3c
        hkBool       m_lockFeetWhenPlanted;      ///< +0x40
        hkBool       m_useCharacterUpVector;     ///< +0x41
        hkBool       m_isQuadrupedNarrow;        ///< +0x42

        /// @note Vanilla inlines it into the type registry's finish function; out of line here
        ///       (Behavior/Driver/FootIk/hkbFootIkDriverInfo.cpp), it also brings the class's vtable and scalar deleting
        ///       destructor into that unit.
        hkbFootIkDriverInfo(hkFinishLoadedObjectFlag flag);
    };
} }
