/// @file
/// @brief hkaFootPlacementIkSolver: places a leg's foot on the ground found by ray casts (declarations so far).
/// @note The folder follows the Havok SDK's Animation/Ik tree (the exe keeps no __FILE__ string for it).
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    class hkaPose;
    class hkaRaycastInterface;

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

            /// @brief No skeleton or bones (-1), zero vectors, and the Havok SDK defaults for the heights, knee limits and ray
            ///        distances.
            /// @note Out of line in vanilla (0x140a83890).
            Setup();
        };

        /// @brief One step's inputs.
        /// @note 0xa0 bytes. Names follow the Havok SDK except m_hipIkGain (provisional: doFootPlacement passes it to
        ///       hkaTwoJointsIkSolver as the first joint's gain; hkbFootIkModifier leaves it 0).
        struct Input
        {
            hkQsTransform              m_originalAnkleTransformMS;   ///< +0x00
            hkQsTransform              m_worldFromModel;             ///< +0x30
            hkBool                     m_footPlacementOn;            ///< +0x60
            const hkaRaycastInterface* m_raycastInterface;           ///< +0x68
            hkReal                     m_onOffGain;                  ///< +0x70
            hkReal                     m_groundAscendingGain;        ///< +0x74
            hkReal                     m_groundDescendingGain;       ///< +0x78
            hkReal                     m_footPlantedGain;            ///< +0x7c
            hkReal                     m_footRaisedGain;             ///< +0x80
            hkReal                     m_footUnlockGain;             ///< +0x84
            hkReal                     m_hipIkGain;                  ///< +0x88
            hkUint32                   m_collisionFilterInfo;        ///< +0x8c
            hkBool                     m_useCollisionFilterInfo;     ///< +0x90

            /// @brief Placement on, no raycast interface, the Havok SDK default gains (0.2 on/off, else 1; the hip 0),
            ///        no collision filter.
            /// @note Out of line in vanilla (0x140a83910).
            Input();
        };
        /// @brief One step's results.
        /// @note 0x20 bytes. m_groundNormalWS is provisional (doFootPlacement writes the solver's previous ground normal
        ///       there).
        struct Output
        {
            hkVector4 m_groundNormalWS;   ///< +0x00
            hkReal    m_verticalError;    ///< +0x10
            hkBool    m_hitSomething;     ///< +0x14
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

        /// @brief Casts for the ground under the leg and bends it (through hkaTwoJointsIkSolver) to place the foot,
        ///        writing the result into `poseInOut` (model space).
        /// @note Out of line in vanilla (0x140a83a20).
        void doFootPlacement(const Input& input, Output& output, hkaPose& poseInOut);
    };
} }
