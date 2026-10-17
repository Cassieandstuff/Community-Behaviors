/// @file
/// @brief BSSynchronizedClipGeneratorUtils: the pose and world-transform work behind BSSynchronizedClipGenerator::generate.
/// @note A file of its own in vanilla: the RTTI names `BSSynchronizedClipGeneratorUtils::FindEventFunctor`, whose
///       vtable function sits among these functions, apart from the class's own (a `…Utils.cpp`, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier). The path is inferred from that one.
#pragma once

#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>

namespace CB { namespace schema
{
    namespace BSSynchronizedClipGeneratorUtils
    {
        /// @brief Generates the synchronized pose: while the characters walk to their marks, the clip's frame 0 with the
        ///        world transform taken from the marks; once all are at their marks, the clip at the scene's time offset,
        ///        with the root bone's motion moved into the world transform when m_bApplyMotionFromRoot is set.
        /// @note Name provisional.
        void generate(BSSynchronizedClipGenerator* gen, const hkbContext& context, const hkaSkeleton* skeleton,
                      const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output, hkReal timeOffset);
        /// @brief Generates the clip; a clip whose animation is still loading (user data 8) leaves the skeleton's
        ///        reference pose instead.
        /// @note Name provisional.
        void generateClip(BSSynchronizedClipGenerator* gen, const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                          hkbGeneratorOutput& output, hkReal timeOffset);
        /// @brief Sets the world transform to `markWS` × the first root bone's pose, and resets that bone to identity.
        /// @note Name provisional.
        void applyMotionFromRoot(const hkaSkeleton* skeleton, hkbGeneratorOutput& output, const hkQsTransform& markWS);
    }
} }
