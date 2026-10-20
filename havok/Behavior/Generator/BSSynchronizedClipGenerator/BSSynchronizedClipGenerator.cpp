/// @file
/// @brief BSSynchronizedClipGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>
#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGeneratorUtils.h>
#include <Common/Serialize/Util/hkBuiltinTypeRegistry.h>

namespace CB { namespace havok
{
    /// @brief The empty prefix an unset m_SyncAnimPrefix reads as.
    /// @note A writable `const char*` in vanilla's data (loaded, never folded), so declared here, defined by the exe.
    ///       Name provisional.
    extern const char* g_bssyncEmptyString;
    /// @brief "PairEnd", the event every paired graph maps to itself.
    /// @note As g_bssyncEmptyString. Name provisional.
    extern const char* g_bssyncPairEndEvent;

    /// @note m_pSyncScene is cleared here, after construction, not in the constructor: that order is vanilla's. The
    ///       raw store is vanilla's too — the loaded value is garbage, so it must not be released.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing)
    {
        hkFinishLoadedObjectFlag flag;
        flag.m_finishing               = finishing;
        BSSynchronizedClipGenerator* o = new (p) BSSynchronizedClipGenerator(flag);
        o->m_pSyncScene.m_pntr  = HK_NULL;
        return o;
    }

    BSSynchronizedClipGenerator::BSSynchronizedClipGenerator(const BSSynchronizedClipGenerator& gen, hkbBehaviorGraph& rootBehavior)
    :   hkbGenerator(gen),
        m_pClipGenerator(HK_NULL),
        m_SyncAnimPrefix(gen.m_SyncAnimPrefix),
        m_bSyncClipIgnoreMarkPlacement(gen.m_bSyncClipIgnoreMarkPlacement),
        m_fGetToMarkTime(gen.m_fGetToMarkTime),
        m_fMarkErrorThreshold(gen.m_fMarkErrorThreshold),
        m_bLeadCharacter(gen.m_bLeadCharacter),
        m_bReorientSupportChar(gen.m_bReorientSupportChar),
        m_bApplyMotionFromRoot(gen.m_bApplyMotionFromRoot),
        m_pSyncScene(gen.m_pSyncScene),
        m_StartMarkWS(gen.m_StartMarkWS),
        m_EndMarkWS(gen.m_EndMarkWS),
        m_StartMarkMS(gen.m_StartMarkMS),
        m_fCurrentLerp(gen.m_fCurrentLerp),
        m_pLocalSyncBinding(HK_NULL),
        m_pEventMap(HK_NULL),
        m_sAnimationBindingIndex(gen.m_sAnimationBindingIndex),
        m_bAtMark(gen.m_bAtMark),
        m_bAllCharactersInScene(gen.m_bAllCharactersInScene),
        m_bAllCharactersAtMarks(gen.m_bAllCharactersAtMarks)
    {
        if (gen.m_pClipGenerator) m_pClipGenerator = static_cast<hkbClipGenerator*>(gen.m_pClipGenerator->cloneNode(rootBehavior));
    }

    /// @note A clone owns a reference to its cloned clip generator; a template's is the graph's.
    BSSynchronizedClipGenerator::~BSSynchronizedClipGenerator()
    {
        delete m_pLocalSyncBinding;
        m_pLocalSyncBinding = HK_NULL;
        delete m_pEventMap;
        m_pEventMap          = HK_NULL;
        m_pSyncScene = HK_NULL;
        if (m_pClipGenerator && m_pClipGenerator->m_cloneState == 2) m_pClipGenerator->removeReference();
        m_pClipGenerator = HK_NULL;
    }

