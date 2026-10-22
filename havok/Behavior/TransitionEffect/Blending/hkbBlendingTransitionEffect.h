/// @file
/// @brief hkbBlendingTransitionEffect: a transition that cross-fades from the state it leaves into the one it enters.
#pragma once

#include <Behavior/TransitionEffect/hkbTransitionEffect.h>

namespace CB { namespace havok
{
    struct hkbGeneratorSyncInfo;

    /// @note The destructor is implicit (the pose array frees itself). Slots 33-38 are this class's own virtuals; their
    ///       names are provisional, read off what hkbBlendingTransitionEffect's versions do. Bethesda's
    ///       BSCyclicBlendTransitionGenerator drives an effect through updateInternal/updateSyncInternal with itself as
    ///       the node whose sync record they keep.
    class hkbBlendingTransitionEffect : public hkbTransitionEffect
    {
    public:
        enum FlagBits
        {
            FLAG_NONE                         = 0,
            FLAG_IGNORE_FROM_WORLD_FROM_MODEL = 1,
            FLAG_SYNC                         = 2,
            FLAG_IGNORE_TO_WORLD_FROM_MODEL   = 4
        };
        enum EndMode
        {
            END_MODE_NONE                                   = 0,
            END_MODE_TRANSITION_UNTIL_END_OF_FROM_GENERATOR = 1,
            END_MODE_CAP_DURATION_AT_END_OF_FROM_GENERATOR  = 2
        };

        hkReal                  m_duration;                               ///< +0x50
        hkReal                  m_toGeneratorStartTimeFraction;           ///< +0x54
        hkUint16                m_flags;                                  ///< +0x58 — FlagBits
        hkInt8                  m_endMode;                                ///< +0x5a — EndMode
        hkInt8                  m_blendCurve;                             ///< +0x5b — hkbBlendCurveUtils::BlendCurve
        hkbGenerator*           m_fromGenerator;                          ///< +0x60
        hkbGenerator*           m_toGenerator;                            ///< +0x68
        /// @brief The character's local pose when the transition began (ragdoll hand-over), taken on the first update
        ///        after m_initializeCharacterPose is raised.
        hkArray<hkQsTransform>  m_characterPoseAtBeginningOfTransition;   ///< +0x70
        hkReal                  m_timeRemaining;                          ///< +0x80
        hkReal                  m_timeInTransition;                       ///< +0x84
        /// @brief The to-generator was already active at activate (a self transition).
        hkBool                  m_applySelfTransition;                    ///< +0x88
        hkBool                  m_initializeCharacterPose;                ///< +0x89

        hkbBlendingTransitionEffect();
        /// @brief The cloneNode copy: the reflected settings copied, the runtime state restarted.
        hkbBlendingTransitionEffect(const hkbBlendingTransitionEffect& te);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        /// @brief Starts the countdown unless a transition is already running.
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        /// @brief Under FLAG_ACTIVE_ONLY the to-generator, then the from-generator (when different), with the
        ///        self-transition and event-mode flags; nothing otherwise.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        /// @pre `internalState` is an hkbBlendingTransitionEffectInternalState.
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @pre `internalState` is an hkbBlendingTransitionEffectInternalState.
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        /// @brief True when the transition is a no-op (from- and to-generator are the same).
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
        /// @brief Started, and either a self transition or out of time.
        virtual bool        isDone();                                                                ///< slot 28
        virtual void        setFromGenerator(hkbGenerator* fromGen);                                 ///< slot 29
        virtual void        setToGenerator(hkbGenerator* toGen);                                     ///< slot 30
        /// @brief m_duration.
        virtual hkReal      getFromGeneratorBlendOutTime();                                          ///< slot 31
        /// @brief m_duration.
        virtual hkReal      getToGeneratorBlendInTime();                                             ///< slot 32

        /// @brief Whether both generators advance (a nonzero base frequency), so their timelines can be synced.
        /// @note Name provisional.
        virtual bool        canSyncGenerators(hkbBehaviorGraph& rootBehavior);                       ///< slot 33
        /// @brief No time has passed in the transition yet.
        /// @note Name provisional.
        virtual bool        isAtStart();                                                             ///< slot 34
        /// @brief Whether the transition's timeline is the from-generator's alone (false here).
        /// @note Name provisional.
        virtual bool        useFromGeneratorSyncInfo();                                              ///< slot 35
        /// @note Name provisional.
        virtual const hkbGeneratorSyncInfo& getFromGeneratorSyncInfo(hkbBehaviorGraph& rootBehavior);  ///< slot 36
        /// @brief m_duration.
        /// @note Name provisional.
        virtual hkReal      getDuration();                                                           ///< slot 37
        /// @brief isAtStart().
        /// @note Name provisional; nothing in the reimplemented code calls it.
        virtual bool        isStarting();                                                            ///< slot 38

        /// @brief update's body: advances the sync record of `syncNode` (wrapped into its cycle) and the transition's
        ///        clocks, and takes the character pose when one is due.
        /// @note Name provisional.
        void updateInternal(const hkbContext& context, hkReal timestep, hkbNode* syncNode);
        /// @brief updateSync's body: fills `syncNode`'s sync record (the from-generator's, a blend of both under
        ///        FLAG_SYNC, or none), starts the to-generator in step, then recomputes m_timeRemaining.
        /// @note Name provisional.
        void updateSyncInternal(const hkbContext& context, hkbNode* syncNode);
        /// @brief m_timeRemaining from `duration` and m_endMode, against the from-generator's timeline.
        /// @note Name provisional.
        void computeTimeRemaining(hkReal duration, const hkbGeneratorSyncInfo& fromSyncInfo);
    };
    static_assert(sizeof(hkbBlendingTransitionEffect) == 0x90, "hkbBlendingTransitionEffect size");

    struct hkbBlendingTransitionEffectInternalState : hkReferencedObject
    {
        hkArray<hkQsTransform> m_characterPoseAtBeginningOfTransition;   ///< +0x10
        hkReal                 m_timeRemaining;                          ///< +0x20
        hkReal                 m_timeInTransition;                       ///< +0x24
        hkBool                 m_applySelfTransition;                    ///< +0x28
        hkBool                 m_initializeCharacterPose;                ///< +0x29
    };
} }
