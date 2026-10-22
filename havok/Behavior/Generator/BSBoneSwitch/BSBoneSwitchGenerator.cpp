/// @file
/// @brief BSBoneSwitchGenerator and BSBoneSwitchGeneratorBoneData, reimplemented against vanilla (AE 1.6.1170).
/// @note One translation unit for both: vanilla's bone-data functions follow the generator's.
#include <Behavior/Generator/BSBoneSwitch/BSBoneSwitchGenerator.h>

#include <Behavior/Generator/BSBoneSwitch/BSBoneSwitchGeneratorUtils.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace havok
{
    BSBoneSwitchGenerator::BSBoneSwitchGenerator()
    :   m_pDefaultGenerator(HK_NULL)
    {
        m_userData |= 1;
    }

    BSBoneSwitchGenerator::BSBoneSwitchGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_ChildrenA(flag)
    {
        m_userData |= 1;
    }

    BSBoneSwitchGenerator::~BSBoneSwitchGenerator()
    {
        if (m_memSizeAndFlags != 0) {
            for (int i = 0; i < m_ChildrenA.getSize(); i++) {
                if (m_ChildrenA[i]) {
                    if (m_ChildrenA[i]->m_pGenerator) m_ChildrenA[i]->m_pGenerator->removeReference();
                    m_ChildrenA[i]->removeReference();
                }
            }
        }
    }

    const void* BSBoneSwitchGenerator::getClassType() const
    {
        return &staticClass();
    }

    void BSBoneSwitchGenerator::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        const int numChildren = m_ChildrenA.getSize();
        for (int i = 0; i < numChildren; i++) m_ChildrenA[i]->collectBindables(collector);
    }

    int BSBoneSwitchGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return (m_pDefaultGenerator != HK_NULL) + m_ChildrenA.getSize();
    }

    void BSBoneSwitchGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const int numChildren = getMaxNumChildren(flags);
        childrenInfo.m_childInfos.setSize(numChildren);
        int index = 0;
        if (m_pDefaultGenerator) {
            ChildInfo& info          = childrenInfo.m_childInfos[0];
            info.m_node              = m_pDefaultGenerator;
            info.m_isActive          = false;
            childrenInfo.m_anyActive = false;
            index                    = 1;
        }
        const int numSwitched = m_ChildrenA.getSize();
        for (int i = 0; i < numSwitched; i++) {
            ChildInfo& info = childrenInfo.m_childInfos[index++];
            info.m_node     = m_ChildrenA[i]->m_pGenerator;
            info.m_isActive = false;
        }
    }

    bool BSBoneSwitchGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_pDefaultGenerator) {
            errorString = "A BSBoneSwitchGenerator must have a default generator.";
        } else if (m_ChildrenA.getSize() == 0) {
            errorString = "A BSBoneSwitchGenerator must have at least one child.";
        } else {
            hkArray<bool> boneUsed;
            for (int i = 0; i < m_ChildrenA.getSize(); i++) {
                if (errorString.getLength() != 0) break;
                const BSBoneSwitchGeneratorBoneData* child = m_ChildrenA[i];
                if (!child || !child->m_pGenerator || !child->m_spBoneWeight) {
                    errorString = "One of the children is unspecified";
                } else {
                    if (boneUsed.getSize() == 0) {
                        boneUsed.setSize(child->m_spBoneWeight->m_boneWeights.getSize());
                        for (int b = 0; b < boneUsed.getSize(); b++) boneUsed[b] = false;
                    }
                    if (boneUsed.getSize() != m_ChildrenA[i]->m_spBoneWeight->m_boneWeights.getSize()) {
                        errorString = "One of the children has a different size of bone weights than the others";
                    } else {
                        for (int b = 0; b < boneUsed.getSize(); b++) {
                            if (errorString.getLength() != 0) break;
                            if (m_ChildrenA[i]->m_spBoneWeight->m_boneWeights[b] > 0.0f) {
                                if (boneUsed[b]) errorString = "Two sets of bone weights overlap";
                                else boneUsed[b] = true;
                            }
                        }
                    }
                }
            }
        }
        return errorString.getLength() == 0;
    }

    hkbNode* BSBoneSwitchGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSBoneSwitchGenerator(*this);
    }

    void BSBoneSwitchGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                         hkReal timeOffset)
    {
        hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        BSBoneSwitchGeneratorUtils::generate(this, activeChildrenOutput, output);
    }

    bool BSBoneSwitchGenerator::canRecycleOutput() const
    {
        return false;
    }

    BSBoneSwitchGenerator::BSBoneSwitchGenerator(const BSBoneSwitchGenerator& gen)
    :   hkbGenerator(gen),
        m_pDefaultGenerator(gen.m_pDefaultGenerator)
    {
        m_userData |= 1;
        const int numChildren = gen.m_ChildrenA.getSize();
        m_ChildrenA.reserveExactly(numChildren);
        for (int i = 0; i < numChildren; i++) m_ChildrenA.pushBack(new BSBoneSwitchGeneratorBoneData(*gen.m_ChildrenA[i]));
    }

    void BSBoneSwitchGeneratorBoneData::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_spBoneWeight) m_spBoneWeight->collectBindables(collector);
    }

    BSBoneSwitchGeneratorBoneData::BSBoneSwitchGeneratorBoneData(hkFinishLoadedObjectFlag flag)
    :   hkbBindable(flag),
        m_spBoneWeight(flag)
    {
    }

    /// @note A weight array bound to variables is copied (each clone binds its own); an unbound one is shared.
    BSBoneSwitchGeneratorBoneData::BSBoneSwitchGeneratorBoneData(const BSBoneSwitchGeneratorBoneData& other)
    :   hkbBindable(other),
        m_pGenerator(other.m_pGenerator)
    {
        m_pGenerator->addReference();
        if (other.m_spBoneWeight) {
            if (other.m_spBoneWeight->m_variableBindingSet == HK_NULL) m_spBoneWeight = other.m_spBoneWeight;
            else m_spBoneWeight.setAndDontIncrementRefCount(new hkbBoneWeightArray(*other.m_spBoneWeight));
        }
    }
} }
