/// @file
/// @brief hkbAttributeModifier: writes constant values into the output's attribute track.
#pragma once

#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Sized 0x60 (no reflected schema YAML; the layout is the Havok SDK's and vanilla's). The `slot N` annotations
    ///       are vanilla's vtable slots; it keeps the base getClassType. The destructor is implicit: the scalar deleting
    ///       destructor (slot 0) inlines it. Vanilla's TU has no default constructor.
    class hkbAttributeModifier : public hkbModifier
    {
    public:
        /// @brief One attribute (an id internal to the modifier's behavior graph, -1 = unset) and the value to write.
        /// @note Havok's hkbAttributeModifierAssignment. Not POD to containers: vanilla's array copy null-checks each
        ///       copy-constructed element.
        struct Assignment
        {
            int    m_attributeIndex;   ///< +0x00
            hkReal m_attributeValue;   ///< +0x04

            HK_FORCE_INLINE Assignment() {}
        };

        hkArray<Assignment> m_assignments;   ///< +0x50

        /// @brief The cloneNode copy.
        hkbAttributeModifier(const hkbAttributeModifier& mod);

        /// @brief Shares the template itself when nothing is bound (no per-instance state), else copies it.
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
    static_assert(sizeof(hkbAttributeModifier) == 0x60, "hkbAttributeModifier size");
} }
