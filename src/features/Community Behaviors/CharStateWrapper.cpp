#include "PCH.h"

#include <feature-sdk/havok/Physics/Utilities/CharacterControl/StateMachine/hkpCharacterStateManager.h>

#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

/// @file
/// @brief Step 1 of the character controller spec: OnGround, Jumping and InAir run through CB's wrapper states. The
///        palette's hkpCharacterStateManager::getState returns a wrapper for ids 0 to 2; the wrapper's Simulate decides
///        per controller whether CB manages it (the player) and, for now, forwards every controller to the vanilla
///        state, logging that it ran. Behaviour stays vanilla.
/// @note Why getState and not registerState: one hkpCharacterStateManager is shared by every controller (created by
///       the bhkCharacterControllerCinfo constructor, 0x140e9c960), so a registered state would replace it for every
///       actor; the wrapper dispatches per controller instead.

namespace CB::feature {

    namespace {

        namespace hk = CB::schema;
        using GetState = havok::hkpCharacterStateManager::getState;
        using SimulateFn = void (*)(const void* state, hk::bhkCharacterController* controller);

        constexpr int kSlots        = 9;   ///< hkReferencedObject 0-2, getType 3, enter/leave 4-5, update/change 6-7, Simulate 8
        constexpr int kSimulateSlot = 8;
        constexpr int kWrapped      = 3;   ///< OnGround 0, Jumping 1, InAir 2

        /// @brief Object sizes per vanilla (bhkCharacterState.h): OnGround and InAir 0x18, Jumping 0x10.
        constexpr std::array<std::size_t, kWrapped> kStateSize = { 0x18, 0x10, 0x18 };

        /// @brief A byte copy of one vanilla state object under a cloned vtable whose Simulate is CB's.
        /// @invariant Every slot but Simulate is vanilla's own code, run on a byte-identical copy (the shared state
        ///            objects hold configuration only, never per-character data).
        /// @note Reference counting: m_memSizeAndFlags 0 and a reference count of 0x4000 so no add/removeReference
        ///       pair can ever free it, whichever rule Havok applies (removeReference is out of line, unread).
        /// @note vtable[0] is vanilla's RTTI complete-object locator (MSVC reads it at vptr[-1] for dynamic_cast); the
        ///       object's vptr points at vtable[1].
        struct Wrapper {
            alignas(16) std::uint8_t     bytes[0x20]{};
            std::array<const void*, kSlots + 1> vtable{};
            const void*                  vanilla = nullptr;           ///< the vanilla state object it copies
            SimulateFn                   vanillaSimulate = nullptr;
            std::atomic<bool>            ready{ false };
        };

        std::array<Wrapper, kWrapped> g_wrappers;
        std::mutex                    g_buildLock;

        bool IsManaged(const hk::bhkCharacterController* controller)
        {
            auto*       player = RE::PlayerCharacter::GetSingleton();
            const auto* own    = player ? player->GetCharController() : nullptr;
            return own && static_cast<const void*>(controller) == static_cast<const void*>(own);
        }

        /// @brief Player calls per state id, logged at the first call and every power of two.
        std::array<std::atomic<std::uint64_t>, kWrapped> g_playerCalls{};
        std::atomic<int>                                 g_lastPlayerState{ -1 };

        template <int Id>
        void WrapperSimulate(const void* /*self*/, hk::bhkCharacterController* controller)
        {
            const Wrapper& w = g_wrappers[Id];
            if (IsManaged(controller)) {
                const std::uint64_t n = g_playerCalls[Id].fetch_add(1, std::memory_order_relaxed) + 1;
                if ((n & (n - 1)) == 0) LOG_INFO("state wrapper: player Simulate in state {} ran {} times through CB", Id, n);
                const int previous = g_lastPlayerState.exchange(Id, std::memory_order_relaxed);
                if (previous != Id) LOG_INFO("state wrapper: player state {} -> {}", previous, Id);
            }
            w.vanillaSimulate(w.vanilla, controller);
        }

        constexpr std::array<SimulateFn, kWrapped> kWrapperSimulate = { &WrapperSimulate<0>, &WrapperSimulate<1>, &WrapperSimulate<2> };

        /// @pre `vanilla` is the manager's registered state for `id` (0 to 2).
        void Build(int id, const void* vanilla)
        {
            Wrapper& w = g_wrappers[id];
            const auto* vanillaTable = *static_cast<const void* const* const*>(vanilla);
            std::memcpy(w.bytes, vanilla, kStateSize[id]);
            for (int s = -1; s < kSlots; s++) w.vtable[s + 1] = vanillaTable[s];
            w.vanillaSimulate           = reinterpret_cast<SimulateFn>(vanillaTable[kSimulateSlot]);
            w.vtable[kSimulateSlot + 1] = reinterpret_cast<const void*>(kWrapperSimulate[id]);
            const void* table = w.vtable.data() + 1;
            std::memcpy(w.bytes, &table, sizeof table);
            const std::uint16_t memSizeAndFlags = 0;
            const std::int16_t  referenceCount  = 0x4000;
            std::memcpy(w.bytes + 0x08, &memSizeAndFlags, sizeof memSizeAndFlags);
            std::memcpy(w.bytes + 0x0a, &referenceCount, sizeof referenceCount);
            w.vanilla = vanilla;
            w.ready.store(true, std::memory_order_release);
            LOG_INFO("state wrapper: state {} wraps vanilla {:#x} (vtable {:#x}, Simulate {:#x})", id, reinterpret_cast<std::uintptr_t>(vanilla),
                     reinterpret_cast<std::uintptr_t>(vanillaTable), reinterpret_cast<std::uintptr_t>(vanillaTable[kSimulateSlot]));
        }

        hk::hkpCharacterState* OnGetState(GetState::Next next, const hk::hkpCharacterStateManager* manager, hk::hkpCharacterStateType type)
        {
            hk::hkpCharacterState* vanilla = next(manager, type);
            const int id = static_cast<int>(type);
            if (!vanilla || id < 0 || id >= kWrapped) return vanilla;
            Wrapper& w = g_wrappers[id];
            if (!w.ready.load(std::memory_order_acquire) || w.vanilla != vanilla) {
                std::scoped_lock lock(g_buildLock);
                if (!w.ready.load(std::memory_order_relaxed)) Build(id, vanilla);
                else if (w.vanilla != vanilla) {
                    LOG_WARN("state wrapper: state {} changed under the wrapper ({:#x} -> {:#x}); passing vanilla through", id,
                             reinterpret_cast<std::uintptr_t>(w.vanilla), reinterpret_cast<std::uintptr_t>(vanilla));
                    return vanilla;
                }
            }
            return reinterpret_cast<hk::hkpCharacterState*>(w.bytes);
        }

        [[maybe_unused]] const bool s_subscribed =
            GetState::Subscribe(&OnGetState, core::hooks::Priority::Normal, "CharStateWrapper", "char_state_wrapper");

    }  // namespace

}  // namespace CB::feature
