/// @file
/// @brief BSOffsetAnimationGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGenerator.h>

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGeneratorUtils.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    BSOffsetAnimationGenerator::BSOffsetAnimationGenerator()
    :   m_pDefaultGenerator(HK_NULL),
        m_pOffsetClipGenerator(HK_NULL),
        m_fOffsetVariable(0.0f),
        m_fOffsetRangeStart(0.0f),
        m_fOffsetRangeEnd(1.0f),
        m_fCurrentPercentage(0.0f),
        m_iCurrentFrame(hkUint32(-1)),
        m_bZeroOffset(true),
        m_bOffsetValid(false)
    {
        m_userData = 0;
    }

    BSOffsetAnimationGenerator::BSOffsetAnimationGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_fCurrentPercentage(0.0f),
        m_iCurrentFrame(hkUint32(-1)),
        m_bZeroOffset(true),
        m_bOffsetValid(false)
    {
        m_userData = 0;
    }

    const void* BSOffsetAnimationGenerator::getClassType() const
    {
        return &staticClass();
    }

    void BSOffsetAnimationGenerator::activate(const hkbContext& context)
    {
        hkbClipGenerator* clip  = getOffsetClipGenerator();
        const hkUint16    index = clip ? clip->m_animationBindingIndex : hkUint16(-1);
        if (g_animationClipLoader && !g_animationClipLoader->loadAnimation(context, index)) {
            m_userData = 8;
            return;
        }
        sampleOffset(context);
        m_userData = 0xc;
    }

    void BSOffsetAnimationGenerator::deactivate(const hkbContext& context)
    {
        if (m_userData == 0xc) {
            hkbClipGenerator* clip  = getOffsetClipGenerator();
            const hkUint16    index = clip ? clip->m_animationBindingIndex : hkUint16(-1);
            if (g_animationClipLoader) g_animationClipLoader->unloadAnimation(context, index);
        }
        m_BoneIndexA.clear();
        m_BoneOffsetA.clear();
        m_bOffsetValid = false;
    }

    hkbNode* BSOffsetAnimationGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSOffsetAnimationGenerator(*this);
    }

    int BSOffsetAnimationGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return computeNumChildren(flags);
    }

    void BSOffsetAnimationGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const int numChildren = computeNumChildren(flags);
        childrenInfo.m_childInfos.setSize(numChildren);
        childrenInfo.m_childInfos[0].m_node     = m_pDefaultGenerator;
        childrenInfo.m_childInfos[0].m_isActive = true;
        childrenInfo.m_anyActive                = true;
        if (numChildren > 1) {
            childrenInfo.m_childInfos[1].m_node     = m_pOffsetClipGenerator;
            childrenInfo.m_childInfos[1].m_isActive = false;
        }
    }

    bool BSOffsetAnimationGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_pDefaultGenerator) {
            errorString = "Default Generator not set.";
        } else if (!m_pOffsetClipGenerator) {
            errorString = "BSOffsetAnimationGenerator requires an offset to function.";
        } else if (m_pOffsetClipGenerator->getClassType() != &hkbClipGenerator::staticClass()) {
            errorString = "Offset clip generator not of clip generator type.";
        } else if (m_fOffsetRangeEnd <= m_fOffsetRangeStart) {
            errorString = "The range is not setup so that the end is greater than the start.";
        }
        return errorString.getLength() == 0;
    }

    void BSOffsetAnimationGenerator::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("BSOffsetAnimationGenerator::update");
        bool loaded = true;
        if (m_userData == 8) {
            hkbClipGenerator* clip  = getOffsetClipGenerator();
            const hkUint16    index = clip ? clip->m_animationBindingIndex : hkUint16(-1);
            if (g_animationClipLoader) loaded = g_animationClipLoader->loadAnimation(context, index);
            if (loaded) m_userData = 0xc;
        }
        if (loaded) sampleOffset(context);
        HK_TIMER_END();
    }

    void BSOffsetAnimationGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                              hkReal timeOffset)
    {
        HK_TIMER_BEGIN("BSOffsetAnimationGenerator::generate");
        if (&output != activeChildrenOutput[0] && activeChildrenOutput[0])
            hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        BSOffsetAnimationGeneratorUtils::applyOffsets(this, m_BoneOffsetA, m_BoneIndexA, activeChildrenOutput, output);
        HK_TIMER_END();
    }

    bool BSOffsetAnimationGenerator::canRecycleOutput() const
    {
        return true;
    }

    void BSOffsetAnimationGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        const hkbNode* child = m_pDefaultGenerator;
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

    /// @note The percentage is m_fOffsetVariable over the range's width (the start is not subtracted). The frame index
    ///       is truncated through a 64-bit conversion and clamped (unsigned) to the second-to-last frame.
    void BSOffsetAnimationGenerator::sampleOffset(const hkbContext& context)
    {
        const hkReal range      = m_fOffsetRangeEnd - m_fOffsetRangeStart;
        hkReal percentage = (range > HK_REAL_EPSILON) ? m_fOffsetVariable / range : 0.0f;
        percentage        = (percentage < 0.0f) ? 0.0f : hkMath::min2(percentage, 1.0f);
        if (m_bOffsetValid && hkMath::fabs(percentage - m_fCurrentPercentage) <= HK_REAL_EPSILON) return;
        m_fCurrentPercentage = percentage;

        hkbClipGenerator*                clip       = getOffsetClipGenerator();
        const hkUint16                   index      = clip ? clip->m_animationBindingIndex : hkUint16(-1);
        hkbCharacter*                    character  = context.m_character;
        hkbAnimationBindingSet*          bindingSet = character ? character->getAnimationBindingSet() : HK_NULL;
        hkbAnimationBindingWithTriggers* bwt        = (bindingSet && index < bindingSet->m_bindings.getSize()) ? bindingSet->m_bindings[index] : HK_NULL;
        const hkaAnimationBinding*       binding    = bwt ? bwt->m_binding : HK_NULL;
        hkaAnimation*                    animation  = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();

        if (!animation || binding->m_blendHint != hkaAnimationBinding::ADDITIVE) {
            m_bZeroOffset = true;
            if (animation) {
                hkStringBuf buf;
                buf.printf("The BSOffsetAnimationGenerator, %s, pointed to a non-additive animation %s.", m_name.cString(), clip->m_name.cString());
                hkError::getInstance().message(hkError::MESSAGE_WARNING, 0, buf.cString(), "Generator\\BSOffsetAnimation\\BSOffsetAnimationGenerator.cpp", 0x122);
            }
        } else {
            const hkReal frameDuration = animation->m_duration * (1.0f / hkReal(animation->getNumOriginalFrames() - 1));
            const hkReal time          = (animation->m_duration - frameDuration) * percentage + frameDuration;
            const hkUint32 numFrames   = animation->getNumOriginalFrames();
            hkUint32       frame       = hkUint32(__int64((time / animation->m_duration) * hkReal(numFrames - 1)));
            if (frame > numFrames - 2) frame = numFrames - 2;
            if (frame != m_iCurrentFrame) {
                m_iCurrentFrame = frame;
                if (!m_bOffsetValid) {
                    const int numTracks = animation->m_numberOfTransformTracks;
                    if (numTracks > 0) {
                        m_BoneIndexA.setSize(numTracks, -1);
                        m_BoneOffsetA.setSize(numTracks);
                    }
                    m_bOffsetValid = true;
                }
                m_bZeroOffset = !sampleBoneOffsets(binding, animation, time);
            }
        }
    }

    hkBool BSOffsetAnimationGenerator::sampleBoneOffsets(const hkaAnimationBinding* binding, const hkaAnimation* animation, hkReal time)
    {
        animation->samplePartialTracks(time, m_BoneOffsetA.getSize(), m_BoneOffsetA.begin(), 0, HK_NULL, HK_NULL);
        const int numMapped = binding->m_transformTrackToBoneIndices.getSize();
        hkBool    anyOffset = false;
        for (int i = 0; i < m_BoneIndexA.getSize(); i++) {
            const hkQsTransform identity(hkQsTransform::IDENTITY);
            if (m_BoneOffsetA[i].isApproximatelyEqual(identity, 1e-3f)) {
                m_BoneIndexA[i] = -1;
            } else {
                anyOffset        = true;
                m_BoneIndexA[i] = (numMapped == 0) ? hkInt16(i) : binding->m_transformTrackToBoneIndices[i];
            }
        }
        return anyOffset;
    }

    BSOffsetAnimationGenerator::BSOffsetAnimationGenerator(const BSOffsetAnimationGenerator& gen)
    :   hkbGenerator(gen),
        m_pDefaultGenerator(gen.m_pDefaultGenerator),
        m_pOffsetClipGenerator(gen.m_pOffsetClipGenerator),
        m_fOffsetVariable(gen.m_fOffsetVariable),
        m_fOffsetRangeStart(gen.m_fOffsetRangeStart),
        m_fOffsetRangeEnd(gen.m_fOffsetRangeEnd),
        m_fCurrentPercentage(0.0f),
        m_iCurrentFrame(hkUint32(-1)),
        m_bZeroOffset(true),
        m_bOffsetValid(false)
    {
        m_userData = 0;
    }

    /// @note The offset clip is left out for a clone (its template keeps it) and when only active children are asked
    ///       for (it is never active).
    int BSOffsetAnimationGenerator::computeNumChildren(GetChildrenFlags flags)
    {
        if (m_cloneState == CLONE_STATE_CLONE || (flags & FLAG_ACTIVE_ONLY)) return 1;
        return 2;
    }
} }
