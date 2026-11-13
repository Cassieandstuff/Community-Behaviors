#include "PCH.h"

#include "DynamicReferenceGenerator.h"

#include "Bindings.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/game/Character.h>

#include <Behavior/Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Behavior/Character/hkbCharacter.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Reflection/hkClass.h>

#include <new>
#include <utility>

namespace CB::feature::dynamic {

    namespace {

        constexpr const char* kClassName = "CBDynamicBehaviorReferenceGenerator";

        void Finish(void* object)
        {
            if (object) new (object) DynamicReferenceGenerator(::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<DynamicReferenceGenerator*>(object)->~DynamicReferenceGenerator(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(DynamicReferenceGenerator) unsigned char scratch[sizeof(DynamicReferenceGenerator)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }
        /// @note Registered whatever the flags: an unregistered class name loads with a null vtable.
        [[maybe_unused]] const bool s_declared = platform::classes::Declare(
            { kClassName, sizeof(DynamicReferenceGenerator), &Finish, &Cleanup, &Vtable });

        const char* Text(const ::hkStringPtr& s) { return s.cString() ? s.cString() : ""; }

    }  // namespace

    DynamicReferenceGenerator::DynamicReferenceGenerator(::hkFinishLoadedObjectFlag flag) :
        hkbGenerator(flag), m_key(flag), m_ticket(0), m_running(nullptr)
    {}

    DynamicReferenceGenerator::DynamicReferenceGenerator(const DynamicReferenceGenerator& other) :
        hkbGenerator(other), m_key(other.m_key), m_fallback(other.m_fallback), m_ticket(0), m_running(nullptr)
    {}

    DynamicReferenceGenerator::~DynamicReferenceGenerator() { Release(std::exchange(m_ticket, 0)); }

    const ::hkClass* DynamicReferenceGenerator::getClassType() const
    {
        static const auto* const cls = static_cast<const ::hkClass*>(platform::classes::ClassOf(kClassName));
        return cls;
    }

    void DynamicReferenceGenerator::activate(const ::hkbContext& context)
    {
        Release(std::exchange(m_ticket, 0));
        RE::Actor*          actor = platform::game::ActorOf(context.m_character);
        const std::uint32_t id    = actor ? actor->GetFormID() : 0;
        const Held          held  = actor ? Acquire(id, Text(m_key), context.m_character) : Held{};
        m_ticket                  = held.ticket;
        ::hkbGenerator* previous  = m_running;
        if (held.generator) {
            m_running = held.generator;
        } else {
            m_running = m_fallback;
            if (!held.path.empty())
                LOG_WARN("[dynamic-behavior] {:08X} '{}': '{}' is bound but not loaded - {}", id, Text(m_key), held.path,
                         m_fallback ? "playing the fallback" : "the slot is inert");
            else
                LOG_INFO("[dynamic-behavior] {:08X} '{}': nothing bound - {}", id, Text(m_key), m_fallback ? "playing the fallback" : "the slot is inert");
        }
        if (previous != m_running && context.m_character && context.m_character->m_behaviorGraph)
            context.m_character->m_behaviorGraph->m_updateActiveNodes = true;
    }

    void DynamicReferenceGenerator::deactivate(const ::hkbContext& context)
    {
        (void)context;
        Release(std::exchange(m_ticket, 0));
    }

    int DynamicReferenceGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        (void)flags;
        return 1;
    }

    /// @note Inactive, the template reports its fallback, so the graph's walks (linking, clip loading) see it.
    void DynamicReferenceGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        ::hkbGenerator* child = (flags & FLAG_ACTIVE_ONLY) ? m_running : (m_running ? m_running : m_fallback);
        if (!child) {
            childrenInfo.m_childInfos.setSize(0);
            return;
        }
        childrenInfo.m_childInfos.setSize(1);
        childrenInfo.m_childInfos[0].m_node = child;
    }

    bool DynamicReferenceGenerator::isValid(::hkStringPtr& errorString) const
    {
        if (!m_key.cString() || !*m_key.cString()) {
            errorString = "The key is empty.";
            return false;
        }
        return true;
    }

    ::hkbNode* DynamicReferenceGenerator::cloneNode(::hkbBehaviorGraph& rootBehavior) const
    {
        (void)rootBehavior;
        return new DynamicReferenceGenerator(*this);
    }

    /// @note Nothing running: the output is left as the graph handed it, as a reference whose behavior failed to load.
    void DynamicReferenceGenerator::generate(const ::hkbContext& context, const ::hkbGeneratorOutput** activeChildrenOutput,
                                             ::hkbGeneratorOutput& output, ::hkReal timeOffset)
    {
        (void)context;
        (void)timeOffset;
        if (m_running && activeChildrenOutput && activeChildrenOutput[0] && &output != activeChildrenOutput[0])
            ::hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
    }

}  // namespace CB::feature::dynamic
