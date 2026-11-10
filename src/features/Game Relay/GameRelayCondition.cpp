#include "PCH.h"

#include "GameRelayCondition.h"

#include <core/debug/Settings.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>
#include <platform/relay/Relay.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphData.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphStringData.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Utils/hkbSymbolIdMap.h>
#include <Behavior/Behavior/Variables/hkbVariableInfo.h>
#include <Behavior/Behavior/Variables/hkbVariableValueSet.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace CB::feature::relay {

    namespace {

        namespace pr = CB::platform::relay;

        constexpr const char* kClassName = "CBGameRelayCondition";

        void Finish(void* object)
        {
            if (object) new (object) GameRelayCondition(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<GameRelayCondition*>(object)->~GameRelayCondition(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(GameRelayCondition) unsigned char scratch[sizeof(GameRelayCondition)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }
        /// @note Registered whatever the flags: a behavior holding this class must load, and an unregistered class name
        ///       loads with a null vtable.
        [[maybe_unused]] const bool s_declared =
            platform::classes::Declare({ kClassName, sizeof(GameRelayCondition), &Finish, &Cleanup, &Vtable });

        /// @return the compiled form of `text`, compiled once per distinct text for the life of the process.
        /// @note  A text that fails to compile is cached too (invalid), so its error is logged once.
        const pr::CompiledExpr* CompiledFor(const char* text)
        {
            static std::mutex                                                          lock;
            static std::unordered_map<std::string, std::unique_ptr<pr::CompiledExpr>> cache;
            const std::string                                                          key = text ? text : "";
            std::scoped_lock                                                           guard(lock);
            auto&                                                                      slot = cache[key];
            if (!slot) {
                std::string err;
                slot = std::make_unique<pr::CompiledExpr>(pr::Compile(key, err));
                if (!slot->Valid()) LOG_WARN("[relay-condition] '{}' does not compile ({}) - the condition is false", key, err);
            }
            return slot.get();
        }

        /// @return the index of variable `name` in `strings`, -1 when it declares none; names compared exactly.
        /// @note  Cached per (string data, interned name): a behavior's string data is its shared template, never edited
        ///        once loaded, and GraphVar's argument is interned, so its pointer is its identity.
        int LocalVariableIndex(const ::hkbBehaviorGraphStringData* strings, const char* name)
        {
            static std::shared_mutex                                                   lock;
            static std::map<std::pair<const void*, const char*>, int>                  cache;
            const std::pair<const void*, const char*>                                  key{ strings, name };
            {
                std::shared_lock guard(lock);
                if (const auto it = cache.find(key); it != cache.end()) return it->second;
            }
            int index = -1;
            for (int i = 0; i < strings->m_variableNames.getSize(); ++i) {
                const char* declared = strings->m_variableNames[i].cString();
                if (declared && std::strcmp(declared, name) == 0) {
                    index = i;
                    break;
                }
            }
            std::unique_lock guard(lock);
            cache.emplace(key, index);
            return index;
        }

        /// @brief The variables of the behavior a condition runs in, typed as that behavior declares them and valued
        ///        from the root graph, as Havok's own expressions read them (hkbCompiledExpressionSet's
        ///        getVariableValueAsReal).
        /// @note  Why not GraphVar's own path: it types a variable by the ROOT graph's declaration, so a variable only a
        ///        referenced behavior declares would read as a float.
        class BehaviorVariables final : public pr::VariableSource {
        public:
            explicit BehaviorVariables(const ::hkbContext& context) : _context(context) {}

            bool Read(const char* name, pr::Value& out) const override
            {
                const ::hkbBehaviorGraph* behavior = _context.m_behavior;
                const ::hkbCharacter*     character = _context.m_character;
                const ::hkbBehaviorGraph* root = character ? character->m_behaviorGraph.val() : nullptr;
                if (!name || !behavior || !root || !behavior->m_data || !behavior->m_data->m_stringData || !root->m_variableValueSet)
                    return false;
                const int local = LocalVariableIndex(behavior->m_data->m_stringData, name);
                if (local < 0 || local >= behavior->m_data->m_variableInfos.getSize()) return false;
                const int rootIndex = ::hkbSymbolIdMap::convertInternalId(behavior->m_variableIdMap, root->m_variableIdMap, local);
                const auto& words = root->m_variableValueSet->m_wordVariableValues;
                if (rootIndex < 0 || rootIndex >= words.getSize()) return false;
                const ::hkInt32 word = words[rootIndex].m_value;
                switch (behavior->m_data->m_variableInfos[local].m_type) {
                    case ::hkbVariableInfo::VARIABLE_TYPE_BOOL:  out = pr::Value::Bool(word != 0); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT8:  out = pr::Value::Int(static_cast<::hkInt8>(word)); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT16: out = pr::Value::Int(static_cast<::hkInt16>(word)); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT32: out = pr::Value::Int(word); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_REAL:  out = pr::Value::Float(std::bit_cast<float>(word)); return true;
                    default: return false;   ///< pointer / vector / quaternion: not a word value
                }
            }

        private:
            const ::hkbContext& _context;
        };

        /// @brief relay_condition_trace: log a condition's first result per character and every change after.
        void Trace(const GameRelayCondition* condition, const ::hkbCharacter* character, RE::Actor* actor, bool result)
        {
            static const bool on = core::debug::FlagOn("relay_condition_trace");
            if (!on) return;
            static std::mutex                                             lock;
            static std::map<std::pair<const void*, const void*>, bool>    last;
            {
                std::scoped_lock guard(lock);
                auto [it, first] = last.try_emplace({ condition, character }, result);
                if (!first) {
                    if (it->second == result) return;
                    it->second = result;
                }
            }
            LOG_INFO("[relay-condition] '{}' on {:08X}: {}", condition->m_expression.cString() ? condition->m_expression.cString() : "",
                     actor ? actor->GetFormID() : 0u, result);
        }

    }  // namespace

    GameRelayCondition::GameRelayCondition(::hkFinishLoadedObjectFlag flag) :
        hkbCondition(flag), m_expression(flag), m_compiled(nullptr)
    {}

    const ::hkClass* GameRelayCondition::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    bool GameRelayCondition::isTrue(const ::hkbContext& context)
    {
        const pr::CompiledExpr* expr = m_compiled.load(std::memory_order_acquire);
        if (!expr) {
            expr = CompiledFor(m_expression.cString());
            m_compiled.store(expr, std::memory_order_release);
        }
        RE::Actor* actor = platform::game::ActorOf(context.m_character);
        if (!expr->Valid() || !actor) return false;

        static thread_local pr::EvalContext ctx;
        ctx.Reset(actor);
        const BehaviorVariables variables(context);
        ctx.variables = &variables;
        const bool result = expr->Evaluate(ctx).AsBool();
        ctx.variables = nullptr;
        Trace(this, context.m_character, actor, result);
        return result;
    }

}  // namespace CB::feature::relay
