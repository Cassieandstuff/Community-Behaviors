/// @file
/// @brief hkbHandIkDriver, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/HandIk/hkbHandIkDriver.h>

namespace CB { namespace schema
{
    hkbHandIkDriver::hkbHandIkDriver()
    :   m_isSetUp(false)
    {
    }

    void hkbHandIkDriver::reset()
    {
        m_isSetUp = false;
    }
} }
