#include "PCH.h"

#include "Bindings.h"

#include "../../../include/CB-Bindings.h"

#include <Behavior/Behavior/Generator/hkbGenerator.h>

#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace CB::feature::dynamic {

    namespace {

        /// @brief One binding. It outlives its Unbind while a generator still holds it.
        struct Entry {
            std::uint32_t   actor = 0;
            std::string     key, path;
            ::hkbGenerator* loaded = nullptr;   ///< the behavior, loaded and linked for the actor's character
            int             holders = 0;
            bool            bound   = true;
        };

        struct Registry {
            std::mutex                                                             lock;
            std::map<std::pair<std::uint32_t, std::string>, std::shared_ptr<Entry>, std::less<>> bound;
            std::map<std::uint64_t, std::shared_ptr<Entry>>                        held;
            std::uint64_t                                                          nextTicket = 1;
        };
        Registry& Bindings()
        {
            static Registry r;
            return r;
        }

        /// @brief Load `path` for `character` and link its events and variables into the character's by name.
        /// @return the behavior's root generator; null when it cannot load.
        /// @warning Not built: CB has no seam yet for the exe's behavior-reference load (docs/bugs/dynamic-behavior-load.md).
        ///          Until it does, a binding resolves to nothing and the generator plays its fallback.
        ::hkbGenerator* LoadBehavior(::hkbCharacter* character, const std::string& path)
        {
            static std::once_flag once;
            std::call_once(once, [] {
                LOG_WARN("[dynamic-behavior] loading a bound behavior is not implemented yet - bound slots play their fallback");
            });
            (void)character;
            (void)path;
            return nullptr;
        }

        /// @brief Drop an unbound entry nobody holds. Caller holds the lock.
        void Retire(const std::shared_ptr<Entry>& e)
        {
            if (e->bound || e->holders > 0) return;
            if (e->loaded) e->loaded->removeReference();
            e->loaded = nullptr;
        }

        void OnMessage(SKSE::MessagingInterface::Message* msg)
        {
            if (!msg || (msg->type != cb::bindings::kBind && msg->type != cb::bindings::kUnbind)) return;
            if (msg->dataLen < sizeof(cb::bindings::BindMessage) || !msg->data) {
                LOG_WARN("[dynamic-behavior] a binding message from '{}' is too short - ignored", msg->sender ? msg->sender : "?");
                return;
            }
            const auto& m   = *static_cast<const cb::bindings::BindMessage*>(msg->data);
            const char* key = m.key ? m.key : "";
            if (msg->type == cb::bindings::kBind) Bind(m.actor, key, m.behaviorPath ? m.behaviorPath : "");
            else Unbind(m.actor, key);
        }

    }  // namespace

    bool Bind(std::uint32_t actor, std::string_view key, std::string_view behaviorPath)
    {
        if (key.empty() || behaviorPath.empty()) return false;
        Registry&        r = Bindings();
        std::scoped_lock guard(r.lock);
        auto             e = std::make_shared<Entry>();
        e->actor           = actor;
        e->key             = std::string(key);
        e->path            = std::string(behaviorPath);
        auto& slot         = r.bound[{ actor, e->key }];
        if (slot) {
            slot->bound = false;
            Retire(slot);
        }
        slot = std::move(e);
        LOG_INFO("[dynamic-behavior] {:08X} '{}' -> '{}'", actor, key, behaviorPath);
        return true;
    }

    bool Unbind(std::uint32_t actor, std::string_view key)
    {
        Registry&        r = Bindings();
        std::scoped_lock guard(r.lock);
        const auto       it = r.bound.find(std::pair{ actor, std::string(key) });
        if (it == r.bound.end()) return false;
        it->second->bound = false;
        Retire(it->second);
        r.bound.erase(it);
        LOG_INFO("[dynamic-behavior] {:08X} '{}' unbound", actor, key);
        return true;
    }

    std::optional<std::string> BoundPath(std::uint32_t actor, std::string_view key)
    {
        Registry&        r = Bindings();
        std::scoped_lock guard(r.lock);
        const auto       it = r.bound.find(std::pair{ actor, std::string(key) });
        if (it == r.bound.end()) return std::nullopt;
        return it->second->path;
    }

    Held Acquire(std::uint32_t actor, std::string_view key, ::hkbCharacter* character)
    {
        Registry&        r = Bindings();
        std::scoped_lock guard(r.lock);
        const auto       it = r.bound.find(std::pair{ actor, std::string(key) });
        if (it == r.bound.end()) return {};
        const std::shared_ptr<Entry>& e = it->second;
        /// @note The load belongs at Bind; it runs here only because Bind has no character until the actor's graph exists.
        if (!e->loaded) e->loaded = LoadBehavior(character, e->path);
        ++e->holders;
        const std::uint64_t ticket = r.nextTicket++;
        r.held.emplace(ticket, e);
        return { e->loaded, ticket, e->path };
    }

    void Release(std::uint64_t ticket)
    {
        if (!ticket) return;
        Registry&        r = Bindings();
        std::scoped_lock guard(r.lock);
        const auto       it = r.held.find(ticket);
        if (it == r.held.end()) return;
        --it->second->holders;
        Retire(it->second);
        r.held.erase(it);
    }

    void ListenForBindings(const SKSE::MessagingInterface& messaging)
    {
        if (!messaging.RegisterListener(nullptr, &OnMessage)) LOG_WARN("[dynamic-behavior] could not listen for binding messages");
    }

}  // namespace CB::feature::dynamic
