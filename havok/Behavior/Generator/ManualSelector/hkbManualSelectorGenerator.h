/// @file
/// @brief hkbManualSelectorGenerator: plays the one child its (bindable) selected index picks.
#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace havok
{
    struct hkbManualSelectorGeneratorInternalState : hkReferencedObject
    {
        hkInt8 m_currentGeneratorIndex;   ///< +0x10
    };

    class hkbManualSelectorGenerator : public hkbGenerator
    {
    public:
        /// @note Each child holds a reference (raw pointers: the constructors and the destructor count them by hand).
        hkArray<hkbGenerator*> m_generators;               ///< +0x48
        hkInt8                 m_selectedGeneratorIndex;   ///< +0x58 — what the graph asks for (may be out of range)
        hkInt8                 m_currentGeneratorIndex;    ///< +0x59 — the child playing, clamped into range

        hkbManualSelectorGenerator();
        /// @brief The cloneNode copy: shares the children (each gains a reference), the current index restarted.
        hkbManualSelectorGenerator(const hkbManualSelectorGenerator& gen);
        /// @brief Releases each child.
        virtual ~hkbManualSelectorGenerator();   ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        /// @brief Follows the selected index; a switch to a different child asks the root graph to recompute its
        ///        active nodes.
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        /// @brief One under FLAG_ACTIVE_ONLY, else every child.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                               ///< slot 8
        /// @brief The current child under FLAG_ACTIVE_ONLY, else every child.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        /// @brief At least one child, none null, and the selected index in range.
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        /// @pre `internalState` is an hkbManualSelectorGeneratorInternalState.
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @pre `internalState` is an hkbManualSelectorGeneratorInternalState.
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        /// @brief Copies the current child's output into `output`, unless they are the same buffer.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        /// @brief Makes `info` a proxy for the current child's active-node record (or for the record that one defers
        ///        to).
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25

        /// @brief Puts `generator` at `index` (growing the roster with nulls as needed), releasing the child it replaces.
        /// @note Name provisional; nothing in the exe calls it.
        void setGenerator(int index, hkbGenerator* generator);
        /// @brief m_selectedGeneratorIndex clamped into [0, last child].
        /// @note Name provisional.
        hkInt8 computeCurrentGeneratorIndex() const;
    };
    static_assert(sizeof(hkbManualSelectorGenerator) == 0x60, "hkbManualSelectorGenerator size");
} }
