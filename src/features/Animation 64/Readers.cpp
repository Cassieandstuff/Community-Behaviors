#include "PCH.h"

#include "features/Animation 64/BindingIndex.h"

#include <platform/classes/ClassRegistry.h>
#include <platform/hooks/hookslib.h>

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGenerator.h>
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>
#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>

/// @file
/// @brief CB's own readers of the clip's binding index, converted to the 64-bit slot ([Animation64] bReaders): every
///        havok/ function that indexes the binding set with it, or gates on it, replaced by a copy that reads
///        BindingIndex() instead of the 16-bit mirror.
/// @note  havok/ stays the byte-matched mirror of vanilla; each copy here is its function as havok/ has it, with only
///        the index reads changed. Calls inside the takeover image reach vanilla's entries, so a replacement here runs
///        for every caller. Two more differences, both invisible to the game: clip activate drops its HK_TIMER pair (it
///        feeds Havok's own monitor stream through another unbound global; the subscriber is a CB profiler zone), and
///        warnings are formatted by vsnprintf instead of hkStringBuf (Warn).
/// @note  Left on the 16-bit field: BSOffsetAnimationGenerator's activate, deactivate and update, which only hand the
///        index to the clip loader's loadAnimation / unloadAnimation (an hkInt16 parameter of the exe's
///        AnimationFileManagerSingleton). They convert with the loader.

namespace CB::feature::widen {

    namespace {

        namespace hk    = CB::havok;
        namespace hooks = CB::platform::hooks;

        constexpr const char* kWho  = "Animation64";
        constexpr const char* kFlag = "animation64_readers";

        /// @note The clip loader and hkError's instance are mutable globals the link shim leaves unbound: read live.
        hk::IAnimationClipLoader* Loader() { return hooks::Live<&hk::g_animationClipLoader>(); }

        /// @return the role prefix an unset m_SyncAnimPrefix stands for: the exe's g_bssyncEmptyString, read through its
        ///         pin; "NPC", vanilla's value, on a runtime without one.
        /// @warning Never a literal "": paired animations name the unprefixed role's annotation track "NPC" and none
        ///          ""; with "" the mark lookup fails, the game's scene check rejects the paired idle, and the package
        ///          retries it every frame (the mount jitter, docs/bugs/CB-16).
        const char* DefaultSyncPrefix()
        {
            static const char* const prefix = [] {
                const std::uintptr_t at = hooks::engine::Resolve(hooks::engine::BSSynchronizedClipGenerator_EmptyString);
                return at ? *reinterpret_cast<const char* const*>(at) : "NPC";
            }();
            return prefix;
        }

        /// @brief Raises a Havok warning, the text printf-formatted as hkStringBuf::printf formats it.
        /// @note hkStringBuf's inline storage allocates through hkContainerTempAllocator, another unbound mutable global.
        void Warn(int id, const char* file, int line, const char* format, ...)
        {
            char    text[1024];
            va_list args;
            va_start(args, format);
            std::vsnprintf(text, sizeof text, format, args);
            va_end(args);
            hooks::Live<&hk::hkError::s_instance>()->message(hk::hkError::MESSAGE_WARNING, id, text, file, line);
        }

        /// @return the binding at `index` in `set`, or null when there is no set or the index is outside it.
        /// @note  Vanilla compares a signed index against the size only, so -1 reached m_bindings[-1] wherever the
        ///        caller had not ruled it out first; here no negative index has a binding.
        hk::hkbAnimationBindingWithTriggers* BindingAt(const hk::hkbAnimationBindingSet* set, std::int64_t index)
        {
            return (set && index >= 0 && index < set->m_bindings.getSize()) ? set->m_bindings[static_cast<int>(index)] : HK_NULL;
        }

        using ClipActivate   = hooks::Override<&hk::hkbClipGenerator::activate>;
        using ClipDeactivate = hooks::Override<&hk::hkbClipGenerator::deactivate>;
        using ClipIsValid    = hooks::Override<&hk::hkbClipGenerator::isValid>;
        using ClipRebind     = hooks::Override<&hk::hkbClipGenerator::rebindFromBindingSet>;
        using OffsetSample   = hooks::Override<&hk::BSOffsetAnimationGenerator::sampleOffset>;
        using SyncMark       = hooks::Override<&hk::BSSynchronizedClipGenerator::sampleSyncMark>;
        using SyncActivate   = hooks::Override<&hk::BSSynchronizedClipGenerator::activateClipGenerator>;

