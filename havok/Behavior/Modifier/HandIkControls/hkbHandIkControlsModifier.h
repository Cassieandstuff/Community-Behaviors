/// @file
/// @brief hkbHandIkControlsModifier: writes per-hand IK targets into the generator output's hand IK tracks.
#pragma once

#include <Behavior/Modifier/HandIkControls/hkbHandIkControlData.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note 0x60 bytes; members per the exe's reflected class. The `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: the scalar deleting destructor (slot 0) inlines it. Vanilla's TU has no default
    ///       constructor.
    class hkbHandIkControlsModifier : public hkbModifier
    {
    public:
        /// @note 0x70 bytes (hkbHandIkControlsModifierHand). Not POD to containers: vanilla's array copy null-checks each
        ///       copy-constructed element.
        struct Hand
        {
            hkbHandIkControlData m_controlData;   ///< +0x00
            /// @brief Which of the character's hands (an index into its hand IK setup, 0-3).
            int                  m_handIndex;     ///< +0x60
            hkBool               m_enable;        ///< +0x64
        };

        hkArray<Hand> m_hands;   ///< +0x50

        /// @brief The cloneNode copy.
        hkbHandIkControlsModifier(const hkbHandIkControlsModifier& mod);

        /// @brief Releases each hand's target handle.
        virtual void     deactivate(const hkbContext& context);                         ///< slot 7
        /// @brief At most four hands, each with a distinct index below four.
        virtual bool     isValid(hkStringPtr& errorString) const;                       ///< slot 10
        /// @brief Shares the template itself when nothing is bound (no per-instance state), else copies it.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
    static_assert(sizeof(hkbHandIkControlsModifier::Hand) == 0x70, "hkbHandIkControlsModifier::Hand size");
    static_assert(sizeof(hkbHandIkControlsModifier) == 0x60, "hkbHandIkControlsModifier size");
} }
