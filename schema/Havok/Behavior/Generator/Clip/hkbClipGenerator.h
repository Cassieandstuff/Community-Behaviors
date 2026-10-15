/// @file
/// @brief hkbClipGenerator: plays one animation clip (single-play, looping, user-controlled or ping-pong), raises its
///        triggers and extracts its root motion.
#pragma once

#include <Animation/Playback/Control/Default/hkaDefaultAnimationControl.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/Clip/hkbClipTriggerArray.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    class hkbClipGenerator;
    extern const hkClass hkbClipGeneratorClass;

    /// @brief Bethesda's clip loader (AnimationFileManagerSingleton), which activate/update/deactivate call into.
    struct IAnimationClipLoader
    {
        virtual ~IAnimationClipLoader();
        virtual int  queue(const hkbContext& context, hkbClipGenerator* clip, void* sync);    ///< slot 1
        virtual bool load(const hkbContext& context, hkbClipGenerator* clip, void* sync);     ///< slot 2
        virtual int  unload(const hkbContext& context, hkbClipGenerator* clip, void* sync);   ///< slot 3
    };
    extern IAnimationClipLoader* g_animationClipLoader;

    /// @brief Mirrors the pose in `output` (generate's FLAG_MIRROR_POSE path).
    /// @note Name provisional.
    void hkbMirrorPose(hkbGeneratorOutput& output, const hkQsTransform* referencePose);

    /// @brief Scatters sampled transform tracks into the pose through `trackToBone` (empty = identity), skipping bones
    ///        at or past `numBones`; with `mirror`, onto each bone's mirror partner (mirroring the transform too when
    ///        `mirrorTransforms`). Marks every bone written in `bonesSampled`, and bit `numBones` when all were.
    /// @note Name and parameter names provisional (0x140b03380, outside the clip's own TU).
    void hkbCopyTracksToPose(int numTracks, const hkQsTransform* tracks, int numBones, hkQsTransform* poseOut, hkBitField& bonesSampled,
                             const hkArray<hkInt16>& trackToBone, bool additive, const hkaMirroredSkeleton* mirror, bool mirrorTransforms);
    /// @brief Scatters sampled float tracks into the float slots through the binding's float-track-to-slot table.
    /// @note Name provisional (0x140b03550, outside the clip's own TU).
    void hkbCopyFloatTracksToSlots(int numFloatTracks, const hkReal* floatTracks, int numFloatSlots, const hkaAnimationBinding* binding,
                                   hkReal* floatSlotsOut);
    /// @brief Adds `weight` × each sampled float track to its slot; a slot still at HK_REAL_MAX (unset) is replaced.
    /// @note Name provisional (0x140b036a0, outside the clip's own TU).
    void hkbBlendFloatTracksToSlots(int numFloatTracks, const hkReal* floatTracks, int numFloatSlots, const hkaAnimationBinding* binding,
                                    hkReal weight, hkReal* floatSlotsInOut);

    /// @note The `slot N` annotations are vanilla's vtable slots. The declared virtuals reproduce vanilla's order only
    ///       through slot 7 (hkbNode); the rest of the vtable is not declared yet, so slots 8+ are not this class's own.
    class hkbClipGenerator : public hkbGenerator
    {
    public:
        /// @note The schema names bit 0x8 FLAG_FORCE_DENSE_POSE, but generate mirrors the pose under it; its meaning is
        ///       unverified, hence FLAG_MIRROR_POSE. 0x4 (FLAG_MIRROR) mirrors the extracted motion, and sampleAnimation
        ///       scatters the sampled tracks through the mirrored skeleton under it.
        enum
        {
            FLAG_CONTINUE_MOTION_AT_END = 0x1,
            FLAG_SYNC_HALF_CYCLE_IN_PING_PONG_MODE = 0x2,
            FLAG_MIRROR                 = 0x4,
            FLAG_MIRROR_POSE            = 0x8,
            FLAG_DONT_MERGE_ANNOTATIONS = 0x10,
            FLAG_IGNORE_MOTION          = 0x20,
        };
        enum { MODE_SINGLE_PLAY = 0, MODE_LOOPING = 1, MODE_USER_CONTROLLED = 2, MODE_PING_PONG = 3 };

        /// @brief An entry of m_animDatas.
        /// @note 0xe0 bytes; contents not identified yet (the schema has the array as serialize-ignored, untyped).
        struct AnimData
        {
            hkUint8 m_opaque[0xe0];
        };

        /// @note 0x10 bytes.
        struct Echo
        {
            hkReal m_offsetLocalTime;
            hkReal m_weight;
            hkReal m_dwdt;
            hkReal m_pad;
        };

        hkStringPtr                           m_animationName;                        ///< +0x48
        hkRefPtr<hkbClipTriggerArray>         m_triggers;                             ///< +0x50
        hkReal                                m_cropStartAmountLocalTime;             ///< +0x58
        hkReal                                m_cropEndAmountLocalTime;               ///< +0x5c
        hkReal                                m_startTime;                            ///< +0x60
        hkReal                                m_playbackSpeed;                        ///< +0x64
        hkReal                                m_enforcedDuration;                     ///< +0x68
        hkReal                                m_userControlledTimeFraction;           ///< +0x6c
        hkInt16                               m_animationBindingIndex;                ///< +0x70
        hkInt8                                m_mode;                                 ///< +0x72
        hkInt8                                m_flags;                                ///< +0x73
        hkUint32                              m_pad74;                                ///< +0x74
        hkArray<AnimData>                     m_animDatas;                            ///< +0x78
        hkRefPtr<hkaDefaultAnimationControl>  m_animationControl;                     ///< +0x88
        hkRefPtr<hkbClipTriggerArray>         m_originalTriggers;                     ///< +0x90
        hkaDefaultAnimationControlMapperData* m_mapperData;                           ///< +0x98
        hkaAnimationBinding*                  m_binding;                              ///< +0xa0
        hkReferencedObject*                   m_mirroredAnimation;                    ///< +0xa8
        hkQsTransform                         m_extractedMotion;                      ///< +0xb0
        hkArray<Echo>                         m_echos;                                ///< +0xe0
        hkReal                                m_localTime;                            ///< +0xf0
        hkReal                                m_time;                                 ///< +0xf4 — elapsed since activation
        hkReal                                m_previousUserControlledTimeFraction;   ///< +0xf8
        hkInt32                               m_bufferSize;                           ///< +0xfc
        hkInt32                               m_echoBufferSize;                       ///< +0x100
        hkBool                                m_atEnd;                                ///< +0x104
        hkBool                                m_ignoreStartTime;                      ///< +0x105
        hkBool                                m_pingPongBackward;                     ///< +0x106

        /// @brief The cloneNode copy: settings and triggers from `gen`, fresh runtime state, a new animation control.
        hkbClipGenerator(const hkbClipGenerator& gen);
        /// @brief The packfile load: finishes the loaded members and resets the runtime ones (no animation control,
        ///        binding or echos; m_userData 0). m_mapperData and m_mirroredAnimation are left as loaded.
        hkbClipGenerator(hkFinishLoadedObjectFlag flag);
        /// @brief Releases m_mirroredAnimation (a raw reference); the members release the rest.
        virtual ~hkbClipGenerator();                                         ///< slot 0

        static const struct hkClass& staticClass();
        /// @brief How many echos a clip blends at most (2 in vanilla; writable data).
        /// @note Name provisional.
        static hkInt8 s_maxEchos;

        virtual const void* getClassType() const;                                ///< slot 1
        /// @brief The clip's own reflected members, its echo buffer and animation control, then hkbGenerator's members.
        /// @note `cls` is ignored: the clip names its own class, and hkbGenerator's for the base.
        virtual void        calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        virtual void        activate(const hkbContext& context);                 ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual void        deactivate(const hkbContext& context);               ///< slot 7
        virtual bool        isValid(hkStringPtr& errorString) const;             ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
        /// @brief Seeks to `elapsed` seconds of playback: scaled by the effective speed from the start time, wrapped into
        ///        one cycle (two durations for a full ping-pong cycle), mirrored for a negative speed. Sets
        ///        m_ignoreStartTime so the next reset keeps it.
        /// @note Name provisional.
        virtual void        setElapsedTime(hkReal elapsed);                       ///< slot 26
        /// @brief Starts an echo of the current pose: shifts the live echos by the clip's offset, resets the clip, and adds
        ///        a fading echo (weight 1 − total, fading over `echoDuration`) — replacing the lightest one at s_maxEchos.
        ///        An echoDuration of 0 drops every echo.
        /// @note Name provisional.
        virtual void        startEcho(hkReal echoDuration);                       ///< slot 27
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                         ///< slot 23
        /// @brief Reports the clip's timeline for synchronization: frequency and local time in seconds of playback at the
        ///        effective speed, the sync-point triggers (at most MAX_SYNC_POINTS) at the clip's own speed, the
        ///        cyclic/additive/mirrored state.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);   ///< slot 25

        /// @brief The animation's duration minus both crops, never negative; 0.01333 s without an animation.
        /// @note Compiled apart (hkbClipGeneratorOpaque.cpp) so its callers can't see its body: vanilla's callers keep
        ///       values in registers across calls to it, which VS2015 24247 only does for a callee it can't see.
        hkReal computeUsableDuration() const;
        /// @brief playbackSpeed × usable/enforced duration, negated while ping-pong runs backward.
        /// @note Name provisional.
        hkReal computeEffectivePlaybackSpeed() const;
        /// @brief m_startTime rescaled by usable/enforced duration, capped at the usable duration.
        /// @note Name provisional.
        hkReal computeStartTime() const;
        /// @brief Pushes the crops to the control, positions the local time and resets the motion and per-pass state.
        /// @note Name provisional; runs at the end of activate and from update when the clip is at an end.
        void resetAtEnd();
        /// @brief Bubble-sorts m_triggers (stable, early-out) by resolved time — acyclic triggers by their own time, the
        ///        rest end-relative-resolved and clamped into the enforced duration, else the usable one.
        /// @note Name provisional.
        void sortTriggers();
        /// @brief Clears m_ignoreStartTime.
        /// @note Name unknown.
        void fn_140ad11e0();
        /// @brief At an end, re-derives m_atEnd from the effective speed (still at an end in the direction of travel).
        /// @note Name unknown.
        void fn_140ad11f0();
        /// @brief One step's previous and new local time, wrapped loop count and end state.
        /// @note Name provisional.
        void advanceLocalTime(hkReal timestep, hkReal& prevLocalTime, hkReal& localTime, int& loops, bool& atEnd);
        void getExtractedMotionDelta(const hkbContext& context, hkReal prevLocalTime, hkReal localTime, int loops, hkQsTransform& out);
        /// @brief Splits a step into per-loop ranges for processTriggersInRange.
        /// @note Name provisional.
        void processTriggers(hkReal prevLocalTime, hkReal localTime, int loops, hkbEventQueue* queue, const hkbContext& context);
        /// @brief Fires the non-acyclic triggers one pass crosses; the end is inclusive only when `atEnd`.
        /// @note Name provisional.
        void processTriggersInRange(hkReal fromTime, hkReal toTime, bool atEnd, hkbEventQueue* queue, const hkbContext& context);
        /// @brief Fires the acyclic triggers whose time falls in [from, to) of the elapsed clip time.
        /// @note Name provisional.
        void processAcyclicTriggers(hkReal fromTime, hkReal toTime, hkbEventQueue* queue, const hkbContext& context);
        void addTrigger(hkReal localTime, const hkbEvent& event, bool relativeToEndOfClip, bool acyclic, bool isAnnotation);
        /// @brief Samples the clip into `poseLocal` and the float slots: at m_localTime, or with echos a blend of every
        ///        echo's time (its offset ahead of the clip) with the clip itself taking the remaining weight. Retargets
        ///        through m_mapperData, mirrors under FLAG_MIRROR, and limits the tracks to the character's LOD.
        void sampleAnimation(const hkbContext& context, hkQsTransform* poseLocal, int numPoseLocal, hkReal* floatSlots, int numFloatSlots,
                             void* animationCache, hkBitField& partitionsSampled);

        void setAnimationControl(hkaDefaultAnimationControl* animationControl);
        /// @brief Takes the binding at m_animationBindingIndex from the character's binding set (null when out of range or
        ///        without a set) into the control and m_binding.
        /// @note Name provisional. No -1 guard: an index of -1 compares below any size and reads the slot before the array.
        void rebindFromBindingSet(const hkbContext& context);
        /// @brief Drops the triggers merged in from the animation's annotations (m_isAnnotation), keeping the others' order.
        /// @note Name provisional.
        void removeAnnotationTriggers();
        /// @note Name provisional. Sets only the clip's time, not the control's.
        void   setLocalTime(hkReal localTime);
        /// @note Name provisional.
        hkReal getLocalTime() const;
    };
    static_assert(sizeof(hkbClipGenerator) == 0x110, "hkbClipGenerator size");
} }
