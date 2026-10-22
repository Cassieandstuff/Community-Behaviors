/// @file
/// @brief Matching device for hkaRagdollRaycastInterface's scalar deleting destructor: the interface is header-only, and
///        vanilla's linked copy (0x140e9bfb0) is a COMDAT from Bethesda's bhkRagdollPenetrationUtil unit, built with
///        C++ exceptions on.
/// @note Not a Havok file: compiled with /EHsc so the same COMDAT comes out; newRaycastInterface only makes the unit
///       emit the interface's vtable (a trivial implementation's constructor stores it).
#include <Animation/Ragdoll/PenetrationDetection/hkaRagdollRaycastInterface.h>

namespace CB { namespace havok
{
    namespace
    {
        class hkaRagdollRaycastInterfaceEhUnit_Implementation : public hkaRagdollRaycastInterface
        {
        public:
            virtual hkBool castRay(const hkpAabbPhantom*, const hkVector4&, const hkVector4&, hkVector4&, hkVector4&) { return false; }
        };
    }

    /// @note Matching device only (see the file note).
    hkaRagdollRaycastInterface* hkaRagdollRaycastInterfaceEhUnit_newRaycastInterface()
    {
        return new hkaRagdollRaycastInterfaceEhUnit_Implementation;
    }
} }
