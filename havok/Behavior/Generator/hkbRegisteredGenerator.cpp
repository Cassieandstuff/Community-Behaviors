/// @file
/// @brief hkbRegisteredGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/hkbRegisteredGenerator.h>

namespace CB { namespace havok
{
    hkbRegisteredGenerator::hkbRegisteredGenerator(hkFinishLoadedObjectFlag flag) : hkbBindable(flag), m_generator(flag)
    {
    }
} }
