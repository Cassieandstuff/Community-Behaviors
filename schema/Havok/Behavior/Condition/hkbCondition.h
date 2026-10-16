/// @file
/// @brief hkbCondition: a test a state machine transition can be guarded by.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;

    class hkbCondition : public hkReferencedObject
    {
    public:
        HK_FORCE_INLINE hkbCondition() {}
        HK_FORCE_INLINE hkbCondition(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag) {}

        /// @brief Whether the condition holds in `context`.
        /// @note Pure in vanilla's hkbCondition vtable (0x14195e5c8 slot 3 = _purecall).
        virtual bool isTrue(const hkbContext& context) = 0;   ///< slot 3
    };
} }
