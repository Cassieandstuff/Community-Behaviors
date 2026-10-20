/// @file
/// @brief hkbMirrorModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Mirror/hkbMirrorModifier.h>

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Modifier/Mirror/hkbMirrorModifierUtils.h>

namespace CB { namespace havok
{
    hkbMirrorModifier::hkbMirrorModifier()
    :   m_isAdditive(false)
    {
    }

    hkbMirrorModifier::hkbMirrorModifier(const hkbMirrorModifier& mod)
    :   hkbModifier(mod),
        m_isAdditive(mod.m_isAdditive)
    {
    }

    hkbNode* hkbMirrorModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet && m_variableBindingSet->m_bindings.getSize() > 0) {
            return new hkbMirrorModifier(*this);
        }
        hkbMirrorModifier* self = const_cast<hkbMirrorModifier*>(this);
        self->m_cloneState      = CLONE_STATE_SHARABLE;
        addReference();
        return self;
    }

    void hkbMirrorModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbCharacterSetup*         setup            = context.m_character->m_setup;
        const hkaMirroredSkeleton* mirroredSkeleton = setup->getMirroredSkeleton();
        if (mirroredSkeleton) {
            hkbMirrorModifierUtils::modify(*this, *mirroredSkeleton, setup->m_animationSkeleton->m_referencePose.m_data,
                                           *context.m_character->m_worldFromModel, inOut);
        }
    }

    void hkbMirrorModifier::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        info.m_syncInfo.mirror(context);
    }
} }
