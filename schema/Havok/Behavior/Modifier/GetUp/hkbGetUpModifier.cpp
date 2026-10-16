/// @file
/// @brief hkbGetUpModifier, reimplemented against vanilla (AE 1.6.1170) — its internal state's destructor so far.
#include <Behavior/Modifier/GetUp/hkbGetUpModifier.h>

namespace CB { namespace schema
{
    hkbGetUpModifierInternalState::hkbGetUpModifierInternalState(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag)
    {
    }
} }
