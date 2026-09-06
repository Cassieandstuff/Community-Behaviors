#include "PCH.h"

#include "SyncClipProbe.h"

#include <mutex>
#include <unordered_set>

namespace CB::syncprobe {

    // BSSynchronizedClipGenerator vtable — REL::VariantID(SE 281992, AE 232610, VR 0x17c4f88).
    // Layout (from CommonLibSSE-NG): name(hkStringPtr) @0x38 (inherited hkbNode), clipGenerator
    // @0x50, syncAnimPrefix(hkStringPtr) @0x58, leadCharacter(bool) @0x6C.
    // Virtual slots (shared hkbNode/hkbGenerator/hkbClipGenerator table):
    //   4  = Activate(const hkbContext&)   — fires ONCE when the clip is entered
    //   0x17 = Generate(const hkbContext&) — fires EVERY frame the clip is live (produces the pose)
    // Hooking both answers: does a paired clip ever get entered (Activate), and if not, does it
    // ever execute at all (Generate)? Activate silent + Generate silent == the sync clip never
    // runs (failure is upstream of the clip); Generate firing without Activate == Activate bypassed.
    namespace {
        constexpr std::size_t kActivateSlot = 4;
        constexpr std::size_t kGenerateSlot = 0x17;
        constexpr std::size_t kOffName      = 0x38;
        constexpr std::size_t kOffSyncPfx   = 0x58;
        constexpr std::size_t kOffLead      = 0x6C;

        // Activate (slot 4) is the 2-arg hkbNode::activate(this, const hkbContext&). Generate
        // (slot 0x17) is hkbGenerator::generate(this, const hkbContext&, const hkbGeneratorOutput**
        // activeChildrenOutput, hkbGeneratorOutput& output, float timeOffset) — FIVE args
        // (RCX/RDX/R8/R9 + stack). A pass-through observational hook MUST carry EVERY argument
        // through: forwarding only (this, context) leaves R8/R9/timeOffset as stale garbage, so the
        // real generate reads a null output buffer (R9) and the engine faults in hkbClipGenerator::
        // Generate (mov rax,[rbx], rbx=0). Distinct typedefs keep each slot's ABI exact.
        using ActivateFn = void (*)(void* a_this, const void* a_context);
        using GenerateFn = void (*)(void* a_this, const void* a_context, void* a_activeChildrenOutput,
                                    void* a_output, float a_timeOffset);
        ActivateFn s_origActivate = nullptr;
        GenerateFn s_origGenerate = nullptr;

        // Generate fires per-frame; log each distinct clip only ONCE so the run doesn't drown.
        std::mutex                    s_seenMx;
        std::unordered_set<const void*> s_seenGenerate;

        // hkStringPtr stores the char* with bit0 as an "owned" flag — mask it off.
        const char* hkstr(std::uintptr_t obj, std::size_t off) {
            const auto raw = *reinterpret_cast<std::uintptr_t*>(obj + off);
            const auto p   = reinterpret_cast<const char*>(raw & ~std::uintptr_t{ 1 });
            return p ? p : "";
        }

        void Hook_Activate(void* a_this, const void* a_context) {
            const auto self = reinterpret_cast<std::uintptr_t>(a_this);
            LOG_INFO("SyncProbe: ACTIVATE '{}'  prefix='{}'  {}",
                     hkstr(self, kOffName), hkstr(self, kOffSyncPfx),
                     *reinterpret_cast<bool*>(self + kOffLead) ? "LEAD" : "support");
            s_origActivate(a_this, a_context);
        }

        void Hook_Generate(void* a_this, const void* a_context, void* a_activeChildrenOutput,
                           void* a_output, float a_timeOffset) {
            bool first = false;
            {
                std::lock_guard<std::mutex> lk(s_seenMx);
                first = s_seenGenerate.insert(a_this).second;
            }
            if (first) {
                const auto self = reinterpret_cast<std::uintptr_t>(a_this);
                LOG_INFO("SyncProbe: GENERATE (running) '{}'  prefix='{}'  {}",
                         hkstr(self, kOffName), hkstr(self, kOffSyncPfx),
                         *reinterpret_cast<bool*>(self + kOffLead) ? "LEAD" : "support");
            }
            // Forward ALL five args — dropping any of them corrupts the real generate's call.
            s_origGenerate(a_this, a_context, a_activeChildrenOutput, a_output, a_timeOffset);
        }

        template <class FnT>
        bool hookSlot(std::uintptr_t vtblAddr, std::size_t slot, void* detour, FnT& origOut) {
            const std::uintptr_t at = vtblAddr + slot * sizeof(void*);
            origOut = reinterpret_cast<FnT>(*reinterpret_cast<std::uintptr_t*>(at));
            REL::safe_write<std::uintptr_t>(at, reinterpret_cast<std::uintptr_t>(detour));
            return origOut != nullptr;
        }
    }

    bool Install() {
        try {
            const bool ae = REL::Module::get().version()[1] >= 6;   // 1.6.x = AE
            REL::Relocation<std::uintptr_t> vtbl{ REL::ID(ae ? 232610 : 281992) };  // BSSynchronizedClipGenerator vtable (AE : SE)
            hookSlot(vtbl.address(), kActivateSlot, reinterpret_cast<void*>(&Hook_Activate), s_origActivate);
            hookSlot(vtbl.address(), kGenerateSlot, reinterpret_cast<void*>(&Hook_Generate), s_origGenerate);
            LOG_INFO("SyncProbe: hooked BSSynchronizedClipGenerator Activate(slot {}, orig {:X}) + "
                     "Generate(slot {:#x}, orig {:X}) at vtable {:X}. Logging paired-clip entry + execution.",
                     kActivateSlot, reinterpret_cast<std::uintptr_t>(s_origActivate),
                     kGenerateSlot, reinterpret_cast<std::uintptr_t>(s_origGenerate), vtbl.address());
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR("SyncProbe: install failed: {}", e.what());
            return false;
        }
    }

}  // namespace CB::syncprobe
