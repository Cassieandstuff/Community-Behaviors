#include "havok/classes/Animation.h"
#include "havok/core/HavokRegistry.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

// Read/Write bodies for the hka* skeleton slice, plus their registration.
// Field order and padding mirror the HKX2E C# sources exactly — the packfile
// layout is positional, so an extra or missing pad byte silently corrupts every
// field after it.

namespace havok {

// ── hkaBone ─────────────────────────────────────────────────────────────────

void hkaBone::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_name            = des.ReadStringPointer(br);
    m_lockTranslation = br.ReadBoolean();
    br.SetPosition(br.Position() + 7);  // pad to 16
}

void hkaBone::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_name);
    bw.WriteBoolean(m_lockTranslation);
    bw.SetPosition(bw.Position() + 7);
}

// ── hkaSkeletonLocalFrameOnBone ─────────────────────────────────────────────

void hkaSkeletonLocalFrameOnBone::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    des.SkipPointer(br);  // hkLocalFrame — not ported; see header
    m_boneIndex = br.ReadInt32();
    br.SetPosition(br.Position() + 4);
}

void hkaSkeletonLocalFrameOnBone::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteVoidPointer(bw);  // always null: the pointer is dropped on read
    bw.WriteInt32(m_boneIndex);
    bw.SetPosition(bw.Position() + 4);
}

// ── hkaSkeleton ─────────────────────────────────────────────────────────────

void hkaSkeleton::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_name = des.ReadStringPointer(br);

    // TWO record layouts exist in the wild, differing by an 8-byte gap between
    // m_name and the array block:
    //
    //   vanilla (120 bytes, matches the HKX2E C# class comment)
    //     +016 m_name ptr | +024 array ptr | +032 size,cap | ...
    //   variant (128 bytes; seen in Pandora Behaviour Engine template skeletons)
    //     +016 m_name ptr | +024 EIGHT BYTES | +032 array ptr | +040 size,cap
    //
    // Assuming either one breaks the other — a sweep of 60 skeletons on disk
    // splits roughly 10/50 between them, so neither is rare enough to ignore.
    // Detect instead of guessing: an hkArray header ends in a (size, capacity)
    // pair where capacity == size | 0x80000000 (the DONT_DEALLOCATE flag). That
    // pattern is specific enough to locate the first array unambiguously.
    //
    // Probe is read-only — position is restored before the real reads. Bounds
    // are checked because the probe deliberately looks past the current field.
    {
        const std::size_t afterName = br.Position();

        const auto looksLikeArrayTail = [&](std::size_t at) {
            if (at + 8 > br.Length()) return false;
            br.SetPosition(at);
            const std::uint32_t size = br.ReadUInt32();
            const std::uint32_t cap  = br.ReadUInt32();
            return cap == (size | (static_cast<std::uint32_t>(0x80) << 24));
        };

        // Vanilla puts size/cap 8 bytes after the name (just past the array
        // pointer); the variant puts it 16 bytes after.
        const bool vanilla = looksLikeArrayTail(afterName + 8);
        const bool variant = !vanilla && looksLikeArrayTail(afterName + 16);

        br.SetPosition(afterName);
        if (variant) br.Skip(8);
        // Neither matched: fall through with the vanilla layout so the shared
        // array reader raises its own, more specific assertion.
    }

    m_parentIndices   = des.ReadInt16Array(br);
    m_bones           = des.ReadClassArray<hkaBone>(br);
    m_referencePose   = des.ReadQSTransformArray(br);
    m_referenceFloats = des.ReadSingleArray(br);
    m_floatSlots      = des.ReadStringPointerArray(br);
    m_localFrames     = des.ReadClassArray<hkaSkeletonLocalFrameOnBone>(br);
}

