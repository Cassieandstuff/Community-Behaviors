#pragma once

#include <platform/hooks/Chain.h>

#include <cstddef>
#include <cstdint>
#include <string>

/// @file
/// @brief VirtualSlot<Slot, Sig>: subscribe to one entry of a class's vtable — the virtual function every object of
///        exactly that class dispatches through. A subclass with its own vtable keeps its own entry, even where it
///        inherits the same function.
/// @note The identity is CommonLib's: the vtable's REL::VariantID (RE::VTABLE_<Class>, pinned for SE, AE and VR) and
///       the slot's index, so it needs no engine.yaml row.
/// @note Every object of the class runs the subscribers, so a subscriber that cares about one object filters by it.

namespace CB::platform::hooks {

    /// @brief `Slot` supplies `name`, `static REL::VariantID Table()` and `static std::size_t Index()`.
    /// @note Index() is per runtime: VR inserts virtual functions, shifting later slots.
    template <class Slot, class Sig>
    struct VirtualSlot : Chain<VirtualSlot<Slot, Sig>, Sig> {
        static constexpr const char* name = Slot::name;

        static bool Attach(void* detour, void** original, std::string& why, const char*& how)
        {
            const std::uintptr_t table = Slot::Table().address();
            if (!table) {
                why = "no vtable on this runtime";
                return false;
            }
            const std::uintptr_t entry = table + Slot::Index() * sizeof(std::uintptr_t);
            *original                  = *reinterpret_cast<void**>(entry);
            REL::safe_write(entry, reinterpret_cast<std::uintptr_t>(detour));
            how = "a vtable slot";
            return true;
        }
    };

}  // namespace CB::platform::hooks