    /// @note The binding's animation is taken through a temporary hkRefPtr (add/release), as in vanilla.
    bool BSSynchronizedClipGenerator::sampleSyncMark(const hkbContext& context, hkQsTransform& markOut)
    {
        bool                       found     = false;
        hkbClipGenerator*          clip      = m_pClipGenerator;
        const hkaAnimationBinding* binding   = (clip && clip->m_animationControl) ? clip->m_animationControl->getAnimationBinding() : HK_NULL;
        hkaAnimation*              animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();
        bool                       queued    = false;
        int                        status    = 1;

        if (!animation && clip && clip->m_userData == 0) {
            if (g_animationClipLoader) status = g_animationClipLoader->queue(context, clip, this);

            hkbAnimationBindingSet*          bindingSet = context.m_character->getAnimationBindingSet();
            hkbAnimationBindingWithTriggers* bwt =
                (clip->m_animationBindingIndex < bindingSet->m_bindings.getSize()) ? bindingSet->m_bindings[clip->m_animationBindingIndex] : HK_NULL;
            const hkaAnimationBinding* loaded = bwt ? bwt->m_binding : HK_NULL;
            animation                         = loaded ? loaded->m_animation : hkRefPtr<hkaAnimation>();
            queued                            = true;
        }

        if (animation && status == 1) {
            const int    numTracks  = animation->m_annotationTracks.getSize();
            hkInt16      trackIndex = -1;
            hkStringBuf  trackName((m_SyncAnimPrefix == HK_NULL) ? g_bssyncEmptyString : m_SyncAnimPrefix);
            for (int i = 0; i < numTracks; i++) {
                if (trackIndex != -1) break;
                if (hkString::strCmp(animation->m_annotationTracks[i].m_trackName.cString(), trackName.cString()) == 0) trackIndex = static_cast<hkInt16>(i);
            }
            if (trackIndex != -1) {
                const hkInt16 tracks = trackIndex;
                animation->sampleIndividualTransformTracks(0.0f, &tracks, 1, &markOut);
                found = true;
            }
        }

        if (queued && g_animationClipLoader) g_animationClipLoader->unload(context, clip, this);
        return found;
    }

    /// @note The default prefix is read before the new map is stored to the member, as vanilla orders it (a local
    ///       for the map keeps that order; assigning the member first sinks the read below the store).
    void BSSynchronizedClipGenerator::buildEventMap(const hkbBehaviorGraphStringData* stringData)
    {
        hkPointerMap<int, int>* eventMap = new hkPointerMap<int, int>();
        const char*             prefix   = g_bssyncEmptyString;
        m_pEventMap                       = eventMap;
        if (m_SyncAnimPrefix) prefix     = m_SyncAnimPrefix;
        const int   prefixLen = hkString::strLen(prefix);
        for (int i = 0; i < stringData->m_eventNames.getSize(); i++) {
            const char* name    = stringData->m_eventNames[i].cString();
            const int   nameLen = hkString::strLen(name);
            int         partner = -1;
            if (hkString::strCmp(g_bssyncPairEndEvent, name) == 0) {
                partner = i;
            } else if (nameLen > prefixLen && hkString::strNcmp(prefix, name, prefixLen) == 0) {
                const char* unprefixed = name + prefixLen;
                for (int j = 0; j < stringData->m_eventNames.getSize(); j++) {
                    if (partner != -1) break;
                    if (hkString::strCmp(unprefixed, stringData->m_eventNames[j].cString()) == 0) partner = j;
                }
            }
            if (partner != -1) m_pEventMap->insert(i, partner);
        }
    }

    void BSSynchronizedClipGenerator::remapEvents(int startIndex, hkbEventQueue* queue)
    {
        if (m_pEventMap) {
            const int numNew = queue->m_elementsInUse - startIndex;
            for (int i = 0; i < numNew; i++) {
                hkbEvent e;
                queue->dequeue(e);
                const int id = queue->getInternalId(e);

                hkbSymbolIdMap* idMap  = queue->m_symbolIdMap;
                const bool      direct = queue->m_directMap;
                if (idMap) {
                    int external = id;
                    if (external != -1) external = direct ? idMap->getExternal(external) : idMap->getInternal(external);
                    int partner;
                    if (m_pEventMap->get(external, &partner) == HK_SUCCESS) {
                        partner = direct ? idMap->getInternal(partner) : idMap->getExternal(partner);
                        if (partner != -1) {
                            hkbEvent remapped(partner, e.m_payload, e.m_sender);
                            remapped.m_id = queue->toExternal(remapped.m_id);
                            queue->enqueue(remapped);
                        }
                    }
                }
            }
        }
    }

    /// @note A scene slot stands in for the clip's own binding index only for the activation.
    void BSSynchronizedClipGenerator::activateClipGenerator(const hkbContext& context, hkbClipGenerator* clip)
    {
        const hkInt16 sceneIndex = m_sAnimationBindingIndex;
        const hkInt16 clipIndex  = clip->m_animationBindingIndex;
        if (sceneIndex != -1) clip->m_animationBindingIndex = sceneIndex;
        clip->activate(context);
        if (sceneIndex == -1) {
            if (clip->m_animationControl && m_pLocalSyncBinding) clip->m_animationControl->setBinding(m_pLocalSyncBinding);
        } else {
            clip->m_animationBindingIndex = clipIndex;
        }
    }


