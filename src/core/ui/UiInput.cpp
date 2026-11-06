#include "PCH.h"

#include "core/ui/UiHost.h"
#include "core/ui/UiInput.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

#include <imgui.h>

#include <platform/hooks/CallSite.h>

/// @file
/// @brief See UiInput.h. Keyboard and mouse only: the first clients are a prompt and a config window, both mouse first.
///        A gamepad is not translated yet.

namespace CB::core::ui {

    namespace {

        namespace hooks = CB::platform::hooks;

        using Dispatch = hooks::CallSite<hooks::engine::PollInputDevices_DispatchCall, void(RE::BSInputDeviceManager*, RE::InputEvent* const*)>;

        /// @brief Ahead of Advanced Input's subscriber (Priority::First): a swallowed event must not reach it either.
        constexpr auto kPriority = static_cast<hooks::Priority>(static_cast<std::int32_t>(hooks::Priority::First) - 1000);

        /// @brief Events a frame can take before the oldest are dropped: a host that stops drawing must not grow this.
        constexpr std::size_t kMaxQueued = 512;

        struct Queued {
            enum class Kind : std::uint8_t { Key, MouseButton, Wheel, Char };
            Kind          kind;
            std::uint32_t code;   ///< ImGuiKey, mouse button index, or the typed code point
            bool          down;   ///< Key, MouseButton
            float         wheel;  ///< Wheel: +1 up, -1 down
        };

        std::mutex          g_lock;
        std::vector<Queued> g_queue;

        std::atomic<std::uint64_t> g_pumps{ 0 };

        /// @brief DirectInput scancodes (the physical key on a US layout) to ImGuiKey. Text comes from CharEvent, which
        ///        carries the character the layout makes, so a letter's position only matters for shortcuts.
        const std::array<ImGuiKey, 256>& KeyTable()
        {
            static const auto table = [] {
                std::array<ImGuiKey, 256> t{};   // value-initialised: ImGuiKey_None
                using K = RE::BSKeyboardDevice::Key;
                const auto set = [&t](auto scan, ImGuiKey key) { t[static_cast<std::size_t>(scan)] = key; };

                set(K::kEscape, ImGuiKey_Escape);
                set(K::kBackspace, ImGuiKey_Backspace);
                set(K::kTab, ImGuiKey_Tab);
                set(K::kEnter, ImGuiKey_Enter);
                set(K::kKP_Enter, ImGuiKey_KeypadEnter);
                set(K::kSpacebar, ImGuiKey_Space);
                set(K::kLeftControl, ImGuiKey_LeftCtrl);
                set(K::kRightControl, ImGuiKey_RightCtrl);
                set(K::kLeftShift, ImGuiKey_LeftShift);
                set(K::kRightShift, ImGuiKey_RightShift);
                set(K::kLeftAlt, ImGuiKey_LeftAlt);
                set(K::kRightAlt, ImGuiKey_RightAlt);
                set(K::kHome, ImGuiKey_Home);
                set(K::kEnd, ImGuiKey_End);
                set(K::kPageUp, ImGuiKey_PageUp);
                set(K::kPageDown, ImGuiKey_PageDown);
                set(K::kInsert, ImGuiKey_Insert);
                set(K::kDelete, ImGuiKey_Delete);
                set(K::kUp, ImGuiKey_UpArrow);
                set(K::kDown, ImGuiKey_DownArrow);
                set(K::kLeft, ImGuiKey_LeftArrow);
                set(K::kRight, ImGuiKey_RightArrow);

                /// @note The top-row digits run 0x02..0x0B (1..9, then 0); the letter rows are QWERTYUIOP from 0x10,
                ///       ASDFGHJKL from 0x1E and ZXCVBNM from 0x2C; F1..F10 run 0x3B..0x44, F11 and F12 are 0x57 and 0x58.
                for (int i = 0; i < 9; ++i) set(0x02 + i, static_cast<ImGuiKey>(ImGuiKey_1 + i));
                set(0x0B, ImGuiKey_0);
                const auto row = [&set](std::uint32_t first, const char* letters) {
                    for (std::uint32_t i = 0; letters[i]; ++i) set(first + i, static_cast<ImGuiKey>(ImGuiKey_A + (letters[i] - 'A')));
                };
                row(0x10, "QWERTYUIOP");
                row(0x1E, "ASDFGHJKL");
                row(0x2C, "ZXCVBNM");
                for (int i = 0; i < 10; ++i) set(0x3B + i, static_cast<ImGuiKey>(ImGuiKey_F1 + i));
                set(0x57, ImGuiKey_F11);
                set(0x58, ImGuiKey_F12);
                return t;
            }();
            return table;
        }

