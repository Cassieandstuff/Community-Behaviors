/// @file
/// @brief BSSynchronizedClipGenerator: Bethesda's clip generator for paired (synchronized) animations.
/// @note Bethesda's own behavior classes live in Havok's Behavior tree (the exe's strings place
///       BSCyclicBlendTransitionGenerator and BSOffsetAnimationGenerator under Generator\).
#pragma once

#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    struct BSSynchronizedClipGenerator : hkbGenerator
    {
        char  m_pad48[0x70 - 0x48];
        void* m_synchronizedScene;   ///< +0x70 — runtime only, cleared on load
        char  m_pad78[0x130 - 0x78];

        HK_FORCE_INLINE BSSynchronizedClipGenerator(hkFinishLoadedObjectFlag flag) : hkbGenerator(flag) {}
    };

    /// @brief The packfile finish function: constructs a loaded object in place and clears its runtime pointer.
    /// @return `p`, as the constructed object.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing);
} }
