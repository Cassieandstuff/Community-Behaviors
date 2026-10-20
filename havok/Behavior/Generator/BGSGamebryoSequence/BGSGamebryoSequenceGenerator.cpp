/// @file
/// @brief BGSGamebryoSequenceGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BGSGamebryoSequence/BGSGamebryoSequenceGenerator.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Container/BitField/hkBitField.h>
#include <Common/Base/Container/String/hkString.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief The largest finite float (0x7f7fffff); HK_REAL_MAX is a hair below it.
        const hkReal s_floatMax = 3.402823466e+38f;
    }

    /// @note Both constructors (and the copy) end by clearing m_userData.
    BGSGamebryoSequenceGenerator::BGSGamebryoSequenceGenerator()
    :   m_pSequence(HK_NULL),
        m_eBlendModeFunction(BMF_NONE),
        m_fPercent(1.0f),
        m_fTime(-s_floatMax),
        m_bDelayedActivate(false),
        m_bLooping(true)
    {
        m_userData = 0;
    }

    /// @note m_events is constructed empty, not finished: it is never serialized.
    BGSGamebryoSequenceGenerator::BGSGamebryoSequenceGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_fTime(-s_floatMax),
        m_bDelayedActivate(false),
        m_bLooping(true)
    {
        m_userData = 0;
    }

    BGSGamebryoSequenceGenerator::BGSGamebryoSequenceGenerator(const BGSGamebryoSequenceGenerator& gen)
    :   hkbGenerator(gen),
        m_pSequence(gen.m_pSequence),
        m_eBlendModeFunction(gen.m_eBlendModeFunction),
        m_fPercent(gen.m_fPercent),
        m_fTime(gen.m_fTime),
        m_bDelayedActivate(false),
        m_bLooping(true)
    {
        m_userData = 0;
    }

    BGSGamebryoSequenceGenerator::~BGSGamebryoSequenceGenerator()
    {
    }

    const void* BGSGamebryoSequenceGenerator::getClassType() const
    {
        return &staticClass();
    }

    void BGSGamebryoSequenceGenerator::activate(const hkbContext& context)
    {
        m_fTime            = 0.0f;
        m_bDelayedActivate = true;
    }

    /// @note An event fires when its time lies in [previous time, time), the interval wrapping at the duration while
    ///       looping. m_bLooping is read at each test (not held in a bool local): vanilla tests it twice, a local merges them.
    void BGSGamebryoSequenceGenerator::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("BGSGamebryoSequenceGenerator::update");

        m_fTime += timestep;
        hkbBehaviorGraph* behaviorGraph = context.m_character->m_behaviorGraph;
        if (behaviorGraph && m_bDelayedActivate) {
            if (IGamebryoSequenceGeneratorHolderSingleton::s_instance) {
                IGamebryoSequenceGeneratorHolderSingleton::s_instance->activateSequence(behaviorGraph, this, timestep, m_events);
            }
            m_bDelayedActivate = false;
        }

        hkReal duration = s_floatMax;
        if (IGamebryoSequenceGeneratorHolderSingleton::s_instance &&
            IGamebryoSequenceGeneratorHolderSingleton::s_instance->getSequenceDuration(this, duration)) {
            hkbEventQueue* queue = context.getEventQueue();
            if (queue) {
                hkReal     time     = m_fTime;
                hkReal     prevTime = m_bLooping ? ::fmodf(time - timestep, duration) : time - timestep;
                if (m_bLooping) time = ::fmodf(time, duration);

                for (int i = 0; i < m_events.getSize(); i++) {
                    if ((prevTime < time && prevTime <= m_events[i].m_time && m_events[i].m_time < time) ||
                        (prevTime > time && (prevTime <= m_events[i].m_time || m_events[i].m_time < time))) {
                        hkbEvent event(m_events[i].m_event.m_id, m_events[i].m_event.getPayload(), this);
                        event.m_id = queue->toExternal(event.m_id);
                        queue->enqueue(event);
                    }
                }
            }
        }

        HK_TIMER_END();
    }

    void BGSGamebryoSequenceGenerator::deactivate(const hkbContext& context)
    {
        m_fTime            = -s_floatMax;
        m_bDelayedActivate = false;
        m_events.clear();
        if (IGamebryoSequenceGeneratorHolderSingleton::s_instance) IGamebryoSequenceGeneratorHolderSingleton::s_instance->deactivateSequence(this);
    }

    bool BGSGamebryoSequenceGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_pSequence) {
            errorString = "m_pSequence is NULL (unset).";
            return false;
        }
        return true;
    }

    hkbNode* BGSGamebryoSequenceGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BGSGamebryoSequenceGenerator(*this);
    }

    void BGSGamebryoSequenceGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                                hkReal timeOffset)
    {
        HK_TIMER_BEGIN("BGSGamebryoSequenceGenerator::generate");
        output.accessWorldFromModel()                                                       = *context.m_character->m_worldFromModel;
        output.getTrackHeader(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL).m_onFraction = 1.0f;
        const int  numPoseLocal = output.getNumPoseLocal();
        hkBitField partition(numPoseLocal + 1, reinterpret_cast<hkUint32*>(output.accessPoseLocal() + numPoseLocal));
        HK_TIMER_END();
    }

    bool BGSGamebryoSequenceGenerator::canRecycleOutput() const
    {
        return true;
    }

    void BGSGamebryoSequenceGenerator::setElapsedTime(hkReal elapsed)
    {
        m_fTime = elapsed;
    }

    hkReal BGSGamebryoSequenceGenerator::getBlendWeight() const
    {
        hkReal weight = 1.0f;
        switch (m_eBlendModeFunction) {
            case BMF_PERCENT:           weight = m_fPercent; break;
            case BMF_ONE_MINUS_PERCENT: weight = 1.0f - m_fPercent; break;
            default:                    return weight;
        }
        if (weight < 0.0f) return 0.0f;
        if (weight > 1.0f) return 1.0f;
        return weight;
    }
} }
