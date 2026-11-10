#include "PCH.h"

#include <platform/thread/Thread.h>

#include <platform/hooks/Override.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Context/hkbContext.h>

#include <algorithm>
#include <vector>

namespace CB::platform::thread {

    namespace {

        namespace hooks = platform::hooks;

        struct Tables {
            std::mutex                  lock;
            std::vector<detail::Table*> all;
        };
        /// @note Never destroyed: a table's own destructor may unregister during static destruction at exit.
        Tables& All()
        {
            static Tables* tables = new Tables;
            return *tables;
        }

        void DropEverywhere(const ::hkbCharacter* character)
        {
            if (!character) return;
            Tables&                     t = All();
            std::vector<detail::Table*> copy;
            {
                std::scoped_lock lock(t.lock);
                copy = t.all;
            }
            for (detail::Table* table : copy) table->Drop(character);
        }

        /// @brief Whether `graph` is `context`'s character's root graph (a referenced behavior is not).
        bool IsRoot(const ::hkbBehaviorGraph* graph, const ::hkbContext& context)
        {
            return context.m_character && context.m_character->m_behaviorGraph == graph;
        }

        using GraphActivate   = hooks::Override<&::hkbBehaviorGraph::activate>;
        using GraphDeactivate = hooks::Override<&::hkbBehaviorGraph::deactivate>;

        /// @note Dropped before the graph runs: a new life starts with no state from the last.
        void OnActivate(GraphActivate::Next next, ::hkbBehaviorGraph* graph, const ::hkbContext& context)
        {
            if (IsRoot(graph, context)) DropEverywhere(context.m_character);
            next(graph, context);
        }

        void OnDeactivate(GraphDeactivate::Next next, ::hkbBehaviorGraph* graph, const ::hkbContext& context)
        {
            next(graph, context);
            if (IsRoot(graph, context)) DropEverywhere(context.m_character);
        }

        [[maybe_unused]] const bool s_lifetime =
            (GraphActivate::Subscribe(&OnActivate, hooks::Priority::First, "thread::PerCharacter"),
             GraphDeactivate::Subscribe(&OnDeactivate, hooks::Priority::Last, "thread::PerCharacter"));

    }  // namespace

    bool OnGameThread(std::function<void()> fn)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks || !fn) return false;
        tasks->AddTask(std::move(fn));
        return true;
    }

    namespace detail {
        void Register(Table* table)
        {
            Tables&          t = All();
            std::scoped_lock lock(t.lock);
            t.all.push_back(table);
        }

        void Unregister(Table* table)
        {
            Tables&          t = All();
            std::scoped_lock lock(t.lock);
            std::erase(t.all, table);
        }
    }  // namespace detail

}  // namespace CB::platform::thread
