/// @file
/// @brief hkpCharacterMotion, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Dynamics/Motion/Rigid/hkpCharacterMotion.h>

namespace CB { namespace havok
{
    /// @note A zero inverse inertia (infinite inertia) reads back as HK_REAL_HIGH on the diagonal.
    void hkpCharacterMotion::getInertiaLocal(hkMatrix3& inertiaOut) const
    {
        const hkReal inertiaInv = m_inertiaAndMassInv(0);
        inertiaOut.setZero();
        if (inertiaInv == 0.0f) {
            inertiaOut.m_col0(0) = HK_REAL_HIGH;
            inertiaOut.m_col1(1) = HK_REAL_HIGH;
            inertiaOut.m_col2(2) = HK_REAL_HIGH;
        } else {
            const hkReal inertia = 1.0f / inertiaInv;
            inertiaOut.m_col0(0) = inertia;
            inertiaOut.m_col1(1) = inertia;
            inertiaOut.m_col2(2) = inertia;
        }
    }

    void hkpCharacterMotion::getInertiaWorld(hkMatrix3& inertiaOut) const
    {
        const hkReal inertia = 1.0f / m_inertiaAndMassInv(0);
        inertiaOut.setZero();
        inertiaOut.m_col0(0) = inertia;
        inertiaOut.m_col1(1) = inertia;
        inertiaOut.m_col2(2) = inertia;
    }

    /// @note Keeps the largest diagonal entry's inverse (0 when it is not positive).
    void hkpCharacterMotion::setInertiaLocal(const hkMatrix3& inertia)
    {
        const hkReal maxDiag    = hkMath::max2(hkMath::max2(inertia.m_col1(1), inertia.m_col0(0)), inertia.m_col2(2));
        const hkReal inertiaInv = (maxDiag > 0.0f) ? 1.0f / maxDiag : 0.0f;
        m_inertiaAndMassInv(0) = inertiaInv;
        m_inertiaAndMassInv(1) = inertiaInv;
        m_inertiaAndMassInv(2) = inertiaInv;
    }

    /// @note Keeps the smallest diagonal entry.
    void hkpCharacterMotion::setInertiaInvLocal(const hkMatrix3& inertiaInv)
    {
        const hkReal minDiag = hkMath::min2(hkMath::min2(inertiaInv.m_col1(1), inertiaInv.m_col0(0)), inertiaInv.m_col2(2));
        m_inertiaAndMassInv(0) = minDiag;
        m_inertiaAndMassInv(1) = minDiag;
        m_inertiaAndMassInv(2) = minDiag;
    }

    void hkpCharacterMotion::getInertiaInvLocal(hkMatrix3& inertiaInvOut) const
    {
        const hkReal inertiaInv = m_inertiaAndMassInv(0);
        inertiaInvOut.setZero();
        inertiaInvOut.m_col0(0) = inertiaInv;
        inertiaInvOut.m_col1(1) = inertiaInv;
        inertiaInvOut.m_col2(2) = inertiaInv;
    }

    void hkpCharacterMotion::getInertiaInvWorld(hkMatrix3& inertiaInvOut) const
    {
        const hkReal inertiaInv = m_inertiaAndMassInv(0);
        inertiaInvOut.setZero();
        inertiaInvOut.m_col0(0) = inertiaInv;
        inertiaInvOut.m_col1(1) = inertiaInv;
        inertiaInvOut.m_col2(2) = inertiaInv;
    }

    void hkpCharacterMotion::getProjectedPointVelocity(const hkVector4& p, const hkVector4& normal, hkReal& velOut, hkReal& invVirtMassOut) const
    {
        hkVector4 arm;
        arm.setSub4(p, getCenterOfMassInWorld());
        hkVector4 armCrossNormal;
        armCrossNormal.setCross(normal, arm);
        velOut         = (m_linearVelocity.dot3(normal) + armCrossNormal.dot3(m_angularVelocity)).getReal();
        invVirtMassOut = m_inertiaAndMassInv(3);
    }

    void hkpCharacterMotion::applyPointImpulse(const hkVector4& imp, const hkVector4& p)
    {
        m_linearVelocity.addMul4(m_inertiaAndMassInv.getSimdAt3(), imp);
        hkVector4 arm;
        arm.setSub4(p, getCenterOfMassInWorld());
        hkVector4 angularImpulse;
        angularImpulse.setCross(arm, imp);
        m_angularVelocity.addMul4(angularImpulse, m_inertiaAndMassInv);
    }

    void hkpCharacterMotion::applyAngularImpulse(const hkVector4& imp)
    {
        m_angularVelocity.addMul4(m_inertiaAndMassInv, imp);
    }

    void hkpCharacterMotion::applyForce(const hkReal deltaTime, const hkVector4& force)
    {
        hkVector4 impulse;
        impulse.setMul4(hkSimdReal(deltaTime), force);
        m_linearVelocity.addMul4(impulse, m_inertiaAndMassInv.getSimdAt3());
    }

    void hkpCharacterMotion::applyForce(const hkReal deltaTime, const hkVector4& force, const hkVector4& p)
    {
        hkVector4 impulse;
        impulse.setMul4(hkSimdReal(deltaTime), force);
        applyPointImpulse(impulse, p);
    }

    void hkpCharacterMotion::applyTorque(const hkReal deltaTime, const hkVector4& torque)
    {
        hkVector4 angularImpulse;
        angularImpulse.setMul4(hkSimdReal(deltaTime), torque);
        applyAngularImpulse(angularImpulse);
    }
} }
