#include "PCH.h"

#include "CharPoseSearchSchema.h"

#include <platform/classes/ClassRegistry.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphData.h>
#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraphStringData.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Character/hkbCharacterData.h>
#include <Behavior/Behavior/Character/hkbCharacterSetup.h>
#include <Behavior/Behavior/Character/hkbCharacterStringData.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Variables/hkbVariableValueSet.h>
#include <Common/Base/Reflection/hkClass.h>

#include <cstring>
#include <new>

namespace CB::feature::controller::motion {

    namespace {

        /// @brief Declare `T` (finish constructor, destructor, vtable) for the platform to register as `name`.
        template <class T>
        bool DeclareClass(const char* name)
        {
            struct Ops {
                static void Finish(void* object)
                {
                    if (object) new (object) T(::hkFinishLoadedObjectFlag{});
                }
                static void        Cleanup(void* object) { static_cast<T*>(object)->~T(); }
                static const void* Vtable()
                {
                    alignas(T) unsigned char scratch[sizeof(T)] = {};
                    Finish(scratch);
                    return *reinterpret_cast<const void* const*>(scratch);
                }
            };
            return platform::classes::Declare({ name, sizeof(T), &Ops::Finish, &Ops::Cleanup, &Ops::Vtable });
        }

        constexpr const char* kChannel = "CBPoseSearchChannel";
        constexpr const char* kProfile = "CBPoseSearchProfile";

        [[maybe_unused]] const bool s_declared = DeclareClass<PoseSearchChannel>(kChannel) && DeclareClass<PoseSearchProfile>(kProfile);

        const ::hkClass* ClassNamed(const char* name) { return static_cast<const ::hkClass*>(platform::classes::ClassOf(name)); }

        const ::hkbBehaviorGraphStringData* NamesOf(const ::hkbContext& context)
        {
            const ::hkbBehaviorGraph* graph = context.m_behavior;
            return graph && graph->m_data ? graph->m_data->m_stringData.val() : nullptr;
        }

    }  // namespace

    PoseSearchChannel::PoseSearchChannel(::hkFinishLoadedObjectFlag flag) : ::hkbBindable(flag), m_name(flag), m_curve(flag), m_samples(flag) {}
    PoseSearchProfile::PoseSearchProfile(::hkFinishLoadedObjectFlag flag) : ::hkbBindable(flag), m_name(flag), m_weights(flag) {}

    const ::hkClass* PoseSearchChannel::getClassType() const { static const auto* c = ClassNamed(kChannel); return c; }
    const ::hkClass* PoseSearchProfile::getClassType() const { static const auto* c = ClassNamed(kProfile); return c; }

    std::string VariableOf(const ::hkbContext& context, const ::hkbVariableBindingSet::Binding& b)
    {
        const auto* names = NamesOf(context);
        if (b.m_bindingType != ::hkbVariableBindingSet::Binding::BINDING_TYPE_VARIABLE || !names) return {};
        if (b.m_variableIndex < 0 || b.m_variableIndex >= names->m_variableNames.getSize()) return {};
        return Text(names->m_variableNames[b.m_variableIndex]);
    }

    std::string BoundVariable(const ::hkbContext& context, const ::hkbBindable& bindable, std::string_view member)
    {
        std::string out;
        ForEachVariableBinding(context, bindable, [&](std::string_view path, const std::string& variable) {
            if (out.empty() && path == member) out = variable;
        });
        return out;
    }

    const ::hkReferencedObject* BoundCharacterProperty(const ::hkbContext& context, const ::hkbBindable& bindable, std::string_view member)
    {
        const ::hkbVariableBindingSet* set   = bindable.m_variableBindingSet.val();
        const auto*                    names = NamesOf(context);
        if (!set || !names || !context.m_character) return nullptr;
        const ::hkbCharacterSetup* setup = context.m_character->m_setup.val();
        const ::hkbCharacterData*  data  = setup ? setup->m_data.val() : nullptr;
        if (!data || !data->m_stringData || !data->m_characterPropertyValues) return nullptr;
        for (int i = 0; i < set->m_bindings.getSize(); ++i) {
            const auto& b    = set->m_bindings[i];
            const char* path = b.m_memberPath.cString();
            if (!path || member != path || b.m_bindingType != ::hkbVariableBindingSet::Binding::BINDING_TYPE_CHARACTER_PROPERTY) continue;
            if (b.m_variableIndex < 0 || b.m_variableIndex >= names->m_characterPropertyNames.getSize()) return nullptr;
            /// @note By name: the behavior numbers its properties its own way, the character its own.
            const char*                   want   = names->m_characterPropertyNames[b.m_variableIndex].cString();
            const auto&                   have   = data->m_stringData->m_characterPropertyNames;
            const ::hkbVariableValueSet&  values = *data->m_characterPropertyValues;
            for (int p = 0; want && p < have.getSize(); ++p) {
                if (!have[p].cString() || std::strcmp(have[p].cString(), want) != 0) continue;
                if (p >= values.m_wordVariableValues.getSize()) return nullptr;
                const int variant = values.m_wordVariableValues[p].m_value;
                return variant >= 0 && variant < values.m_variantVariableValues.getSize() ? values.m_variantVariableValues[variant].val() : nullptr;
            }
            return nullptr;
        }
        return nullptr;
    }

}  // namespace CB::feature::controller::motion
