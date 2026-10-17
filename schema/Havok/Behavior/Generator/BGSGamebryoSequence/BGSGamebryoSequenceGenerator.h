/// @file
/// @brief BGSGamebryoSequenceGenerator: Bethesda's generator that plays a Gamebryo (.kf) controller sequence.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    class hkbBehaviorGraph;
    class BGSGamebryoSequenceGenerator;

    /// @brief An event the sequence raises when its playback passes `m_time` (seconds).
    /// @note Name provisional: an unreflected element type, filled by the game (IGamebryoSequenceGeneratorHolderSingleton).
    struct BGSGamebryoSequenceGeneratorEvent
    {
        hkReal           m_time;    ///< +0x00
        hkbEventProperty m_event;   ///< +0x08
    };

    /// @brief The game side that owns the Gamebryo sequences (the exe's BSTSingletonSDM<BGSGamebryoSequenceGeneratorHolderSingleton>).
    /// @note Only the slots the generator calls are declared; their names are provisional.
    class IGamebryoSequenceGeneratorHolderSingleton
    {
    public:
        virtual ~IGamebryoSequenceGeneratorHolderSingleton();   ///< slot 0
        /// @brief Starts `generator`'s sequence on the graph's actor and fills its events.
        virtual void activateSequence(hkbBehaviorGraph* behaviorGraph, BGSGamebryoSequenceGenerator* generator, hkReal timestep,
                                      hkArray<BGSGamebryoSequenceGeneratorEvent>& events) = 0;   ///< slot 1
        virtual void deactivateSequence(BGSGamebryoSequenceGenerator* generator) = 0;             ///< slot 2
        /// @brief The sequence's length, when it is playing.
        virtual bool getSequenceDuration(BGSGamebryoSequenceGenerator* generator, hkReal& duration) = 0;   ///< slot 3

        /// @note Null until the game installs it.
        static IGamebryoSequenceGeneratorHolderSingleton* s_instance;
    };

    /// @note Member names are the class's reflected names (the exe's hkClass strings). The `slot N` annotations are
    ///       vanilla's vtable slots. The animation itself is played by the game (Gamebryo); the generator only keeps time,
    ///       raises the sequence's events and hands the pose through untouched (all bones unset).
    class BGSGamebryoSequenceGenerator : public hkbGenerator
    {
    public:
        /// @brief BlendModeFunction values (schema/metadata/enums/BlendModeFunction.yaml).
        enum BlendModeFunction { BMF_NONE = 0, BMF_PERCENT = 1, BMF_ONE_MINUS_PERCENT = 2 };

        const char*                                m_pSequence;            ///< +0x48, the sequence name
        hkInt8                                     m_eBlendModeFunction;   ///< +0x50, BlendModeFunction
        hkReal                                     m_fPercent;             ///< +0x54
        hkArray<BGSGamebryoSequenceGeneratorEvent> m_events;               ///< +0x58, not serialized
        hkReal                                     m_fTime;                ///< +0x68, -HK_FLOAT_MAX while inactive
        hkBool                                     m_bDelayedActivate;     ///< +0x6c, the sequence starts on the next update
        hkBool                                     m_bLooping;             ///< +0x6d

        BGSGamebryoSequenceGenerator();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BGSGamebryoSequenceGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: the settings and the time; no events, m_bDelayedActivate and m_bLooping reset as the
        ///        constructors set them.
        BGSGamebryoSequenceGenerator(const BGSGamebryoSequenceGenerator& gen);
        /// @note User-declared: vanilla's (0x140b92a00) re-stamps the vtable.
        virtual ~BGSGamebryoSequenceGenerator();

        static const hkClass& staticClass();

        /// @brief The sequence's blend weight: m_fPercent or 1 - m_fPercent by m_eBlendModeFunction, clamped to [0, 1];
        ///        1 (unclamped) for BMF_NONE.
        /// @note Name provisional (0x140b93040); IGamebryoSequenceGeneratorHolderSingleton (slot 4) calls it.
        hkReal getBlendWeight() const;

        virtual const void* getClassType() const;                                                    ///< slot 1
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        /// @brief Advances the time, starts a delayed sequence and raises the events passed since the last update.
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        /// @brief Writes the character's worldFromModel and clears the pose partition (no bone set).
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        virtual void        setElapsedTime(hkReal elapsed);                                          ///< slot 26
    };
    static_assert(sizeof(BGSGamebryoSequenceGeneratorEvent) == 0x18, "BGSGamebryoSequenceGeneratorEvent size");
    static_assert(sizeof(BGSGamebryoSequenceGenerator) == 0x70, "BGSGamebryoSequenceGenerator size");
} }
