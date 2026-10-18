/// @file
/// @brief bhkCharProxyController functions still being matched against vanilla (AE 1.6.1170) (see
///        tools/cbmatch/parked.yaml); the constructor and Init are parked in the schema unit as its drivers.
/// @note Bethesda's engine code: built with C++ exceptions on and /Ob2, as the schema unit.
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyControllerCinfo.h>
#include <Physics/Utilities/CharacterControl/CharacterProxy/hkpCharacterProxy.h>

namespace CB { namespace schema
{
    bhkCharProxyController::~bhkCharProxyController()
    {
        {
            BSSpinLockGuard guard(s_controllersLock);
            bhkCharProxyController* self = this;
            s_controllers.RemoveFast(self);
        }
        if (IsInWorld()) m_characterProxy.RemoveListener(this);
    }

    void bhkCharProxyController::GetControllers(BSScrapArray<NiPointer<bhkCharProxyController> >& controllers)
    {
        BSSpinLockGuard guard(s_controllersLock);
        controllers.Clear();
        controllers.Resize(s_controllers.GetSize());
        for (hkUint32 i = 0; i < s_controllers.GetSize(); ++i) controllers[i] = s_controllers[i];
    }
} }
