#include "PCH.h"

#include <core/debug/Settings.h>
#include <platform/startup/Startup.h>

#include <algorithm>
#include <vector>

/// @note windows.h's GetObject macro (GetObjectW) would rename BGSDefaultObjectManager::GetObject.
#undef GetObject

/// @file
/// @brief Puts every mountable race on the proxy character controller (horse_proxy_controller, spec H8 + H9), so the
///        Advanced Controller's ground layer can run for the ridden horse.
/// @note  Why the race flag: the engine picks the controller once, when an actor's Havok is set up (0x14065d590): the
///        proxy for the player, for any actor while [HAVOK] bUseCharacterRB is 0, or when the race has
///        kAlwaysUseProxyController; else the rigid body. Nothing rebuilds a live controller, and mounting does not, so
///        the flag is set at kDataLoaded, before any actor exists. Every horse is then built on the proxy, NPC horses too;
///        CB drives only the one the player rides, the rest run vanilla's proxy logic.
/// @note  "Mountable" is the engine's own test (Actor::Revert 0x140688f30.., Actor kIsAMount): the default objects
///        kKeywordHorse or kKeywordMount, on the actor's race or its base. Vanilla's horses carry ActorTypeHorse on
///        their bases only (32 of them in Skyrim.esm; HorseRace has none), so the races flagged are those of every actor
///        base that carries one, plus every race that carries one itself. Flying races (dragons) are left out.

namespace CB::feature::controller {

    namespace {

        /// @note Through the runtime-mapped DefaultObjectID: the per-runtime DEFAULT_OBJECT enum lacks kKeywordMount here.
        RE::BGSKeyword* DefaultKeyword(RE::BGSDefaultObjectManager* objects, RE::DefaultObjectID id)
        {
            RE::BGSKeyword** slot = objects ? objects->GetObject<RE::BGSKeyword>(id) : nullptr;
            return slot ? *slot : nullptr;
        }

        void FlagMountableRaces()
        {
            if (!core::debug::FlagOn("horse_proxy_controller")) return;
            auto* objects  = RE::BGSDefaultObjectManager::GetSingleton();
            auto* data     = RE::TESDataHandler::GetSingleton();
            auto* horseKey = DefaultKeyword(objects, RE::DefaultObjectID::kKeywordHorse);
            auto* mountKey = DefaultKeyword(objects, RE::DefaultObjectID::kKeywordMount);
            if (!data || (!horseKey && !mountKey)) {
                LOG_WARN("mount proxy: no horse or mount keyword in the default objects - no race changed");
                return;
            }
            using Flag = RE::RACE_DATA::Flag;
            const auto keyed = [&](RE::BGSKeywordForm* form) {
                return form && ((horseKey && form->HasKeyword(horseKey)) || (mountKey && form->HasKeyword(mountKey)));
            };
            std::vector<RE::TESRace*> races;
            const auto add = [&](RE::TESRace* race) {
                if (race && std::find(races.begin(), races.end(), race) == races.end()) races.push_back(race);
            };
            for (auto* race : data->GetFormArray<RE::TESRace>())
                if (keyed(race)) add(race);
            for (auto* npc : data->GetFormArray<RE::TESNPC>())
                if (npc && keyed(npc)) add(npc->GetRace());
            int flagged = 0, already = 0;
            for (auto* race : races) {
                if (race->data.flags.any(Flag::kFlies)) continue;
                if (race->data.flags.any(Flag::kAlwaysUseProxyController)) {
                    ++already;
                    continue;
                }
                race->data.flags.set(Flag::kAlwaysUseProxyController);
                ++flagged;
                LOG_INFO("mount proxy: {} ({:08X}) now always uses the proxy controller", race->GetFormEditorID(), race->GetFormID());
            }
            LOG_INFO("mount proxy: {} mountable race(s) put on the proxy controller, {} already were", flagged, already);
        }

        [[maybe_unused]] const bool s_registered = platform::startup::OnDataLoaded(&FlagMountableRaces, "mount proxy (horse_proxy_controller)");

    }  // namespace

}  // namespace CB::feature::controller
