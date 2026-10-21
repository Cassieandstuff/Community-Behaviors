#include "PCH.h"

#include "CharPoseSearch.h"

#include <platform/classes/ClassRegistry.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <new>

/// @file
/// @brief CBPoseSearchGenerator's behavior, after havok/'s hkbManualSelectorGenerator line for line, with int32 indices.
/// @note  Registered whatever the flags: a behavior holding this class loads with the add-on whether or not motion
///        matching runs, and an unregistered class name loads with a null vtable.

namespace CB::feature::controller::motion {

    namespace {

        namespace hk = CB::havok;

        constexpr const char* kClassName = "CBPoseSearchGenerator";

        void Finish(void* object)
        {
            if (object) new (object) PoseSearchGenerator(hk::hkFinishLoadedObjectFlag{});
        }
        void Cleanup(void* object) { static_cast<PoseSearchGenerator*>(object)->~PoseSearchGenerator(); }
        /// @note As the exe's own type infos take it: a finish constructor run on a scratch buffer, its vtable read off.
        const void* Vtable()
        {
            alignas(PoseSearchGenerator) unsigned char scratch[sizeof(PoseSearchGenerator)] = {};
            Finish(scratch);
            return *reinterpret_cast<const void* const*>(scratch);
        }

        [[maybe_unused]] const bool s_declared = platform::classes::Declare(
            { kClassName, sizeof(PoseSearchGenerator), &Finish, &Cleanup, &Vtable });

    }  // namespace

    /// @note Only the runtime index resets: the children and the selected index are the loaded bytes.
    PoseSearchGenerator::PoseSearchGenerator(hk::hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_generators(flag)
    {
        m_currentIndex = 0;
    }

    PoseSearchGenerator::PoseSearchGenerator(const PoseSearchGenerator& other)
    :   hkbGenerator(other),
        m_selectedIndex(other.m_selectedIndex),
        m_currentIndex(0)
    {
        m_generators = other.m_generators;
        for (int i = 0; i < m_generators.getSize(); i++) m_generators[i]->addReference();
    }

    PoseSearchGenerator::~PoseSearchGenerator()
    {
        for (int i = 0; i < m_generators.getSize(); i++)
            if (m_generators[i]) m_generators[i]->removeReference();
    }

    const void* PoseSearchGenerator::getClassType() const
    {
        static const void* const cls = platform::classes::ClassOf(kClassName);
        return cls;
    }

    void PoseSearchGenerator::activate(const hk::hkbContext& context)
    {
        m_currentIndex = computeCurrentIndex();
    }

    void PoseSearchGenerator::update(const hk::hkbContext& context, hk::hkReal timestep)
    {
        const int previous = m_currentIndex;
        m_currentIndex     = computeCurrentIndex();
        if (previous != m_currentIndex && m_generators[previous] != m_generators[m_currentIndex]) {
            hk::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes  = true;
        }
    }

    int PoseSearchGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (flags & FLAG_ACTIVE_ONLY) return 1;
        return m_generators.getSize();
    }

    void PoseSearchGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (flags & FLAG_ACTIVE_ONLY) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_generators[m_currentIndex];
        } else {
            childrenInfo.m_childInfos.setSize(m_generators.getSize());
            for (int i = 0; i < m_generators.getSize(); i++) childrenInfo.m_childInfos[i].m_node = m_generators[i];
        }
    }

    bool PoseSearchGenerator::isValid(hk::hkStringPtr& errorString) const
    {
        const int n = m_generators.getSize();
        if (n == 0) {
            errorString = "You must have at least one child generator.";
            return false;
        }
        for (int i = 0; i < n; i++) {
            if (m_generators[i] == HK_NULL) {
                errorString = "At least one of the child generators is null.";
                return false;
            }
        }
        if (m_selectedIndex < 0 || m_selectedIndex >= n) {
            errorString = "The selected generator is out of range.";
            return false;
        }
        return true;
    }

    hk::hkbNode* PoseSearchGenerator::cloneNode(hk::hkbBehaviorGraph& rootBehavior) const
    {
        return new PoseSearchGenerator(*this);
    }

    void PoseSearchGenerator::generate(const hk::hkbContext& context, const hk::hkbGeneratorOutput** activeChildrenOutput,
                                       hk::hkbGeneratorOutput& output, hk::hkReal timeOffset)
    {
        if (&output != activeChildrenOutput[0]) hk::hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
    }

    bool PoseSearchGenerator::canRecycleOutput() const
    {
        return true;
    }

    void PoseSearchGenerator::updateSync(const hk::hkbContext& context, hk::hkbNodeInfo& info)
    {
        hk::hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const hk::hkbNode*    child        = m_generators[m_currentIndex];
        if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
        const int index   = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
        info.m_isProxy    = true;
        info.m_proxyIndex = (*rootBehavior->m_activeNodes)[index].m_isProxy ? (*rootBehavior->m_activeNodes)[index].m_proxyIndex : index;
    }

    int PoseSearchGenerator::computeCurrentIndex() const
    {
        const int last = m_generators.getSize() - 1;
        if (m_selectedIndex < 0) return 0;
        return m_selectedIndex > last ? last : m_selectedIndex;
    }

}  // namespace CB::feature::controller::motion
