/// @file
/// @brief hkbReferencePoseGenerator: outputs a skeleton's reference pose.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    /// @note Stateless: cloneNode shares the template instead of copying it. The destructor is implicit (vanilla's
    ///       scalar deleting destructor, slot 0, inlines it).
    class hkbReferencePoseGenerator : public hkbGenerator
    {
    public:
        /// @note Not reflected for serialization (null when loaded): unset, generate falls back to the character's
        ///       animation skeleton. Raw: neither the constructor nor the destructor touch its reference count.
        const hkaSkeleton* m_skeleton;   ///< +0x48

        hkbReferencePoseGenerator();
        explicit hkbReferencePoseGenerator(const hkaSkeleton* skeleton);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                            ///< slot 1
        /// @note Marks this node CLONE_STATE_SHARABLE and returns it with a new reference.
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                 ///< slot 12
        /// @brief The skeleton's reference pose (every partition marked sampled), the character's world transform, and
        ///        an identity extracted motion.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                     ///< slot 23
    };
    static_assert(sizeof(hkbReferencePoseGenerator) == 0x50, "hkbReferencePoseGenerator size");
} }
