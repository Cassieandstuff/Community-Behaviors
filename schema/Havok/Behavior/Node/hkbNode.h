/// @file
/// @brief hkbBindable and hkbNode: the base of every behavior graph node.
#pragma once

#include <Behavior/Event/hkbEvent.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;
    class hkbBehaviorGraph;

    struct hkbVariableBindingSet : hkReferencedObject
    {
        /// @note 0x28 bytes, starting with its hkStringPtr m_memberPath; the rest is not declared yet.
        struct Binding;

        /// @brief The binding whose member path is `memberPath` (exact compare), else null.
        const Binding* findBindingByMemberPath(const char* memberPath) const;
    };

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

        virtual void collectBindables(void* collector);   ///< slot 3
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
        ///       The other bits are the Havok SDK's, unverified here.
        enum GetChildrenFlagBits { FLAG_ACTIVE_ONLY = 1, FLAG_GENERATORS_ONLY = 4, FLAG_IGNORE_REFERENCED_BEHAVIORS = 8 };
        typedef int GetChildrenFlags;

        /// @brief One child getChildren reports.
        /// @note 0x10 bytes; the blender sets a bool at +0xa (whether that child is active) — the rest is unidentified.
        struct ChildInfo
        {
            hkbNode* m_node;
            hkUint8  m_opaque[8];
        };
        /// @brief Where getChildren reports to.
        /// @note The blender also sets a bool at +0xa of it; unidentified.
        struct ChildrenInfo
        {
            hkArray<ChildInfo>& m_childInfos;
        };

        hkUlong     m_userData;     ///< +0x30 — Bethesda keys clip loading off it (the clip tests 0 and 8)
        hkStringPtr m_name;         ///< +0x38
        hkInt16     m_id;           ///< +0x40
        hkInt8      m_cloneState;   ///< +0x42

        /// @note Inlined into the node classes' default constructors, where its vtable store survives (the out-of-line
        ///       hkStringPtr constructor could read it); the earlier bases' stores die.
        HK_FORCE_INLINE hkbNode() : m_userData(0), m_id(-1), m_cloneState(CLONE_STATE_DEFAULT) {}
        HK_FORCE_INLINE hkbNode(hkFinishLoadedObjectFlag flag) : hkbBindable(flag), m_name(flag) { m_id = -1; }
        /// @brief The clone copy (cloneNode); marks the SOURCE node as cloned.
        hkbNode(const hkbNode& node);

        virtual void activate(const hkbContext& context);                      ///< slot 4
        virtual void update(const hkbContext& context, hkReal timestep);       ///< slot 5
        /// @note By value (on x64, a pointer to the caller's copy): hkbSequence's override reads the id through it.
        virtual void handleEvent(const hkbContext& context, const hkbEvent e); ///< slot 6
        virtual void deactivate(const hkbContext& context);                    ///< slot 7
        /// @note Name provisional (the overrides return their child count). hkbNode's own returns 2 — unexplained.
        virtual int  getMaxNumChildren(GetChildrenFlags flags);                ///< slot 8
        virtual void getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool isValid(hkStringPtr& errorString) const;                  ///< slot 10
        virtual void vfn11();                                                  ///< slot 11
        virtual hkbNode* cloneNode(hkbBehaviorGraph& rootBehavior) const;      ///< slot 12
        virtual hkReferencedObject* createInternalState();                     ///< slot 13
        virtual void getInternalState(hkReferencedObject& internalState) const;   ///< slot 14
        virtual void vfn15();                                                  ///< slot 15
        virtual void setInternalState(const hkReferencedObject& internalState);   ///< slot 16
        virtual void vfn17();                                                  ///< slot 17
        virtual void vfn18();                                                  ///< slot 18
        virtual void vfn19();                                                  ///< slot 19
        virtual void vfn20();                                                  ///< slot 20
        /// @note hkbNode's returns false, hkbGenerator's true.
        virtual bool vfn21();                                                  ///< slot 21
        virtual void vfn22();                                                  ///< slot 22
    };
} }