void hkaSkeleton::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointer(bw, m_name);
    // Writes the VANILLA layout (no gap) — it matches the HKX2E reference and is
    // what the game ships. The 128-byte variant is read-only support.
    s.WriteInt16Array(bw, m_parentIndices);
    s.WriteClassArray(bw, m_bones);
    s.WriteQSTransformArray(bw, m_referencePose);
    s.WriteSingleArray(bw, m_referenceFloats);
    s.WriteStringPointerArray(bw, m_floatSlots);
    s.WriteClassArray(bw, m_localFrames);
}

// ── hkaAnnotationTrackAnnotation ────────────────────────────────────────────

void hkaAnnotationTrackAnnotation::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_time = br.ReadSingle();
    br.Skip(4);
    m_text = des.ReadStringPointer(br);
}

void hkaAnnotationTrackAnnotation::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    bw.WriteSingle(m_time);
    bw.Skip(4);
    s.WriteStringPointer(bw, m_text);
}

// ── hkaAnnotationTrack ──────────────────────────────────────────────────────

void hkaAnnotationTrack::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    m_trackName   = des.ReadStringPointer(br);
    m_annotations = des.ReadClassArray<hkaAnnotationTrackAnnotation>(br);
}

void hkaAnnotationTrack::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    s.WriteStringPointer(bw, m_trackName);
    s.WriteClassArray(bw, m_annotations);
}

// ── hkaDefaultAnimatedReferenceFrame ────────────────────────────────────────
// Base (hkReferencedObject, 16B) + m_up + m_forward + m_duration + pad(4) +
// m_referenceFrameSamples (hkArray<Vector4>). m_frameType (base
// hkaAnimatedReferenceFrame) is +nosave, so nothing precedes m_up.

void hkaDefaultAnimatedReferenceFrame::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_up                    = br.ReadVector4();
    m_forward               = br.ReadVector4();
    m_duration              = br.ReadSingle();
    br.Skip(4);  // pad to 8 before the array header (serde-hkx: field at +56)
    m_referenceFrameSamples = des.ReadVector4Array(br);
}

void hkaDefaultAnimatedReferenceFrame::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteVector4(m_up);
    bw.WriteVector4(m_forward);
    bw.WriteSingle(m_duration);
    bw.Skip(4);
    s.WriteVector4Array(bw, m_referenceFrameSamples);
}

// ── hkaAnimation ────────────────────────────────────────────────────────────

void hkaAnimation::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_type                    = br.ReadInt32();
    m_duration                = br.ReadSingle();
    m_numberOfTransformTracks = br.ReadInt32();
    m_numberOfFloatTracks     = br.ReadInt32();
    m_extractedMotion         = des.ReadClassPointer<hkaDefaultAnimatedReferenceFrame>(br);
    m_annotationTracks        = des.ReadClassArray<hkaAnnotationTrack>(br);
}

void hkaAnimation::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    bw.WriteInt32(m_type);
    bw.WriteSingle(m_duration);
    bw.WriteInt32(m_numberOfTransformTracks);
    bw.WriteInt32(m_numberOfFloatTracks);
    s.WriteClassPointer(bw, m_extractedMotion);  // null in all but 13 dragon files
    s.WriteClassArray(bw, m_annotationTracks);
}

// ── hkaInterleavedUncompressedAnimation ─────────────────────────────────────

void hkaInterleavedUncompressedAnimation::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkaAnimation::Read(des, br);
    m_transforms = des.ReadQSTransformArray(br);
    m_floats     = des.ReadSingleArray(br);
}

void hkaInterleavedUncompressedAnimation::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkaAnimation::Write(s, bw);
    s.WriteQSTransformArray(bw, m_transforms);
    s.WriteSingleArray(bw, m_floats);
}

// ── hkaSplineCompressedAnimation ────────────────────────────────────────────

void hkaSplineCompressedAnimation::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkaAnimation::Read(des, br);
    m_numFrames               = br.ReadInt32();
    m_numBlocks               = br.ReadInt32();
    m_maxFramesPerBlock       = br.ReadInt32();
    m_maskAndQuantizationSize = br.ReadInt32();
    m_blockDuration           = br.ReadSingle();
    m_blockInverseDuration    = br.ReadSingle();
    m_frameDuration           = br.ReadSingle();
    br.Skip(4);
    m_blockOffsets      = des.ReadUInt32Array(br);
    m_floatBlockOffsets = des.ReadUInt32Array(br);
    m_transformOffsets  = des.ReadUInt32Array(br);
    m_floatOffsets      = des.ReadUInt32Array(br);
    m_data              = des.ReadByteArray(br);
    m_endian            = br.ReadInt32();
    br.Skip(4);
}

