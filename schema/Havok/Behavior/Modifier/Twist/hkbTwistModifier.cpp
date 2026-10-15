/// @file
/// @brief hkbTwistModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Twist/hkbTwistModifier.h>

namespace CB { namespace schema
{
    /// @note Both constructors set bit 0 of m_userData; what it flags is unknown. VARIANT only by the schedule of the
    ///       m_cloneState store (vanilla after m_id; no spelling of hkbNode's constructor moves it).
    hkbTwistModifier::hkbTwistModifier()
    :   m_axisOfRotation(1.0f, 0.0f, 0.0f, 0.0f),
        m_twistAngle(0.0f),
        m_startBoneIndex(-1),
        m_endBoneIndex(-1),
        m_setAngleMethod(LINEAR),
        m_rotationAxisCoordinates(ROTATION_AXIS_IN_MODEL_COORDINATES),
        m_isAdditive(true)
    {
        m_userData |= 1;
    }

    /// @note The settings (axis through m_isAdditive) copy as one 27-byte block.
    hkbTwistModifier::hkbTwistModifier(const hkbTwistModifier& mod)
    :   hkbModifier(mod)
    {
        hkString::memCpy(&m_axisOfRotation, &mod.m_axisOfRotation, int(reinterpret_cast<const char*>(&mod.m_isAdditive + 1) - reinterpret_cast<const char*>(&mod.m_axisOfRotation)));
        m_userData |= 1;
    }

    /// @note The chain is rebuilt on each activation; the ancestors are appended (deactivate empties both lists).
    void hkbTwistModifier::activate(const hkbContext& context)
    {
        if (m_startBoneIndex >= 0 && m_endBoneIndex >= 0) {
            const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
            if (hkaSkeletonUtils::getBoneChain(*skeleton, m_startBoneIndex, m_endBoneIndex, m_boneChainIndices)) {
                int parent = skeleton->m_parentIndices[m_startBoneIndex];
                while (parent > -1) {
                    m_parentBoneIndices.pushBack(hkInt16(parent));
                    parent = skeleton->m_parentIndices[parent];
                }
            }
        }
    }

    void hkbTwistModifier::deactivate(const hkbContext& context)
    {
        m_boneChainIndices.clear();
        m_parentBoneIndices.clear();
    }

    /// @note A bone index may be unset (-1) when a variable binds it.
    bool hkbTwistModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool startValid = m_startBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("startBoneIndex"));
        const bool endValid   = m_endBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("endBoneIndex"));

        if (startValid && endValid) {
            if (m_axisOfRotation.equals3(g_vectorConstantZero, 0.001f)) {
                errorString = "The axis of rotation cannot be zero.";
                return false;
            }
            return true;
        }
        errorString = "You must specify the start and end bone indices.";
        return false;
    }

    hkbNode* hkbTwistModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbTwistModifier(*this);
    }

    void hkbTwistModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("hkbTwistModifier");

        hkbCharacter* character = context.m_character;
        if (character && character->m_setup && character->m_setup->m_animationSkeleton) {
            hkbTwistModifierUtils::modifyInternal(*this, inOut, character->m_setup->m_animationSkeleton->m_referencePose.m_data);
        }

        HK_TIMER_END();
    }
} }
