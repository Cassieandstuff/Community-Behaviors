/// @file
/// @brief hkbBindable and hkbNode: the base of every behavior graph node.
#pragma once

#include <Behavior/Event/hkbEvent.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    struct hkbContext;
    class hkbBehaviorGraph;
    struct hkbBindable;

    /// @brief Receives the bindables hkbBindable::collectBindables walks.
    /// @note Slot 0 is the (pure) collect callback, slot 1 the destructor (0x140ac1900), as vanilla's vtable 0x141941bc8 has them.
    class hkbBindableCollector
    {
    public:
        virtual void collectBindable(hkbBindable* bindable) = 0;   ///< slot 0
        virtual ~hkbBindableCollector() {}                        ///< slot 1

        /// @brief The class allocator: the deleting destructors free the static size (8 here, 0x10 for hkbNode's
        ///        local collector), with no recorded size.
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }
    };

    struct hkbNodeInternalStateInfo;

    struct hkbVariableBindingSet : hkReferencedObject
    {
        /// @note 0x28 bytes, laid out per the reflected hkbVariableBindingSetBinding.
        struct Binding;

        hkArray<Binding> m_bindings;                 ///< +0x10
        hkInt32          m_indexOfBindingToEnable;   ///< +0x20
        hkBool           m_hasOutputBinding;         ///< +0x24

        /// @brief The binding whose member path is `memberPath` (exact compare), else null.
        const Binding* findBindingByMemberPath(const char* memberPath) const;

        /// @brief Removes binding `index` (the last one takes its place), keeping m_indexOfBindingToEnable on its binding.
        HK_FORCE_INLINE void removeBinding(int index);

        /// @brief Removes every binding, last first, unless the set was placed by a packfile load (no allocated size).
        /// @note Out of line in vanilla (0x140ae4f40, Behavior/Variables/hkbVariableBindingSet.cpp).
        ~hkbVariableBindingSet();
    };

    /// @note BSCyclicBlendTransitionGenerator::handleEvent walks the bindings comparing the member path, 0x28 apart.
    struct hkbVariableBindingSet::Binding
    {
        /// @note Havok's BindingType: a graph variable (0) or a character property (1).
        enum BindingType { BINDING_TYPE_VARIABLE = 0, BINDING_TYPE_CHARACTER_PROPERTY = 1 };

        hkStringPtr    m_memberPath;              ///< +0x00
        const hkClass* m_memberClass;             ///< +0x08 (not serialized)
        hkInt32        m_offsetInObjectPlusOne;   ///< +0x10
        hkInt32        m_offsetInArrayPlusOne;    ///< +0x14
        hkInt32        m_rootVariableIndex;       ///< +0x18
        hkInt32        m_variableIndex;           ///< +0x1c
        hkInt8         m_bitIndex;                ///< +0x20
        hkInt8         m_bindingType;             ///< +0x21
        hkUint8        m_memberType;              ///< +0x22
        hkInt8         m_variableType;            ///< +0x23
        hkInt8         m_flags;                   ///< +0x24
    };

    HK_FORCE_INLINE void hkbVariableBindingSet::removeBinding(int index)
    {
        m_bindings.removeAt(index);
        if (index == m_indexOfBindingToEnable) {
            m_indexOfBindingToEnable = -1;
        } else if (m_indexOfBindingToEnable == m_bindings.getSize()) {
            m_indexOfBindingToEnable = index;
        }
    }

    /// @note The destructor is implicit: under /Ob1 VS2015 inlines it into some node destructors (hkbSequence's) and
    ///       calls the out-of-line copy from others (the clip's, the timer's) — the same split vanilla shows. A
    ///       user-provided one adds a vtable store vanilla's lacks.
    struct hkbBindable : hkReferencedObject
    {
        /// @brief An entry of m_cachedBindables.
        /// @note 0x10 bytes (the destructor frees capacity << 4); contents not identified. Name provisional.
        struct CachedBindable
        {
            hkUint8 m_opaque[0x10];
        };

        hkRefPtr<hkbVariableBindingSet> m_variableBindingSet;   ///< +0x10
        hkArray<CachedBindable>         m_cachedBindables;      ///< +0x18
        hkBool                          m_areBindablesCached;   ///< +0x28

        HK_FORCE_INLINE hkbBindable() : m_areBindablesCached(false) {}
        HK_FORCE_INLINE hkbBindable(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag), m_variableBindingSet(flag), m_areBindablesCached(false) {}
        /// @brief Shares the binding set (one more reference); the bindable cache starts empty.
        HK_FORCE_INLINE hkbBindable(const hkbBindable& bindable)
        :   hkReferencedObject(bindable), m_variableBindingSet(bindable.m_variableBindingSet), m_areBindablesCached(false) {}

        /// @brief Hands `collector` this object and every bindable it owns.
        virtual void collectBindables(hkbBindableCollector& collector);   ///< slot 3

        /// @brief Copies the bound graph variables into this object's members (`fromMembers` false) or the members back
        ///        into the variables (true).
        /// @return Nonzero when the object has bindings (the modifier update copies back only then).
        /// @note Name and return meaning provisional; out of line in vanilla (0x140ac1660).
        int copyVariablesToMembers(const hkbContext& context, bool fromMembers);
    };

    /// @note A `class`, as in Havok: the class-key is part of the mangled name of the constructors that take one.
    /// @note The virtuals are declared in vanilla's vtable order (AE 1.6.1170) so derived classes override the right
    ///       slots and calls through a base pointer index the right one. vfnN are slots whose name and signature are not
    ///       identified yet; nothing calls or overrides them so far.
    class hkbNode : public hkbBindable
    {
    public:
        /// @note Values per the Havok SDK, unverified here. The clip's owner releases m_pClipGenerator only when it is in
        ///       state 2, which fits CLONE_STATE_CLONE (a clone the owner made).
        enum CloneState { CLONE_STATE_DEFAULT = 0, CLONE_STATE_TEMPLATE = 1, CLONE_STATE_CLONE = 2, CLONE_STATE_SHARABLE = 3 };
        /// @note Bit 1 = active children only: the blender then reports its active count and skips weightless children.
        ///       Bit 2 = generators only: every modifier wrapper (hkbModifierList, hkbModifierWrapper and its subclasses,
        ///       hkbModifierGenerator's modifier) leaves its modifiers out under it — so this build's value is 2, not the
        ///       Havok SDK's 4. FLAG_IGNORE_REFERENCED_BEHAVIORS is the SDK's, unverified here.
        enum GetChildrenFlagBits { FLAG_ACTIVE_ONLY = 1, FLAG_GENERATORS_ONLY = 2, FLAG_IGNORE_REFERENCED_BEHAVIORS = 8 };
        typedef int GetChildrenFlags;

        /// @brief One child getChildren reports.
        /// @note 0x10 bytes. Declared POD to hkArray (hkIsPodType, below): setSize must not run a construct loop over it.
        ///       The flags the transition effects copy between ChildrenInfo and ChildInfo are hkBool (vanilla copies the
        ///       raw byte); m_isActive stays `bool` as its other writers were matched with it.
        struct ChildInfo
        {
            hkbNode* m_node;
            /// @note Name provisional: the transition effects set it on the child their event mode
            ///       (EVENT_MODE_IGNORE_FROM/TO_GENERATOR) keeps events from.
            hkBool   m_ignoreEvents;   ///< +0x8
            /// @note Name provisional: the transition effects set it on an already-active to-generator under
            ///       SELF_TRANSITION_MODE_RESET.
            hkBool   m_reactivate;     ///< +0x9
            /// @note Name provisional: set for a child reported under FLAG_ACTIVE_ONLY (the blender, the behavior
            ///       reference generator); hkbBlendingTransitionEffect sets it on both children under its FLAG_SYNC.
            bool     m_isActive;   ///< +0xa
            /// @note Name provisional: hkbGeneratorTransitionEffect sets it on the to-generator it reports while the
            ///       transition generator still plays (stages 0 and 1), when its generate reads no output of it.
            hkBool   m_skipGenerate;   ///< +0xb
            hkUint8  m_padC[4];
        };
        /// @brief Where getChildren reports to.
        struct ChildrenInfo
        {
            hkArray<ChildInfo>& m_childInfos;
            /// @note Name provisional: raised with a child's m_ignoreEvents (the transition effects).
            hkBool              m_anyIgnoreEvents;   ///< +0x8
            /// @note Name provisional: raised with a child's m_reactivate (the transition effects).
            hkBool              m_anyReactivate;     ///< +0x9
            /// @note Name provisional: set alongside a reported child's m_isActive (the behavior reference generator).
            bool                m_anyActive;   ///< +0xa
            /// @note Name provisional: raised with a child's m_skipGenerate (hkbGeneratorTransitionEffect).
            hkBool              m_anySkipGenerate;   ///< +0xb

            HK_FORCE_INLINE ChildrenInfo(hkArray<ChildInfo>& childInfos)
            :   m_childInfos(childInfos), m_anyIgnoreEvents(false), m_anyReactivate(false), m_anyActive(false), m_anySkipGenerate(false) {}
        };

        hkUlong     m_userData;     ///< +0x30 — Bethesda keys clip loading off it (the clip tests 0 and 8)
        hkStringPtr m_name;         ///< +0x38
        hkInt16     m_id;           ///< +0x40
        hkInt8      m_cloneState;   ///< +0x42
        hkBool      m_padNode[1];   ///< +0x43

        /// @note Inlined into the node classes' default constructors, where its vtable store survives (the out-of-line
        ///       hkStringPtr constructor could read it); the earlier bases' stores die.
        HK_FORCE_INLINE hkbNode() : m_userData(0), m_id(-1) { m_cloneState = CLONE_STATE_DEFAULT; }
        HK_FORCE_INLINE hkbNode(hkFinishLoadedObjectFlag flag) : hkbBindable(flag), m_name(flag) { m_id = -1; }
        /// @brief The clone copy (cloneNode); marks the SOURCE node as cloned.
        hkbNode(const hkbNode& node);

        /// @brief Gathers the variable indices of the variable bindings collectBindables reaches (vanilla RTTI:
        ///        hkbNode::BoundVariablesCollector).
        class BoundVariablesCollector;

        /// @brief Adds the variables bound to the node or its bindables, then the ones getActiveVariables reports, into
        ///        `activeVariables` (index → true).
        /// @note Name provisional (0x140ac1b20); the Havok SDK's own split is likely a non-virtual getActiveVariables over
        ///       a virtual getActiveVariablesSpecial (slot 20 here), unverified — slot 20 keeps its current name.
        void collectActiveVariables(hkPointerMap<int, bool>& activeVariables);

        virtual void activate(const hkbContext& context);                      ///< slot 4
        virtual void update(const hkbContext& context, hkReal timestep);       ///< slot 5
        /// @note By value (on x64, a pointer to the caller's copy): hkbSequence's override reads the id through it.
        virtual void handleEvent(const hkbContext& context, const hkbEvent e); ///< slot 6
        virtual void deactivate(const hkbContext& context);                    ///< slot 7
        /// @note Name provisional (the overrides return their child count). hkbNode's own returns 2 — unexplained.
        virtual int  getMaxNumChildren(GetChildrenFlags flags);                ///< slot 8
        virtual void getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool isValid(hkStringPtr& errorString) const;                  ///< slot 10
        /// @note Havok's name: hkbEvaluateExpressionModifier's override compiles its expressions once against the
        ///       context's behavior (the SDK's precompute); hkbNode's is a no-op.
        virtual void precompute(const hkbContext& context);                    ///< slot 11
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;      ///< slot 12
        virtual hkReferencedObject* createInternalState();                     ///< slot 13
        virtual void getInternalState(hkReferencedObject& internalState) const;   ///< slot 14
        /// @brief Fills `internalState` with the state the node keeps outside itself (in its shared data).
        /// @note Name and signature after the Havok SDK's getInternalStateUser (unverified beyond the state in r8).
        virtual void getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& internalState);   ///< slot 15
        virtual void setInternalState(const hkReferencedObject& internalState);   ///< slot 16
        /// @brief Restores what getInternalStateUser saved.
        /// @note Name and signature after the Havok SDK's setInternalStateUser (unverified beyond the state in r8).
        virtual void setInternalStateUser(const hkbContext& context, const hkReferencedObject& internalState,
                                          hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap);   ///< slot 17
        /// @brief Adds the event ids the node reacts to into `activeEvents` (id → true).
        /// @note Name after the Havok SDK's hkbNode::getActiveEvents; hkbEventDrivenModifier's adds its two event ids.
        virtual void getActiveEvents(hkPointerMap<int, bool>& activeEvents) const;   ///< slot 18
        /// @note Name provisional: hkbNode's returns false, hkbTransitionEffect's true.
        virtual bool isTransitionEffect() const;                               ///< slot 19
        /// @brief Adds the variable indices the node reads or writes into `activeVariables` (index → true).
        /// @note Name provisional (the Havok SDK's getActiveVariables): hkbEvaluateExpressionModifier's adds its assignment
        ///       variables and the ones its compiled expressions read.
        virtual void getActiveVariables(hkPointerMap<int, bool>& activeVariables) const;   ///< slot 20
        /// @note hkbNode's returns false, hkbGenerator's true.
        virtual bool vfn21();                                                  ///< slot 21
        /// @note hkbNode's returns false (0x140ab7da0), hkbBehaviorGraph's true (0x140ab7d90); meaning unidentified.
        virtual bool vfn22();                                                  ///< slot 22
    };

    template <> struct hkIsPodType<hkbNode::ChildInfo> { enum { result = 1 }; };
} }
