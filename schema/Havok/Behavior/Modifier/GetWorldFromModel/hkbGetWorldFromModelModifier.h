/// @file
/// @brief hkbGetWorldFromModelModifier: publishes the output's world-from-model translation and rotation.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    class hkbGetWorldFromModelModifierInternalState : public hkReferencedObject
    {
    public:
        hkVector4    m_translationOut;   ///< +0x10
        hkQuaternion m_rotationOut;      ///< +0x20
    };

    /// @note Sized 0x70. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbGetWorldFromModelModifier : public hkbModifier
    {
    public:
        hkVector4    m_translationOut;   ///< +0x50
        hkQuaternion m_rotationOut;      ///< +0x60

        hkbGetWorldFromModelModifier();
        /// @brief The cloneNode copy: the outputs reset.
        hkbGetWorldFromModelModifier(const hkbGetWorldFromModelModifier& mod);

        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
