/// @file
/// @brief hkbBlendingTransitionEffect: the transition that cross-fades the from-generator into the to-generator.
/// @note Declared only as far as BSCyclicBlendTransitionGenerator uses it; the class itself is not reimplemented
///       here (its functions sit at 0x140b01200-0x140b02210). The folder follows Havok's TransitionEffect\ layout
///       (inferred; the exe names only TransitionEffect\Generator\).
#pragma once

#include <Behavior/TransitionEffect/hkbTransitionEffect.h>

namespace CB { namespace schema
{
    /// @note Member names are the class's reflected names (the last four runtime-only).
    class hkbBlendingTransitionEffect : public hkbTransitionEffect
    {
    public:
        enum FlagBits { FLAG_NONE = 0x0, FLAG_IGNORE_FROM_WORLD_FROM_MODEL = 0x1, FLAG_SYNC = 0x2, FLAG_IGNORE_TO_WORLD_FROM_MODEL = 0x4 };

        hkReal                 m_duration;                               ///< +0x50 — seconds
        hkReal                 m_toGeneratorStartTimeFraction;           ///< +0x54
        hkUint16               m_flags;                                  ///< +0x58 — FlagBits
        hkInt8                 m_endMode;                                ///< +0x5a
        hkInt8                 m_blendCurve;                             ///< +0x5b
        hkUint32               m_pad5c;
        hkbGenerator*          m_fromGenerator;                          ///< +0x60
        hkbGenerator*          m_toGenerator;                            ///< +0x68
        hkArray<hkQsTransform> m_characterPoseAtBeginningOfTransition;   ///< +0x70
        hkReal                 m_timeRemaining;                          ///< +0x80
        hkReal                 m_timeInTransition;                       ///< +0x84
        hkBool                 m_applySelfTransition;                    ///< +0x88
        hkBool                 m_initializeCharacterPose;                ///< +0x89

        /// @note Out of line in vanilla (0x140b01200).
        hkbBlendingTransitionEffect();

        virtual void   generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                hkReal timeOffset);                     ///< slot 23
        virtual bool   isDone();                                        ///< slot 28
        virtual void   setFromGenerator(hkbGenerator* fromGen);         ///< slot 29
        virtual void   setToGenerator(hkbGenerator* toGen);             ///< slot 30
        virtual hkReal getFromGeneratorBlendOutTime();                  ///< slot 31
        virtual hkReal getToGeneratorBlendInTime();                     ///< slot 32

        /// @brief The effect's update, advancing the sync timeline of `syncNode`'s active-node record.
        /// @note Name provisional (0x140b01c20): update (slot 5) passes the effect itself; BSCyclicBlendTransitionGenerator
        ///       passes itself, so the effect drives that node's timeline.
        void updateInternal(const hkbContext& context, hkReal timestep, hkbNode* syncNode);
        /// @brief The effect's updateSync, written into `syncNode`'s active-node record.
        /// @note Name provisional (0x140b016e0): updateSync (slot 25) passes the effect itself.
        void updateSyncInternal(const hkbContext& context, hkbNode* syncNode);
    };
    static_assert(sizeof(hkbBlendingTransitionEffect) == 0x90, "hkbBlendingTransitionEffect size");
} }