        void OnClipActivate(ClipActivate::Next, hk::hkbClipGenerator* clip, const hk::hkbContext& context)
        {
            using namespace CB::havok;
            /// @note Its `new`s and reserve run Havok's allocation inline, which can come before kDataLoaded.
            platform::classes::EnsureHavokGlobalsMirrored();
            if (clip->m_userData == 0 && Loader()) Loader()->queue(context, clip, HK_NULL);

            if (clip->m_userData == 8) {
                clip->m_animationControl.setAndDontIncrementRefCount(new hkaDefaultAnimationControl(HK_NULL, true, 2));
            } else {
                if (const std::int64_t index = BindingIndex(clip); index != -1) {
                    clip->m_originalTriggers = clip->m_triggers;

                    hkbAnimationBindingWithTriggers* bwt     = BindingAt(context.m_character->getAnimationBindingSet(), index);
                    hkaAnimationBinding*             binding = bwt ? bwt->m_binding : HK_NULL;

                    if (clip->m_animationControl) {
                        clip->m_animationControl->setBinding(binding);
                    } else {
                        clip->m_animationControl.setAndDontIncrementRefCount(
                            new hkaDefaultAnimationControl((binding && binding->m_animation) ? binding : HK_NULL, true, 2));
                    }

                    if (!(clip->m_flags & hkbClipGenerator::FLAG_DONT_CONVERT_ANNOTATIONS_TO_TRIGGERS)) {
                        const int numTriggers = bwt ? bwt->m_triggers.getSize() : 0;
                        if (numTriggers > 0) {
                            if (clip->m_triggers) clip->m_triggers.setAndDontIncrementRefCount(new hkbClipTriggerArray(*clip->m_triggers));
                            else                  clip->m_triggers.setAndDontIncrementRefCount(new hkbClipTriggerArray());

                            clip->m_triggers->m_triggers.reserve(clip->m_triggers->m_triggers.getSize() + numTriggers);

                            const hkReal duration = clip->computeUsableDuration();
                            for (int i = 0; i < numTriggers; ++i) {
                                const hkbAnimationBindingWithTriggers::Trigger& trig = bwt->m_triggers[i];
                                const hkReal                                     t    = trig.m_time - clip->m_cropStartAmountLocalTime;
                                if (t >= 0.0f && t <= duration) {
                                    const int        externalId = trig.m_event.getId();
                                    hkbEventPayload* payload    = trig.m_event.getPayload();
                                    hkbSymbolIdMap*  map        = context.m_behavior->m_eventIdMap;
                                    const int        internalId = map ? map->getInternal(externalId) : externalId;

                                    if (internalId == -1) {
                                        hkbBehaviorGraph*                 graph = context.m_character->m_behaviorGraph;
                                        const hkbBehaviorGraphStringData* sd = (graph && graph->m_data) ? graph->m_data->m_stringData.val() : HK_NULL;
                                        const int                         n  = sd ? sd->m_eventNames.getSize() : 0;
                                        Warn(0x0, "Generator\\Clip\\hkbClipGenerator.cpp", 0x10d, "%s: Could not find event %d (%s) in graph %s ",
                                             clip->m_name.cString(), externalId,
                                             (externalId >= 0 && externalId < n) ? sd->m_eventNames[externalId].cString() : "unknown",
                                             context.m_behavior ? context.m_behavior->m_name.cString() : "");
                                    } else {
                                        clip->addTrigger(t, hkbEvent(internalId, payload, HK_NULL), false, false, true);
                                    }
                                }
                            }
                        }
                    }
                }

                if (!clip->m_animationControl->m_binding) {
                    hkbBehaviorGraph* graph = context.m_character->m_behaviorGraph;
                    Warn(int(0xa781b89f), "Generator\\Clip\\hkbClipGenerator.cpp", 0x122,
                         "%s's %s: Animation control exists but binding was not created.  Was the animation loaded and connected to the "
                         "control?",
                         graph ? graph->m_name.cString() : "", clip->m_name.cString());
                }

                clip->m_binding = const_cast<hkaAnimationBinding*>(clip->m_animationControl->getAnimationBinding());

                hkaAnimationBinding* binding = clip->m_binding;
                hkbCharacterSetup*   setup   = context.m_character->m_setup;
                if (binding && binding->m_originalSkeletonName.cString() && setup->m_animationSkeleton &&
                    hkString::strCmp(setup->m_animationSkeleton->m_name.cString(), binding->m_originalSkeletonName.cString()) != 0) {
                    const hkaSkeletonMapper* mapper = setup->getRetargetingSkeletonMapper(binding->m_originalSkeletonName.cString());
                    if (mapper) clip->m_mapperData = new hkaDefaultAnimationControlMapperData(mapper, binding);
                }

                clip->resetAtEnd();
            }
        }

