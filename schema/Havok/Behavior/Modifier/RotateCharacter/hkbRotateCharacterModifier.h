/// @file
/// @brief hkbRotateCharacterModifier: turns the character about an axis at a set rate.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    struct hkbRotateCharacterModifierInternalState : hkReferencedObject
    {
        hkReal m_angle;   ///< +0x10
    };

    /// @note The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla's scalar deleting
    ///       destructor (slot 0) inlines it.
    class hkbRotateCharacterModifier : public hkbModifier
    {
    public:
        hkReal    m_degreesPerSecond;   ///< +0x50
        hkReal    m_speedMultiplier;    ///< +0x54
        hkVector4 m_axisOfRotation;     ///< +0x60
        /// @brief The rotation accumulated by update (radians) and not yet applied; modify applies it and clears it.
        hkReal    m_angle;              ///< +0x70

        hkbRotateCharacterModifier();
        /// @brief The cloneNode copy: the settings, with no rotation pending.
        hkbRotateCharacterModifier(const hkbRotateCharacterModifier& mod);

        virtual void                activate(const hkbContext& context);                          ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        virtual hkReferencedObject* createInternalState();                                        ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;   ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);   ///< slot 16
        virtual void                modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
