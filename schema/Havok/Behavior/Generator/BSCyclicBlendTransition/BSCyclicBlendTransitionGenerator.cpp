/// @file
/// @brief BSCyclicBlendTransitionGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSCyclicBlendTransition/BSCyclicBlendTransitionGenerator.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    BSCyclicBlendTransitionGenerator::BSCyclicBlendTransitionGenerator()
    :   m_pBlenderGenerator(HK_NULL),
        m_fBlendParameter(0.0f),
        m_fTransitionDuration(0.2f),
        m_eBlendCurve(0),
        m_pTransitionBlenderGenerator(HK_NULL),
        m_pTransitionEffect(HK_NULL),
        m_currentMode(MODE_INACTIVE)
    {
    }

    BSCyclicBlendTransitionGenerator::BSCyclicBlendTransitionGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_EventToFreezeBlendValue(flag),
        m_EventToCrossBlend(flag),
        m_pTransitionBlenderGenerator(HK_NULL),
        m_pTransitionEffect(HK_NULL),
        m_currentMode(MODE_INACTIVE)
    {
    }

    const void* BSCyclicBlendTransitionGenerator::getClassType() const
    {
        return &staticClass();
    }

    /// @note The frozen copy is marked a template (CLONE_STATE_TEMPLATE) so the graph does not treat it as its clone.
    void BSCyclicBlendTransitionGenerator::activate(const hkbContext& context)
    {
        m_currentMode                  = MODE_NONE;
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        if (m_pBlenderGenerator && rootBehavior && m_pBlenderGenerator->getClassType() == &hkbBlenderGenerator::staticClass()) {
            hkbBlenderGenerator* frozen    = static_cast<hkbBlenderGenerator*>(m_pBlenderGenerator->cloneNode(*rootBehavior));
            m_pTransitionBlenderGenerator  = frozen;
            frozen->m_cloneState           = CLONE_STATE_TEMPLATE;
            m_pTransitionEffect            = new hkbBlendingTransitionEffect();
            m_pTransitionEffect->m_flags  |= hkbBlendingTransitionEffect::FLAG_SYNC;
            static_cast<hkbBlenderGenerator*>(rootBehavior->getOrCreateNodeClone(m_pBlenderGenerator))->m_blendParameter = m_fBlendParameter;
        }
    }

    void BSCyclicBlendTransitionGenerator::deactivate(const hkbContext& context)
    {
        m_currentMode = MODE_INACTIVE;
        if (m_pTransitionBlenderGenerator) {
            m_pTransitionBlenderGenerator->removeReference();
            m_pTransitionBlenderGenerator = HK_NULL;
        }
        if (m_pTransitionEffect) {
            m_pTransitionEffect->removeReference();
            m_pTransitionEffect = HK_NULL;
        }
    }

    void BSCyclicBlendTransitionGenerator::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("BSCyclicBlendTransitionGenerator::update");
        if (m_currentMode != MODE_FREEZE) {
            hkbBlenderGenerator* blender = getBlenderClone(context, m_pBlenderGenerator);
            if (blender) blender->m_blendParameter = m_fBlendParameter;
            bool activeNodesChanged = false;
            if (m_currentMode == MODE_CROSS_BLEND_START) {
                m_currentMode      = MODE_CROSS_BLEND;
                activeNodesChanged = true;
            } else if (m_currentMode == MODE_CROSS_BLEND && m_pTransitionEffect) {
                m_pTransitionEffect->updateInternal(context, timestep, this);
                if (m_pTransitionEffect->isDone()) {
                    m_currentMode      = MODE_NONE;
                    activeNodesChanged = true;
                }
            }
            if (activeNodesChanged && context.m_character->m_behaviorGraph) context.m_character->m_behaviorGraph->m_updateActiveNodes = true;
        }
        HK_TIMER_END();
    }

    /// @note The cross blend reverses from where it is: the time already blended, as a fraction of
    ///       m_fTransitionDuration, becomes the time left.
    void BSCyclicBlendTransitionGenerator::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        if (m_currentMode != MODE_NONE && e.m_id == m_EventToCrossBlend.m_id) {
            hkbBlenderGenerator* blender        = getBlenderClone(context, m_pBlenderGenerator);
            const hkReal         blendParameter = blender ? blender->m_blendParameter : m_fBlendParameter;
            hkbBlenderGenerator* frozen         = getBlenderClone(context, m_pTransitionBlenderGenerator);
            if (frozen) frozen->m_blendParameter = blendParameter;
            hkbBlendingTransitionEffect* effect = m_pTransitionEffect;
            if (effect && m_fTransitionDuration > HK_REAL_EPSILON) {
                if (m_currentMode != MODE_CROSS_BLEND) {
                    m_currentMode      = MODE_CROSS_BLEND_START;
                    effect->m_duration = m_fTransitionDuration;
                    m_pTransitionEffect->setToGenerator(m_pBlenderGenerator);
                    m_pTransitionEffect->setFromGenerator(m_pTransitionBlenderGenerator);
                    hkbBlendingTransitionEffect* started = m_pTransitionEffect;
                    started->m_timeRemaining             = started->m_duration;
                    started->m_timeInTransition          = 0.0f;
                } else {
                    const hkReal timeInTransition = (1.0f - effect->m_timeInTransition / m_fTransitionDuration) * effect->m_duration;
                    effect->m_timeInTransition    = timeInTransition;
                    effect->m_timeRemaining       = effect->m_duration - timeInTransition;
                }
                context.m_flags |= hkbContext::FLAG_ACTIVE_NODES_CHANGED;
            }
        } else if (m_currentMode == MODE_NONE && e.m_id == m_EventToFreezeBlendValue.m_id) {
            m_currentMode = MODE_FREEZE;
            context.m_flags |= hkbContext::FLAG_ACTIVE_NODES_CHANGED;
            bool                   bound    = false;
            hkbBlenderGenerator*   blender  = getBlenderClone(context, m_pBlenderGenerator);
            hkbVariableBindingSet* bindings = blender ? blender->m_variableBindingSet.val() : HK_NULL;
            if (bindings) {
                for (int i = 0; !bound; i++) {
                    if (i >= bindings->m_bindings.getSize()) return;
                    const char* memberPath = bindings->m_bindings[i].m_memberPath.cString();
                    bound                  = (memberPath ? hkString::strCmp(memberPath, "blendParameter") : -1) == 0;
                }
                hkStringBuf buf;
                buf.printf("The blender generator, %s, pointed to by %s still has it's blend parameter still pointing to a variable.  The cyclic "
                           "blend transition generator cannot function properly and will cause visual artifacts.",
                           m_pBlenderGenerator ? m_pBlenderGenerator->m_name.cString() : "unknown", m_name.cString());
                hkError::getInstance().message(hkError::MESSAGE_WARNING, 0, buf.cString(), "Generator\\BSCyclicBlendTransition\\BSCyclicBlendTransitionGenerator.cpp",
                                               0xc3);
            }
        }
    }

    hkbNode* BSCyclicBlendTransitionGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSCyclicBlendTransitionGenerator(*this);
    }

    int BSCyclicBlendTransitionGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return computeNumChildren();
    }

    void BSCyclicBlendTransitionGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const int numChildren = computeNumChildren();
        childrenInfo.m_childInfos.setSize(numChildren);
        const bool active = (flags & FLAG_ACTIVE_ONLY) != 0;
        if (m_pBlenderGenerator) {
            childrenInfo.m_childInfos[0].m_node     = m_pBlenderGenerator;
            childrenInfo.m_childInfos[0].m_isActive = active;
        }
        if (m_currentMode == MODE_CROSS_BLEND && m_pTransitionBlenderGenerator) {
            childrenInfo.m_childInfos[1].m_node     = m_pTransitionBlenderGenerator;
            childrenInfo.m_childInfos[1].m_isActive = active;
        }
        childrenInfo.m_anyActive = active;
    }

    bool BSCyclicBlendTransitionGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_pBlenderGenerator) {
            errorString = "Blender Generator not set.";
            return false;
        }
        if (m_pBlenderGenerator->getClassType() != &hkbBlenderGenerator::staticClass()) {
            errorString = "Attached a non blender generator.";
            return false;
        }
        if (m_fTransitionDuration <= 0.0f) {
            errorString = "Please set a positive number for the duration of the blend.";
            return false;
        }
        if (m_EventToCrossBlend.m_id == -1 && m_EventToFreezeBlendValue.m_id == -1) {
            errorString = "Please specify a cross blend or freeze event ID.";
            return false;
        }
        if (!(static_cast<hkbBlenderGenerator*>(m_pBlenderGenerator)->m_flags & hkbBlenderGenerator::FLAG_PARAMETRIC_BLEND)) {
            errorString = "Attached blender generator is not a cyclic parametric blend.";
            return false;
        }
        if (m_fTransitionDuration <= HK_REAL_EPSILON) {
            errorString = "This modifier requires at least some blend duration.";
            return false;
        }
        return true;
    }

    void BSCyclicBlendTransitionGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                                                    hkbGeneratorOutput& output, hkReal timeOffset)
    {
        HK_TIMER_BEGIN("BSCyclicBlendTransitionGenerator::generate");
        if (m_currentMode != MODE_CROSS_BLEND) {
            if (&output != activeChildrenOutput[0] && activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        } else if (m_pTransitionEffect) {
            m_pTransitionEffect->generate(context, activeChildrenOutput, output, timeOffset);
        }
        HK_TIMER_END();
    }

    bool BSCyclicBlendTransitionGenerator::canRecycleOutput() const
    {
        return m_currentMode != MODE_CROSS_BLEND;
    }

    void BSCyclicBlendTransitionGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        if (m_currentMode == MODE_CROSS_BLEND && m_pTransitionEffect) {
            m_pTransitionEffect->updateSyncInternal(context, this);
            return;
        }
        const hkbNode* child = m_pBlenderGenerator;
        if (child) {
            hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
            const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
            const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
            if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];
            hkString::memCpy(&info.m_syncInfo, &childInfo->m_syncInfo, sizeof(hkbGeneratorSyncInfo));
            info.m_syncInfo.m_isMirrored = false;
        } else {
            hkbGenerator::updateSync(context, info);
        }
    }

    hkbBlenderGenerator* BSCyclicBlendTransitionGenerator::getBlenderClone(const hkbContext& context, hkbGenerator* generator)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        if (rootBehavior && generator && generator->getClassType() == &hkbBlenderGenerator::staticClass())
            return static_cast<hkbBlenderGenerator*>(rootBehavior->getNodeClone(generator));
        return HK_NULL;
    }

    int BSCyclicBlendTransitionGenerator::computeNumChildren()
    {
        int numChildren = (m_pBlenderGenerator != HK_NULL);
        if (m_currentMode == MODE_CROSS_BLEND && m_pTransitionBlenderGenerator) numChildren++;
        return numChildren;
    }

    BSCyclicBlendTransitionGenerator::BSCyclicBlendTransitionGenerator(const BSCyclicBlendTransitionGenerator& gen)
    :   hkbGenerator(gen),
        m_pBlenderGenerator(gen.m_pBlenderGenerator),
        m_EventToFreezeBlendValue(gen.m_EventToFreezeBlendValue),
        m_EventToCrossBlend(gen.m_EventToCrossBlend),
        m_fBlendParameter(gen.m_fBlendParameter),
        m_fTransitionDuration(gen.m_fTransitionDuration),
        m_eBlendCurve(gen.m_eBlendCurve),
        m_pTransitionBlenderGenerator(HK_NULL),
        m_pTransitionEffect(HK_NULL),
        m_currentMode(MODE_INACTIVE)
    {
    }
} }
