/// @file
/// @brief hkbMoveCharacterModifier: moves the character by a model-space velocity.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    class hkbMoveCharacterModifierInternalState : public hkReferencedObject
    {
    public:
        hkReal m_timeSinceLastModify;   ///< +0x10
    };

    /// @note Sized 0x70. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbMoveCharacterModifier : public hkbModifier
    {
    public:
        hkVector4 m_offsetPerSecondMS;     ///< +0x50
        /// @brief The time update has accumulated since modify last moved the character; modify applies and clears it.
        hkReal    m_timeSinceLastModify;   ///< +0x60

        hkbMoveCharacterModifier();
        /// @brief The cloneNode copy: the velocity, with no time pending.
        hkbMoveCharacterModifier(const hkbMoveCharacterModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);               ///< slot 5
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