        void OnClipDeactivate(ClipDeactivate::Next, hk::hkbClipGenerator* clip, const hk::hkbContext& context)
        {
            using namespace CB::havok;
            clip->fn_140ad11e0();

            clip->m_echos.setSize(0);

            if (clip->m_mapperData) clip->m_mapperData->removeReference();
            clip->m_mapperData = HK_NULL;

            if (BindingIndex(clip) != -1) {
                clip->m_triggers         = clip->m_originalTriggers;
                clip->m_originalTriggers = HK_NULL;
            }

            if (clip->m_mirroredAnimation) {
                clip->m_mirroredAnimation->removeReference();
                clip->m_mirroredAnimation = HK_NULL;
            }

            clip->m_binding = HK_NULL;
            if (clip->m_animationControl) clip->m_animationControl->setBinding(HK_NULL);

            if (clip->m_userData != 0 && Loader()) Loader()->unload(context, clip, HK_NULL);
        }

        /// @note BindingIndex() takes a mutable clip (it adopts a mirror an unconverted writer changed); isValid is
        ///       const in Havok's interface only.
        bool OnClipIsValid(ClipIsValid::Next, const hk::hkbClipGenerator* clip, hk::hkStringPtr& errorString)
        {
            using namespace CB::havok;
            if (!clip->m_animationControl && BindingIndex(const_cast<hkbClipGenerator*>(clip)) == -1) {
                errorString = "The animation has not been loaded.";
                return false;
            }
            if (clip->m_mode < hkbClipGenerator::MODE_SINGLE_PLAY || clip->m_mode > 4) {
                errorString = "The mode is invalid.";
                return false;
            }
            const hkReal duration =
                clip->m_animationControl->m_binding->m_animation->m_duration - (clip->m_cropEndAmountLocalTime + clip->m_cropStartAmountLocalTime);
            if (duration <= 0.0f) {
                errorString = "The clip is cropped to a duration of zero.";
                return false;
            }
            return true;
        }

        void OnClipRebind(ClipRebind::Next, hk::hkbClipGenerator* clip, const hk::hkbContext& context)
        {
            using namespace CB::havok;
            hkbAnimationBindingSet*          bindingSet = context.m_character ? context.m_character->getAnimationBindingSet() : HK_NULL;
            hkbAnimationBindingWithTriggers* bwt        = BindingAt(bindingSet, BindingIndex(clip));
            hkaAnimationBinding*             binding    = bwt ? bwt->m_binding : HK_NULL;
            if (clip->m_animationControl) clip->m_animationControl->setBinding(binding);
            clip->m_binding = binding;
        }

        void OnOffsetSample(OffsetSample::Next, hk::BSOffsetAnimationGenerator* gen, const hk::hkbContext& context)
        {
            using namespace CB::havok;
            /// @note Its setSize calls grow hkArrays inline.
            platform::classes::EnsureHavokGlobalsMirrored();
            const hkReal range      = gen->m_fOffsetRangeEnd - gen->m_fOffsetRangeStart;
            hkReal       percentage = (range > HK_REAL_EPSILON) ? gen->m_fOffsetVariable / range : 0.0f;
            percentage              = (percentage < 0.0f) ? 0.0f : hkMath::min2(percentage, 1.0f);
            if (gen->m_bOffsetValid && hkMath::fabs(percentage - gen->m_fCurrentPercentage) <= HK_REAL_EPSILON) return;
            gen->m_fCurrentPercentage = percentage;

            hkbClipGenerator*                clip       = gen->getOffsetClipGenerator();
            hkbCharacter*                    character  = context.m_character;
            hkbAnimationBindingSet*          bindingSet = character ? character->getAnimationBindingSet() : HK_NULL;
            hkbAnimationBindingWithTriggers* bwt        = clip ? BindingAt(bindingSet, BindingIndex(clip)) : HK_NULL;
            const hkaAnimationBinding*       binding    = bwt ? bwt->m_binding : HK_NULL;
            hkaAnimation*                    animation  = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();

            if (!animation || binding->m_blendHint != hkaAnimationBinding::ADDITIVE) {
                gen->m_bZeroOffset = true;
                if (animation)
                    Warn(0, "Generator\\BSOffsetAnimation\\BSOffsetAnimationGenerator.cpp", 0x122,
                         "The BSOffsetAnimationGenerator, %s, pointed to a non-additive animation %s.", gen->m_name.cString(), clip->m_name.cString());
            } else {
                const hkReal   frameDuration = animation->m_duration * (1.0f / hkReal(animation->getNumOriginalFrames() - 1));
                const hkReal   time          = (animation->m_duration - frameDuration) * percentage + frameDuration;
                const hkUint32 numFrames     = animation->getNumOriginalFrames();
                hkUint32       frame         = hkUint32(__int64((time / animation->m_duration) * hkReal(numFrames - 1)));
                if (frame > numFrames - 2) frame = numFrames - 2;
                if (frame != gen->m_iCurrentFrame) {
                    gen->m_iCurrentFrame = frame;
                    if (!gen->m_bOffsetValid) {
                        const int numTracks = animation->m_numberOfTransformTracks;
                        if (numTracks > 0) {
                            gen->m_BoneIndexA.setSize(numTracks, -1);
                            gen->m_BoneOffsetA.setSize(numTracks);
                        }
                        gen->m_bOffsetValid = true;
                    }
                    gen->m_bZeroOffset = !gen->sampleBoneOffsets(binding, animation, time);
                }
            }
        }

