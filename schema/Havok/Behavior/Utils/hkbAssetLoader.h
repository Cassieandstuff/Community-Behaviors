/// @file
/// @brief hkbAssetLoader: the interface a behavior project loads its assets through.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes; not reflected. Its two own virtuals (slots 3 and 4) are pure in vanilla's vtable; their names and
    ///       signatures are not identified yet.
    class hkbAssetLoader : public hkReferencedObject
    {
    public:
        /// @note Name provisional: an array of pointer-sized entries the destructor frees.
        hkArray<void*> m_entries;   ///< +0x10

        /// @note User-provided (vanilla's out-of-line copy, 0x140bc6d00, stores the class's vtable first).
        virtual ~hkbAssetLoader() {}
        virtual void vfn3() = 0;
        virtual void vfn4() = 0;
    };
} }
