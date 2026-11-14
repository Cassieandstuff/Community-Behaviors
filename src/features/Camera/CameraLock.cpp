#include "PCH.h"

#include "CameraLock.h"

#include "CameraZoom.h"

#include <core/debug/Settings.h>
#include <platform/hooks/VirtualSlot.h>
#include <platform/startup/Startup.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <string>

namespace CB::feature::camera::lock {

    namespace {

        constexpr std::string_view kSection   = "Camera";
        constexpr std::uint32_t    kMouseBase = 256;
        /// @brief A look input this long (stick deflection, or the mouse's per-frame movement) counts as holding away.
        constexpr float kLookHeld = 0.3f;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        std::string Text(std::string_view key) { return core::debug::Value(kSection, key); }

        float ReadFloat(std::string_view key, float def)
        {
            const std::string text = Text(key);
            float             v    = 0.0f;
            const auto [end, ec]   = std::from_chars(text.data(), text.data() + text.size(), v);
            return text.empty() || ec != std::errc{} ? def : v;
        }

        long ReadInt(std::string_view key, long def)
        {
            const std::string text = Text(key);
            if (text.empty()) return def;
            try {
                return std::stol(text, nullptr, 0);
            } catch (...) {
                return def;
            }
        }

        struct Store {
            std::mutex lock;
            Settings   settings;
            bool       loaded = false;
        };
        Store& S()
        {
            static Store s;
            return s;
        }

        /// @pre `s.lock` held.
        void LoadOnce(Store& s)
        {
            if (s.loaded) return;
            s.loaded = true;
            Settings& v = s.settings;
            v.mode            = static_cast<Mode>(std::clamp<long>(ReadInt("iLockMode", static_cast<long>(v.mode)), 0, 3));
            v.maxTargets      = static_cast<int>(std::clamp<long>(ReadInt("iMaxTargetsToBlend", v.maxTargets), 0, 8));
            v.breakTime       = std::clamp(ReadFloat("fLockBreakTime", v.breakTime), 0.1f, 5.0f);
            v.firstPersonHold = std::clamp(ReadFloat("fFirstPersonHoldTime", v.firstPersonHold), 0.1f, 3.0f);
            v.maxDistance     = (std::max)(100.0f, ReadFloat("fFramingMaxDistance", v.maxDistance));
            v.range           = (std::max)(100.0f, ReadFloat("fLockRange", v.range));
            v.fullWeightAt    = (std::max)(0.0f, ReadFloat("fFramingFullWeight", v.fullWeightAt));
            v.zeroWeightAt    = (std::max)(v.fullWeightAt + 1.0f, ReadFloat("fFramingZeroWeight", v.zeroWeightAt));
            const long key    = (std::max)(0L, ReadInt("iLockKey", 0));
            v.keyboardKey     = static_cast<std::uint32_t>(key) >= kMouseBase
                                    ? input::Key{ input::Device::Mouse, static_cast<std::uint32_t>(key) - kMouseBase }
                                    : input::Key{ input::Device::Keyboard, static_cast<std::uint32_t>(key) };
            v.gamepadKey      = { input::Device::Gamepad, static_cast<std::uint32_t>((std::max)(0L, ReadInt("iLockGamepadKey", 0))) };
            LOG_INFO("camera lock: mode {}, {} targets, break {:.2f} s, first person hold {:.2f} s, framing up to {:.0f}, range {:.0f}",
                     static_cast<int>(v.mode), v.maxTargets, v.breakTime, v.firstPersonHold, v.maxDistance, v.range);
        }

        /// @brief The engagement, shared by the camera frame, the POV key and the hit events (all on the main thread; the
        ///        lock guards the rare menu reader).
        struct State {
            std::mutex      lock;
            bool            toggled = false;   ///< the POV / custom key's toggle
            bool            broken  = false;   ///< ended by the look stick: off until a hit (or the key, or a draw)
            bool            wasDrawn = false;
            float           heldAway = 0.0f;   ///< seconds the look input has been held
            RE::ActorHandle lastHit;           ///< the last actor the player hit
            RE::ActorHandle primary;           ///< the target the body faces, kept until it has to change
            bool            povHoldFired = false;
            float           flickTime = -1.0f;   ///< seconds the look input has been out past the flick threshold; -1 while in
            float           flickX    = 0.0f;    ///< its strongest sideways value meanwhile
        };
        State& St()
        {
            static State s;
            return s;
        }

