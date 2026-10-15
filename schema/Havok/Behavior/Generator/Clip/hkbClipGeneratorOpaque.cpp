/// @file
/// @brief hkbClipGenerator functions compiled apart from their callers, a matching device rather than a vanilla file.
/// @note Vanilla's resetAtEnd keeps values in registers across computeUsableDuration() calls, which VS2015 24247 only
///       does when the callee's body is invisible to the caller. Yet the exe places computeUsableDuration among the
///       clip's own functions, i.e. in hkbClipGenerator.cpp: vanilla's compiler build simply didn't use the visible
///       body. Compiling it here reproduces that without claiming a file Havok never had.
#include <Behavior/Generator/Clip/hkbClipGenerator.h>

namespace CB { namespace schema
{
    hkReal hkbClipGenerator::computeUsableDuration() const
    {
        const hkaDefaultAnimationControl* control  = m_animationControl;
        const hkReal                      duration = (control->m_binding && control->m_binding->m_animation)
                                                         ? control->m_binding->m_animation->m_duration
                                                         : 0.01333f;
        const hkReal usable = duration - (control->m_cropEndAmountLocalTime + control->m_cropStartAmountLocalTime);
        return (usable > 0.0f) ? usable : 0.0f;
    }
} }
