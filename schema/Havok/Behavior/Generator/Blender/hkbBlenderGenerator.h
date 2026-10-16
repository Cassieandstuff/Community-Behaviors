/// @file
/// @brief hkbBlenderGenerator: blends the poses of its children by weight (or by a blend parameter over sorted
///        children), optionally keeping them synchronized.
#pragma once

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/Blender/hkbBlenderGeneratorChild.h>
#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    /// @note A `class`, as in Havok (the class-key is part of the constructors' mangled names).
    class hkbBlenderGenerator : public hkbGenerator
    {
    public:
        /// @note Values read from the exe's reflected enum (0x14194ce70); schema/metadata/enums/BlenderFlags.yaml does
        ///       not match them.
        enum BlenderFlags
        {
            FLAG_SYNC                                       = 0x1,
            FLAG_SMOOTH_GENERATOR_WEIGHTS                   = 0x4,
            FLAG_DONT_DEACTIVATE_CHILDREN_WITH_ZERO_WEIGHTS = 0x8,
            FLAG_PARAMETRIC_BLEND                           = 0x10,
            FLAG_IS_PARAMETRIC_BLEND_CYCLIC                 = 0x20,
            FLAG_FORCE_DENSE_POSE                           = 0x40,
        };

        /// @brief A child's runtime state (reflected as hkbBlenderGeneratorChildInternalState).
        struct ChildInternalState
        {
            HK_DECLARE_POD_TYPE();

            hkBool m_isActive;        ///< +0x0
            /// @note Set when the child turns active, cleared once updateSync has moved it onto the blend's timeline.
            hkBool m_syncNextFrame;   ///< +0x1
        };

        hkReal                             m_referencePoseWeightThreshold;   ///< +0x48
        hkReal                             m_blendParameter;                 ///< +0x4c
        hkReal                             m_minCyclicBlendParameter;        ///< +0x50
        hkReal                             m_maxCyclicBlendParameter;        ///< +0x54
        /// @note -1 = none.
        hkInt16                            m_indexOfSyncMasterChild;         ///< +0x58
        hkInt16                            m_flags;                          ///< +0x5a — BlenderFlags
        hkBool                             m_subtractLastChild;              ///< +0x5c
        /// @note Each child holds a reference, and its generator one more (raw pointers: the constructors and the
        ///       destructor count them by hand).
        hkArray<hkbBlenderGeneratorChild*> m_children;                       ///< +0x60
        hkArray<ChildInternalState>        m_childrenInternalStates;         ///< +0x70
        /// @brief Child indices by ascending m_weight (parametric blends).
        hkArray<hkInt16>                   m_sortedChildren;                 ///< +0x80
        /// @brief The end child's share of a parametric blend.
        hkReal                             m_endIntervalWeight;              ///< +0x90
        int                                m_numActiveChildren;              ///< +0x94
        hkInt16                            m_beginIntervalIndex;             ///< +0x98
        hkInt16                            m_endIntervalIndex;               ///< +0x9a
        /// @brief Asks the next updateSync to start the children's timelines over from the blend's.
        hkBool                             m_initSync;                       ///< +0x9c
        hkBool                             m_doSubtractiveBlend;             ///< +0x9d

        /// @note Inline: vanilla's only default construction is inlined into hkbPoseMatchingGenerator's.
        HK_FORCE_INLINE hkbBlenderGenerator()
        :   m_referencePoseWeightThreshold(0.0f),
            m_blendParameter(0.0f),
            m_minCyclicBlendParameter(0.0f),
            m_maxCyclicBlendParameter(1.0f),
            m_indexOfSyncMasterChild(-1),
            m_flags(0),
            m_subtractLastChild(false),
            m_endIntervalWeight(0.0f),
            m_numActiveChildren(0),
            m_beginIntervalIndex(0),
            m_endIntervalIndex(0),
            m_initSync(false),
            m_doSubtractiveBlend(false)
        {
        }
        /// @brief The cloneNode copy: clones every child (see hkbBlenderGeneratorChild's copy).
        hkbBlenderGenerator(const hkbBlenderGenerator& gen);
        /// @brief Releases each child and its generator, unless a packfile placed this blender (it holds none then).
        virtual ~hkbBlenderGenerator();   ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        /// @brief This blender, then each child.
        virtual void        collectBindables(hkbBindableCollector& collector);                       ///< slot 3
        /// @brief Restarts the parametric interval, clears every child's state and works out which are active.
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        /// @brief Advances the blend's own timeline (wrapping at its period) and recomputes the active children.
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        /// @brief The active count under FLAG_ACTIVE_ONLY, else every child.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                               ///< slot 8
        /// @brief The children's generators (only the active ones under FLAG_ACTIVE_ONLY); m_isActive marks the sync
        ///        master, or every child of a synchronized blender asked for active children.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        /// @pre `internalState` is an hkbBlenderGeneratorInternalState.
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @pre `internalState` is an hkbBlenderGeneratorInternalState.
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        /// @note The pose work is hkbBlenderGeneratorUtils::generateInternal; `timeOffset` is not passed on.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        /// @brief Only a lone active child with no reference-pose threshold can write into the blender's output.
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        /// @brief Combines the active children's sync infos into `info` (synchronized), or copies the sync master's,
        ///        and moves children that just turned active onto the combined timeline.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
        /// @brief Resynchronizes the children from the blend's timeline on the next updateSync.
        virtual void        startEcho(hkReal echoDuration);                                          ///< slot 27

        /// @brief Puts `generator` in child `index` (growing the roster, a fresh child at `index`), releasing the
        ///        generator it replaces.
        /// @note Name provisional; nothing in the exe calls it.
        void setChildGenerator(int index, hkbGenerator* generator);
        /// @brief Sets one bone's weight (clamped to [0, 1]) in child `childIndex`'s bone weights, creating them as
        ///        needed; trailing weights of 1 are trimmed off.
        /// @note Name provisional; nothing in the exe calls it.
        void setChildBoneWeight(int childIndex, int boneIndex, hkReal boneWeight);
        /// @brief Recomputes which children are active (non-zero weight, the sync master, or all under
        ///        FLAG_DONT_DEACTIVATE_CHILDREN_WITH_ZERO_WEIGHTS) and m_numActiveChildren; true if any child turned on
        ///        or off.
        /// @note Name provisional.
        bool updateActiveChildren();
        /// @brief Sorts the children by parameter and finds the interval m_blendParameter falls in (clamped, or wrapped
        ///        between m_minCyclicBlendParameter and m_maxCyclicBlendParameter when cyclic): m_beginIntervalIndex,
        ///        m_endIntervalIndex and m_endIntervalWeight.
        /// @note Name provisional.
        void computeBlendInterval();
        /// @brief The sync point ids every active child's timeline has, into `syncPointIdsOut`; returns their count.
        /// @note Name provisional; nothing in the exe calls it.
        int  computeCommonSyncPointIds(hkbBehaviorGraph& rootBehavior, int* syncPointIdsOut);
        /// @brief Sorts `sortedChildren` (indices into `children`) by ascending m_weight (a bubble sort).
        /// @note Name provisional.
        static void sortChildren(hkInt16* sortedChildren, int numChildren, hkbBlenderGeneratorChild* const* children);
    };
    static_assert(sizeof(hkbBlenderGenerator) == 0xa0, "hkbBlenderGenerator size");

    struct hkbBlenderGeneratorInternalState : hkReferencedObject
    {
        hkArray<hkbBlenderGenerator::ChildInternalState> m_childrenInternalStates;   ///< +0x10
        hkArray<hkInt16>                                 m_sortedChildren;           ///< +0x20
        hkReal                                           m_endIntervalWeight;        ///< +0x30
        int                                              m_numActiveChildren;        ///< +0x34
        hkInt16                                          m_beginIntervalIndex;       ///< +0x38
        hkInt16                                          m_endIntervalIndex;         ///< +0x3a
        hkBool                                           m_initSync;                 ///< +0x3c
        hkBool                                           m_doSubtractiveBlend;       ///< +0x3d
    };
    static_assert(sizeof(hkbBlenderGeneratorInternalState) == 0x40, "hkbBlenderGeneratorInternalState size");
} }
