/// @file
/// @brief hkbModifierGenerator: a generator that runs a modifier over its child generator's output.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    extern const hkClass hkbModifierGeneratorClass;

    /// @note The `slot N` annotations are vanilla's vtable slots. The destructor is implicit: vanilla's scalar deleting
    ///       destructor (slot 0) stamps no vtable.
    class hkbModifierGenerator : public hkbGenerator
    {
    public:
        hkRefPtr<hkbModifier>  m_modifier;    ///< +0x48
        hkRefPtr<hkbGenerator> m_generator;   ///< +0x50

        hkbModifierGenerator(hkbModifier* modifier = HK_NULL, hkbGenerator* generator = HK_NULL);
        /// @brief The cloneNode copy: shares the child templates.
        hkbModifierGenerator(const hkbModifierGenerator& gen);

        /// @note Out of line among the type-registration getters (0x140ac3620).
        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        /// @brief The generator, then (unless FLAG_GENERATORS_ONLY) the modifier.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        /// @brief Takes the child's output and runs the modifier on it; a predictive generate (non-zero `timeOffset`)
        ///        skips the modifier.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                                     hkbGeneratorOutput& output, hkReal timeOffset);         ///< slot 23
        virtual bool        canRecycleOutput() const;                                       ///< slot 24
        /// @brief Reports the child generator's sync info (never mirrored), then lets a modifier list or tweener adjust it.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 25
    };
} }
