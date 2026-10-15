/// @file
/// @brief hkbSetWorldFromModelModifier: overwrites the character's world-from-model translation and/or rotation.
#pragma once

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    /// @note Sized 0x80. The destructor is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbSetWorldFromModelModifier : public hkbModifier
    {
    public:
        hkVector4    m_translation;      ///< +0x50
        hkQuaternion m_rotation;         ///< +0x60
        hkBool       m_setTranslation;   ///< +0x70
        hkBool       m_setRotation;      ///< +0x71

        hkbSetWorldFromModelModifier();
        /// @brief The cloneNode copy.
        hkbSetWorldFromModelModifier(const hkbSetWorldFromModelModifier& mod);

        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void     modify(const hkbContext& context, hkbGeneratorOutput& inOut);   ///< slot 23
    };
} }
