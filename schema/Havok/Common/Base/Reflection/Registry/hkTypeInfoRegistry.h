/// @file
/// @brief hkTypeInfoRegistry: finishes objects placed in memory by name (vtables, finish constructors).
#pragma once

#include <Common/Base/Object/hkReferencedObject.h>

namespace CB { namespace schema
{
    class hkTypeInfoRegistry : public hkReferencedObject
    {
    public:
        /// @note Name and signature unidentified; declared to keep slot 4 in place.
        virtual void        vfn3();                                                          ///< slot 3
        /// @brief Runs the finish constructor of the class named `className` on `obj` (its vtable and members).
        /// @note Name after the Havok SDK's finishLoadedObject; the result (Havok's hkTypeInfo) is opaque here.
        virtual const void* finishLoadedObject(void* obj, const char* className) const;   ///< slot 4
    };
} }
