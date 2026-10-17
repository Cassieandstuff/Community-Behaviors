/// @file
/// @brief BSBoneSwitchGenerator: Bethesda's generator that takes chosen bones from other generators over a default one.
/// @note The folder is inferred from the exe's Generator\BSCyclicBlendTransition\ and Generator\BSOffsetAnimation\
///       paths; this class's own path is not in the exe.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbBoneWeightArray.h>

namespace CB { namespace schema
{
    /// @brief One switched child: a generator and the bones it supplies (those with a positive weight).
    /// @note Member names are the class's reflected names. The destructor is implicit (the scalar deleting destructor
    ///       stamps no vtable): it releases only the bone weights — the generator reference a clone took is released
    ///       by BSBoneSwitchGenerator's destructor.
    class BSBoneSwitchGeneratorBoneData : public hkbBindable
    {
    public:
        hkbGenerator*                m_pGenerator;     ///< +0x30
        hkRefPtr<hkbBoneWeightArray> m_spBoneWeight;   ///< +0x38

        BSBoneSwitchGeneratorBoneData(hkFinishLoadedObjectFlag flag);
        /// @brief The clone copy: shares the generator (with a reference) and the bone weights unless they are bound
        ///        to variables, which get a copy of their own.
        BSBoneSwitchGeneratorBoneData(const BSBoneSwitchGeneratorBoneData& other);

        /// @brief The child, then its bone weights.
        virtual void collectBindables(hkbBindableCollector& collector);   ///< slot 3
    };
    static_assert(sizeof(BSBoneSwitchGeneratorBoneData) == 0x40, "BSBoneSwitchGeneratorBoneData size");

    /// @brief Plays m_pDefaultGenerator, then overwrites the bones each child weights positively with that child's pose.
    /// @note Member names are the class's reflected names. Every constructor sets bit 0 of m_userData.
    class BSBoneSwitchGenerator : public hkbGenerator
    {
    public:
        hkUint8                                m_pad48[8];            ///< +0x48 — unidentified, not reflected
        /// @note Shared by clones, never referenced.
        hkbGenerator*                          m_pDefaultGenerator;   ///< +0x50
        /// @note A clone owns its children (each a copy holding a reference to its generator).
        hkArray<BSBoneSwitchGeneratorBoneData*> m_ChildrenA;          ///< +0x58
        hkUint8                                m_pad68[8];            ///< +0x68

        BSBoneSwitchGenerator();
        BSBoneSwitchGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: the default generator shared, every child copied.
        BSBoneSwitchGenerator(const BSBoneSwitchGenerator& gen);
        /// @brief A heap-allocated generator (m_memSizeAndFlags non-zero: a clone, not a packfile object) releases its
        ///        children and their generators.
        virtual ~BSBoneSwitchGenerator();                                                   ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        /// @brief This node, then every child's bindables.
        virtual void        collectBindables(hkbBindableCollector& collector);              ///< slot 3
        /// @brief One per child, plus the default generator when it is set.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                      ///< slot 8
        /// @brief The default generator (when set) then every child's generator, none reported active.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        /// @brief Needs a default generator and at least one child; every child needs a generator and bone weights of
        ///        one common size, and no bone may be weighted by two children.
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        /// @brief The default generator's output (child 0), with each child's bones written over it.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                    ///< slot 23
        virtual bool        canRecycleOutput() const;                                       ///< slot 24
    };
    static_assert(sizeof(BSBoneSwitchGenerator) == 0x70, "BSBoneSwitchGenerator size");
} }
