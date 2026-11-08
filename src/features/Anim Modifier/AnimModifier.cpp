#include "PCH.h"

#include "AnimModifier.h"

#include <core/debug/Settings.h>
#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>
#include <platform/relay/Relay.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphData.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphStringData.h>
#include <Behavior/Behavior/Character/hkbAnimationBindingSet.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Variables/hkbVariableInfo.h>
#include <Behavior/Behavior/Variables/hkbVariableValueSet.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace CB::feature::replacer {

    namespace {

        namespace anim  = CB::platform::anim;
        namespace relay = CB::platform::relay;

        constexpr const char* kClassName = "CBAnimModifier";
        constexpr const char* kFlag      = "anim_modifier";

        /// @brief How long after its clip activates a file still streaming in may swap in: a swap later than this
        ///        would visibly restart the clip, so the clip keeps its own animation until it next activates.
        constexpr ::hkReal kLateSwap = 0.1f;

        void Finish(void* object)
        {
            if (object) new (object) AnimModifier(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<AnimModifier*>(object)->~AnimModifier(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(AnimModifier) unsigned char scratch[sizeof(AnimModifier)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }
        /// @note Registered whatever the flag: a behavior holding this class must load, and an unregistered class name
        ///       loads with a null vtable.
        [[maybe_unused]] const bool s_declared = platform::classes::Declare({ kClassName, sizeof(AnimModifier), &Finish, &Cleanup, &Vtable });

        bool Enabled()
        {
            static const bool on = core::debug::FlagOn(kFlag);
            return on;
        }

        /// @brief Compiled `when` expressions, by text: compiled once, shared by every node and character.
        /// @note  Keyed by content, never by a node's address: a reloaded graph at a recycled address reads its own text.
        class ExpressionCache {
        public:
            /// @return the compiled `text`, null when it does not compile (warned once per text).
            const relay::CompiledExpr* Of(const char* text)
            {
                {
                    std::shared_lock read(lock_);
                    if (const auto it = compiled_.find(text); it != compiled_.end()) return it->second ? it->second.get() : nullptr;
                }
                std::string err;
                auto        expr = std::make_unique<relay::CompiledExpr>(relay::Compile(text, err));
                if (!expr->Valid()) {
                    LOG_WARN("[anim modifier] condition '{}' does not compile ({}): its rule never holds", text, err);
                    expr.reset();
                }
                std::unique_lock write(lock_);
                auto& slot = compiled_[text];
                if (!slot) slot = std::move(expr);
                return slot ? slot.get() : nullptr;
            }

        private:
            std::shared_mutex                                                      lock_;
            std::unordered_map<std::string, std::unique_ptr<relay::CompiledExpr>> compiled_;
        };

        ExpressionCache& Expressions()
        {
            static ExpressionCache cache;
            return cache;
        }

        /// @brief The modifier's own behavior graph's variables, by declared name (byte-exact), for GraphVar: read from
        ///        the graph's value set, never through the actor, so a condition reads the variables its behavior file
        ///        declares (a referenced behavior's included) without the animation graph manager's lock.
        class BehaviorVariables final : public relay::VariableSource {
        public:
            explicit BehaviorVariables(const ::hkbBehaviorGraph* graph) : graph_(graph) {}

            bool Read(const char* name, relay::Value& out) const override
            {
                if (!graph_ || !name || !graph_->m_data || !graph_->m_data->m_stringData || !graph_->m_variableValueSet) return false;
                const auto& names  = graph_->m_data->m_stringData->m_variableNames;
                const auto& infos  = graph_->m_data->m_variableInfos;
                const auto& values = graph_->m_variableValueSet->m_wordVariableValues;
                const int   count  = std::min({ names.getSize(), infos.getSize(), values.getSize() });
                for (int i = 0; i < count; ++i) {
                    const char* declared = names[i].cString();
                    if (!declared || std::strcmp(declared, name) != 0) continue;
                    const std::int32_t word = values[i].m_value;
                    switch (static_cast<::hkbVariableInfo::VariableType>(infos[i].m_type)) {
                    case ::hkbVariableInfo::VARIABLE_TYPE_BOOL: out = relay::Value::Bool(word != 0); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT8:
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT16:
                    case ::hkbVariableInfo::VARIABLE_TYPE_INT32: out = relay::Value::Int(word); return true;
                    case ::hkbVariableInfo::VARIABLE_TYPE_REAL: out = relay::Value::Float(std::bit_cast<float>(word)); return true;
                    default: return false;
                    }
                }
                return false;
            }

        private:
            const ::hkbBehaviorGraph* graph_;
        };

        /// @return the rule to play: the highest priority whose `when` holds (equal priorities: list order), -1 for none.
        int Pick(const AnimModifier& rules, const ::hkbContext& context, RE::Actor* actor)
        {
            const int count = rules.m_rules.getSize();
            if (count == 0) return -1;
            std::vector<int> order(static_cast<std::size_t>(count));
            for (int i = 0; i < count; ++i) order[static_cast<std::size_t>(i)] = i;
            std::stable_sort(order.begin(), order.end(),
                             [&](int a, int b) { return rules.m_rules[a].m_priority > rules.m_rules[b].m_priority; });

            static thread_local relay::EvalContext ctx;
            const BehaviorVariables                variables(context.m_behavior);
            ctx.Reset(actor);
            ctx.variables = &variables;
            for (const int i : order) {
                const AnimModifierRule& rule = rules.m_rules[i];
                const char*             file = rule.m_animation.cString();
                if (!file || !*file) continue;
                const char* when = rule.m_when.cString();
                if (!when || !*when) return i;
                if (const relay::CompiledExpr* expr = Expressions().Of(when); expr && expr->Evaluate(ctx).AsBool()) return i;
            }
            return -1;
        }

        /// @return the binding the clip's own roster slot holds, null without one.
        ::hkaAnimationBinding* OwnBinding(const ::hkbClipGenerator& clip, const ::hkbContext& context)
        {
            const ::hkbAnimationBindingSet* set = context.m_character ? context.m_character->getAnimationBindingSet() : nullptr;
            const int                       at  = clip.m_animationBindingIndex;
            if (!set || at < 0 || at >= set->m_bindings.getSize() || !set->m_bindings[at]) return nullptr;
            return set->m_bindings[at]->m_binding;
        }

        /// @brief Logs `what` once per (clip template, file): the first swap, or the first give-up, of each pair.
        void NoteOnce(const ::hkbClipGenerator* clip, const char* file, const char* what)
        {
            static std::mutex                                 lock;
            static std::unordered_set<std::string>            noted;
            const std::string                                 key = std::to_string(reinterpret_cast<std::uintptr_t>(clip)) + '|' + what + '|' + (file ? file : "");
            {
                std::scoped_lock guard(lock);
                if (!noted.insert(key).second) return;
            }
            LOG_INFO("[anim modifier] clip '{}': {} '{}'", clip && clip->m_name.cString() ? clip->m_name.cString() : "", what, file ? file : "");
        }

        /// @brief Warns once per clip template that a CBAnimModifier cannot reach it.
        void WarnUnreached(const ::hkbClipGenerator* clip, const char* why)
        {
            static std::mutex                            lock;
            static std::unordered_set<const void*>       warned;
            {
                std::scoped_lock guard(lock);
                if (!warned.insert(clip).second) return;
            }
            LOG_WARN("[anim modifier] clip '{}': {}", clip && clip->m_name.cString() ? clip->m_name.cString() : "", why);
        }

    }  // namespace

    AnimModifier::AnimModifier(::hkFinishLoadedObjectFlag flag) :
        hkbModifier(flag),
        m_rules(flag)
    {
        m_userData |= 2;
    }

    AnimModifier::AnimModifier(const AnimModifier& other) :
        hkbModifier(other),
        m_clip(other.m_clip)
    {
        m_userData |= 2;
    }

    AnimModifier::~AnimModifier() = default;

    const ::hkClass* AnimModifier::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    const AnimModifier* AnimModifier::Template(const ::hkbContext& context) const
    {
        if (m_cloneState != CLONE_STATE_CLONE) return this;
        ::hkbBehaviorGraph* root = context.m_character ? context.m_character->m_behaviorGraph.val() : nullptr;
        return root ? static_cast<const AnimModifier*>(root->getNodeTemplate(this)) : nullptr;
    }

    ::hkbClipGenerator* AnimModifier::ActiveClip(const ::hkbContext& context) const
    {
        ::hkbBehaviorGraph* root = context.m_character ? context.m_character->m_behaviorGraph.val() : nullptr;
        if (!m_clip || !root || !root->m_activeNodeTemplateToIndexMap || !root->m_activeNodeTemplateToIndexMap->hasKey(m_clip)) return nullptr;
        ::hkbNode* clone = root->getNodeClone(m_clip);
        if (!clone || clone->getClassType() != &::hkbClipGenerator::staticClass()) return nullptr;
        return static_cast<::hkbClipGenerator*>(clone);
    }

    void AnimModifier::TrySwap(const ::hkbContext& context, ::hkbClipGenerator& clip, bool atActivate)
    {
        const AnimModifier* from = Template(context);
        if (!from || m_rule < 0 || m_rule >= from->m_rules.getSize()) {
            m_state &= ~kPending;
            return;
        }
        const char* path = from->m_rules[m_rule].m_animation.cString();
        if (!atActivate && clip.m_time > kLateSwap) {
            NoteOnce(m_clip, path, "still loading after the swap window, keeps its own animation this time:");
            m_state &= ~kPending;
            return;
        }
        platform::anim::LoadedAnimation file = anim::Acquire(anim::IdOf(path ? path : ""));
        if (!file) {
            if (atActivate) NoteOnce(m_clip, path, "not loaded yet, asked the engine to stream in");
            m_state |= kPending;
            return;
        }
        m_state &= ~kPending;
        if (!anim::Rebind(clip, context, file.Binding())) {
            WarnUnreached(m_clip, "not a rostered clip with a control (index -1 or none): it keeps its own animation");
            return;
        }
        m_playing = std::move(file);
        NoteOnce(m_clip, path, "plays");
    }

    void AnimModifier::activate(const ::hkbContext& context)
    {
        m_playing.Reset();
        m_rule  = -1;
        m_state = 0;
        if (!Enabled()) return;

        const AnimModifier* from = Template(context);
        if (!from) return;
        ::hkbClipGenerator* clip = ActiveClip(context);
        if (!clip) {
            if (m_clip) WarnUnreached(m_clip, "a CBAnimModifier activated without it: wrap the modifier over a subtree that holds its clip");
            return;
        }
        m_rule = Pick(*from, context, platform::game::ActorOf(context.m_character));
        if (m_rule >= 0) TrySwap(context, *clip, true);
    }

    void AnimModifier::update(const ::hkbContext& context, ::hkReal timestep)
    {
        if (!(m_state & kPending)) return;
        if (::hkbClipGenerator* clip = ActiveClip(context)) TrySwap(context, *clip, false);
        else m_state &= ~kPending;
    }

    void AnimModifier::deactivate(const ::hkbContext& context)
    {
        if (m_playing) {
            /// @note getNodeClone hands back the template when the clone is gone (released in this pass); a template is
            ///       never activated, so its m_binding is null and never matches.
            ::hkbBehaviorGraph* root = context.m_character ? context.m_character->m_behaviorGraph.val() : nullptr;
            ::hkbNode*          node = root && m_clip ? root->getNodeClone(m_clip) : nullptr;
            if (node && node != m_clip && node->getClassType() == &::hkbClipGenerator::staticClass()) {
                auto* clip = static_cast<::hkbClipGenerator*>(node);
                /// @note A clip left active with no binding of its own must not keep this one past its release: it is
                ///       cleared, as the clip's own deactivate would clear it.
                if (clip->m_binding == m_playing.Binding() && !anim::Rebind(*clip, context, OwnBinding(*clip, context))) {
                    clip->m_binding = nullptr;
                    if (clip->m_animationControl) clip->m_animationControl->setAnimationBinding(nullptr);
                }
            }
        }
        m_playing.Reset();
        m_rule  = -1;
        m_state = 0;
    }

    bool AnimModifier::isValid(::hkStringPtr& errorString) const
    {
        if (m_clip) return true;
        errorString = "CBAnimModifier: no clip";
        return false;
    }

    ::hkbNode* AnimModifier::cloneNode(::hkbBehaviorGraph& rootBehavior) const { return new AnimModifier(*this); }

}  // namespace CB::feature::replacer