        std::atomic<bool>   g_engaged{ false };
        std::mutex          g_primaryLock;
        bool                g_havePrimary = false;
        float               g_primaryX = 0.0f, g_primaryY = 0.0f;
        double              g_primaryAt = -1.0e9;

        void ToggleByKey()
        {
            State&           st = St();
            std::scoped_lock guard(st.lock);
            st.toggled  = !st.toggled;
            st.broken   = false;
            st.heldAway = 0.0f;
            LOG_INFO("camera lock: {} by the key", st.toggled ? "on" : "off");
        }

        bool Bound(input::Key k) { return k.code != 0 || k.device == input::Device::Mouse; }

        float WrapPi(float a)
        {
            constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;
            a = std::fmod(a + kPi, kTwoPi);
            if (a < 0.0f) a += kTwoPi;
            return a - kPi;
        }

        /// @brief Whether `actor` can still be fought from `at`: alive, in combat, within `range`.
        bool Fightable(RE::Actor* actor, const RE::NiPoint3& at, float range)
        {
            return actor && !actor->IsDead() && actor->IsInCombat() && actor->GetPosition().GetDistance(at) <= range;
        }

        /// @brief A hit by the player on an actor re-arms a lock the look stick ended, and names the primary target.
        struct HitSink final : RE::BSTEventSink<RE::TESHitEvent> {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override
            {
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (!event || !player || event->cause.get() != player || !event->target) return RE::BSEventNotifyControl::kContinue;
                auto* target = event->target->As<RE::Actor>();
                if (!target || target->IsDead()) return RE::BSEventNotifyControl::kContinue;
                State&           st = St();
                std::scoped_lock guard(st.lock);
                st.lastHit = target->GetHandle();
                st.primary = st.lastHit;
                if (st.broken) {
                    st.broken  = false;
                    st.toggled = true;
                    LOG_INFO("camera lock: re-armed by a hit");
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        };

        void InstallHitSink()
        {
            static HitSink sink;
            if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) holder->AddEventSink<RE::TESHitEvent>(&sink);
        }

        [[maybe_unused]] const bool s_hits = platform::startup::OnDataLoaded(&InstallHitSink, "camera lock hits");

        namespace hooks = platform::hooks;

        /// @brief PlayerInputHandler::ProcessButton (vtable index 4 in every runtime) of the handler bound to the POV key.
        struct TogglePovSlot {
            static constexpr const char* name = "TogglePOVHandler::ProcessButton";
            static REL::VariantID        Table() { return RE::VTABLE_TogglePOVHandler[0]; }
            static std::size_t           Index() { return 4; }
        };
        using TogglePov = hooks::VirtualSlot<TogglePovSlot, void(RE::TogglePOVHandler*, RE::ButtonEvent*, RE::PlayerControlsData*)>;

        /// @brief Outside first person: in the POV key mode a tap toggles the lock and a hold enters first person; in
        ///        the others the key works as vanilla's, unless first person is off. Inside first person the key leaves.
        /// @note  The key's hold-to-zoom goes with it: CB's zoom reads the wheel.
        void OnTogglePov(TogglePov::Next next, RE::TogglePOVHandler* handler, RE::ButtonEvent* event, RE::PlayerControlsData* data)
        {
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!camera || !event || camera->IsInFirstPerson()) return next(handler, event, data);
            const bool firstPerson = zoom::Current().firstPerson;
            if (Current().mode != Mode::PovKey) {
                if (firstPerson) next(handler, event, data);
                return;
            }
            State& st = St();
            if (event->IsDown()) {
                st.povHoldFired = false;
            } else if (event->IsHeld()) {
                if (!st.povHoldFired && firstPerson && event->HeldDuration() >= Current().firstPersonHold) {
                    st.povHoldFired = true;
                    camera->ForceFirstPerson();
                }
            } else if (event->IsUp()) {
                if (!st.povHoldFired) ToggleByKey();
                st.povHoldFired = false;
            }
        }

