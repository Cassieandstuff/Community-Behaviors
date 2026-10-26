/// @file
/// @brief BSSynchronizedClipGenerator: Bethesda's clip generator for paired (synchronized) animations.
/// @note Bethesda's own behavior classes live in Havok's Behavior tree (the exe's strings place
///       BSCyclicBlendTransitionGenerator and BSOffsetAnimationGenerator under Generator\).
#pragma once

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

namespace CB { namespace havok
{
    struct hkaAnimationBinding;
    struct hkaSkeleton;

    /// @brief Plays one role of a paired animation: drives a wrapped hkbClipGenerator in step with the other
    ///        participants of a synchronized scene, after walking its character onto the scene's mark.
    /// @note Member names are the class's reflected names (hkClass); the runtime-only ones are reflected too, flagged
    ///       serialize-ignored. The role is picked by m_SyncAnimPrefix: the animation's annotation tracks and events named
    ///       with that prefix belong to this character.
    class BSSynchronizedClipGenerator : public hkbGenerator
    {
    public:
        /// @brief The scene the participants share. The game installs its own (CommonLib names it
        ///        BGSSynchronizedAnimationInstance); activate falls back to hkbDefaultSynchronizedScene when none is set.
        /// @note Both nested class names are the exe's RTTI. Virtual names provisional, inferred from what the clip
        ///       generator does with them.
        class hkbSynchronizedAnimationScene : public hkReferencedObject
        {
        public:
            virtual bool   areAllCharactersInScene();                  ///< slot 3
            virtual bool   areAllCharactersAtMarks();                  ///< slot 4
            /// @brief The scene's time: generate plays the clip at it.
            virtual hkReal getTime();                                  ///< slot 5
            /// @brief generate hands the scene its own time offset first.
            virtual void   setTime(hkReal time);                       ///< slot 6
            /// @brief Runs at the end of every deactivate.
            virtual void   onClipDeactivated();                        ///< slot 7
            /// @brief Runs at the end of every activate.
            virtual void   onClipActivated();                          ///< slot 8
            /// @brief deactivate reports a character whose clip stopped before 20% of the animation.
            virtual void   onCharacterInterrupted(hkbCharacter* character);   ///< slot 9
        };

        /// @note Every virtual is a no-op, returns true, or gets/sets m_time.
        class hkbDefaultSynchronizedScene : public hkbSynchronizedAnimationScene
        {
        public:
            hkReal   m_time;    ///< +0x10 — name provisional; slots 5 and 6 get and set it
            hkUint32 m_pad14;

            virtual bool   areAllCharactersInScene();
            virtual bool   areAllCharactersAtMarks();
            virtual hkReal getTime();
            virtual void   setTime(hkReal time);
            virtual void   onClipDeactivated();
            virtual void   onClipActivated();
            virtual void   onCharacterInterrupted(hkbCharacter* character);
        };

        hkUint8                                 m_pad48[8];                        ///< +0x48 — unidentified
        /// @note Reflected as an hkbGenerator; isValid only accepts an hkbClipGenerator (or a subclass), and every
        ///       other member reads it as one. A clone owns a reference to its cloned clip; a template's is the graph's.
        hkbClipGenerator*                       m_pClipGenerator;                  ///< +0x50
        /// @note A plain string (reflected `cstring`), not an hkStringPtr: vanilla reads it unmasked and never frees it.
        ///       Null means the role "NPC" (g_bssyncEmptyString), never "": the game's paired-idle start fails a role
        ///       whose track it cannot find.
        char*                                   m_SyncAnimPrefix;                  ///< +0x58
        hkBool                                  m_bSyncClipIgnoreMarkPlacement;    ///< +0x60
        hkReal                                  m_fGetToMarkTime;                  ///< +0x64 — seconds
        hkReal                                  m_fMarkErrorThreshold;             ///< +0x68
        hkBool                                  m_bLeadCharacter;                  ///< +0x6c
        hkBool                                  m_bReorientSupportChar;            ///< +0x6d
        hkBool                                  m_bApplyMotionFromRoot;            ///< +0x6e
        hkRefPtr<hkbSynchronizedAnimationScene> m_pSyncScene;                      ///< +0x70 — runtime only, cleared on load
        hkUint8                                 m_pad78[8];                        ///< +0x78
        hkQsTransform                           m_StartMarkWS;                     ///< +0x80 — the character's world transform at activate
        hkQsTransform                           m_EndMarkWS;                       ///< +0xb0
        hkQsTransform                           m_StartMarkMS;                     ///< +0xe0 — the role's mark, from the animation (identity without one)
        hkReal                                  m_fCurrentLerp;                    ///< +0x110 — 0 → 1 over m_fGetToMarkTime
        hkUint32                                m_pad114;
        /// @note Owned (the destructor deletes it), not reference-counted: the binding activate builds when there is no
        ///       appended scene slot.
        hkaAnimationBinding*                    m_pLocalSyncBinding;               ///< +0x118
        /// @note Owned; maps a prefixed event's id to its unprefixed partner's (buildEventMap).
        hkPointerMap<int, int>*                 m_pEventMap;                       ///< +0x120
        hkInt16                                 m_sAnimationBindingIndex;          ///< +0x128 — the appended scene slot, -1 for none
        hkBool                                  m_bAtMark;                         ///< +0x12a
        hkBool                                  m_bAllCharactersInScene;           ///< +0x12b
        hkBool                                  m_bAllCharactersAtMarks;           ///< +0x12c

