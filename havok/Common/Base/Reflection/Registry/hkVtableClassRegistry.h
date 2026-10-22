/// @file
/// @brief hkVtableClassRegistry: maps a polymorphic object's vtable to its hkClass.
#pragma once

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/Reflection/hkClass.h>

namespace CB { namespace havok
{
    class hkVtableClassRegistry : public hkReferencedObject
    {
    public:
        /// @note Name and signature unidentified; declared to keep slot 4 in place.
        virtual void           vfn3();                                                ///< slot 3
        /// @return The class registered for `obj`'s vtable, or null.
        virtual const hkClass* getClassFromVirtualInstance(const void* obj) const;   ///< slot 4
    };
} }