        [[maybe_unused]] const bool s_togglePov = TogglePov::Subscribe(&OnTogglePov, hooks::Priority::Normal, "Camera", "camera");

        /// @brief Where `actor`'s body is (two thirds of its height up), and how big it is.
        Candidate CandidateOf(RE::Actor* actor)
        {
            const RE::NiPoint3 at    = actor->GetPosition();
            const float        scale = actor->GetScale();
            const float        h     = (std::max)(actor->GetHeight(), 60.0f);
            return { actor->GetHandle(), at.x, at.y, at.z + h * 0.66f, 45.0f * scale };
        }

    }  // namespace

    Settings Current()
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        LoadOnce(s);
        return s.settings;
    }

    void Set(const Settings& settings)
    {
        Store&           s = S();
        std::scoped_lock guard(s.lock);
        s.loaded   = true;
        s.settings = settings;
    }

    bool Save()
    {
        const Settings now = Current();
        const auto     set = [](std::string_view key, const std::string& v) { return core::debug::SetValue(kSection, key, v); };
        const std::uint32_t key = now.keyboardKey.device == input::Device::Mouse ? kMouseBase + now.keyboardKey.code : now.keyboardKey.code;
        bool ok = set("iLockMode", std::to_string(static_cast<int>(now.mode)));
        ok = set("iMaxTargetsToBlend", std::to_string(now.maxTargets)) && ok;
        ok = set("fLockBreakTime", std::format("{:g}", now.breakTime)) && ok;
        ok = set("fFirstPersonHoldTime", std::format("{:g}", now.firstPersonHold)) && ok;
        ok = set("fFramingMaxDistance", std::format("{:g}", now.maxDistance)) && ok;
        ok = set("fLockRange", std::format("{:g}", now.range)) && ok;
        ok = set("fFramingFullWeight", std::format("{:g}", now.fullWeightAt)) && ok;
        ok = set("fFramingZeroWeight", std::format("{:g}", now.zeroWeightAt)) && ok;
        ok = set("iLockKey", std::to_string(key)) && ok;
        ok = set("iLockGamepadKey", std::format("0x{:X}", now.gamepadKey.code)) && ok;
        LOG_INFO("camera lock: saved to settings.ini [Camera]{}", ok ? "" : " (with errors)");
        return ok;
    }

    Frame Step(RE::PlayerCharacter& player, float dt, float lookX, float lookY, float cameraYaw)
    {
        const Settings s = Current();
        Frame          out;
        if (s.mode == Mode::CustomKey &&
            ((Bound(s.keyboardKey) && input::Pressed(s.keyboardKey)) || (Bound(s.gamepadKey) && input::Pressed(s.gamepadKey))))
            ToggleByKey();

        RE::Actor* lastHit = nullptr;
        RE::Actor* primary = nullptr;
        int        flick   = 0;
        {
            State&           st     = St();
            std::scoped_lock guard(st.lock);
            const bool       drawn  = player.AsActorState()->IsWeaponDrawn();
            const bool       sprint = player.AsActorState()->IsSprinting();
            if (drawn && !st.wasDrawn && s.mode == Mode::Automatic) st.broken = false;
            st.wasDrawn = drawn;
            switch (s.mode) {
                case Mode::Off: out.engaged = false; break;
                case Mode::PovKey:
                case Mode::CustomKey: out.engaged = st.toggled && !st.broken; break;
                case Mode::Automatic: out.engaged = drawn && !sprint && !st.broken; break;
            }
            /// @note Holding the look input away (either axis) for the break time ends the lock.
            if (out.engaged && std::hypot(lookX, lookY) > kLookHeld) st.heldAway += dt;
            else st.heldAway = 0.0f;
            /// @note A flick: out past 0.5 and back under 0.2 within a quarter second, mostly sideways.
            const float look = std::hypot(lookX, lookY);
            if (look > 0.5f) {
                st.flickTime = st.flickTime < 0.0f ? 0.0f : st.flickTime + dt;
                if (std::abs(lookX) > std::abs(st.flickX)) st.flickX = lookX;
            } else if (look < 0.2f) {
                if (out.engaged && st.flickTime >= 0.0f && st.flickTime <= 0.25f && std::abs(st.flickX) > 0.4f) flick = st.flickX > 0.0f ? 1 : -1;
                st.flickTime = -1.0f;
                st.flickX    = 0.0f;
            }
            if (out.engaged && st.heldAway >= s.breakTime) {
                st.broken   = true;
                st.toggled  = false;
                st.heldAway = 0.0f;
                out.engaged = false;
                LOG_INFO("camera lock: broken by the look stick (held {:.2f} s)", s.breakTime);
            }
            lastHit = st.lastHit.get().get();
            primary = st.primary.get().get();
        }
        g_engaged.store(out.engaged, std::memory_order_relaxed);
        if (!out.engaged || s.maxTargets <= 0) return out;

        auto* lists = RE::ProcessLists::GetSingleton();
        if (!lists) return out;
        const RE::NiPoint3 at = player.GetPosition();
        struct Ranked {
            float      key;
            RE::Actor* actor;
        };
        std::vector<Ranked> ranked;
        for (const RE::ActorHandle& handle : lists->highActorHandles) {
            const RE::NiPointer<RE::Actor> ptr   = handle.get();
            RE::Actor*                     actor = ptr.get();
            if (!actor || actor->IsDead() || !actor->IsInCombat() || actor->IsPlayerTeammate()) continue;
            const bool onPlayer = actor->GetActorRuntimeData().currentCombatTarget.get().get() == &player;
            if (!onPlayer && actor != lastHit) continue;
            const float d = actor->GetPosition().GetDistance(at);
            if (d > s.range) continue;
            ranked.push_back({ actor == lastHit ? -1.0f : d, actor });
        }
        std::ranges::sort(ranked, {}, &Ranked::key);

        /// @note The primary: kept while it can still be fought, else the best-ranked; a flick moves it to the nearest
        ///       combatant on that side of it, as the camera sees them.
        if (!Fightable(primary, at, s.range) || std::ranges::none_of(ranked, [&](const Ranked& r) { return r.actor == primary; }))
            primary = ranked.empty() ? nullptr : ranked.front().actor;
        if (flick != 0 && primary) {
            const auto bearing = [&](RE::Actor* a) {
                const RE::NiPoint3 p = a->GetPosition();
                return WrapPi(std::atan2(p.x - at.x, p.y - at.y) - cameraYaw);
            };
            const float from = bearing(primary);
            RE::Actor*  best = nullptr;
            float       gap  = 10.0f;
            for (const Ranked& r : ranked) {
                const float d = (bearing(r.actor) - from) * static_cast<float>(flick);
                if (r.actor != primary && d > 0.0f && d < gap) gap = d, best = r.actor;
            }
            if (best) {
                primary = best;
                LOG_INFO("camera lock: flicked {} to a new target", flick > 0 ? "right" : "left");
            }
        }
        {
            State&           st = St();
            std::scoped_lock guard(st.lock);
            st.primary = primary ? primary->GetHandle() : RE::ActorHandle{};
        }
        if (primary) {
            out.primary = primary->GetHandle();
            out.candidates.push_back(CandidateOf(primary));
        }
        for (const Ranked& r : ranked) {
            if (static_cast<int>(out.candidates.size()) >= s.maxTargets) break;
            if (r.actor != primary) out.candidates.push_back(CandidateOf(r.actor));
        }
        return out;
    }

    void PublishPrimary(bool have, float x, float y)
    {
        std::scoped_lock guard(g_primaryLock);
        g_havePrimary = have;
        g_primaryX = x, g_primaryY = y;
        g_primaryAt = Now();
    }

    bool Primary(float& x, float& y)
    {
        std::scoped_lock guard(g_primaryLock);
        if (!g_havePrimary || Now() - g_primaryAt > 0.25) return false;
        x = g_primaryX, y = g_primaryY;
        return true;
    }

    bool Engaged() { return g_engaged.load(std::memory_order_relaxed); }

}  // namespace CB::feature::camera::lock
