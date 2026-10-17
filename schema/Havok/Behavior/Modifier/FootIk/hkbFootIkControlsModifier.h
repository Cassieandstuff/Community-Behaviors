/// @file
/// @brief hkbFootIkControlsModifier: drives the character's foot IK driver through the foot IK controls track, and
///        reports the driver's per-leg ground state back.
#pragma once

#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Modifier/FootIk/hkbFootIkGains.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Sized 0xb0. The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla calls
    ///       it out of line (0x140ac6980) from the scalar deleting destructor (slot 0). Vanilla's TU has no default
    ///       constructor.
    class hkbFootIkControlsModifier : public hkbModifier
    {
    public:
        /// @brief One leg's ground state, copied from the driver.
        /// @note 0x30 bytes (Havok's hkbFootIkControlsModifierLeg).
        struct Leg
        {
            hkVector4        m_groundPosition;    ///< +0x00
            /// @brief Raised while the leg is planted but its ray hit nothing.
            hkbEventProperty m_ungroundedEvent;   ///< +0x10
            hkReal           m_verticalError;     ///< +0x20
            hkBool           m_hitSomething;      ///< +0x24
            hkBool           m_isPlantedMS;       ///< +0x25
        };

        hkbFootIkControlData m_controlData;               ///< +0x50
        hkArray<Leg>         m_legs;                      ///< +0x80
        hkVector4            m_errorOutTranslation;       ///< +0x90
        hkQuaternion         m_alignWithGroundRotation;   ///< +0xa0

        /// @brief The cloneNode copy: the control data, with every leg's ground state cleared.
        /// @note The legs are not copied (the reset loop runs over the copy's own, empty array), as in vanilla.
        hkbFootIkControlsModifier(const hkbFootIkControlsModifier& mod);

        virtual void     activate(const hkbContext& context);                              ///< slot 4
        /// @brief Raises each leg's ungrounded event while it is planted but its ray hit nothing.
        virtual void     update(const hkbContext& context, hkReal timestep);               ///< slot 5
        /// @brief Shares the template itself when nothing is bound, else copies it.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);    ///< slot 23
    };
    static_assert(sizeof(hkbFootIkControlsModifier) == 0xb0, "hkbFootIkControlsModifier size");
} }
