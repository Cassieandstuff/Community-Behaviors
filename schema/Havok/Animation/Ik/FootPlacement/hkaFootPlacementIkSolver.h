/// @file
/// @brief hkaFootPlacementIkSolver: places a leg's foot on the ground found by ray casts (declarations so far).
/// @note The folder follows the Havok SDK's Animation/Ik tree (the exe keeps no __FILE__ string for it).
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    /// @note 0xf0 bytes. The Setup field names follow the Havok SDK; the solver's own members after m_setup are
    ///       provisional (the constructor's stores are all that is confirmed).
    class hkaFootPlacementIkSolver : public hkReferencedObject
    {
    public:
        /// @note 0x80 bytes.
        struct Setup
        {
            const hkaSkeleton* m_skeleton;                   ///< +0x00
            hkInt16            m_hipIndex;                   ///< +0x08
            hkInt16            m_kneeIndex;                  ///< +0x0a
            hkInt16            m_ankleIndex;                 ///< +0x0c
            hkVector4          m_kneeAxisLS;                 ///< +0x10
            hkVector4          m_footEndLS;                  ///< +0x20
            hkVector4          m_worldUpDirectionWS;         ///< +0x30
            hkVector4          m_modelUpDirectionMS;         ///< +0x40
            hkReal             m_originalGroundHeightMS;     ///< +0x50
            hkReal             m_footPlantedAnkleHeightMS;   ///< +0x54
            hkReal             m_footRaisedAnkleHeightMS;    ///< +0x58
            hkReal             m_maxAnkleHeightMS;           ///< +0x5c
            hkReal             m_minAnkleHeightMS;           ///< +0x60
            hkReal             m_cosineMaxKneeAngle;         ///< +0x64
            hkReal             m_cosineMinKneeAngle;         ///< +0x68
            hkReal             m_raycastDistanceUp;          ///< +0x6c
            hkReal             m_raycastDistanceDown;        ///< +0x70
            hkBool             m_useFootLocking;             ///< +0x74
        };

        Setup     m_setup;                          ///< +0x10
        hkReal    m_currentWeight;                  ///< +0x90
        hkReal    m_previousGroundHeightWS;         ///< +0x94
        /// @brief Starts as the world up direction.
        hkVector4 m_previousGroundNormalWS;         ///< +0xa0
        hkReal    m_previousVerticalDisplacement;   ///< +0xb0
        hkBool    m_isFootLocked;                   ///< +0xb4
        hkVector4 m_footLockedPositionMS;           ///< +0xc0
        hkVector4 m_footLockedGroundNormalMS;       ///< +0xd0
        hkVector4 m_footUnlockedPositionMS;         ///< +0xe0

        hkaFootPlacementIkSolver(const Setup& setup);
    };
} }
