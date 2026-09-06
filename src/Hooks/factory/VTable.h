#pragma once
// Hooks/factory/VTable.h — engine-vtable accessors for objects we BUILD ourselves.
//
// These are NOT in-place vfunc swaps (see Install.h) — they READ an engine vtable so a
// hand-constructed object behaves like a genuine engine instance. Three idioms cover
// the ER / Havok-Injection use cases:
//
//   * stamp an engine vtable into a built object      -> VTablePtr   (obj->vtable = ...)
//   * identify an object's type by its vtable address -> VTableAddress (compare)
//   * clone an engine vtable, then override slots      -> CopyVTable  (synthesized state)
//
// `vtblId` is e.g. RE::VTABLE_hkbStateMachine[0] (a REL id / VariantID) or any REL id.
//
// Plugin-world only.

#include <PCH.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hooks {

// The resolved address of an engine vtable, as an integer — for identifying an
// object's runtime type: `*reinterpret_cast<std::uintptr_t*>(obj) == VTableAddress(id)`.
template <class VtblID>
[[nodiscard]] inline std::uintptr_t VTableAddress(VtblID vtblId) {
    return REL::Relocation<std::uintptr_t>{ vtblId }.address();
}

// The same address as a void* — stamp it into a hand-built object's vtable slot so the
// engine treats the object as a real instance: `obj->vtable = hooks::VTablePtr(id);`.
template <class VtblID>
[[nodiscard]] inline void* VTablePtr(VtblID vtblId) {
    return reinterpret_cast<void*>(VTableAddress(vtblId));
}

// Copy `count` slots of an engine vtable into `dst` (a caller-owned buffer), so the
// caller can override individual slots to synthesize a custom vtable (a trampoline
// vtable for a fabricated engine object).
template <class VtblID>
inline void CopyVTable(VtblID vtblId, std::uintptr_t* dst, std::size_t count) {
    const std::uintptr_t* src = REL::Relocation<std::uintptr_t*>{ vtblId }.get();
    std::memcpy(dst, src, count * sizeof(std::uintptr_t));
}

} // namespace hooks
