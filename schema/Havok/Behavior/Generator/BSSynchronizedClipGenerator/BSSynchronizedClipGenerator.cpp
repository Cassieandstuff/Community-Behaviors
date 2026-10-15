/// @file
/// @brief BSSynchronizedClipGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGenerator.h>

namespace CB { namespace schema
{
    /// @brief The empty prefix an unset m_SyncAnimPrefix reads as.
    /// @note A writable `const char*` in vanilla's data (loaded, never folded), so declared here, defined by the exe.
    ///       Name provisional.
    extern const char* g_bssyncEmptyString;
    /// @brief "PairEnd", the event every paired graph maps to itself.
    /// @note As g_bssyncEmptyString. Name provisional.
    extern const char* g_bssyncPairEndEvent;

    /// @note m_synchronizedScene is cleared here, after construction, not in the constructor: that order is vanilla's.
    ///       The loaded bytes are stale, so the pointer is overwritten without releasing it.
    void* finishLoadedObjectBSSynchronizedClipGenerator(void* p, int finishing)
    {
        hkFinishLoadedObjectFlag flag;
        flag.m_finishing               = finishing;
        BSSynchronizedClipGenerator* o = new (p) BSSynchronizedClipGenerator(flag);
        o->m_synchronizedScene.m_pntr  = HK_NULL;
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
        m_synchronizedScene(gen.m_synchronizedScene),
        m_StartMarkWS(gen.m_StartMarkWS),
        m_EndMarkWS(gen.m_EndMarkWS),
        m_StartMarkMS(gen.m_StartMarkMS),
        m_fCurrentLerp(gen.m_fCurrentLerp),
        m_sceneBinding(HK_NULL),
        m_eventMap(HK_NULL),
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
        delete m_sceneBinding;
        m_sceneBinding = HK_NULL;
        delete m_eventMap;
        m_eventMap          = HK_NULL;
        m_synchronizedScene = HK_NULL;
        if (m_pClipGenerator && m_pClipGenerator->m_cloneState == 2) m_pClipGenerator->removeReference();
        m_pClipGenerator = HK_NULL;
    }

    /// @note The binding's animation is taken through a temporary hkRefPtr (add/release), as in vanilla.
    bool BSSynchronizedClipGenerator::sampleSyncMark(const hkbContext& context, hkQsTransform& markOut)
    {
        bool                       found     = false;
        hkbClipGenerator*          clip      = m_pClipGenerator;
        const hkaAnimationBinding* binding   = (clip && clip->m_animationControl) ? clip->m_animationControl->m_binding.val() : HK_NULL;
        hkaAnimation*              animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();
        bool                       queued    = false;
        int                        status    = 1;

        if (!animation && clip && clip->m_userData == 0) {
            if (g_animationClipLoader) status = g_animationClipLoader->queue(context, clip, this);

            hkbAnimationBindingSet*          bindingSet = context.m_character->getAnimationBindingSet();
            hkbAnimationBindingWithTriggers* bwt =
                (clip->m_animationBindingIndex < bindingSet->m_bindings.getSize()) ? bindingSet->m_bindings[clip->m_animationBindingIndex] : HK_NULL;
            const hkaAnimationBinding* loaded = bwt ? bwt->m_binding.val() : HK_NULL;
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
        m_eventMap                       = eventMap;
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
            if (partner != -1) m_eventMap->insert(i, partner);
        }
    }

    void BSSynchronizedClipGenerator::remapEvents(int startIndex, hkbEventQueue* queue)
    {
        if (m_eventMap) {
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
                    if (m_eventMap->get(external, &partner) == HK_SUCCESS) {
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
            if (clip->m_animationControl && m_sceneBinding) clip->m_animationControl->setBinding(m_sceneBinding);
        } else {
            clip->m_animationBindingIndex = clipIndex;
        }
    }
} }
