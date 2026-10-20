/// @file
/// @brief hkbTransitionEffect: the generator a state machine plays while it hands over from one state's generator to
///        the next.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace havok
{
    class hkbBehaviorGraph;

    /// @note The destructor is implicit: vanilla's scalar deleting destructor (slot 0) stamps no vtable. Slots 28-32 are
    ///       pure here; the names and order are the Havok SDK's, matched to what each override reads and writes.
    class hkbTransitionEffect : public hkbGenerator
    {
    public:
        /// @brief What to do when the transition's to-generator is already playing.
        enum SelfTransitionMode
        {
            SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC = 0,
            SELF_TRANSITION_MODE_CONTINUE                            = 1,
            SELF_TRANSITION_MODE_RESET                               = 2,
            SELF_TRANSITION_MODE_BLEND                               = 3
        };
        /// @brief Which of the two generators receives events during the transition.
        enum EventMode
        {
            EVENT_MODE_DEFAULT               = 0,
            EVENT_MODE_PROCESS_ALL           = 1,
            EVENT_MODE_IGNORE_FROM_GENERATOR = 2,
            EVENT_MODE_IGNORE_TO_GENERATOR   = 3
        };

        hkInt8 m_selfTransitionMode;   ///< +0x48 — SelfTransitionMode
        hkInt8 m_eventMode;            ///< +0x49 — EventMode
        /// @brief The project's default event mode (hkbProjectData::m_defaultEventMode), copied in activate; stands in
        ///        for m_eventMode while that is EVENT_MODE_DEFAULT.
        hkInt8 m_defaultEventMode;     ///< +0x4a — EventMode, not serialized

        hkbTransitionEffect();
        /// @brief The cloneNode copy: the modes copied, the default event mode reset.
        hkbTransitionEffect(const hkbTransitionEffect& te);

        virtual bool isTransitionEffect() const;   ///< slot 19

        /// @brief Whether the transition has finished, so the state machine can drop it.
        virtual bool   isDone() = 0;                                   ///< slot 28
        /// @brief Called before activation with the state the machine leaves.
        virtual void   setFromGenerator(hkbGenerator* fromGen) = 0;    ///< slot 29
        /// @brief Called before activation with the state the machine enters.
        virtual void   setToGenerator(hkbGenerator* toGen) = 0;        ///< slot 30
        /// @brief Seconds the from-generator takes to fade out.
        virtual hkReal getFromGeneratorBlendOutTime() = 0;             ///< slot 31
        /// @brief Seconds the to-generator takes to fade in.
        virtual hkReal getToGeneratorBlendInTime() = 0;                ///< slot 32

        /// @brief m_eventMode, or m_defaultEventMode while that is EVENT_MODE_DEFAULT.
        EventMode getEventMode() const;
        /// @brief Restarts an already-active `toGenerator` as m_selfTransitionMode asks: RESET restarts it at once,
        ///        BLEND echoes it over getToGeneratorBlendInTime(), CONTINUE leaves it be; the default picks CONTINUE for a
        ///        cyclic generator and BLEND otherwise.
        /// @note Name provisional.
        void applySelfTransitionMode(hkbBehaviorGraph& rootBehavior, hkbGenerator* toGenerator);
        /// @brief Whether `generator` is active, looking through hkbModifierGenerator and hkbBehaviorReferenceGenerator
        ///        wrappers to the generator they play.
        /// @note Name provisional. Reads nothing of the effect.
        bool isGeneratorActive(const hkbContext& context, hkbGenerator* generator);
    };
    static_assert(sizeof(hkbTransitionEffect) == 0x50, "hkbTransitionEffect size");
} }
