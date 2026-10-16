/// @file
/// @brief hkBuiltinTypeRegistry: the singleton holding the type registries of the classes built into the exe.
#pragma once

#include <Common/Base/Reflection/Registry/hkTypeInfoRegistry.h>
#include <Common/Base/Reflection/Registry/hkVtableClassRegistry.h>

namespace CB { namespace schema
{
    class hkBuiltinTypeRegistry : public hkReferencedObject
    {
    public:
        /// @note Havok's hkSingleton<hkBuiltinTypeRegistry>::s_instance.
        static hkBuiltinTypeRegistry* s_instance;
        static HK_FORCE_INLINE hkBuiltinTypeRegistry& getInstance() { return *s_instance; }

        /// @note Slots 3 and 4 named per the Havok SDK's order, unverified.
        virtual hkTypeInfoRegistry*    getTypeInfoRegistry();      ///< slot 3
        virtual void*                  getClassNameRegistry();     ///< slot 4
        virtual hkVtableClassRegistry* getVtableClassRegistry();   ///< slot 5
    };
} }