    const void* BSSynchronizedClipGenerator::getClassType() const
    {
        return &staticClass();
    }

    /// @note The fallback scene is value-initialised (`new T()`: vanilla zeroes it before the constructor runs) and
    ///       assigned through the hkRefPtr, which adds a reference to the constructor's one — so it is never freed (as in
    ///       vanilla). The skeleton goes through an hkRefPtr temporary, as in vanilla (the copy's addReference/
    ///       removeReference pair is visible).
    void BSSynchronizedClipGenerator::activate(const hkbContext& context)
    {
        hkbBehaviorGraphStringData* stringData = (context.m_character && context.m_character->m_behaviorGraph && context.m_character->m_behaviorGraph->m_data)
                                                     ? context.m_character->m_behaviorGraph->m_data->m_stringData.val()
                                                     : HK_NULL;
        buildEventMap(stringData);

        hkbClipGenerator* clip = static_cast<hkbClipGenerator*>(m_pClipGenerator);
        if (m_sAnimationBindingIndex == -1 && !m_pLocalSyncBinding) {
            const hkaAnimationBinding* binding  = clip->m_animationControl ? clip->m_animationControl->getAnimationBinding() : HK_NULL;
            hkbCharacterSetup*         setup    = context.m_character->m_setup;
            const hkaSkeleton*         skeleton = setup ? setup->m_animationSkeleton : hkRefPtr<hkaSkeleton>();
            if (binding) m_pLocalSyncBinding = createLocalSyncBinding(skeleton, binding, m_SyncAnimPrefix);
        }

        if (!g_animationClipLoader || g_animationClipLoader->queue(context, clip, this)) activateClipGenerator(context, clip);

        if (!m_pSyncScene) m_pSyncScene = new hkbDefaultSynchronizedScene();

        if (!sampleSyncMark(context, m_StartMarkMS)) m_StartMarkMS.setIdentity();

        m_StartMarkWS = *context.m_character->m_worldFromModel;
        m_EndMarkWS   = m_StartMarkWS;
        if (m_fGetToMarkTime == 0.0f) m_fCurrentLerp = 1.0f;

        m_pSyncScene->onClipActivated();
    }

    /// @note `loaded` is an int: vanilla widens the loader's bool into a 32-bit register and tests that (a bool local
    ///       keeps it byte-wide). The atMarks store is scheduled after the scene reload in vanilla, before it in ours
    ///       (VARIANT).
    void BSSynchronizedClipGenerator::update(const hkbContext& context, hkReal timestep)
    {
        hkbClipGenerator* clip   = static_cast<hkbClipGenerator*>(m_pClipGenerator);
        int               loaded = true;
        if (clip->m_userData == 8) {
            loaded = g_animationClipLoader ? g_animationClipLoader->load(context, clip, this) : true;
            if (loaded) activateClipGenerator(context, clip);
        }

        if (m_pSyncScene) {
            m_bAllCharactersAtMarks = m_pSyncScene->areAllCharactersAtMarks();
            m_bAllCharactersInScene = m_pSyncScene->areAllCharactersInScene();
        }

        const int firstEvent = context.getEventQueue() ? context.getEventQueue()->getSize() : 0;
        if (loaded && m_pSyncScene && m_bAllCharactersAtMarks) m_pClipGenerator->update(context, timestep);
        if (context.getEventQueue() && context.getEventQueue()->getSize() > firstEvent) remapEvents(firstEvent, context.getEventQueue());

        if (m_fCurrentLerp < 1.0f) m_fCurrentLerp = hkMath::min2(timestep / m_fGetToMarkTime + m_fCurrentLerp, 1.0f);

        if (!m_bAtMark) {
            if (m_bReorientSupportChar) {
                m_bAtMark = (m_fCurrentLerp == 1.0f) && m_EndMarkWS.isApproximatelyEqual(*context.m_character->m_worldFromModel, m_fMarkErrorThreshold);
            } else {
                m_bAtMark = (m_fCurrentLerp == 1.0f) &&
                            m_EndMarkWS.m_translation.equals3(context.m_character->m_worldFromModel->m_translation, m_fMarkErrorThreshold);
            }
        }
    }

