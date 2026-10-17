/// @file
/// @brief hkbBindable and hkbNode, reimplemented against vanilla (AE 1.6.1170).
/// @note Vanilla compiled most of hkbNode's default virtuals from the header (inline COMDAT copies at
///       0x140ab7d00–0x140ab7e10, beside the type registration); they are defined here out of line so no other unit sees
///       a body vanilla could not see. The bytes are the same either way.
#include <Behavior/Node/hkbNode.h>

#include <Common/Base/Container/String/hkString.h>
#include <Common/Serialize/Util/hkBuiltinTypeRegistry.h>

namespace CB { namespace schema
{
    void hkbBindable::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
    }

    hkbNode::hkbNode(const hkbNode& node)
    :   hkbBindable(node),
        m_userData(0),
        m_name(node.m_name, false),
        m_id(node.m_id)
    {
        m_cloneState = CLONE_STATE_CLONE;
        const_cast<hkbNode&>(node).m_cloneState = CLONE_STATE_TEMPLATE;
    }

    void hkbNode::activate(const hkbContext& context)
    {
    }

    /// @note The default marks the node (bit 0 of m_userData) as having no update of its own; hkbTwistModifier's
    ///       constructors set the same bit.
    void hkbNode::update(const hkbContext& context, hkReal timestep)
    {
        m_userData |= 1;
    }

    void hkbNode::handleEvent(const hkbContext& context, const hkbEvent e)
    {
    }

    void hkbNode::deactivate(const hkbContext& context)
    {
    }

    int hkbNode::getMaxNumChildren(GetChildrenFlags flags)
    {
        return 2;
    }

    void hkbNode::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
    }

    bool hkbNode::isValid(hkStringPtr& errorString) const
    {
        return true;
    }

    void hkbNode::precompute(const hkbContext& context)
    {
    }

    /// @brief The reflective clone: a byte copy of the whole object (its reflected size), finished in place by the type
    ///        registry under the class's name, with its own copy of the name and one more reference on the binding set.
    /// @note The source becomes a template, the copy a clone.
    hkbNode* hkbNode::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        const hkClass* klass = hkBuiltinTypeRegistry::getInstance().getVtableClassRegistry()->getClassFromVirtualInstance(this);
        const int      size  = klass->getObjectSize();
        hkbNode*       clone = static_cast<hkbNode*>(hkMemoryRouter::getInstance().heap().blockAlloc(size));
        hkString::memCpy(clone, this, size);
        new (&clone->m_name) hkStringPtr();
        clone->m_name = m_name;
        hkBuiltinTypeRegistry::getInstance().getTypeInfoRegistry()->finishLoadedObject(clone, klass->getName());
        clone->m_memSizeAndFlags = static_cast<hkUint16>(size);
        clone->m_referenceCount  = 1;
        clone->m_cloneState      = CLONE_STATE_CLONE;
        const_cast<hkbNode*>(this)->m_cloneState = CLONE_STATE_TEMPLATE;
        if (m_variableBindingSet) {
            m_variableBindingSet->addReference();
        }
        return clone;
    }

    hkReferencedObject* hkbNode::createInternalState()
    {
        return HK_NULL;
    }

    void hkbNode::getInternalState(hkReferencedObject& internalState) const
    {
    }

    void hkbNode::getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& internalState)
    {
    }

    void hkbNode::setInternalState(const hkReferencedObject& internalState)
    {
    }

    void hkbNode::setInternalStateUser(const hkbContext& context, const hkReferencedObject& internalState,
                                       hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap)
    {
    }

    void hkbNode::getActiveEvents(hkPointerMap<int, bool>& activeEvents) const
    {
    }

    bool hkbNode::isTransitionEffect() const
    {
        return false;
    }

    void hkbNode::getActiveVariables(hkPointerMap<int, bool>& activeVariables) const
    {
    }

    class hkbNode::BoundVariablesCollector : public hkbBindableCollector
    {
    public:
        hkPointerMap<int, bool>* m_activeVariables;   ///< +0x8

        HK_FORCE_INLINE BoundVariablesCollector(hkPointerMap<int, bool>& activeVariables) : m_activeVariables(&activeVariables) {}

        virtual void collectBindable(hkbBindable* bindable)
        {
            const hkbVariableBindingSet* bindingSet = bindable->m_variableBindingSet;
            if (bindingSet) {
                const int numBindings = bindingSet->m_bindings.getSize();
                for (int i = 0; i < numBindings; i++) {
                    if (bindingSet->m_bindings[i].m_bindingType == hkbVariableBindingSet::Binding::BINDING_TYPE_VARIABLE) {
                        m_activeVariables->insert(bindingSet->m_bindings[i].m_variableIndex, true);
                    }
                }
            }
        }
    };

    void hkbNode::collectActiveVariables(hkPointerMap<int, bool>& activeVariables)
    {
        BoundVariablesCollector collector(activeVariables);
        collectBindables(collector);
        getActiveVariables(activeVariables);
    }

    bool hkbNode::vfn21()
    {
        return false;
    }

    bool hkbNode::vfn22()
    {
        return false;
    }
} }
