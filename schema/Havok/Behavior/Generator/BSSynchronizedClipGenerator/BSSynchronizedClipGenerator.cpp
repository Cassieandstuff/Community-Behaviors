/// @file
/// @brief BSSynchronizedClipGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>

namespace CB { namespace schema
{
    /// @note m_synchronizedScene is cleared here, after construction, not in the constructor: that order is vanilla's.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing)
    {
        hkFinishLoadedObjectFlag flag;
        flag.m_finishing               = finishing;
        BSSynchronizedClipGenerator* o = new (p) BSSynchronizedClipGenerator(flag);
        o->m_synchronizedScene         = 0;
        return o;
    }
} }
