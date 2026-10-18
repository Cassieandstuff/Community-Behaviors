/// @file
/// @brief hkpCharacterProxy functions being matched against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>

namespace CB { namespace schema
{
    void hkpCharacterProxy::removeCharacterProxyListener(hkpCharacterProxyListener* listener)
    {
        const int i = m_listeners.indexOf(listener);
        m_listeners.removeAt(i);
    }
} }