    void BSSynchronizedClipGenerator::deactivate(const hkbContext& context)
    {
        hkbClipGenerator* clip = static_cast<hkbClipGenerator*>(m_pClipGenerator);
        if (clip) {
            if (m_pSyncScene) {
                const hkaAnimationBinding* binding  = clip->m_animationControl ? clip->m_animationControl->getAnimationBinding() : HK_NULL;
                const hkReal               duration = (binding && binding->m_animation) ? binding->m_animation->m_duration : 0.0f;
                if (clip->getLocalTime() < duration * 0.2f) m_pSyncScene->onCharacterInterrupted(context.m_character);
            }
            const hkUlong userData       = m_pClipGenerator->m_userData;
            m_pClipGenerator->m_userData = 0;
            m_pClipGenerator->deactivate(context);
            m_pClipGenerator->m_userData = userData;
            if (g_animationClipLoader) g_animationClipLoader->unload(context, static_cast<hkbClipGenerator*>(m_pClipGenerator), this);
        }
        if (m_pSyncScene) m_pSyncScene->onClipDeactivated();
    }

    int BSSynchronizedClipGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (m_cloneState == CLONE_STATE_CLONE || (flags & FLAG_ACTIVE_ONLY)) return 0;
        return 1;
    }

    void BSSynchronizedClipGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (m_cloneState == CLONE_STATE_CLONE || (flags & FLAG_ACTIVE_ONLY)) {
            childrenInfo.m_childInfos.setSize(0);
            return;
        }
        childrenInfo.m_childInfos.setSize(1);
        childrenInfo.m_childInfos[0].m_node = m_pClipGenerator;
    }

    bool BSSynchronizedClipGenerator::isValid(hkStringPtr& errorString) const
    {
        hkbClipGenerator* clip = HK_NULL;
        if (m_pClipGenerator) {
            const hkClass* cls = hkBuiltinTypeRegistry::getInstance().getVtableClassRegistry()->getClassFromVirtualInstance(m_pClipGenerator);
            if (cls && hkbClipGeneratorClass.isSuperClass(*cls)) clip = static_cast<hkbClipGenerator*>(m_pClipGenerator);
        }

        const bool valid = (clip != HK_NULL);
        bool       found = false;
        if (clip) {
            const hkaAnimationBinding* binding   = clip->m_animationControl ? clip->m_animationControl->getAnimationBinding() : HK_NULL;
            const hkaAnimation*        animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();
            if (animation) {
                const int numTracks = animation->m_annotationTracks.getSize();
                for (int i = 0; i < numTracks && !found; i++) {
                    hkStringBuf prefix(m_SyncAnimPrefix);
                    hkStringBuf trackName(animation->m_annotationTracks[i].m_trackName);
                    if (hkString::beginsWith(trackName.cString(), prefix.cString())) found = true;
                }
            }
        } else {
            errorString.printf("The clip generator selected is invalid.  Is the node selected a clip generator?");
        }
        if (!found) {
            errorString.printf("The role prefix selected does not match one in the currently selected scene clip generator.  Please refer to "
                               "the animation for proper role names.");
        }
        return valid;
    }

    hkbNode* BSSynchronizedClipGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSSynchronizedClipGenerator(*this, rootBehavior);
    }

    void BSSynchronizedClipGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                               hkReal timeOffset)
    {
        hkbCharacterSetup* setup    = context.m_character->m_setup;
        const hkaSkeleton* skeleton = setup ? setup->m_animationSkeleton : hkRefPtr<hkaSkeleton>();
        BSSynchronizedClipGeneratorUtils::generate(this, context, skeleton, activeChildrenOutput, output, timeOffset);
    }

    bool BSSynchronizedClipGenerator::canRecycleOutput() const
    {
        return m_pClipGenerator->canRecycleOutput();
    }

    /// @note Clears the sync-point count and flags too: vanilla writes +0x48..+0x4f as one qword (speed 1, rest 0).
    void BSSynchronizedClipGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbGeneratorSyncInfo& syncInfo = info.m_syncInfo;
        syncInfo.m_baseFrequency = 0.0f;
        syncInfo.m_localTime     = 0.0f;
        syncInfo.m_playbackSpeed = 1.0f;
        syncInfo.m_numSyncPoints = 0;
        syncInfo.m_isCyclic      = false;
        syncInfo.m_isMirrored    = false;
        syncInfo.m_isAdditive    = false;
    }
} }
