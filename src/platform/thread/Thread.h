#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

class hkbCharacter;

/// @file
/// @brief The runtime SDK's threading services for features: work handed to the game thread, and state kept per
///        character.
/// @note  The model they rest on: graphs update on job threads, several characters at once, but one character's graph
///        on one thread at a time. So a feature touches only ITS character's state inside a graph callback (no locks in
///        feature code), and anything else (raising an event, engine calls that are not graph-local) goes through
///        OnGameThread.

namespace CB::platform::thread {

    /// @brief Run `fn` on the game thread, at the next frame's task pump, in the order queued. Any thread.
    /// @note  SKSE's task interface. Before SKSE hands it over (very early load), `fn` is dropped and false returned.
    bool OnGameThread(std::function<void()> fn);

    namespace detail {
        /// @brief What the character lifetime hook calls on every PerCharacter table.
        struct Table {
            virtual void Drop(const ::hkbCharacter* character) = 0;
            virtual ~Table() = default;
        };
        void Register(Table* table);
        void Unregister(Table* table);
    }  // namespace detail

    /// @brief A `T` per character, created on first use, dropped when the character's root behavior graph activates
    ///        anew or deactivates: one life of the character's graph, never a clock (a pause stops the graph too).
    /// @note  Keyed by the hkbCharacter the graph runs (hkbContext::m_character). A reference from Of stays valid until
    ///        the character's graph next activates or deactivates; only the character's own graph thread should hold it.
    /// @note  A graph destroyed without deactivating (a torn-down actor) leaves its entry until the same address
    ///        activates again: a small, bounded leak, not a stale read.
    /// @note  Declare one at namespace scope (static lifetime): it registers itself with the lifetime hook.
    template <class T>
    class PerCharacter final : detail::Table {
    public:
        PerCharacter() { detail::Register(this); }
        ~PerCharacter() override { detail::Unregister(this); }
        PerCharacter(const PerCharacter&)            = delete;
        PerCharacter& operator=(const PerCharacter&) = delete;

        /// @return `character`'s T, made now when it has none
        T& Of(const ::hkbCharacter* character)
        {
            std::scoped_lock lock(m_lock);
            auto&            slot = m_states[character];
            if (!slot) slot = std::make_unique<T>();
            return *slot;
        }

        /// @return `character`'s T, null when it has none
        T* Find(const ::hkbCharacter* character)
        {
            std::scoped_lock lock(m_lock);
            const auto       it = m_states.find(character);
            return it == m_states.end() ? nullptr : it->second.get();
        }

        void Drop(const ::hkbCharacter* character) override
        {
            std::scoped_lock lock(m_lock);
            m_states.erase(character);
        }

    private:
        std::mutex                                                   m_lock;
        std::unordered_map<const ::hkbCharacter*, std::unique_ptr<T>> m_states;
    };

}  // namespace CB::platform::thread
