/// @file
/// @brief hkbManualSelectorGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/ManualSelector/hkbManualSelectorGenerator.h>

#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    /// @note VARIANT: hkbNode::m_cloneState stored before the vtable address is loaded (schedule only, as hkbSequence's).
    hkbManualSelectorGenerator::hkbManualSelectorGenerator()
    :   m_selectedGeneratorIndex(0),
        m_currentGeneratorIndex(0)
    {
    }

    hkbManualSelectorGenerator::hkbManualSelectorGenerator(const hkbManualSelectorGenerator& gen)
    :   hkbGenerator(gen),
        m_selectedGeneratorIndex(gen.m_selectedGeneratorIndex),
        m_currentGeneratorIndex(0)
    {
        m_generators = gen.m_generators;
        const int numGenerators = m_generators.getSize();
        for (int i = 0; i < numGenerators; i++) m_generators[i]->addReference();
    }

    hkbManualSelectorGenerator::~hkbManualSelectorGenerator()
    {
        for (int i = 0; i < m_generators.getSize(); i++) {
            if (m_generators[i]) m_generators[i]->removeReference();
        }
    }

    const void* hkbManualSelectorGenerator::getClassType() const
    {
        return &staticClass();
    }

    void hkbManualSelectorGenerator::activate(const hkbContext& context)
    {
        m_currentGeneratorIndex = computeCurrentGeneratorIndex();
    }

    /// @note The monitor timer opens after the new index is stored, as in vanilla. Vanilla keeps `flag | true` unfolded
    ///       (setnz; or 1); VS2015 24247 folds it to a constant store (rule H).
    void hkbManualSelectorGenerator::update(const hkbContext& context, hkReal timestep)
    {
        const int previousIndex = m_currentGeneratorIndex;
        m_currentGeneratorIndex = computeCurrentGeneratorIndex();

        HK_TIMER_BEGIN("MSG::requestUpdateActiveNodes");
        if (previousIndex != m_currentGeneratorIndex && m_generators[previousIndex] != m_generators[m_currentGeneratorIndex]) {
            hkbBehaviorGraph* rootBehavior   = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes = rootBehavior->m_updateActiveNodes | true;
        }
        HK_TIMER_END();
    }

    int hkbManualSelectorGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (flags & FLAG_ACTIVE_ONLY) return 1;
        return m_generators.getSize();
    }

    void hkbManualSelectorGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (flags & FLAG_ACTIVE_ONLY) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_generators[m_currentGeneratorIndex];
        } else {
            childrenInfo.m_childInfos.setSize(m_generators.getSize());
            for (int i = 0; i < m_generators.getSize(); i++) childrenInfo.m_childInfos[i].m_node = m_generators[i];
        }
    }

    bool hkbManualSelectorGenerator::isValid(hkStringPtr& errorString) const
    {
        const int numGenerators = m_generators.getSize();
        if (numGenerators == 0) {
            errorString = "You must have at least one child generator.";
            return false;
        }
        for (int i = 0; i < numGenerators; i++) {
            if (m_generators[i] == HK_NULL) {
                errorString = "At least one of the child generators is null.";
                return false;
            }
        }
        if (m_selectedGeneratorIndex < 0 || m_selectedGeneratorIndex >= numGenerators) {
            errorString = "The selected generator is out of range.";
            return false;
        }
        return true;
    }

    hkbNode* hkbManualSelectorGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbManualSelectorGenerator(*this);
    }

    hkReferencedObject* hkbManualSelectorGenerator::createInternalState()
    {
        return new hkbManualSelectorGeneratorInternalState;
    }

    void hkbManualSelectorGenerator::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbManualSelectorGeneratorInternalState&>(internalState).m_currentGeneratorIndex = m_currentGeneratorIndex;
    }

    void hkbManualSelectorGenerator::setInternalState(const hkReferencedObject& internalState)
    {
        m_currentGeneratorIndex = static_cast<const hkbManualSelectorGeneratorInternalState&>(internalState).m_currentGeneratorIndex;
    }

    void hkbManualSelectorGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                              hkReal timeOffset)
    {
        if (&output != activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
    }

    bool hkbManualSelectorGenerator::canRecycleOutput() const
    {
        return true;
    }

    void hkbManualSelectorGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const hkbNode*    child        = m_generators[m_currentGeneratorIndex];
        if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
        const int index = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
        info.m_isProxy  = true;
        info.m_proxyIndex =
            (*rootBehavior->m_activeNodes)[index].m_isProxy ? (*rootBehavior->m_activeNodes)[index].m_proxyIndex : index;
    }

    void hkbManualSelectorGenerator::setGenerator(int index, hkbGenerator* generator)
    {
        if (m_generators.getSize() <= index) {
            m_generators.setSize(index + 1, HK_NULL);
        } else if (m_generators[index]) {
            m_generators[index]->removeReference();
        }
        m_generators[index] = generator;
        generator->addReference();
    }

    hkInt8 hkbManualSelectorGenerator::computeCurrentGeneratorIndex() const
    {
        const hkInt8 lastIndex = static_cast<hkInt8>(m_generators.getSize() - 1);
        if (m_selectedGeneratorIndex < 0) return 0;
        return (m_selectedGeneratorIndex > lastIndex) ? lastIndex : m_selectedGeneratorIndex;
    }
} }
