#pragma once
#include "havok/classes/Base.h"
#include "havok/classes/Events.h"
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Generator leaf classes, hand-ported from HKX2E Autogen. SERIALIZE_IGNORED
// fields that occupy real bytes + a value are kept (round-trip fidelity);
// void-typed ignored fields are omitted (serializer emits empty slots).

namespace havok {

class hkbClipTriggerArray;  // pointer target (fwd)
class hkbBoneWeightArray;   // pointer target (fwd; Arrays.h)

// hkbClipGenerator — size 272, sig 0x333b85b9. Plays a single animation clip.
class hkbClipGenerator : public hkbGenerator {
public:
    std::string                          m_animationName;
    std::shared_ptr<hkbClipTriggerArray> m_triggers;
    float        m_cropStartAmountLocalTime   = 0.f;
    float        m_cropEndAmountLocalTime     = 0.f;
    float        m_startTime                  = 0.f;
    float        m_playbackSpeed              = 0.f;
    float        m_enforcedDuration           = 0.f;
    float        m_userControlledTimeFraction = 0.f;
    std::int16_t m_animationBindingIndex      = 0;
    std::int8_t  m_mode                       = 0;  // enum PlaybackMode
    std::int8_t  m_flags                      = 0;
    // ── SERIALIZE_IGNORED (occupy bytes; kept for round-trip fidelity) ──
    QSTransform  m_extractedMotion{};
    float        m_localTime                            = 0.f;
    float        m_time                                 = 0.f;
    float        m_previousUserControlledTimeFraction   = 0.f;
    std::int32_t m_bufferSize                           = 0;
    std::int32_t m_echoBufferSize                       = 0;
    bool         m_atEnd                                = false;
    bool         m_ignoreStartTime                      = false;
    bool         m_pingPongBackward                     = false;
    HK_CLASS_ID(0x333b85b9u, "hkbClipGenerator")
};

// hkbBlenderGeneratorChild — size 80, sig 0xe2b384b0. One weighted child of a blender.
class hkbBlenderGeneratorChild : public hkbBindable {
public:
    std::shared_ptr<hkbGenerator>       m_generator;
    std::shared_ptr<hkbBoneWeightArray> m_boneWeights;
    float m_weight               = 0.f;
    float m_worldFromModelWeight = 0.f;
    HK_CLASS_ID(0xe2b384b0u, "hkbBlenderGeneratorChild")
};

// hkbBlenderGenerator — size 160, sig 0x22df7147. Parametric/normal blend of children.
class hkbBlenderGenerator : public hkbGenerator {
public:
    float        m_referencePoseWeightThreshold = 0.f;
    float        m_blendParameter               = 0.f;
    float        m_minCyclicBlendParameter      = 0.f;
    float        m_maxCyclicBlendParameter      = 0.f;
    std::int16_t m_indexOfSyncMasterChild       = 0;
    std::int16_t m_flags                        = 0;
    bool         m_subtractLastChild            = false;
    std::vector<std::shared_ptr<hkbBlenderGeneratorChild>> m_children;
    // ── SERIALIZE_IGNORED tail (typed) ──
    float        m_endIntervalWeight  = 0.f;
    std::int32_t m_numActiveChildren  = 0;
    std::int16_t m_beginIntervalIndex = 0;
    std::int16_t m_endIntervalIndex   = 0;
    bool         m_initSync           = false;
    bool         m_doSubtractiveBlend = false;
    HK_CLASS_ID(0x22df7147u, "hkbBlenderGenerator")
};

// hkbManualSelectorGenerator — size 96, sig 0xd932fab8. Integer-indexed selector.
class hkbManualSelectorGenerator : public hkbGenerator {
public:
    std::vector<std::shared_ptr<hkbGenerator>> m_generators;
    std::int8_t m_selectedGeneratorIndex = 0;
    std::int8_t m_currentGeneratorIndex  = 0;
    HK_CLASS_ID(0xd932fab8u, "hkbManualSelectorGenerator")
};

// BSCyclicBlendTransitionGenerator — size 176, sig 0x5119eb06.
class BSCyclicBlendTransitionGenerator : public hkbGenerator {
public:
    std::shared_ptr<hkbGenerator> m_pBlenderGenerator;
    hkbEventProperty m_EventToFreezeBlendValue{};  // inline
    hkbEventProperty m_EventToCrossBlend{};        // inline
    float       m_fBlendParameter    = 0.f;
    float       m_fTransitionDuration = 0.f;
    std::int8_t m_eBlendCurve        = 0;  // enum BlendCurve
    std::int8_t m_currentMode        = 0;  // SERIALIZE_IGNORED
    HK_CLASS_ID(0x5119eb06u, "BSCyclicBlendTransitionGenerator")
};

// BSBoneSwitchGeneratorBoneData — size 64, sig 0xc1215be6.
class BSBoneSwitchGeneratorBoneData : public hkbBindable {
public:
    std::shared_ptr<hkbGenerator>       m_pGenerator;
    std::shared_ptr<hkbBoneWeightArray> m_spBoneWeight;
    HK_CLASS_ID(0xc1215be6u, "BSBoneSwitchGeneratorBoneData")
};

// BSBoneSwitchGenerator — size 112, sig 0xf33d3eea.
class BSBoneSwitchGenerator : public hkbGenerator {
public:
    std::shared_ptr<hkbGenerator> m_pDefaultGenerator;
    std::vector<std::shared_ptr<BSBoneSwitchGeneratorBoneData>> m_ChildrenA;
    HK_CLASS_ID(0xf33d3eeau, "BSBoneSwitchGenerator")
};

// BSiStateTaggingGenerator — size 96, sig 0xf0826fc1.
class BSiStateTaggingGenerator : public hkbGenerator {
public:
    std::shared_ptr<hkbGenerator> m_pDefaultGenerator;
    std::int32_t m_iStateToSetAs = 0;
    std::int32_t m_iPriority     = 0;
    HK_CLASS_ID(0xf0826fc1u, "BSiStateTaggingGenerator")
};

// hkbBehaviorReferenceGenerator — size 88, sig 0x0fcb5423. References another .hkx (BFR).
class hkbBehaviorReferenceGenerator : public hkbGenerator {
public:
    std::string m_behaviorName;
    HK_CLASS_ID(0x0fcb5423u, "hkbBehaviorReferenceGenerator")
};

// BGSGamebryoSequenceGenerator — size 112, sig 0xc8df2d77. Plays a Gamebryo .kf sequence.
class BGSGamebryoSequenceGenerator : public hkbGenerator {
public:
    std::string  m_pSequence;                 // authored .kf sequence name (CString)
    std::int8_t  m_eBlendModeFunction = 1;    // enum BlendModeFunction (i8-backed)
    float        m_fPercent           = 0.f;
    // m_events — hkArray<void>, SERIALIZE_IGNORED (always empty)
    // ── SERIALIZE_IGNORED tail (occupy bytes; always zero) ──
    float        m_fTime            = 0.f;
    bool         m_bDelayedActivate = false;
    bool         m_bLooping         = false;
    HK_CLASS_ID(0xc8df2d77u, "BGSGamebryoSequenceGenerator")
};

} // namespace havok
