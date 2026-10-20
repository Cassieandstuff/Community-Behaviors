/// @file
/// @brief BSCyclicBlendTransitionGenerator: Bethesda's generator that freezes or cross-blends the blend parameter of a
///        cyclic parametric blender.
/// @note The path is the exe's (Generator\BSCyclicBlendTransition\BSCyclicBlendTransitionGenerator.cpp).
#pragma once

#include <Behavior/Generator/Blender/hkbBlenderGenerator.h>
#include <Behavior/TransitionEffect/Blending/hkbBlendingTransitionEffect.h>

namespace CB { namespace havok
{
    /// @brief Plays m_pBlenderGenerator with m_fBlendParameter pushed into its clone every update. m_EventToFreezeBlendValue
    ///        stops that push; m_EventToCrossBlend then blends, over m_fTransitionDuration, from a frozen copy of the
    ///        blender (m_pTransitionBlenderGenerator) back to the live one through m_pTransitionEffect.
    /// @note Member names are the class's reflected names (the last three serialize-ignored). The copy and the effect are
    ///       made in activate and released in deactivate (raw pointers). The destructor is implicit (vanilla's scalar
    ///       deleting destructor inlines it).
    class BSCyclicBlendTransitionGenerator : public hkbGenerator
    {
    public:
        /// @note Names provisional (CommonLib's, after activeragdoll).
        enum Mode
        {
            MODE_INACTIVE          = -1,
            MODE_NONE              = 0,
            MODE_FREEZE            = 1,
            /// @brief The cross blend is running: the effect generates.
            MODE_CROSS_BLEND       = 2,
            /// @brief Set by the cross-blend event; the next update turns it into MODE_CROSS_BLEND.
            MODE_CROSS_BLEND_START = 3,
        };

        hkUint8                      m_pad48[8];                      ///< +0x48 — unidentified, not reflected
        /// @note Reflected as an hkbGenerator; only an hkbBlenderGenerator (that exact class) is used.
        hkbGenerator*                m_pBlenderGenerator;             ///< +0x50
        hkbEventProperty             m_EventToFreezeBlendValue;       ///< +0x58
        hkbEventProperty             m_EventToCrossBlend;             ///< +0x68
        hkReal                       m_fBlendParameter;               ///< +0x78
        hkReal                       m_fTransitionDuration;           ///< +0x7c — seconds
        hkInt8                       m_eBlendCurve;                   ///< +0x80 — BlendCurve
        hkUint8                      m_pad81[0x90 - 0x81];
        hkbBlenderGenerator*         m_pTransitionBlenderGenerator;   ///< +0x90
        hkUint8                      m_pad98[8];
        hkbBlendingTransitionEffect* m_pTransitionEffect;             ///< +0xa0
        hkInt8                       m_currentMode;                   ///< +0xa8 — Mode
        hkUint8                      m_padA9[7];

        BSCyclicBlendTransitionGenerator();
        BSCyclicBlendTransitionGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: settings and blender shared, runtime members reset.
        BSCyclicBlendTransitionGenerator(const BSCyclicBlendTransitionGenerator& gen);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        /// @brief Clones the blender into the frozen copy, makes the (synchronized) blending effect and pushes the blend
        ///        parameter into the blender's clone.
        virtual void        activate(const hkbContext& context);                            ///< slot 4
        /// @brief Pushes the blend parameter (unless frozen) and runs the cross blend, back to MODE_NONE once it is done.
        virtual void        update(const hkbContext& context, hkReal timestep);             ///< slot 5
        /// @brief The freeze event (from MODE_NONE) freezes the blend; the cross-blend event (in any other mode) gives
        ///        the frozen copy the live blend parameter and starts the cross blend, or reverses one already running.
        virtual void        handleEvent(const hkbContext& context, const hkbEvent e);       ///< slot 6
        /// @brief Releases the frozen copy and the effect.
        virtual void        deactivate(const hkbContext& context);                          ///< slot 7
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                      ///< slot 8
        /// @brief The blender, plus the frozen copy while cross-blending; active exactly when active children are asked for.
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        /// @brief The effect's output while cross-blending, else the blender's.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                    ///< slot 23
        virtual bool        canRecycleOutput() const;                                       ///< slot 24
        /// @brief The effect's sync info while cross-blending, else the blender's (unmirrored), else hkbGenerator's.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 25

        /// @brief The root graph's clone of `generator` when it is exactly an hkbBlenderGenerator, else null.
        /// @note Name provisional.
        hkbBlenderGenerator* getBlenderClone(const hkbContext& context, hkbGenerator* generator);
        /// @brief The blender, plus the frozen copy while cross-blending.
        /// @note Name provisional: getMaxNumChildren tail-calls it, getChildren calls it.
        int                  computeNumChildren();
    };
    static_assert(sizeof(BSCyclicBlendTransitionGenerator) == 0xb0, "BSCyclicBlendTransitionGenerator size");
} }
