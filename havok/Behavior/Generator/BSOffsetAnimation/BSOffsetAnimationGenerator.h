/// @file
/// @brief BSOffsetAnimationGenerator: Bethesda's generator that layers one frame of an additive clip, picked by a
///        variable, over its default generator.
/// @note The path is the exe's (Generator\BSOffsetAnimation\BSOffsetAnimationGenerator.cpp).
#pragma once

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

namespace CB { namespace havok
{
    /// @brief Plays m_pDefaultGenerator and multiplies, onto each of its sampled bones, that bone's offset from a frame
    ///        of m_pOffsetClipGenerator's (additive) animation: m_fOffsetVariable / (m_fOffsetRangeEnd -
    ///        m_fOffsetRangeStart), clamped to [0, 1], picks the frame.
    /// @note Member names are the class's reflected names (the last four reflected serialize-ignored). The offset clip is
    ///       never activated or generated: its animation is loaded and sampled through its binding index. m_userData
    ///       tracks that load: 8 = waiting for the animation, 0xc = loaded (0 before activate). The destructor is
    ///       implicit (out of line: the scalar deleting destructor calls it).
    class BSOffsetAnimationGenerator : public hkbGenerator
    {
    public:
        hkUint8                m_pad48[8];               ///< +0x48 — unidentified, not reflected
        hkbGenerator*          m_pDefaultGenerator;      ///< +0x50
        hkUint8                m_pad58[8];               ///< +0x58
        /// @note Reflected as an hkbGenerator; only an hkbClipGenerator (that exact class) is used.
        hkbGenerator*          m_pOffsetClipGenerator;   ///< +0x60
        hkReal                 m_fOffsetVariable;        ///< +0x68
        hkReal                 m_fOffsetRangeStart;      ///< +0x6c
        hkReal                 m_fOffsetRangeEnd;        ///< +0x70
        hkUint32               m_pad74;
        /// @brief Every transform track of the offset animation at the current frame.
        hkArray<hkQsTransform> m_BoneOffsetA;            ///< +0x78
        /// @brief The bone each m_BoneOffsetA entry offsets, -1 for an identity offset.
        hkArray<hkInt16>       m_BoneIndexA;             ///< +0x88
        hkReal                 m_fCurrentPercentage;     ///< +0x98
        /// @note Unsigned compares: -1 means none sampled yet.
        hkUint32               m_iCurrentFrame;          ///< +0x9c
        /// @brief Set when the current frame offsets nothing (or the animation is missing or not additive).
        hkBool                 m_bZeroOffset;            ///< +0xa0
        /// @brief Set once the arrays are sized for the offset animation (cleared by deactivate).
        hkBool                 m_bOffsetValid;           ///< +0xa1
        hkUint8                m_padA2[14];              ///< +0xa2 — unidentified, not reflected

        BSOffsetAnimationGenerator();
        /// @note Resets the runtime members and the m_userData load state; the arrays are not serialized.
        BSOffsetAnimationGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: settings and children shared, runtime members reset.
        BSOffsetAnimationGenerator(const BSOffsetAnimationGenerator& gen);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        /// @brief Loads the offset animation (m_userData 0xc) and samples it, or leaves the load pending (8).
        virtual void        activate(const hkbContext& context);                            ///< slot 4
        /// @brief Finishes a pending load, then samples the offset again.
        virtual void        update(const hkbContext& context, hkReal timestep);             ///< slot 5
        /// @brief Unloads a loaded offset animation and empties the arrays.
        virtual void        deactivate(const hkbContext& context);                          ///< slot 7
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                      ///< slot 8
        /// @brief The default generator (reported active), then the offset clip unless computeNumChildren leaves it out.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        /// @brief Needs both generators, the offset one an hkbClipGenerator, and a range whose end is past its start.
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        /// @brief The default generator's output (when in another buffer), with the offsets applied.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                    ///< slot 23
        virtual bool        canRecycleOutput() const;                                       ///< slot 24
        /// @brief The default generator's sync info, unmirrored; hkbGenerator's without one.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 25

        /// @brief The offset clip when it is exactly an hkbClipGenerator, else null.
        HK_FORCE_INLINE hkbClipGenerator* getOffsetClipGenerator() const
        {
            return (m_pOffsetClipGenerator && m_pOffsetClipGenerator->getClassType() == &hkbClipGenerator::staticClass())
                       ? static_cast<hkbClipGenerator*>(m_pOffsetClipGenerator)
                       : HK_NULL;
        }

        /// @brief Picks the frame from the offset variable and, when it changed, samples the offsets at it.
        /// @note Name provisional. A missing or non-additive animation sets m_bZeroOffset (the latter with a warning).
        void   sampleOffset(const hkbContext& context);
        /// @brief Samples every transform track at `time` into m_BoneOffsetA and maps each non-identity one to its bone.
        /// @return Whether any track offsets its bone.
        /// @note Name provisional.
        hkBool sampleBoneOffsets(const hkaAnimationBinding* binding, const hkaAnimation* animation, hkReal time);
        /// @brief Two children, or only the default generator for a clone or when only active children are asked for.
        /// @note Name provisional: getMaxNumChildren tail-calls it, getChildren calls it.
        int    computeNumChildren(GetChildrenFlags flags);
    };
    static_assert(sizeof(BSOffsetAnimationGenerator) == 0xb0, "BSOffsetAnimationGenerator size");
} }
