/// @file
/// @brief hkbComputeRotationToTargetModifier: the rotation about an axis that turns a facing direction toward a target.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkbComputeRotationToTargetModifierInternalState : public hkReferencedObject
    {
    public:
        hkQuaternion m_rotationOut;   ///< +0x10
    };

    /// @note Sized 0xc0. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbComputeRotationToTargetModifier : public hkbModifier
    {
    public:
        hkQuaternion m_rotationOut;            ///< +0x50
        hkVector4    m_targetPosition;         ///< +0x60
        hkVector4    m_currentPosition;        ///< +0x70
        hkQuaternion m_currentRotation;        ///< +0x80
        hkVector4    m_localAxisOfRotation;    ///< +0x90
        hkVector4    m_localFacingDirection;   ///< +0xa0
        hkBool       m_resultIsDelta;          ///< +0xb0

        hkbComputeRotationToTargetModifier();
        /// @brief The cloneNode copy: every member, the output included.
        hkbComputeRotationToTargetModifier(const hkbComputeRotationToTargetModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
