/// @file
/// @brief The scalar deleting destructors of hkBaseObject and hkReferencedObject as the exe keeps them: COMDAT copies
///        from a Bethesda translation unit (0x1402b0730, 0x1402b0760), a matching device rather than a vanilla file.
/// @note Bethesda's code is built with exceptions on (/EHsc, set on this unit): hkReferencedObject's copy carries the
///       EH frame state (`mov [rsp+0x20], -2`) and the call-site nops. VS2015 with /EHsc gives VARIANT; Bethesda's own
///       VS2017 15.9 (19.16.27054) gives it EXACT.
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Instantiates both vtables, which brings their deleting destructors into this unit.
    hkReferencedObject* hkReferencedObjectBethesdaCopy_instantiate()
    {
        return new hkReferencedObject;
    }
} }
