/// @file
/// @brief hkbEvaluateHandleModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/EvaluateHandle/hkbEvaluateHandleModifier.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Utils/hkbHandleUtils.h>

namespace CB { namespace schema
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbEvaluateHandleModifier::hkbEvaluateHandleModifier()
    :   m_handlePositionOut(hkVector4::getZero()),
        m_handleRotationOut(hkQuaternion::getIdentity()),
        m_isValidOut(false),
        m_extrapolationTimeStep(0.0f),
        m_handleChangeSpeed(1.0f),
        m_handleChangeMode(HANDLE_CHANGE_MODE_ABRUPT)
    {
    }

    hkbEvaluateHandleModifier::hkbEvaluateHandleModifier(const hkbEvaluateHandleModifier& mod)
    :   hkbModifier(mod),
        m_handlePositionOut(mod.m_handlePositionOut),
        m_handleRotationOut(mod.m_handleRotationOut),
        m_isValidOut(false),
        m_extrapolationTimeStep(mod.m_extrapolationTimeStep),
        m_handleChangeSpeed(mod.m_handleChangeSpeed),
        m_handleChangeMode(mod.m_handleChangeMode)
    {
    }

    void hkbEvaluateHandleModifier::activate(const hkbContext& context)
    {
        m_handlePositionOut.setZero4();
        m_handleRotationOut.setIdentity();
        m_isValidOut = false;
        m_oldHandle.clear();
        m_smoothlyChangingHandles = false;
        m_timeSinceLastModify     = 0.0f;
    }

    void hkbEvaluateHandleModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeSinceLastModify += timestep;
    }

    void hkbEvaluateHandleModifier::deactivate(const hkbContext& context)
    {
        m_handle = HK_NULL;
    }

    hkbNode* hkbEvaluateHandleModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbEvaluateHandleModifier(*this);
    }

    /// @note A rigid body with a positive extrapolation time step is read ahead along its motion; the character's own
    ///       bone is read from the pose being modified. In constant-velocity mode a handle change moves the outputs from
    ///       the old position toward the new one at m_handleChangeSpeed (the rotation slerped by the same fraction) until
    ///       it arrives.
    void hkbEvaluateHandleModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (m_handle && (m_handle->m_rigidBody || m_handle->m_character)) {
            hkTransform worldFromHandle;
            if (m_handle->m_rigidBody && m_extrapolationTimeStep > 0.0f) {
                hkbHandleUtils::getExtrapolatedWorldFromHandle(*m_handle, worldFromHandle, m_extrapolationTimeStep);
            } else if (m_handle->m_character == context.m_character) {
                hkbHandleUtils::getWorldFromHandle(*m_handle, worldFromHandle, &inOut.accessWorldFromModel(), inOut.accessPoseLocal(), inOut.getNumPoseLocal());
            } else {
                hkbHandleUtils::getWorldFromHandle(*m_handle, worldFromHandle, HK_NULL, HK_NULL, 0);
            }

            if (m_handleChangeMode == HANDLE_CHANGE_MODE_ABRUPT) {
                m_smoothlyChangingHandles = false;
                m_oldHandle.clear();
                m_handlePositionOut = worldFromHandle.getTranslation();
                m_handleRotationOut.set(worldFromHandle.m_rotation);
            } else {
                hkbHandle& oldHandle = m_oldHandle;
                if (!oldHandle.m_rigidBody && !oldHandle.m_character) oldHandle.set(*m_handle);

                const bool sameHandle = hkbHandleUtils::areEqual(oldHandle, *m_handle);
                if (sameHandle && !m_smoothlyChangingHandles) {
                    m_handlePositionOut = worldFromHandle.getTranslation();
                    m_handleRotationOut.set(worldFromHandle.m_rotation);
                } else {
                    hkVector4 toNewPosition;
                    toNewPosition.setSub4(worldFromHandle.getTranslation(), m_oldHandlePosition);
                    const hkReal distance = toNewPosition.length3().getReal();
                    const hkReal step     = hkMath::min2(m_timeSinceLastModify * m_handleChangeSpeed, distance);

                    if (hkMath::fabs(distance - step) < 0.001f) {
                        m_handlePositionOut = worldFromHandle.getTranslation();
                        m_handleRotationOut.set(worldFromHandle.m_rotation);
                        m_smoothlyChangingHandles = false;
                    } else {
                        toNewPosition.normalize3();
                        hkVector4 offset;
                        offset.setMul4(toNewPosition, hkSimdReal(step));
                        m_handlePositionOut.setAdd4(offset, m_oldHandlePosition);
                        hkQuaternion newRotation;
                        newRotation.set(worldFromHandle.m_rotation);
                        m_handleRotationOut.setSlerp(m_oldHandleRotation, newRotation, step / distance);
                        m_smoothlyChangingHandles = true;
                    }

                    if (!sameHandle) oldHandle.set(*m_handle);
                }
            }

            m_oldHandlePosition   = m_handlePositionOut;
            m_oldHandleRotation   = m_handleRotationOut;
            m_isValidOut          = true;
            m_timeSinceLastModify = 0.0f;
            return;
        }

        m_isValidOut              = false;
        m_smoothlyChangingHandles = false;
        m_oldHandle.clear();
        m_timeSinceLastModify     = 0.0f;
    }
} }
