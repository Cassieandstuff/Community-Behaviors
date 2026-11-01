#include "PCH.h"

#include <PluginLogger.h>

#include <intrin.h>

/// @file
/// @brief Functions the SDK's code names but whose exe address is not known yet: each links to a trap of its own.
/// @warning A stand-in so the plugin links, not an implementation: reaching one logs its name and caller and stops the
///          game. The real fix is a pin or a recovered body in the SDK.

namespace {

    [[noreturn]] void Trapped(const char* name, void* caller)
    {
        HMODULE owner = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(caller), &owner);
        LOG_ERROR("unbound SDK function called: {} from module+{:#x}", name,
                     reinterpret_cast<std::uintptr_t>(caller) - reinterpret_cast<std::uintptr_t>(owner));
        SKSE::stl::report_and_fail(std::string("Community Behaviors: unbound SDK function called: ") + name);
    }

}  // namespace

extern "C" {

[[noreturn]] void cb_unbound_trap_0() { Trapped("??0StateInfo@hkbStateMachine@@QEAA@AEBV01@AEBV?$hkArray@PEAVhkbStateListener@@UhkContainerHeapAllocator@@@@@Z", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_1() { Trapped("?blend@hkaBlender@@YAXPEIAMPEIBM1AEBVhkSimdReal@@HW4BLEND_MODE@1@@Z", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_2() { Trapped("?blend@hkaBlender@@YAXPEIAVhkQsTransform@@PEIBV2@1AEBVhkSimdReal@@HW4BLEND_MODE@1@W4ROTATION_MODE@1@@Z", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_3() { Trapped("?getClassType@hkbBoneWeightArray@@UEBAPEBVhkClass@@XZ", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_4() { Trapped("?handleEndian@hkaQuantizedAnimation@@AEAAXXZ", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_5() { Trapped("?handleEndian@hkaSplineCompressedAnimation@@AEAAXXZ", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_6() { Trapped("sampleDynamicScalarsInternal", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_7() { Trapped("unpackSignedQuaternion128", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_8() { Trapped("unpackSignedQuaternion16", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_9() { Trapped("unpackSignedQuaternion24", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_10() { Trapped("unpackSignedQuaternion32", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_11() { Trapped("unpackSignedQuaternion40", _ReturnAddress()); }
[[noreturn]] void cb_unbound_trap_12() { Trapped("unpackSignedQuaternion48", _ReturnAddress()); }

}

#pragma comment(linker, "/alternatename:??0StateInfo@hkbStateMachine@@QEAA@AEBV01@AEBV?$hkArray@PEAVhkbStateListener@@UhkContainerHeapAllocator@@@@@Z=cb_unbound_trap_0")
#pragma comment(linker, "/alternatename:?blend@hkaBlender@@YAXPEIAMPEIBM1AEBVhkSimdReal@@HW4BLEND_MODE@1@@Z=cb_unbound_trap_1")
#pragma comment(linker, "/alternatename:?blend@hkaBlender@@YAXPEIAVhkQsTransform@@PEIBV2@1AEBVhkSimdReal@@HW4BLEND_MODE@1@W4ROTATION_MODE@1@@Z=cb_unbound_trap_2")
#pragma comment(linker, "/alternatename:?getClassType@hkbBoneWeightArray@@UEBAPEBVhkClass@@XZ=cb_unbound_trap_3")
#pragma comment(linker, "/alternatename:?handleEndian@hkaQuantizedAnimation@@AEAAXXZ=cb_unbound_trap_4")
#pragma comment(linker, "/alternatename:?handleEndian@hkaSplineCompressedAnimation@@AEAAXXZ=cb_unbound_trap_5")
#pragma comment(linker, "/alternatename:sampleDynamicScalarsInternal=cb_unbound_trap_6")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion128=cb_unbound_trap_7")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion16=cb_unbound_trap_8")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion24=cb_unbound_trap_9")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion32=cb_unbound_trap_10")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion40=cb_unbound_trap_11")
#pragma comment(linker, "/alternatename:unpackSignedQuaternion48=cb_unbound_trap_12")
