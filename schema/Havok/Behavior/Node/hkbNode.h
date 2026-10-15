/// @file
/// @brief hkbBindable and hkbNode: the base of every behavior graph node.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;
    class hkbBehaviorGraph;

    struct hkbVariableBindingSet : hkReferencedObject {};

    struct hkbBindable : hkReferencedObject
    {
        hkRefPtr<hkbVariableBindingSet> m_variableBindingSet;   ///< +0x10
        hkArray<hkbBindable*>           m_cachedBindables;      ///< +0x18
        hkBool                          m_areBindablesCached;   ///< +0x28

        HK_FORCE_INLINE hkbBindable(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag), m_variableBindingSet(flag), m_areBindablesCached(false) {}
        /// @note Out of line in vanilla: every node destructor ends in a tail call to it.
        ~hkbBindable();

        virtual void collectBindables(void* collector);   ///< slot 3
    };

    /// @note A `class`, as in Havok: the class-key is part of the mangled name of the constructors that take one.
    class hkbNode : public hkbBindable
    {
    public:
        hkUlong     m_userData;     ///< +0x30 — Bethesda keys clip loading off it (the clip tests 0 and 8)
        hkStringPtr m_name;         ///< +0x38
        hkInt16     m_id;           ///< +0x40
        hkInt8      m_cloneState;   ///< +0x42

        HK_FORCE_INLINE hkbNode(hkFinishLoadedObjectFlag flag) : hkbBindable(flag), m_name(flag) { m_id = -1; }
        /// @brief The clone copy (cloneNode); marks the SOURCE node as cloned.
        hkbNode(const hkbNode& node);

        virtual void activate(const hkbContext& context);                      ///< slot 4
        virtual void update(const hkbContext& context, hkReal timestep);       ///< slot 5
        virtual void handleEvent(const hkbContext& context, const void* e);    ///< slot 6
        virtual void deactivate(const hkbContext& context);                    ///< slot 7
    };
} }
