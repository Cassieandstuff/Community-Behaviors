#pragma once
#include "havok/classes/Base.h"
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// The hka* animation family, hand-ported from HKX2E Autogen.
//
// Covers skeletons (hkaBone, hkaSkeletonLocalFrameOnBone, hkaSkeleton) and
// animations (hkaAnimation and its two concrete forms, plus binding and
// annotation tracks) — enough for the editor's import/seek direction.
//
// hkaAnimationContainer is absent on purpose. Callers reach objects via
// PackFileDeserializer::ConstructAllOfClass rather than by walking
// hkRootLevelContainer, because a real Skyrim .hkx also carries ragdoll,
// physics, and resource variants whose classes are not registered — walking the
// container would throw on them. See the note on ConstructAllOfClass.

namespace havok {

// hkaBone — size 16, sig 0x35912f8a. Not a hkReferencedObject: a plain inline
// struct stored by value inside hkaSkeleton::m_bones.
class hkaBone : public IHavokObject {
public:
    std::string m_name;
    bool        m_lockTranslation = false;
    HK_CLASS_ID(0x35912f8au, "hkaBone")
};

// hkaSkeletonLocalFrameOnBone — size 16, sig 0x052e8043.
//
// m_localFrame is an hkLocalFrame pointer. That class is not ported, so the
// pointer slot is SKIPPED rather than resolved — resolving it would throw
// ("Havok class not registered"). Vanilla Skyrim skeletons ship an empty
// m_localFrames array, so this element reader normally never runs at all; it
// exists so a file that does carry local frames degrades to "frame dropped"
// instead of failing the whole import.
class hkaSkeletonLocalFrameOnBone : public IHavokObject {
public:
    std::int32_t m_boneIndex = 0;
    HK_CLASS_ID(0x052e8043u, "hkaSkeletonLocalFrameOnBone")
};

// hkaSkeleton — size 120, sig 0x366e8220.
//
// m_parentIndices and m_bones are parallel arrays: bone i's parent is
// m_parentIndices[i] (-1 for a root). m_referencePose is the bind pose, one
// QSTransform per bone, in bone-local space.
class hkaSkeleton : public hkReferencedObject {
public:
    std::string                             m_name;
    std::vector<std::int16_t>               m_parentIndices;
    std::vector<hkaBone>                    m_bones;
    std::vector<QSTransform>                m_referencePose;
    std::vector<float>                      m_referenceFloats;
    std::vector<std::string>                m_floatSlots;
    std::vector<hkaSkeletonLocalFrameOnBone> m_localFrames;
    HK_CLASS_ID(0x366e8220u, "hkaSkeleton")
};

// hkaAnnotationTrackAnnotation — size 16, sig 0x623bf34f. One timestamped text
// entry. SCT carries face-morph keyframes in these (the "MorphFace.<system>|
// <morph>|<weight>" payloads FaceClip parses).
class hkaAnnotationTrackAnnotation : public IHavokObject {
public:
    float       m_time = 0.f;
    std::string m_text;
    HK_CLASS_ID(0x623bf34fu, "hkaAnnotationTrackAnnotation")
};

// hkaAnnotationTrack — size 24, sig 0xd4114fdd. One track per transform track.
class hkaAnnotationTrack : public IHavokObject {
public:
    std::string                              m_trackName;
    std::vector<hkaAnnotationTrackAnnotation> m_annotations;
    HK_CLASS_ID(0xd4114fddu, "hkaAnnotationTrack")
};

// hkaDefaultAnimatedReferenceFrame — size 80, sig 0x6d85e445.
//
// Havok's stored "extracted motion" of an animation's root track. Its serialized
// form is exactly hkReferencedObject's 16 bytes plus the four fields below; the
// base hkaAnimatedReferenceFrame carries only m_frameType, which is +nosave (not
// serialized), so it needs no C++ class of its own here.
//
//   +016 m_up        Vector4   world "up" the motion is measured against ((0,0,1) for Skyrim)
//   +032 m_forward   Vector4   world "forward" ((0,1,0) / (1,0,0) depending on the rig)
//   +048 m_duration  float     motion length in seconds (== the animation duration)
//   +052 (pad 4)
//   +056 m_referenceFrameSamples  hkArray<Vector4>
//
// Each sample is one FRAME of extracted root motion: (x,y,z) is the cumulative
// translation and w is the cumulative yaw angle (radians) about m_up. The sample
// array is dense at 30 fps: N == round(duration*30)+1 samples at t = i/30.
//
// This is the object animationdatasinglefile.txt's MotionRecord is built from —
// BUT it is present in only 13 shipped animations game-wide (all dragon flight);
// every other animation stores a null m_extractedMotion and the toolchain rebuilt
// the cache's motion by sampling the root TRACK directly. See AnimDataDeriver.
//
// Only the "Default" concrete form ships in Skyrim; the parametric variant
// (hkaParameterizedAnimationReferenceFrame) never appears, so it is unported and
// a file carrying one would throw on resolve rather than silently drop it.
class hkaDefaultAnimatedReferenceFrame : public hkReferencedObject {
public:
    Vector4              m_up{};
    Vector4              m_forward{};
    float                m_duration = 0.f;
    std::vector<Vector4> m_referenceFrameSamples;
    HK_CLASS_ID(0x6d85e445u, "hkaDefaultAnimatedReferenceFrame")
};

// hkaAnimation — size 64, sig 0xa6fa7e88. Abstract base of the concrete forms.
//
// m_extractedMotion (the hkaDefaultAnimatedReferenceFrame above) is RESOLVED. It
// is null in almost all shipped animations (only 13 dragon-flight files carry a
// real one), so this is usually nullptr and re-serializes to a null pointer.
class hkaAnimation : public hkReferencedObject {
public:
    std::int32_t                    m_type = 0;
    float                           m_duration = 0.f;
    std::int32_t                    m_numberOfTransformTracks = 0;
    std::int32_t                    m_numberOfFloatTracks = 0;
    std::shared_ptr<hkaDefaultAnimatedReferenceFrame> m_extractedMotion;
    std::vector<hkaAnnotationTrack> m_annotationTracks;
    HK_CLASS_ID(0xa6fa7e88u, "hkaAnimation")
};

// hkaInterleavedUncompressedAnimation — sig 0x930af031. Raw per-frame poses:
// m_transforms is numFrames * numberOfTransformTracks QSTransforms.
class hkaInterleavedUncompressedAnimation : public hkaAnimation {
public:
    std::vector<QSTransform> m_transforms;
    std::vector<float>       m_floats;
    HK_CLASS_ID(0x930af031u, "hkaInterleavedUncompressedAnimation")
};

// hkaSplineCompressedAnimation — sig 0x792ee0bb. The format almost every shipped
// Skyrim animation uses. m_data is an opaque blob of NURBS control points and
// quantised rotations; the decoder in sct-pipeline reads it with the block
// metadata below.
class hkaSplineCompressedAnimation : public hkaAnimation {
public:
    std::int32_t               m_numFrames = 0;
    std::int32_t               m_numBlocks = 0;
    std::int32_t               m_maxFramesPerBlock = 0;
    std::int32_t               m_maskAndQuantizationSize = 0;
    float                      m_blockDuration = 0.f;
    float                      m_blockInverseDuration = 0.f;
    float                      m_frameDuration = 0.f;
    std::vector<std::uint32_t> m_blockOffsets;
    std::vector<std::uint32_t> m_floatBlockOffsets;
    std::vector<std::uint32_t> m_transformOffsets;
    std::vector<std::uint32_t> m_floatOffsets;
    std::vector<std::uint8_t>  m_data;
    std::int32_t               m_endian = 0;
    HK_CLASS_ID(0x792ee0bbu, "hkaSplineCompressedAnimation")
};

// hkaAnimationBinding — size 64, sig 0x66eac971. Maps the animation's transform
// tracks onto skeleton bone indices. Absent or empty means identity.
class hkaAnimationBinding : public hkReferencedObject {
public:
    std::string                   m_originalSkeletonName;
    // Resolved, not skipped: the concrete hkaAnimation subclasses are registered,
    // so this correctly associates a binding with its animation in files that
    // carry more than one.
    std::shared_ptr<hkaAnimation> m_animation;
    std::vector<std::int16_t>     m_transformTrackToBoneIndices;
    std::vector<std::int16_t>   m_floatTrackToFloatSlotIndices;
    std::int8_t                 m_blendHint = 0;
    HK_CLASS_ID(0x66eac971u, "hkaAnimationBinding")
};

// hkaAnimationContainer — the canonical root wrapper for an animation packfile.
// Added for the EMIT direction (AnimationDef -> .hkx): the serializer walks
// hkRootLevelContainer -> this -> m_animations/m_bindings. The READ/import path
// still reaches objects via ConstructAllOfClass (see the note at the top), so it
// never has to walk this on files carrying unregistered physics/ragdoll variants.
// m_attachments/m_skins (hkaBoneAttachment/hkaMeshBinding in Havok) are always
// empty here, so they are typed as base pointers rather than porting those classes.
class hkaAnimationContainer : public hkReferencedObject {
public:
    std::vector<std::shared_ptr<hkaSkeleton>>          m_skeletons;
    std::vector<std::shared_ptr<hkaAnimation>>         m_animations;
    std::vector<std::shared_ptr<hkaAnimationBinding>>  m_bindings;
    std::vector<std::shared_ptr<hkReferencedObject>>   m_attachments;  // hkaBoneAttachment (unused)
    std::vector<std::shared_ptr<hkReferencedObject>>   m_skins;        // hkaMeshBinding (unused)
    HK_CLASS_ID(0x8dc20333u, "hkaAnimationContainer")
};

// Registers the hka* classes above. Called by RegisterAllHavokClasses().
void RegisterAnimationHavokClasses();

} // namespace havok
