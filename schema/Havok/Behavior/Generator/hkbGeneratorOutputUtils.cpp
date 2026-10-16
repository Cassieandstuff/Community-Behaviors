/// @file
/// @brief hkbGeneratorOutputUtils, reimplemented against vanilla (AE 1.6.1170) — only the track copies so far.
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>

namespace CB { namespace schema
{
    void hkbGeneratorOutputUtils::copyPaletteTrack(const hkbGeneratorOutput::ConstTrack& source, hkbGeneratorOutput::Track& dest)
    {
        const int numBones = source.m_header->m_capacity;
        hkLocalBuffer<hkReal> weights(numBones);
        blendPaletteTrack(source, 1.0f, dest, &weights[0], true);
    }

    void hkbGeneratorOutputUtils::copySparseTrack(const hkbGeneratorOutput::ConstTrack& source, hkbGeneratorOutput::Track& dest)
    {
        const int numElements = source.m_header->m_capacity;
        hkLocalBuffer<hkReal> weights(numElements);
        blendSparseTrack(source, 1.0f, dest, &weights[0], true);
    }
} }
