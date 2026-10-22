/// @file
/// @brief hkpWorldConstraintUtil: adding, finding and removing constraints and their modifier atoms in a world.
#pragma once

#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>

namespace CB { namespace havok
{
    class hkpConstraintInstance;
    class hkpConstraintOwner;

    class hkpWorldConstraintUtil
    {
    public:
        /// @brief The first modifier of `type` in `instance`'s modifier chain, or null.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        static hkpModifierConstraintAtom* findModifier(hkpConstraintInstance* instance, hkpConstraintAtom::AtomType type);
        /// @brief Links `newModifier` into `instance`'s modifier chain.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        static void addModifier(hkpConstraintInstance* instance, hkpConstraintOwner& constraintOwner, hkpModifierConstraintAtom* newModifier);
    };
} }