        /// @note The game's paired-idle start (0x1405463f0) calls this for every participant and aborts the scene when
        ///       it returns false, so it must resolve an unset prefix exactly as vanilla does (DefaultSyncPrefix).
        bool OnSyncMark(SyncMark::Next, hk::BSSynchronizedClipGenerator* sync, const hk::hkbContext& context, hk::hkQsTransform& markOut)
        {
            using namespace CB::havok;
            bool                       found     = false;
            hkbClipGenerator*          clip      = sync->m_pClipGenerator;
            const hkaAnimationBinding* binding   = (clip && clip->m_animationControl) ? clip->m_animationControl->getAnimationBinding() : HK_NULL;
            hkaAnimation*              animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();
            bool                       queued    = false;
            int                        status    = 1;

            if (!animation && clip && clip->m_userData == 0) {
                if (Loader()) status = Loader()->queue(context, clip, sync);

                hkbAnimationBindingWithTriggers* bwt    = BindingAt(context.m_character->getAnimationBindingSet(), BindingIndex(clip));
                const hkaAnimationBinding*       loaded = bwt ? bwt->m_binding : HK_NULL;
                animation                               = loaded ? loaded->m_animation : hkRefPtr<hkaAnimation>();
                queued                                  = true;
            }

            if (animation && status == 1) {
                const int   numTracks  = animation->m_annotationTracks.getSize();
                hkInt16     trackIndex = -1;
                const char* trackName  = (sync->m_SyncAnimPrefix == HK_NULL) ? DefaultSyncPrefix() : sync->m_SyncAnimPrefix;
                for (int i = 0; i < numTracks; i++) {
                    if (trackIndex != -1) break;
                    if (hkString::strCmp(animation->m_annotationTracks[i].m_trackName.cString(), trackName) == 0)
                        trackIndex = static_cast<hkInt16>(i);
                }
                if (trackIndex != -1) {
                    const hkInt16 tracks = trackIndex;
                    animation->sampleIndividualTransformTracks(0.0f, &tracks, 1, &markOut);
                    found = true;
                }
            }

            if (queued && Loader()) Loader()->unload(context, clip, sync);
            return found;
        }

        /// @note The scene slot (m_sAnimationBindingIndex) is still 16-bit: it widens on its own (Animation64.h).
        void OnSyncActivate(SyncActivate::Next, hk::BSSynchronizedClipGenerator* sync, const hk::hkbContext& context, hk::hkbClipGenerator* clip)
        {
            const hk::hkInt16  sceneIndex = sync->m_sAnimationBindingIndex;
            const std::int64_t clipIndex  = BindingIndex(clip);
            if (sceneIndex != -1) SetBindingIndex(clip, sceneIndex);
            clip->activate(context);
            if (sceneIndex == -1) {
                if (clip->m_animationControl && sync->m_pLocalSyncBinding) clip->m_animationControl->setBinding(sync->m_pLocalSyncBinding);
            } else {
                SetBindingIndex(clip, clipIndex);
            }
        }

        /// @note Last: a replacement never calls on, so it sits innermost and observers (the activate counter) still see
        ///       every call.
        [[maybe_unused]] const bool s_subscribed =
            (ClipActivate::Subscribe(&OnClipActivate, hooks::Priority::Last, kWho, kFlag),
             ClipDeactivate::Subscribe(&OnClipDeactivate, hooks::Priority::Last, kWho, kFlag),
             ClipIsValid::Subscribe(&OnClipIsValid, hooks::Priority::Last, kWho, kFlag),
             ClipRebind::Subscribe(&OnClipRebind, hooks::Priority::Last, kWho, kFlag),
             OffsetSample::Subscribe(&OnOffsetSample, hooks::Priority::Last, kWho, kFlag),
             SyncMark::Subscribe(&OnSyncMark, hooks::Priority::Last, kWho, kFlag),
             SyncActivate::Subscribe(&OnSyncActivate, hooks::Priority::Last, kWho, kFlag), true);

    }  // namespace

}  // namespace CB::feature::widen
