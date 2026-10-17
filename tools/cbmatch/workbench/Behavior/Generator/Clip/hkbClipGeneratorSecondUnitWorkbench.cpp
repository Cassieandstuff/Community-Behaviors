/// @file
/// @brief Workbench: the functions of hkbClipGeneratorSecondUnit.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Generator/Clip/hkbClipGeneratorSecondUnit.cpp.
#include <Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    void hkbClipGenerator::getInternalState(hkReferencedObject& _internalState) const
    {
        hkbClipGeneratorInternalState& internalState = static_cast<hkbClipGeneratorInternalState&>(_internalState);

        internalState.m_extractedMotion                    = m_extractedMotion;
        internalState.m_echos                              = m_echos;
        internalState.m_localTime                          = m_localTime;
        internalState.m_time                               = m_time;
        internalState.m_previousUserControlledTimeFraction = m_previousUserControlledTimeFraction;
        internalState.m_bufferSize                         = m_bufferSize;
        internalState.m_echoBufferSize                     = m_echoBufferSize;
        internalState.m_atEnd                              = m_atEnd;
        internalState.m_ignoreStartTime                    = m_ignoreStartTime;
        internalState.m_pingPongBackward                   = m_pingPongBackward;
    }

    void hkbClipGenerator::setInternalState(const hkReferencedObject& _internalState)
    {
        const hkbClipGeneratorInternalState& internalState = static_cast<const hkbClipGeneratorInternalState&>(_internalState);

        m_extractedMotion                    = internalState.m_extractedMotion;
        m_echos                              = internalState.m_echos;
        m_localTime                          = internalState.m_localTime;
        m_time                               = internalState.m_time;
        m_previousUserControlledTimeFraction = internalState.m_previousUserControlledTimeFraction;
        m_bufferSize                         = internalState.m_bufferSize;
        m_echoBufferSize                     = internalState.m_echoBufferSize;
        m_atEnd                              = internalState.m_atEnd;
        m_ignoreStartTime                    = internalState.m_ignoreStartTime;
        m_pingPongBackward                   = internalState.m_pingPongBackward;
    }

} }
