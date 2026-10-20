/// @file
/// @brief hkbAttributeModifierUtils: the attribute writes behind hkbAttributeModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0cbb0, in the run of out-of-line modify helpers of a
///       dozen node classes (see hkbRotateCharacterModifierUtils.h). The file name is inferred.
#pragma once

#include <Behavior/Modifier/Attribute/hkbAttributeModifier.h>

namespace CB { namespace havok
{
    struct hkbGeneratorOutput;
    struct hkbSymbolIdMap;

    namespace hkbAttributeModifierUtils
    {
        /// @brief Writes each assignment's value into the output's attribute track, its id translated from `fromMap`'s
        ///        graph to `toMap`'s; ids that are unset, unmapped or past the track are skipped.
        /// @note Name provisional. Nothing happens when the output has no attribute track.
        void modify(const hkbAttributeModifier& mod, hkbGeneratorOutput& output, const hkbSymbolIdMap* fromMap, const hkbSymbolIdMap* toMap);
    }
} }
