#include "PCH.h"

/// @file
/// @brief Functions the SDK's shipped code names but whose exe address is not known yet: each links to a trap.
/// @warning A stand-in so the plugin links, not an implementation: reaching one stops the game with a message, and the
///          real fix is a pin or a recovered body in the SDK. The trap cannot tell which function was called.

extern "C" [[noreturn]] void cb_unbound_trap()
{
    SKSE::stl::report_and_fail("Community Behaviors: an SDK function with no known exe address was called (see UnboundTraps.cpp)");
}

#pragma comment(linker, "/alternatename:??0StateInfo@hkbStateMachine@@QEAA@AEBV01@AEBV?$hkArray@PEAVhkbStateListener@@UhkContainerHeapAllocator@@@@@Z=cb_unbound_trap")
#pragma comment(linker, "/alternatename:?blend@hkaBlender@@YAXPEIAMPEIBM1AEBVhkSimdReal@@HW4BLEND_MODE@1@@Z=cb_unbound_trap")
#pragma comment(linker, "/alternatename:?blend@hkaBlender@@YAXPEIAVhkQsTransform@@PEIBV2@1AEBVhkSimdReal@@HW4BLEND_MODE@1@W4ROTATION_MODE@1@@Z=cb_unbound_trap")
#pragma comment(linker, "/alternatename:?getClassType@hkbBoneWeightArray@@UEBAPEBVhkClass@@XZ=cb_unbound_trap")
#pragma comment(linker, "/alternatename:?handleEndian@hkaQuantizedAnimation@@AEAAXXZ=cb_unbound_trap")
#pragma comment(linker, "/alternatename:?handleEndian@hkaSplineCompressedAnimation@@AEAAXXZ=cb_unbound_trap")
#pragma comment(linker, "/alternatename:sampleDynamicScalarsInternal=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion128=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion16=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion24=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion32=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion40=cb_unbound_trap")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion48=cb_unbound_trap")
