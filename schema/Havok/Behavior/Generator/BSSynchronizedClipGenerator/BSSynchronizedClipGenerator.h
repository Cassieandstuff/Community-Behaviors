/// @file
/// @brief BSSynchronizedClipGenerator: Bethesda's clip generator for paired (synchronized) animations.
/// @note Bethesda's own behavior classes live in Havok's Behavior tree (the exe's strings place
///       BSCyclicBlendTransitionGenerator and BSOffsetAnimationGenerator under Generator\).
#pragma once

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

namespace CB { namespace schema
{
    struct hkaSkeleton;

    /// @note Member names follow the class's reflected names (schema/gen/BSSynchronizedClipGenerator.yaml); the
    ///       unreflected ones are provisional.
    class BSSynchronizedClipGenerator : public hkbGenerator
    {
    public:
        /// @brief The scene the paired characters synchronize through (RTTI
        ///        `BSSynchronizedClipGenerator::hkbSynchronizedAnimationScene`).
        /// @note Only the slots called so far are named; 3 and 4 are placeholders.
        class hkbSynchronizedAnimationScene : public hkReferencedObject
        {
        public:
            virtual void   v3();
            virtual void   v4();
            /// @note Name provisional: generate takes the clip's time offset from it.
            virtual hkReal getSynchronizedTimeOffset();               ///< slot 5
            /// @note Name provisional: generate hands it its own time offset first.
            virtual void   setLocalTimeOffset(hkReal timeOffset);     ///< slot 6
        };

        char                                    m_pad48[0x50 - 0x48];
        hkbClipGenerator*                       m_pClipGenerator;                 ///< +0x50 — owned (a reference) only on clones
        const char*                             m_SyncAnimPrefix;                 ///< +0x58
        hkBool                                  m_bSyncClipIgnoreMarkPlacement;   ///< +0x60
        hkReal                                  m_fGetToMarkTime;                 ///< +0x64
        hkReal                                  m_fMarkErrorThreshold;            ///< +0x68
        hkBool                                  m_bLeadCharacter;                 ///< +0x6c
        hkBool                                  m_bReorientSupportChar;           ///< +0x6d
        hkBool                                  m_bApplyMotionFromRoot;           ///< +0x6e
        hkRefPtr<hkbSynchronizedAnimationScene> m_synchronizedScene;              ///< +0x70 — runtime only, cleared on load
        char                                    m_pad78[0x80 - 0x78];
        hkQsTransform                           m_StartMarkWS;                    ///< +0x80
        hkQsTransform                           m_EndMarkWS;                      ///< +0xb0
        hkQsTransform                           m_StartMarkMS;                    ///< +0xe0
        hkReal                                  m_fCurrentLerp;                   ///< +0x110
        char                                    m_pad114[0x118 - 0x114];
        /// @note Name provisional: the binding activate builds when no scene slot was appended; owned.
        hkaAnimationBinding*                    m_sceneBinding;                   ///< +0x118
        /// @note Name provisional: event id → partner event id (buildEventMap); owned.
        hkPointerMap<int, int>*                 m_eventMap;                       ///< +0x120
        hkInt16                                 m_sAnimationBindingIndex;         ///< +0x128 — the appended scene slot, -1 for none
        hkBool                                  m_bAtMark;                        ///< +0x12a
        hkBool                                  m_bAllCharactersInScene;          ///< +0x12b
        hkBool                                  m_bAllCharactersAtMarks;          ///< +0x12c

        HK_FORCE_INLINE BSSynchronizedClipGenerator(hkFinishLoadedObjectFlag flag) : hkbGenerator(flag), m_synchronizedScene(flag) {}
        /// @brief The cloneNode copy: settings and marks from `gen`, its clip generator cloned into `rootBehavior`.
        BSSynchronizedClipGenerator(const BSSynchronizedClipGenerator& gen, hkbBehaviorGraph& rootBehavior);
        /// @brief Frees the scene binding and the event map, and releases the clip generator when it is a clone's.
        virtual ~BSSynchronizedClipGenerator();   ///< slot 0

        /// @brief Samples the track named m_SyncAnimPrefix at time 0 of the clip's animation into `markOut`, queueing
        ///        and releasing the clip through the clip loader when its animation is not bound yet.
        /// @return Whether the track was found.
        /// @note Name provisional; activate passes m_StartMarkMS.
        bool sampleSyncMark(const hkbContext& context, hkQsTransform& markOut);
        /// @brief Builds m_eventMap from the graph's event names: "PairEnd" maps to itself, `<prefix>X` to X.
        /// @note Name provisional.
        void buildEventMap(const hkbBehaviorGraphStringData* stringData);
        /// @brief Takes the events raised since `startIndex` off `queue` and re-raises the mapped ones, dropping the rest.
        /// @note Name provisional.
        void remapEvents(int startIndex, hkbEventQueue* queue);
        /// @brief Activates `clip` on the appended scene slot when there is one; without one, binds its control to the
        ///        scene binding.
        /// @note Name provisional.
        void activateClipGenerator(const hkbContext& context, hkbClipGenerator* clip);
    };

    /// @brief The packfile finish function: constructs a loaded object in place and clears its runtime pointer.
    /// @return `p`, as the constructed object.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing);
} }
