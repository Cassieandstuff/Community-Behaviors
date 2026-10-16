/// @file
/// @brief hkSweptTransform: a body's motion over one physics step, as two centre-of-mass/rotation key frames.
#pragma once

#include <Common/Base/Math/hkMath.h>

namespace CB { namespace schema
{
    /// @note 0x50 bytes. m_centerOfMass0.w holds the start time, m_centerOfMass1.w the inverse step length.
    class hkSweptTransform
    {
    public:
        hkVector4    m_centerOfMass0;       ///< +0x00
        hkVector4    m_centerOfMass1;       ///< +0x10
        hkQuaternion m_rotation0;           ///< +0x20
        hkQuaternion m_rotation1;           ///< +0x30
        hkVector4    m_centerOfMassLocal;   ///< +0x40
    };
} }
