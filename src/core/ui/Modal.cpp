#include "PCH.h"

#include "core/debug/Settings.h"
#include "core/ui/Modal.h"
#include "core/ui/UiHost.h"
#include "core/ui/UiInput.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>

#include <imgui.h>

#include <platform/startup/Startup.h>

namespace CB::core::ui {

    namespace {

        using Clock = std::chrono::steady_clock;

        /// @brief How long a prompt waits for the game to pump input before it answers its default. Not a time to
        ///        decide: the clock only runs while the game is not dispatching input events at all.
        constexpr std::chrono::seconds kNoPumpLapse{ 20 };

        struct Pending {
            ModalDesc     desc;
            ModalAnswer   answer;
            bool          shown       = false;
            std::uint64_t pumpsAtShow = 0;
            Clock::time_point shownAt;
        };

        std::mutex           g_lock;
        std::deque<Pending>  g_queue;
        std::atomic<bool>    g_open{ false };

        /// @brief Pops the front prompt and answers it, outside the lock (an answer may show the next prompt).
        void Resolve(int choice, bool byDefault)
        {
            ModalAnswer answer;
            {
                std::scoped_lock lock(g_lock);
                if (g_queue.empty()) return;
                answer = std::move(g_queue.front().answer);
                g_queue.pop_front();
                g_open.store(!g_queue.empty(), std::memory_order_release);
            }
            if (answer) answer(choice, byDefault);
        }

        bool Open() { return g_open.load(std::memory_order_acquire); }

        void Draw()
        {
            int       defaultChoice = 0;
            bool      lapsed        = false;
            ModalDesc desc;
            {
                std::scoped_lock lock(g_lock);
                if (g_queue.empty()) return;
                Pending& p = g_queue.front();
                if (!p.shown) {
                    p.shown       = true;
                    p.pumpsAtShow = InputPumps();
                    p.shownAt     = Clock::now();
                    LOG_INFO("ui: prompt up: {}", p.desc.title);
                }
                lapsed        = InputPumps() == p.pumpsAtShow && Clock::now() - p.shownAt > kNoPumpLapse;
                desc          = p.desc;
                defaultChoice = p.desc.defaultChoice;
            }
            if (lapsed) {
                LOG_WARN("ui: the game dispatched no input for {} s with the prompt '{}' up - answering its default.",
                         kNoPumpLapse.count(), desc.title);
                Resolve(defaultChoice, true);
                return;
            }

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 0.0f), ImVec2(io.DisplaySize.x * 0.8f, io.DisplaySize.y * 0.8f));
            ImGui::SetNextWindowFocus();
            int chosen = -1;
            ImGui::Begin((desc.title + "##CommunityBehaviorsModal").c_str(), nullptr,
                         ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + 420.0f);
            ImGui::TextUnformatted(desc.text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();
            for (std::size_t i = 0; i < desc.choices.size(); ++i) {
                if (i) ImGui::SameLine();
                if (ImGui::Button((desc.choices[i] + "##" + std::to_string(i)).c_str())) chosen = static_cast<int>(i);
            }
            ImGui::End();

            /// @note Enter and Escape answer the default, the safe choice: there is no key to the other buttons.
            if (chosen < 0 && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                               ImGui::IsKeyPressed(ImGuiKey_Escape)))
                chosen = defaultChoice;
            if (chosen >= 0) Resolve(chosen, false);
        }

        bool Active() { return Open(); }

        [[maybe_unused]] const bool s_registered = [] {
            RegisterOverlay({ "Modal", &Active, &Draw, 100, &Open });
            return true;
        }();

        /// @brief A prompt to see the whole path work without a mod that needs one: [Debug] bUiTestModal.
        void ShowTest()
        {
            if (!core::debug::FlagOn("ui_test_modal")) return;
            ShowModal({ "Community Behaviors - prompt test",
                        "This is the UI test prompt (settings.ini [Debug] bUiTestModal). The game is not receiving input "
                        "while it is up. Enter or Escape answers No.",
                        { "Yes", "No" }, 1 },
                      [](int choice, bool byDefault) {
                          LOG_INFO("ui: test prompt answered {}{}", choice == 0 ? "Yes" : "No", byDefault ? " (by default)" : "");
                      });
        }

        [[maybe_unused]] const bool s_test = platform::startup::OnDataLoaded(&ShowTest, "ui test prompt");

    }  // namespace

    void ShowModal(ModalDesc desc, ModalAnswer answer)
    {
        if (desc.choices.empty()) desc.choices.push_back("OK");
        if (desc.defaultChoice < 0 || desc.defaultChoice >= static_cast<int>(desc.choices.size())) desc.defaultChoice = 0;
        if (!CanTakeInput()) {
            LOG_WARN("ui: nothing can take input on this runtime - the prompt '{}' answers its default ('{}').", desc.title,
                     desc.choices[static_cast<std::size_t>(desc.defaultChoice)]);
            if (answer) answer(desc.defaultChoice, true);
            return;
        }
        std::scoped_lock lock(g_lock);
        g_queue.push_back({ std::move(desc), std::move(answer) });
        g_open.store(true, std::memory_order_release);
    }

}  // namespace CB::core::ui
