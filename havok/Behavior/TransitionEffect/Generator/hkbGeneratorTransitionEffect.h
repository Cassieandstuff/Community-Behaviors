/// @file
/// @brief hkbGeneratorTransitionEffect: a transition that plays a generator of its own between the two states, blending
///        into it from the from-generator and out of it into the to-generator.
#pragma once

#include <Behavior/TransitionEffect/hkbTransitionEffect.h>

namespace CB { namespace havok
{
    /// @note Bethesda's build warns from the default constructor that the class is unsupported; the vanilla data never
    ///       uses it. The destructor is implicit (hkRefPtr releases the transition generator).
    class hkbGeneratorTransitionEffect : public hkbTransitionEffect
    {
    public:
        /// @brief How far the to-generator has been brought in.
        enum ToGeneratorState
        {
            STATE_INACTIVE                             = 0,
            /// @brief Due to start in step with the transition (updateSync sets its local time).
            STATE_READY_FOR_SET_LOCAL_TIME             = 1,
            /// @brief Due to start as m_selfTransitionMode says (it may already be playing).
            STATE_READY_FOR_APPLY_SELF_TRANSITION_MODE = 2,
            STATE_ACTIVE                               = 3
        };
        enum Stage
        {
            STAGE_BLENDING_IN                 = 0,
            STAGE_PLAYING_TRANSITION_GENERATOR = 1,
            STAGE_BLENDING_OUT                = 2
        };

        hkRefPtr<hkbGenerator> m_transitionGenerator;         ///< +0x50
        hkReal                 m_blendInDuration;             ///< +0x58
        hkReal                 m_blendOutDuration;            ///< +0x5c
        hkBool                 m_syncToGeneratorStartTime;    ///< +0x60
        hkbGenerator*          m_fromGenerator;               ///< +0x68
        hkbGenerator*          m_toGenerator;                 ///< +0x70
        hkReal                 m_timeInTransition;            ///< +0x78
        /// @brief One cycle of the transition generator; 0 until updateSync has seen it advance.
        hkReal                 m_duration;                    ///< +0x7c
        /// @note The blend durations rescaled with m_duration when the transition generator's speed changes.
        hkReal                 m_effectiveBlendInDuration;    ///< +0x80
        hkReal                 m_effectiveBlendOutDuration;   ///< +0x84
        hkInt8                 m_toGeneratorState;            ///< +0x88 — ToGeneratorState
        /// @brief The transition generator was already active at activate (echo it once its time is known).
        hkBool                 m_echoTransitionGenerator;     ///< +0x89
        /// @brief The to-generator was already active at activate.
        hkBool                 m_echoToGenerator;             ///< +0x8a
        hkBool                 m_justActivated;               ///< +0x8b
        /// @brief Asks the root graph to recompute its active nodes on the next update.
        hkBool                 m_updateActiveNodes;           ///< +0x8c
        hkEnum<Stage, hkInt8>  m_stage;                       ///< +0x8d

        hkbGeneratorTransitionEffect();
        /// @brief The cloneNode copy: shares the transition generator; the runtime state restarted.
        hkbGeneratorTransitionEffect(const hkbGeneratorTransitionEffect& te);

        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        /// @brief Leaves the transition finished (m_timeInTransition = m_duration).
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        /// @brief Three: from-, transition and to-generator.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                               ///< slot 8
        /// @brief Under FLAG_ACTIVE_ONLY the generators of the current stage (with the to-generator flagged
        ///        m_skipGenerate while the transition generator still plays), else the transition generator.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        /// @pre `internalState` is an hkbGeneratorTransitionEffectInternalState.
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @pre `internalState` is an hkbGeneratorTransitionEffectInternalState.
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        /// @brief Takes the transition generator's sync info (its cycle sets m_duration), echoes or syncs the
        ///        to-generator when it is due.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
        /// @brief Started, past the blend-out, and the to-generator active.
        virtual bool        isDone();                                                                ///< slot 28
        virtual void        setFromGenerator(hkbGenerator* fromGen);                                 ///< slot 29
        virtual void        setToGenerator(hkbGenerator* toGen);                                     ///< slot 30
        virtual hkReal      getFromGeneratorBlendOutTime();                                          ///< slot 31
        virtual hkReal      getToGeneratorBlendInTime();                                             ///< slot 32

        /// @brief Brings the to-generator in once the blend-out is near (STATE_READY_FOR_SET_LOCAL_TIME), or applies
        ///        the self-transition mode to it (STATE_READY_FOR_APPLY_SELF_TRANSITION_MODE).
        /// @note Name provisional.
        void updateToGeneratorState(hkbBehaviorGraph& rootBehavior);
        /// @brief The stage m_timeInTransition has reached; it never goes back.
        /// @note Name provisional. Inlined into activate and update in vanilla.
        HK_FORCE_INLINE void updateStage();
    };
    static_assert(sizeof(hkbGeneratorTransitionEffect) == 0x90, "hkbGeneratorTransitionEffect size");

    struct hkbGeneratorTransitionEffectInternalState : hkReferencedObject
    {
        hkReal m_timeInTransition;            ///< +0x10
        hkReal m_duration;                    ///< +0x14
        hkReal m_effectiveBlendInDuration;    ///< +0x18
        hkReal m_effectiveBlendOutDuration;   ///< +0x1c
        hkInt8 m_toGeneratorState;            ///< +0x20
        hkBool m_echoTransitionGenerator;     ///< +0x21
        hkBool m_echoToGenerator;             ///< +0x22
        hkBool m_justActivated;               ///< +0x23
        hkBool m_updateActiveNodes;           ///< +0x24
        hkEnum<hkbGeneratorTransitionEffect::Stage, hkInt8> m_stage;   ///< +0x25
    };
} }
