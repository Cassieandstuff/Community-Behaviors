#include "PCH.h"

#include "Input.h"

#include <platform/hooks/CallSite.h>

#include <array>
#include <chrono>
#include <mutex>
#include <unordered_map>

/// @file
/// @brief The buffer behind Input.h, filled at PollInputDevices' one dispatch call (engine.yaml
///        PollInputDevices_DispatchCall).
/// @note  The seed is True Flight's InputBuffer (a ring of per-frame stick + button state, Skyrim Content Tools,
///        Cassieandstuff): re-shaped here per physical key rather than per named action, since CB's actions do not
///        exist yet.

namespace CB::feature::input {

    namespace {

        namespace hooks = CB::platform::hooks;

        /// @brief The dispatch: BSTEventSource<InputEvent*>::SendEvent on the device manager, with the frame's event list.
        using Dispatch = hooks::CallSite<hooks::engine::PollInputDevices_DispatchCall, void(RE::BSInputDeviceManager*, RE::InputEvent* const*)>;

        /// @invariant `downFrame`/`upFrame` are Frame() stamps (0 = never); `downAt` is valid while `down`.
        struct KeyState {
            bool          down      = false;
            std::uint64_t downFrame = 0;
            std::uint64_t upFrame   = 0;
            double        downAt    = 0.0;
            float         lastHold  = 0.0f;   ///< seconds the last completed press lasted
        };

        struct Sample {
            Axis2 left, right, mouse;
        };

        std::mutex                                   g_lock;
        std::uint64_t                                g_frame = 0;
        std::unordered_map<std::uint64_t, KeyState>  g_keys;
        std::array<Sample, kHistory>                 g_ring{};
        Axis2                                        g_left, g_right;   ///< sticks report only on change: held here

        std::uint64_t Id(Key k) { return (static_cast<std::uint64_t>(k.device) << 32) | k.code; }

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        const KeyState* Find(Key k)
        {
            const auto it = g_keys.find(Id(k));
            return it == g_keys.end() ? nullptr : &it->second;
        }

        bool Within(std::uint64_t stamp, std::uint32_t frames) { return stamp != 0 && frames != 0 && g_frame - stamp < frames; }

        /// @note Buttons are recorded by their value and held time: down = value > 0 with no held time yet, up = value 0
        ///       after being held (RE::ButtonEvent's own IsDown/IsUp); the repeats between change nothing.
        void Record(const RE::InputEvent* list)
        {
            const double now = Now();
            std::scoped_lock lock(g_lock);
            ++g_frame;
            Sample s{};
            for (auto* e = list; e; e = e->next) {
                const auto device = static_cast<Device>(e->GetDevice());
                switch (e->GetEventType()) {
                case RE::INPUT_EVENT_TYPE::kButton: {
                    const auto* b = e->AsButtonEvent();
                    auto&       k = g_keys[Id({ device, b->GetIDCode() })];
                    if (b->IsDown() && !k.down) {
                        k.down      = true;
                        k.downFrame = g_frame;
                        k.downAt    = now;
                    } else if (b->IsUp() && k.down) {
                        k.down     = false;
                        k.upFrame  = g_frame;
                        k.lastHold = static_cast<float>(now - k.downAt);
                    }
                    break;
                }
                case RE::INPUT_EVENT_TYPE::kThumbstick: {
                    const auto* t = static_cast<const RE::ThumbstickEvent*>(e);
                    (t->IsRight() ? g_right : g_left) = { t->xValue, t->yValue };
                    break;
                }
                case RE::INPUT_EVENT_TYPE::kMouseMove: {
                    const auto* m = static_cast<const RE::MouseMoveEvent*>(e);
                    s.mouse.x += static_cast<float>(m->mouseInputX);
                    s.mouse.y += static_cast<float>(m->mouseInputY);
                    break;
                }
                default: break;
                }
            }
            s.left                       = g_left;
            s.right                      = g_right;
            g_ring[g_frame % kHistory]   = s;
        }

        void OnDispatch(Dispatch::Next next, RE::BSInputDeviceManager* manager, RE::InputEvent* const* events)
        {
            Record(events ? *events : nullptr);
            next(manager, events);
        }

        [[maybe_unused]] const bool s_dispatch = Dispatch::Subscribe(&OnDispatch, hooks::Priority::First, "Advanced Input", "adv_input");

        Sample At(std::uint32_t framesAgo)
        {
            if (framesAgo >= kHistory || framesAgo >= g_frame) return {};
            return g_ring[(g_frame - framesAgo) % kHistory];
        }

    }  // namespace

    std::uint64_t Frame()
    {
        std::scoped_lock lock(g_lock);
        return g_frame;
    }

    bool IsDown(Key key)
    {
        std::scoped_lock lock(g_lock);
        const auto* k = Find(key);
        return k && k->down;
    }

    bool Pressed(Key key, std::uint32_t frames)
    {
        std::scoped_lock lock(g_lock);
        const auto* k = Find(key);
        return k && Within(k->downFrame, frames);
    }

    bool Released(Key key, std::uint32_t frames)
    {
        std::scoped_lock lock(g_lock);
        const auto* k = Find(key);
        return k && Within(k->upFrame, frames);
    }

    float HeldFor(Key key)
    {
        const double now = Now();
        std::scoped_lock lock(g_lock);
        const auto* k = Find(key);
        return k && k->down ? static_cast<float>(now - k->downAt) : 0.0f;
    }

    bool Tapped(Key key, float maxHold)
    {
        std::scoped_lock lock(g_lock);
        const auto* k = Find(key);
        return k && Within(k->upFrame, 1) && k->lastHold <= maxHold;
    }

    Axis2 Stick(bool right, std::uint32_t framesAgo)
    {
        std::scoped_lock lock(g_lock);
        const Sample s = At(framesAgo);
        return right ? s.right : s.left;
    }

    Axis2 Mouse(std::uint32_t framesAgo)
    {
        std::scoped_lock lock(g_lock);
        return At(framesAgo).mouse;
    }

}  // namespace CB::feature::input
