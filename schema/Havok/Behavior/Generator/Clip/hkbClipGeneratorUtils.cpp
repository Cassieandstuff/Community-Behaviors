/// @file
/// @brief The hkbClipGenerator functions vanilla compiled in a separate translation unit.
/// @note The split is evidence-driven, and the original file's name is unknown. resetAtEnd keeps values in registers
///       across computeUsableDuration() calls, which MSVC only does when the callee's body is invisible to the caller.
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
