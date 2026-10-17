/// @file
/// @brief hkbBehaviorReferenceGenerator: plays another behavior graph (a referenced .hkx) as its one child.
#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    extern const hkClass hkbBehaviorReferenceGeneratorClass;

    /// @note Shared, not copied, by cloneNode. The referenced graph is its own active node: updateSync makes this node's
    ///       record a proxy for the graph's, and generate passes the graph's output through.
    class hkbBehaviorReferenceGenerator : public hkbGenerator
    {
    public:
        hkStringPtr       m_behaviorName;   ///< +0x48
        /// @note Not reflected for serialization; the default constructor leaves it unset (vanilla's writes nothing
        ///       there). Holds a reference the destructor releases only while m_userData is 0.
        hkbBehaviorGraph* m_behavior;       ///< +0x50

        hkbBehaviorReferenceGenerator();
        virtual ~hkbBehaviorReferenceGenerator();   ///< slot 0

        virtual const void* getClassType() const;                                                    ///< slot 1
        /// @brief The class's reflected members, the referenced graph, then hkbGenerator's members.
        /// @note `cls` is ignored, as in the clip's.
        virtual void        calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        /// @brief The referenced graph, unless bit 4 of `flags` asks to skip referenced behaviors; under FLAG_ACTIVE_ONLY
        ///        it is reported active.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        /// @brief Valid once the referenced graph is set.
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        /// @note Marks this node CLONE_STATE_SHARABLE and returns it with a new reference.
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        /// @brief Copies the referenced graph's output (its only child's) into `output`, unless they are the same buffer.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        /// @brief Makes `info` a proxy for the referenced graph's active-node record (or for the record that one defers
        ///        to).
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25

        static const hkClass& staticClass();
    };
    static_assert(sizeof(hkbBehaviorReferenceGenerator) == 0x58, "hkbBehaviorReferenceGenerator size");
} }
