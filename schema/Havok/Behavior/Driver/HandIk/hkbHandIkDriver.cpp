/// @file
/// @brief hkbHandIkDriver, reimplemented against vanilla (AE 1.6.1170) — only what hkbCharacter calls so far.
#include <Behavior/Driver/HandIk/hkbHandIkDriver.h>

namespace CB { namespace schema
{
    void hkbHandIkDriver::reset()
    {
        m_isSetUp = false;
    }
} }
