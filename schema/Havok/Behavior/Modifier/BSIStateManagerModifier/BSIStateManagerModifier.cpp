/// @file
/// @brief BSIStateManagerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSIStateManagerModifier/BSIStateManagerModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/StateMachine/hkbStateMachine.h>

namespace CB { namespace schema
{
    /// @note Every constructor sets bits 0 and 1 of m_userData; what they flag is unknown.
    BSIStateManagerModifier::BSIStateManagerModifier()
    :   m_iStateVar(0)
    {
        m_userData |= 3;
        m_myStateListener.m_pStateManager = this;
    }

    BSIStateManagerModifier::BSIStateManagerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_stateData(flag),
        m_myStateListener(HK_NULL)
    {
        m_userData |= 3;
        m_myStateListener.m_pStateManager = this;
    }

    BSIStateManagerModifier::BSIStateManagerModifier(const BSIStateManagerModifier& mod)
    :   hkbModifier(mod),
        m_iStateVar(mod.m_iStateVar),
        m_myStateListener(HK_NULL)
    {
        m_myStateListener.m_pStateManager = this;
        m_stateData                       = mod.m_stateData;
        m_userData |= 3;
    }

    BSIStateManagerModifier::~BSIStateManagerModifier()
    {
    }

    const void* BSIStateManagerModifier::getClassType() const
    {
        return &staticClass();
    }

    /// @note `valid` is tested before the size (vanilla tests the size at the loop top, `valid` at the bottom). VARIANT:
    ///       vanilla addresses the hkStringBuf through an rbp frame and stores its capacity before its size.
    bool BSIStateManagerModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_stateData.getSize() < 2) {
            errorString = "Not enough states set up in manager (should have at least 2 to make this worthwhile).";
            return false;
        }

        bool valid = true;
        for (int i = 0; valid && i < m_stateData.getSize(); i++) {
            valid = false;
            const char*            error;
            const hkbStateMachine* sm = m_stateData[i].m_pStateMachine;
            if (!sm) {
                error = "State machine at array index %d is not set";
            } else if (sm->getClassType() != &hkbStateMachine::staticClass()) {
                error = "Generator set at array index %d is not actually a state machine";
            } else if (m_stateData[i].m_pStateMachine->getStateIndex(m_stateData[i].m_StateID) == -1) {
                error = "The State machine ID inside array index %d is not valid.";
            } else {
                valid = true;
                continue;
            }

            hkStringBuf buf;
            buf.printf(error, i);
            errorString = buf.cString();
        }
        return valid;
    }

    hkbNode* BSIStateManagerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSIStateManagerModifier(*this);
    }

    /// @note The manager matches entries against `sm`'s template; it clears bit 0 of m_userData first.
    void BSIStateManagerModifier::onStateEntered(const hkbContext& context, hkbStateMachine* sm)
    {
        hkbBehaviorGraph* behavior       = context.m_character->m_behaviorGraph;
        const int         numTransitions = sm->m_activeTransitions.getSize();
        const int         stateId        = (numTransitions > 0) ? sm->m_activeTransitions[numTransitions - 1].m_toStateId : sm->m_currentStateId;
        m_userData &= ~hkUlong(1);

        bool found = false;
        for (int i = 0; !found && behavior && i < m_stateData.getSize(); i++) {
            const hkbNode*      nodeTemplate = behavior->getNodeTemplate(sm);
            const BSiStateData& data         = m_stateData[i];
            if (nodeTemplate == data.m_pStateMachine && stateId == data.m_StateID) {
                m_iStateVar = data.m_iStateToSetAs;
                found       = true;
            }
        }
    }

    /// @note Only a clone made by the graph (m_cloneState CLONE_STATE_CLONE) is updated.
    void BSIStateManagerModifier::BSIStateManagerStateListener::onEnter(const hkbContext& context, hkbStateMachine* sm)
    {
        hkbBehaviorGraph* behavior = context.m_character->m_behaviorGraph;
        if (m_pStateManager && behavior) {
            BSIStateManagerModifier* manager = static_cast<BSIStateManagerModifier*>(behavior->getNodeClone(m_pStateManager));
            if (manager && manager->m_cloneState == CLONE_STATE_CLONE) manager->onStateEntered(context, sm);
        }
    }

    hkbStateListener* BSIStateManagerModifier::BSIStateManagerStateListener::clone()
    {
        addReference();
        return this;
    }
} }