        void Push(const Queued& q)
        {
            std::scoped_lock lock(g_lock);
            if (g_queue.size() >= kMaxQueued) g_queue.erase(g_queue.begin());
            g_queue.push_back(q);
        }

        void Translate(const RE::InputEvent* list)
        {
            for (const RE::InputEvent* e = list; e; e = e->next) {
                if (const auto* c = e->AsCharEvent()) {
                    Push({ Queued::Kind::Char, c->keyCode, true, 0.0f });
                    continue;
                }
                const auto* b = e->AsButtonEvent();
                if (!b || !b->HasIDCode()) continue;
                /// @note Presses and releases only: the repeats of a held key change nothing, ImGui makes its own.
                if (!b->IsDown() && !b->IsUp()) continue;
                const bool down = b->IsDown();
                if (b->GetDevice() == RE::INPUT_DEVICE::kKeyboard) {
                    const std::uint32_t scan = b->GetIDCode();
                    const ImGuiKey      key  = scan < 256 ? KeyTable()[scan] : ImGuiKey_None;
                    if (key != ImGuiKey_None) Push({ Queued::Kind::Key, static_cast<std::uint32_t>(key), down, 0.0f });
                } else if (b->GetDevice() == RE::INPUT_DEVICE::kMouse) {
                    using M = RE::BSWin32MouseDevice::Key;
                    const auto id = static_cast<M>(b->GetIDCode());
                    if (id == M::kWheelUp) { if (down) Push({ Queued::Kind::Wheel, 0, true, 1.0f }); }
                    else if (id == M::kWheelDown) { if (down) Push({ Queued::Kind::Wheel, 0, true, -1.0f }); }
                    else if (b->GetIDCode() < static_cast<std::uint32_t>(ImGuiMouseButton_COUNT)) Push({ Queued::Kind::MouseButton, b->GetIDCode(), down, 0.0f });
                }
            }
        }

        /// @note With an overlay wanting input the game is handed an empty list: its sinks see no event this frame, which
        ///       is how a pause-less overlay keeps the character from walking under a click.
        void OnDispatch(Dispatch::Next next, RE::BSInputDeviceManager* manager, RE::InputEvent* const* events)
        {
            g_pumps.fetch_add(1, std::memory_order_relaxed);
            if (WantsInput()) {
                if (events && *events) Translate(*events);
                static RE::InputEvent* const none[] = { nullptr };
                next(manager, none);
                return;
            }
            next(manager, events);
        }

        [[maybe_unused]] const bool s_dispatch = Dispatch::Subscribe(&OnDispatch, kPriority, "UiHost input");

    }  // namespace

    bool CanTakeInput()
    {
        return Dispatch::Installed();
    }

    std::uint64_t InputPumps()
    {
        return g_pumps.load(std::memory_order_relaxed);
    }

    void DrainInput(ImGuiIO& io)
    {
        std::vector<Queued> take;
        {
            std::scoped_lock lock(g_lock);
            take.swap(g_queue);
        }
        for (const Queued& q : take) {
            switch (q.kind) {
            case Queued::Kind::Key: io.AddKeyEvent(static_cast<ImGuiKey>(q.code), q.down); break;
            case Queued::Kind::MouseButton: io.AddMouseButtonEvent(static_cast<int>(q.code), q.down); break;
            case Queued::Kind::Wheel: io.AddMouseWheelEvent(0.0f, q.wheel); break;
            case Queued::Kind::Char: io.AddInputCharacter(q.code); break;
            }
        }
    }

}  // namespace CB::core::ui
