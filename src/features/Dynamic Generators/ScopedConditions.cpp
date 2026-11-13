#include "PCH.h"

#include "ScopedConditions.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>
#include <hky/relay/DynamicClip.h>

#include <Common/Base/Reflection/hkClass.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <map>
#include <optional>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::feature::dynamic {

    namespace {

        namespace rl = hky::relay;

        template <class T>
        struct Registration {
            static void        Finish(void* object) { if (object) new (object) T(::hkFinishLoadedObjectFlag{}); }
            static void        Cleanup(void* object) { static_cast<T*>(object)->~T(); }
            static const void* Vtable()
            {
                alignas(T) unsigned char scratch[sizeof(T)] = {};
                Finish(scratch);
                return *reinterpret_cast<const void* const*>(scratch);
            }
            static bool Declare(const char* name) { return platform::classes::Declare({ name, sizeof(T), &Finish, &Cleanup, &Vtable }); }
        };

        /// @note Registered whatever the flags: an unregistered class name loads with a null vtable.
        [[maybe_unused]] const bool s_and    = Registration<ConditionAnd>::Declare("CBConditionAnd");
        [[maybe_unused]] const bool s_or     = Registration<ConditionOr>::Declare("CBConditionOr");
        [[maybe_unused]] const bool s_not    = Registration<ConditionNot>::Declare("CBConditionNot");
        [[maybe_unused]] const bool s_scoped = Registration<ScopedCondition>::Declare("CBScopedCondition");

        const ::hkClass* ClassNamed(const char* name) { return static_cast<const ::hkClass*>(platform::classes::ClassOf(name)); }

        std::string_view Text(const ::hkStringPtr& s) { return s.cString() ? std::string_view(s.cString()) : std::string_view(); }

        using Forms = std::vector<RE::TESForm*>;

        /// @brief The form a FormKey names, looked up once per distinct text for the life of the process.
        RE::TESForm* FormOf(std::string_view text)
        {
            static std::mutex                                    lock;
            static std::unordered_map<std::string, RE::TESForm*> cache;
            std::scoped_lock                                     guard(lock);
            const auto [it, fresh] = cache.try_emplace(std::string(text), nullptr);
            if (fresh) {
                const auto key = rl::ParseFormKey(text);
                auto*      dh  = RE::TESDataHandler::GetSingleton();
                if (key && dh) it->second = dh->LookupForm(key->id, key->plugin);
                if (!it->second) LOG_WARN("[scoped-condition] '{}' names no form - a test on it does not hold", text);
            }
            return it->second;
        }

        /// @brief The thread's evaluation pass: chains resolved so far, keyed by the self actor and the chain's text.
        struct Pass {
            int                                                         depth = 0;
            std::map<std::pair<RE::FormID, std::string>, Forms, std::less<>> chains;
        };
        thread_local Pass t_pass;

        std::string ChainKey(const ::hkArray<ConditionScope>& chain)
        {
            std::string key;
            for (int i = 0; i < chain.getSize(); ++i) {
                key += Text(chain[i].m_kind);
                for (int a = 0; a < chain[i].m_arguments.getSize(); ++a) {
                    key += '\x1f';
                    key += Text(chain[i].m_arguments[a]);
                }
                key += '\x1e';
            }
            return key;
        }

        void Add(Forms& out, RE::TESForm* f)
        {
            if (f && std::find(out.begin(), out.end(), f) == out.end()) out.push_back(f);
        }

        std::optional<std::uint32_t> UInt(std::string_view s)
        {
            std::uint32_t v = 0;
            const auto [p, e] = std::from_chars(s.data(), s.data() + s.size(), v);
            if (e != std::errc{} || p != s.data() + s.size()) return std::nullopt;
            return v;
        }

        RE::TESObjectREFR* AliasReference(std::string_view quest, std::string_view aliasId)
        {
            auto*      q  = FormOf(quest);
            const auto id = UInt(aliasId);
            auto*      tq = q ? q->As<RE::TESQuest>() : nullptr;
            if (!tq || !id) return nullptr;
            for (RE::BGSBaseAlias* alias : tq->aliases) {
                if (!alias || alias->aliasID != *id) continue;
                if (alias->GetVMTypeID() != RE::BGSRefAlias::VMTYPEID) return nullptr;
                return static_cast<RE::BGSRefAlias*>(alias)->GetReference();
            }
            return nullptr;
        }

        /// @brief One step over the forms the step before produced. The kinds are engine::relay::EvaluatedScopes().
        Forms Step(const ConditionScope& step, const Forms& in, RE::Actor* self, bool first)
        {
            const std::string_view kind = Text(step.m_kind);
            const auto             arg  = [&](int i) { return i < step.m_arguments.getSize() ? Text(step.m_arguments[i]) : std::string_view(); };
            Forms                  out;
            if (kind == "self") {
                if (first) Add(out, self);
                return out;
            }
            if (kind == "alias") {
                if (first) Add(out, AliasReference(arg(0), arg(1)));
                return out;
            }
            for (RE::TESForm* f : in) {
                RE::Actor* a = f ? f->As<RE::Actor>() : nullptr;
                if (!a) continue;
                if (kind == "base") Add(out, a->GetActorBase());
                else if (kind == "race") Add(out, a->GetRace());
                else if (kind == "equipped.right") Add(out, a->GetEquippedObject(false));
                else if (kind == "equipped.left") Add(out, a->GetEquippedObject(true));
                else if (kind == "ammo") Add(out, a->GetCurrentAmmo());
                else if (kind == "worn") {
                    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;
                    if (const auto slot = UInt(arg(0)); slot && *slot >= 30 && *slot <= 61)
                        Add(out, a->GetWornArmor(static_cast<Slot>(1u << (*slot - 30))));
                    else
                        for (std::uint32_t s = 0; s < 32; ++s) Add(out, a->GetWornArmor(static_cast<Slot>(1u << s)));
                } else if (kind == "furniture") {
                    if (const auto ref = a->GetOccupiedFurniture().get()) Add(out, ref->GetBaseObject());
                } else if (kind == "combatTarget") {
                    if (const auto target = a->GetActorRuntimeData().currentCombatTarget.get()) Add(out, target.get());
                } else if (kind == "mount") {
                    RE::NiPointer<RE::Actor> mount;
                    if (a->GetMount(mount)) Add(out, mount.get());
                }
            }
            return out;
        }

        Forms Resolve(const ::hkArray<ConditionScope>& chain, RE::Actor* self)
        {
            if (chain.getSize() == 0) return self ? Forms{ self } : Forms{};
            std::string key;
            if (t_pass.depth > 0) {
                key = ChainKey(chain);
                if (const auto it = t_pass.chains.find(std::pair{ self ? self->GetFormID() : 0u, key }); it != t_pass.chains.end())
                    return it->second;
            }
            Forms forms = self ? Forms{ self } : Forms{};
            for (int i = 0; i < chain.getSize(); ++i) {
                const std::string_view kind = Text(chain[i].m_kind);
                /// @note A chain that starts elsewhere (alias) does not start from self.
                if (i == 0 && kind == "alias") forms.clear();
                forms = Step(chain[i], forms, self, i == 0);
                if (forms.empty()) break;
            }
            if (t_pass.depth > 0) t_pass.chains.emplace(std::pair{ self ? self->GetFormID() : 0u, std::move(key) }, forms);
            return forms;
        }

        bool HasKeyword(RE::TESForm* f, RE::BGSKeyword* kw)
        {
            if (!f || !kw) return false;
            if (auto* ref = f->As<RE::TESObjectREFR>()) return ref->HasKeyword(kw);
            if (auto* kf = f->As<RE::BGSKeywordForm>()) return kf->HasKeyword(kw);
            return false;
        }

        bool Compare(float a, std::string_view op, float b)
        {
            if (op == "==") return a == b;
            if (op == "!=") return a != b;
            if (op == "<") return a < b;
            if (op == "<=") return a <= b;
            if (op == ">") return a > b;
            if (op == ">=") return a >= b;
            return false;
        }

        bool AnyEffect(RE::Actor* a, const auto& match)
        {
            auto* target = a ? a->AsMagicTarget() : nullptr;
            auto* list   = target ? target->GetActiveEffectList() : nullptr;
            if (!list) return false;
            for (RE::ActiveEffect* ae : *list)
                if (ae && !ae->flags.any(RE::ActiveEffect::Flag::kInactive) && match(ae->GetBaseObject())) return true;
            return false;
        }

        /// @brief The test on one form. The tests are engine::relay::EvaluatedTests().
        bool Test(const ScopedCondition& c, RE::TESForm* f, const Forms& other)
        {
            const std::string_view test = Text(c.m_test);
            const auto             arg  = [&](int i) { return i < c.m_arguments.getSize() ? Text(c.m_arguments[i]) : std::string_view(); };
            if (test == "Exists") return f != nullptr;
            if (test == "IsSame") return std::find(other.begin(), other.end(), f) != other.end();
            if (test == "IsForm") {
                RE::TESForm* want = FormOf(arg(0));
                if (!want) return false;
                if (f == want) return true;
                auto* ref = f->As<RE::TESObjectREFR>();
                return ref && ref->GetBaseObject() == want;
            }
            if (test == "HasKeyword") {
                RE::TESForm* kw = FormOf(arg(0));
                return HasKeyword(f, kw ? kw->As<RE::BGSKeyword>() : nullptr);
            }
            RE::Actor* a = f->As<RE::Actor>();
            if (!a) return false;
            if (test == "ActorValue") {
                const std::string name(arg(0));
                auto*             list = RE::ActorValueList::GetSingleton();
                const auto        av   = list ? list->LookupActorValueByName(name.c_str()) : RE::ActorValue::kNone;
                float             want = 0.0f;
                const std::string_view v = arg(2);
                const auto [p, e] = std::from_chars(v.data(), v.data() + v.size(), want);
                if (av == RE::ActorValue::kNone || e != std::errc{}) return false;
                return Compare(a->AsActorValueOwner()->GetActorValue(av), arg(1), want);
            }
            if (test == "HasMagicEffect") {
                RE::TESForm* want = FormOf(arg(0));
                return want && AnyEffect(a, [&](RE::EffectSetting* e) { return e == want; });
            }
            if (test == "HasMagicEffectWithKeyword") {
                RE::TESForm* kw = FormOf(arg(0));
                auto*        k  = kw ? kw->As<RE::BGSKeyword>() : nullptr;
                return k && AnyEffect(a, [&](RE::EffectSetting* e) { return e && e->HasKeyword(k); });
            }
            return false;
        }

    }  // namespace

    EvaluationScope::EvaluationScope() { ++t_pass.depth; }
    EvaluationScope::~EvaluationScope()
    {
        if (--t_pass.depth == 0) t_pass.chains.clear();
    }

    ConditionAnd::ConditionAnd(::hkFinishLoadedObjectFlag flag) : hkbCondition(flag), m_conditions(flag) {}
    const ::hkClass* ConditionAnd::getClassType() const
    {
        static const auto* const cls = ClassNamed("CBConditionAnd");
        return cls;
    }
    bool ConditionAnd::isTrue(const ::hkbContext& context)
    {
        for (int i = 0; i < m_conditions.getSize(); ++i)
            if (m_conditions[i] && !m_conditions[i]->isTrue(context)) return false;
        return true;
    }

    ConditionOr::ConditionOr(::hkFinishLoadedObjectFlag flag) : hkbCondition(flag), m_conditions(flag) {}
    const ::hkClass* ConditionOr::getClassType() const
    {
        static const auto* const cls = ClassNamed("CBConditionOr");
        return cls;
    }
    bool ConditionOr::isTrue(const ::hkbContext& context)
    {
        for (int i = 0; i < m_conditions.getSize(); ++i)
            if (m_conditions[i] && m_conditions[i]->isTrue(context)) return true;
        return false;
    }

    ConditionNot::ConditionNot(::hkFinishLoadedObjectFlag flag) : hkbCondition(flag) {}
    const ::hkClass* ConditionNot::getClassType() const
    {
        static const auto* const cls = ClassNamed("CBConditionNot");
        return cls;
    }
    bool ConditionNot::isTrue(const ::hkbContext& context) { return !(m_condition && m_condition->isTrue(context)); }

    ScopedCondition::ScopedCondition(::hkFinishLoadedObjectFlag flag) :
        hkbCondition(flag), m_scope(flag), m_test(flag), m_arguments(flag), m_other(flag)
    {}
    const ::hkClass* ScopedCondition::getClassType() const
    {
        static const auto* const cls = ClassNamed("CBScopedCondition");
        return cls;
    }

    bool ScopedCondition::isTrue(const ::hkbContext& context)
    {
        RE::Actor* self = platform::game::ActorOf(context.m_character);
        if (!self) return false;
        /// @note A pass of one, so a condition outside a dynamic clip generator still resolves each chain once.
        const EvaluationScope pass;
        const Forms           forms = Resolve(m_scope, self);
        if (forms.empty()) return false;
        const Forms other = Text(m_test) == "IsSame" ? Resolve(m_other, self) : Forms{};
        if (m_quantifier == 1) return std::all_of(forms.begin(), forms.end(), [&](RE::TESForm* f) { return Test(*this, f, other); });
        return std::any_of(forms.begin(), forms.end(), [&](RE::TESForm* f) { return Test(*this, f, other); });
    }

}  // namespace CB::feature::dynamic
