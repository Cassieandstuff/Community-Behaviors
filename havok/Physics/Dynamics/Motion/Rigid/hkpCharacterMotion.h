/// @file
/// @brief hkpCharacterMotion: the motion of a character controller's body (MOTION_CHARACTER): sphere-like inertia, one
///        value on the diagonal.
#pragma once

#include <Physics/Dynamics/Motion/hkpMotion.h>

namespace CB { namespace havok
{
    /// @note 0x140 bytes per Havok's reflection (no members of its own). The inertia is kept as one inverse value in
    ///       m_inertiaAndMassInv.xyz (w: the inverse mass).
    class hkpCharacterMotion : public hkpMotion
    {
    public:
        virtual void getInertiaLocal(hkMatrix3& inertiaOut) const;                                     ///< slot 5
        virtual void getInertiaWorld(hkMatrix3& inertiaOut) const;                                     ///< slot 6
        virtual void setInertiaLocal(const hkMatrix3& inertia);                                        ///< slot 7
        virtual void setInertiaInvLocal(const hkMatrix3& inertiaInv);                                  ///< slot 8
        virtual void getInertiaInvLocal(hkMatrix3& inertiaInvOut) const;                               ///< slot 9
        virtual void getInertiaInvWorld(hkMatrix3& inertiaInvOut) const;                               ///< slot 10
        virtual void getProjectedPointVelocity(const hkVector4& p, const hkVector4& normal, hkReal& velOut, hkReal& invVirtMassOut) const;   ///< slot 18
        virtual void applyPointImpulse(const hkVector4& imp, const hkVector4& p);                      ///< slot 20
        virtual void applyAngularImpulse(const hkVector4& imp);                                        ///< slot 21
        virtual void applyForce(const hkReal deltaTime, const hkVector4& force);                       ///< slot 23
        virtual void applyForce(const hkReal deltaTime, const hkVector4& force, const hkVector4& p);  ///< slot 22
        virtual void applyTorque(const hkReal deltaTime, const hkVector4& torque);                     ///< slot 24
    };
} }
