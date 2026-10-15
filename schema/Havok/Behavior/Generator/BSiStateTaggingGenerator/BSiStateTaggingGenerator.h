/// @file
/// @brief BSiStateTaggingGenerator: Bethesda's pass-through generator that tags its subtree with an iState.
#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    /// @brief Plays m_pDefaultGenerator unchanged; m_iStateToSetAs / m_iPriority are read by the game's iState
    ///        tagging (BSiStateTaggingUtils), not by this node's callbacks.
    /// @note Member names are the class's reflected names. Every constructor sets bit 0 of m_userData (as
    ///       hkbTwistModifier's do); what it flags is unknown. The destructor is implicit (the child is a raw pointer,
    ///       never released).
    class BSiStateTaggingGenerator : public hkbGenerator
    {
    public:
        hkUint8       m_pad48[8];              ///< +0x48 — unidentified, not reflected
        /// @note Shared by clones (the clone copy copies the pointer), not cloned.
        hkbGenerator* m_pDefaultGenerator;     ///< +0x50
        hkInt32       m_iStateToSetAs;         ///< +0x58
        hkInt32       m_iPriority;             ///< +0x5c

        BSiStateTaggingGenerator();
        BSiStateTaggingGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: settings and child pointer.
        BSiStateTaggingGenerator(const BSiStateTaggingGenerator& gen);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        /// @brief One child (the default generator) when it is set.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                               ///< slot 8
        /// @brief The default generator, reported active.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        /// @brief Valid when the default generator is set and nothing else has written `errorString`.
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        /// @brief Copies the child's output into `output` (when it is another buffer).
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        /// @brief The child's sync info (from its active-node record, or the record that one defers to), unmirrored;
        ///        hkbGenerator's without a child.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
    };
    static_assert(sizeof(BSiStateTaggingGenerator) == 0x60, "BSiStateTaggingGenerator size");
} }