        HK_FORCE_INLINE BSSynchronizedClipGenerator(hkFinishLoadedObjectFlag flag) : hkbGenerator(flag), m_pSyncScene(flag) {}
        /// @brief The cloneNode copy: settings and marks from `gen`, its clip generator cloned into `rootBehavior`.
        BSSynchronizedClipGenerator(const BSSynchronizedClipGenerator& gen, hkbBehaviorGraph& rootBehavior);
        /// @brief Deletes the local binding and the event map, releases the scene, and releases the wrapped clip when it
        ///        is a clone this node made.
        virtual ~BSSynchronizedClipGenerator();                                             ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                           ///< slot 1
        /// @brief Builds the event map and (without a binding index) the local sync binding, queues and activates the
        ///        clip, takes the role's start mark and the character's transform, and tells the scene.
        virtual void        activate(const hkbContext& context);                            ///< slot 4
        /// @brief Activates the clip once its animation loads, steps it only while every participant is at its mark,
        ///        re-raises the clip's prefixed events unprefixed, and advances the walk onto the mark.
        virtual void        update(const hkbContext& context, hkReal timestep);             ///< slot 5
        /// @brief Deactivates and unloads the clip (with m_userData cleared for the call), reporting an early stop to
        ///        the scene, then tells the scene.
        virtual void        deactivate(const hkbContext& context);                          ///< slot 7
        /// @brief One child (the clip), none for a clone or when only active children are asked for.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                      ///< slot 8
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);   ///< slot 9
        /// @brief The clip must be an hkbClipGenerator, and one of its animation's annotation tracks must start with the
        ///        role prefix.
        /// @note Returns whether the clip is valid only: a missing role leaves its message in `errorString` but still
        ///       returns true. With no clip, both messages are written (the role message last).
        virtual bool        isValid(hkStringPtr& errorString) const;                        ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                ///< slot 12
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                    ///< slot 23
        virtual bool        canRecycleOutput() const;                                       ///< slot 24
        /// @note Same body as hkbGenerator's.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);       ///< slot 25

        /// @brief Samples the role's annotation track (named m_SyncAnimPrefix) at time 0 of the clip's animation into
        ///        `markOut`, queueing and releasing the clip through the clip loader when its animation is not bound yet.
        /// @return Whether the track was found.
        /// @note Name provisional; activate passes m_StartMarkMS.
        bool sampleSyncMark(const hkbContext& context, hkQsTransform& markOut);
        /// @brief Builds m_pEventMap from the graph's event names: "PairEnd" maps to itself, `<prefix>X` to X.
        /// @note Name provisional.
        void buildEventMap(const hkbBehaviorGraphStringData* stringData);
        /// @brief Takes the events raised since `startIndex` off `queue` and re-raises the mapped ones, dropping the rest.
        /// @note Name provisional.
        void remapEvents(int startIndex, hkbEventQueue* queue);
        /// @brief Activates `clip` with this node's binding index swapped in (restored after), then binds the local
        ///        sync binding into its control when there is no index.
        /// @note Name provisional.
        void activateClipGenerator(const hkbContext& context, hkbClipGenerator* clip);
        /// @brief A binding of `binding`'s animation to `skeleton`, through the bones its annotation tracks name after
        ///        `prefix`.
        /// @note Name provisional; vanilla's lives beside BSSynchronizedClipGeneratorUtils::generate (0x140b9fe20).
        static hkaAnimationBinding* createLocalSyncBinding(const hkaSkeleton* skeleton, const hkaAnimationBinding* binding, const char* prefix);
    };
    static_assert(sizeof(BSSynchronizedClipGenerator) == 0x130, "BSSynchronizedClipGenerator size");
    static_assert(sizeof(BSSynchronizedClipGenerator::hkbDefaultSynchronizedScene) == 0x18, "hkbDefaultSynchronizedScene size");

    /// @brief The packfile finish function: constructs a loaded object in place and clears its runtime pointer.
    /// @return `p`, as the constructed object.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing);
} }
