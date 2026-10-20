/// @file
/// @brief The simplex solver: moves a point against a set of planes (what the character controllers resolve their
///        motion and support with).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief One plane the solver moves against.
    /// @note 0x40 bytes (hkpCharacterRigidBody::getSupportInfo's stride); POD as Havok declares it (vanilla's setSize
    ///       constructs nothing). Not reflected; members per the Havok SDK, the ones getSupportInfo writes.
    ///       Members end at +0x34, the rest is alignment padding: a copy moves the members alone
    ///       (bhkCharProxyController::processConstraintsCallback's vertical planes).
    struct hkSurfaceConstraintInfo
    {
        HK_DECLARE_POD_TYPE();

        hkVector4 m_plane;                     ///< +0x00 — normal, distance in w
        hkVector4 m_velocity;                  ///< +0x10
        hkReal    m_staticFriction;            ///< +0x20
        hkReal    m_extraUpStaticFriction;     ///< +0x24
        hkReal    m_extraDownStaticFriction;   ///< +0x28
        hkReal    m_dynamicFriction;           ///< +0x2c
        /// @brief Which plane wins a conflict: higher first.
        int       m_priority;                  ///< +0x30
    };

    /// @brief What the solver reports per plane.
    /// @note 0x10 bytes. Only the flag getSupportInfo reads is declared.
    struct hkSurfaceConstraintInteraction
    {
        HK_DECLARE_POD_TYPE();

        /// @brief Whether the solution touched the plane.
        hkUint8 m_touched;   ///< +0x00
        hkUint8 m_pad01[0x10 - 0x01];
    };

    /// @note 0x60 bytes (getSupportInfo's frame). Not reflected; members per the Havok SDK, the ones it writes.
    struct hkSimplexSolverInput
    {
        hkVector4                m_position;             ///< +0x00
        hkVector4                m_velocity;             ///< +0x10
        hkVector4                m_maxSurfaceVelocity;   ///< +0x20
        hkVector4                m_upVector;             ///< +0x30
        hkReal                   m_deltaTime;            ///< +0x40
        hkReal                   m_minDeltaTime;         ///< +0x44
        hkSurfaceConstraintInfo* m_constraints;          ///< +0x48
        int                      m_numConstraints;       ///< +0x50
        hkUint8                  m_pad54[0x60 - 0x54];
    };

    /// @note 0x30 bytes (getSupportInfo's frame). Not reflected.
    struct hkSimplexSolverOutput
    {
        hkVector4                       m_position;            ///< +0x00
        hkVector4                       m_velocity;            ///< +0x10
        hkReal                          m_deltaTime;           ///< +0x20
        hkSurfaceConstraintInteraction* m_planeInteractions;   ///< +0x28 — one per input constraint, caller-owned
    };

    /// @note Out of line in vanilla (hkp code, not reimplemented).
    void hkSimplexSolverSolve(const hkSimplexSolverInput& input, hkSimplexSolverOutput& output);
} }
