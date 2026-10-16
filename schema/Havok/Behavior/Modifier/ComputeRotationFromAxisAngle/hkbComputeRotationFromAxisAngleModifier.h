/// @file
/// @brief hkbComputeRotationFromAxisAngleModifier: builds a rotation from an axis and an angle in degrees.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkbComputeRotationFromAxisAngleModifierInternalState : public hkReferencedObject
    {
    public:
        hkQuaternion m_rotationOut;   ///< +0x10
    };

    /// @note Sized 0x80. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbComputeRotationFromAxisAngleModifier : public hkbModifier
    {
    public:
        hkQuaternion m_rotationOut;    ///< +0x50
        hkVector4    m_axis;           ///< +0x60
        hkReal       m_angleDegrees;   ///< +0x70

        hkbComputeRotationFromAxisAngleModifier();
        /// @brief The cloneNode copy: every member, the output included.
        hkbComputeRotationFromAxisAngleModifier(const hkbComputeRotationFromAxisAngleModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
