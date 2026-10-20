/// @file
/// @brief Matching device for hkbAssetLoader's destructors: the interface is header-only, and vanilla's linked copies
///        (0x140bc6d00, 0x140bc7680) are COMDATs from a Bethesda unit built with C++ exceptions on.
/// @note Not a Havok file: compiled with /EHsc so the same COMDATs come out; newAssetLoader only makes the unit emit the
///       interface's vtable (a trivial implementation's constructor stores it).
#include <Behavior/Utils/hkbAssetLoader.h>

namespace CB { namespace havok
{
    namespace
    {
        class hkbAssetLoaderEhUnit_Implementation : public hkbAssetLoader
        {
        public:
            virtual void vfn3() {}
            virtual void vfn4() {}
        };
    }

    /// @note Matching device only (see the file note).
    hkbAssetLoader* hkbAssetLoaderEhUnit_newAssetLoader()
    {
        return new hkbAssetLoaderEhUnit_Implementation;
    }
} }