void hkaSplineCompressedAnimation::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkaAnimation::Write(s, bw);
    bw.WriteInt32(m_numFrames);
    bw.WriteInt32(m_numBlocks);
    bw.WriteInt32(m_maxFramesPerBlock);
    bw.WriteInt32(m_maskAndQuantizationSize);
    bw.WriteSingle(m_blockDuration);
    bw.WriteSingle(m_blockInverseDuration);
    bw.WriteSingle(m_frameDuration);
    bw.Skip(4);
    s.WriteUInt32Array(bw, m_blockOffsets);
    s.WriteUInt32Array(bw, m_floatBlockOffsets);
    s.WriteUInt32Array(bw, m_transformOffsets);
    s.WriteUInt32Array(bw, m_floatOffsets);
    s.WriteByteArray(bw, m_data);
    bw.WriteInt32(m_endian);
    bw.Skip(4);
}

// ── hkaAnimationBinding ─────────────────────────────────────────────────────

void hkaAnimationBinding::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_originalSkeletonName        = des.ReadStringPointer(br);
    m_animation                   = des.ReadClassPointer<hkaAnimation>(br);
    m_transformTrackToBoneIndices = des.ReadInt16Array(br);
    m_floatTrackToFloatSlotIndices = des.ReadInt16Array(br);
    m_blendHint                   = br.ReadSByte();
    br.Skip(7);
}

void hkaAnimationBinding::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteStringPointer(bw, m_originalSkeletonName);
    s.WriteClassPointer(bw, m_animation);
    s.WriteInt16Array(bw, m_transformTrackToBoneIndices);
    s.WriteInt16Array(bw, m_floatTrackToFloatSlotIndices);
    bw.WriteSByte(m_blendHint);
    bw.Skip(7);
}

void hkaAnimationContainer::Read(PackFileDeserializer& des, BinaryReaderEx& br) {
    hkReferencedObject::Read(des, br);
    m_skeletons   = des.ReadClassPointerArray<hkaSkeleton>(br);
    m_animations  = des.ReadClassPointerArray<hkaAnimation>(br);
    m_bindings    = des.ReadClassPointerArray<hkaAnimationBinding>(br);
    m_attachments = des.ReadClassPointerArray<hkReferencedObject>(br);
    m_skins       = des.ReadClassPointerArray<hkReferencedObject>(br);
}

void hkaAnimationContainer::Write(PackFileSerializer& s, BinaryWriterEx& bw) const {
    hkReferencedObject::Write(s, bw);
    s.WriteClassPointerArray(bw, m_skeletons);
    s.WriteClassPointerArray(bw, m_animations);
    s.WriteClassPointerArray(bw, m_bindings);
    s.WriteClassPointerArray(bw, m_attachments);
    s.WriteClassPointerArray(bw, m_skins);
}

// ── registry ────────────────────────────────────────────────────────────────

#define REG(C) HavokRegistry::Register(#C, [] { return std::shared_ptr<IHavokObject>(std::make_shared<C>()); })

void RegisterAnimationHavokClasses() {
    REG(hkaBone);
    REG(hkaSkeletonLocalFrameOnBone);
    REG(hkaSkeleton);
    REG(hkaAnnotationTrackAnnotation);
    REG(hkaAnnotationTrack);
    REG(hkaDefaultAnimatedReferenceFrame);
    REG(hkaAnimation);
    REG(hkaInterleavedUncompressedAnimation);
    REG(hkaSplineCompressedAnimation);
    REG(hkaAnimationBinding);
    REG(hkaAnimationContainer);
}

} // namespace havok
