// SplineCompressor — direct C++ port of HKBuild's SplineAnimCompressor.cs.
// See the header for the byte-exactness contract. Block/spline layout mirrors
// HavokLib hka_spline_decompressor.cpp; verified by round-tripping through the
// first-party decoder (sct-pipeline HavokAnimation).

#include "havok/anim/SplineCompressor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace havok::anim {
namespace {

// ── Little-endian byte sink (mirrors C# BinaryWriter over a MemoryStream) ─────
struct ByteBuf {
    std::vector<std::uint8_t> bytes;
    void u8(std::uint8_t v) { bytes.push_back(v); }
    void u16(std::uint16_t v) { bytes.push_back(std::uint8_t(v & 0xFF)); bytes.push_back(std::uint8_t((v >> 8) & 0xFF)); }
    void f32(float v) {
        std::uint32_t u;
        std::memcpy(&u, &v, 4);
        bytes.push_back(std::uint8_t(u & 0xFF));
        bytes.push_back(std::uint8_t((u >> 8) & 0xFF));
        bytes.push_back(std::uint8_t((u >> 16) & 0xFF));
        bytes.push_back(std::uint8_t((u >> 24) & 0xFF));
    }
    std::size_t pos() const { return bytes.size(); }
    void alignTo(std::size_t a) { while (bytes.size() % a) bytes.push_back(0); }
};

enum class ChannelType { Identity, Static, Dynamic };
enum class FloatChannelType { Identity, Static, Dynamic };

struct TransformFrame {
    float Tx, Ty, Tz;
    float Rx, Ry, Rz, Rw;
    float Sx, Sy, Sz;
};

// ── Keyframe interpolation ────────────────────────────────────────────────────
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

void SampleVec3(const std::vector<Vec3Keyframe>& kfs, float time, float def, float out[3]) {
    if (kfs.empty()) { out[0] = out[1] = out[2] = def; return; }
    if (kfs.size() == 1 || time <= kfs[0].time) {
        out[0] = kfs[0].value[0]; out[1] = kfs[0].value[1]; out[2] = kfs[0].value[2]; return;
    }
    if (time >= kfs.back().time) {
        out[0] = kfs.back().value[0]; out[1] = kfs.back().value[1]; out[2] = kfs.back().value[2]; return;
    }
    std::size_t i = 0;
    while (i < kfs.size() - 2 && kfs[i + 1].time <= time) ++i;
    float t = (time - kfs[i].time) / (kfs[i + 1].time - kfs[i].time);
    out[0] = Lerp(kfs[i].value[0], kfs[i + 1].value[0], t);
    out[1] = Lerp(kfs[i].value[1], kfs[i + 1].value[1], t);
    out[2] = Lerp(kfs[i].value[2], kfs[i + 1].value[2], t);
}

void SampleQuat(const std::vector<QuatKeyframe>& kfs, float time, float out[4]) {
    if (kfs.empty()) { out[0] = out[1] = out[2] = 0.f; out[3] = 1.f; return; }
    if (kfs.size() == 1 || time <= kfs[0].time) {
        for (int k = 0; k < 4; ++k) out[k] = kfs[0].value[k]; return;
    }
    if (time >= kfs.back().time) {
        for (int k = 0; k < 4; ++k) out[k] = kfs.back().value[k]; return;
    }
    std::size_t i = 0;
    while (i < kfs.size() - 2 && kfs[i + 1].time <= time) ++i;
    float t = (time - kfs[i].time) / (kfs[i + 1].time - kfs[i].time);
    float ax = kfs[i].value[0], ay = kfs[i].value[1], az = kfs[i].value[2], aw = kfs[i].value[3];
    float bx = kfs[i + 1].value[0], by = kfs[i + 1].value[1], bz = kfs[i + 1].value[2], bw = kfs[i + 1].value[3];
    // NLerp with shortest-path flip.
    float dot = ax * bx + ay * by + az * bz + aw * bw;
    if (dot < 0.f) { bx = -bx; by = -by; bz = -bz; bw = -bw; }
    float rx = ax + (bx - ax) * t, ry = ay + (by - ay) * t, rz = az + (bz - az) * t, rw = aw + (bw - aw) * t;
    float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    if (len < 1e-6f) { out[0] = out[1] = out[2] = 0.f; out[3] = 1.f; return; }
    out[0] = rx / len; out[1] = ry / len; out[2] = rz / len; out[3] = rw / len;
}

float SampleFloat(const std::vector<FloatKeyframe>& kfs, float time) {
    if (kfs.empty()) return 0.f;
    if (kfs.size() == 1 || time <= kfs[0].time) return kfs[0].value;
    if (time >= kfs.back().time) return kfs.back().value;
    std::size_t i = 0;
    while (i < kfs.size() - 2 && kfs[i + 1].time <= time) ++i;
    float t = (time - kfs[i].time) / (kfs[i + 1].time - kfs[i].time);
    return Lerp(kfs[i].value, kfs[i + 1].value, t);
}

std::vector<std::vector<TransformFrame>> SampleTransformTracks(const AnimationDef& anim, int numFrames, float frameDuration) {
    std::vector<std::vector<TransformFrame>> result(anim.tracks.size());
    for (std::size_t t = 0; t < anim.tracks.size(); ++t) {
        const AnimTrackDef& track = anim.tracks[t];
        result[t].resize(numFrames);
        for (int f = 0; f < numFrames; ++f) {
            float time = f * frameDuration;
            float pos[3], rot[4], scale[3];
            SampleVec3(track.translation, time, 0.f, pos);
            SampleQuat(track.rotation, time, rot);
            SampleVec3(track.scale, time, 1.f, scale);
            result[t][f] = TransformFrame{ pos[0], pos[1], pos[2],
                                           rot[0], rot[1], rot[2], rot[3],
                                           scale[0], scale[1], scale[2] };
        }
    }
    return result;
}

std::vector<std::vector<float>> SampleFloatTracks(const AnimationDef& anim, int numFrames, float frameDuration) {
    std::vector<std::vector<float>> result(anim.floatTracks.size());
    for (std::size_t t = 0; t < anim.floatTracks.size(); ++t) {
        result[t].resize(numFrames);
        for (int f = 0; f < numFrames; ++f)
            result[t][f] = SampleFloat(anim.floatTracks[t].keyframes, f * frameDuration);
    }
    return result;
}

// ── Channel classification ────────────────────────────────────────────────────
bool IsIdentityTranslation(const TransformFrame& f) {
    return std::fabs(f.Tx) < 1e-5f && std::fabs(f.Ty) < 1e-5f && std::fabs(f.Tz) < 1e-5f;
}
bool IsIdentityRotation(const TransformFrame& f) {
    return std::fabs(f.Rx) < 1e-5f && std::fabs(f.Ry) < 1e-5f && std::fabs(f.Rz) < 1e-5f && std::fabs(f.Rw - 1.f) < 1e-5f;
}
bool IsIdentityScale(const TransformFrame& f) {
    return std::fabs(f.Sx - 1.f) < 1e-5f && std::fabs(f.Sy - 1.f) < 1e-5f && std::fabs(f.Sz - 1.f) < 1e-5f;
}

ChannelType ClassifyTranslation(const TransformFrame* frames, int start, int count, float tol) {
    const TransformFrame& f0 = frames[start];
    bool allIdentity = true, allSame = true;
    for (int i = 0; i < count; ++i) {
        const TransformFrame& f = frames[start + i];
        if (!IsIdentityTranslation(f)) allIdentity = false;
        if (std::fabs(f.Tx - f0.Tx) > tol || std::fabs(f.Ty - f0.Ty) > tol || std::fabs(f.Tz - f0.Tz) > tol) allSame = false;
    }
    if (allIdentity) return ChannelType::Identity;
    if (allSame) return ChannelType::Static;
    return ChannelType::Dynamic;
}

ChannelType ClassifyRotation(const TransformFrame* frames, int start, int count, float tol) {
    const TransformFrame& f0 = frames[start];
    bool allIdentity = true, allSame = true;
    for (int i = 0; i < count; ++i) {
        const TransformFrame& f = frames[start + i];
        if (!IsIdentityRotation(f)) allIdentity = false;
        float dot = f.Rx * f0.Rx + f.Ry * f0.Ry + f.Rz * f0.Rz + f.Rw * f0.Rw;
        if (std::fabs(dot) < 1.f - tol) allSame = false;
    }
    if (allIdentity) return ChannelType::Identity;
    if (allSame) return ChannelType::Static;
    return ChannelType::Dynamic;
}

ChannelType ClassifyScale(const TransformFrame* frames, int start, int count, float tol) {
    const TransformFrame& f0 = frames[start];
    bool allIdentity = true, allSame = true;
    for (int i = 0; i < count; ++i) {
        const TransformFrame& f = frames[start + i];
        if (!IsIdentityScale(f)) allIdentity = false;
        if (std::fabs(f.Sx - f0.Sx) > tol || std::fabs(f.Sy - f0.Sy) > tol || std::fabs(f.Sz - f0.Sz) > tol) allSame = false;
    }
    if (allIdentity) return ChannelType::Identity;
    if (allSame) return ChannelType::Static;
    return ChannelType::Dynamic;
}

FloatChannelType ClassifyFloat(const float* frames, int start, int count) {
    float v0 = frames[start];
    if (std::fabs(v0) < 1e-5f) {
        bool allZero = true;
        for (int i = 1; i < count; ++i)
            if (std::fabs(frames[start + i]) > 1e-5f) { allZero = false; break; }
        if (allZero) return FloatChannelType::Identity;
    }
    bool allSame = true;
    for (int i = 1; i < count; ++i)
        if (std::fabs(frames[start + i] - v0) > 1e-5f) { allSame = false; break; }
    return allSame ? FloatChannelType::Static : FloatChannelType::Dynamic;
}

// ── Mask encoding ─────────────────────────────────────────────────────────────
void WriteMask(ByteBuf& bw, ChannelType pos, ChannelType rot, ChannelType scale) {
    // byte 0: quantizationTypes — pos BITS16(=1), rot THREECOMP40(raw enum=1) in
    // bits[5:2], scale BITS16(=1). Vanilla 0x45.
    std::uint8_t posQ = 1, rotQ = 1, scaleQ = 1;
    std::uint8_t qTypes = std::uint8_t((posQ & 0x3) | ((rotQ & 0xF) << 2) | ((scaleQ & 0x3) << 6));

    std::uint8_t posTypes = 0;
    if (pos == ChannelType::Static)  posTypes = 0x07;
    if (pos == ChannelType::Dynamic) posTypes = 0x70;

    std::uint8_t rotTypes = 0;
    if (rot == ChannelType::Static)  rotTypes = 0x01;
    if (rot == ChannelType::Dynamic) rotTypes = 0x10;

    std::uint8_t scaleTypes = 0;
    if (scale == ChannelType::Static)  scaleTypes = 0x07;
    if (scale == ChannelType::Dynamic) scaleTypes = 0x70;

    bw.u8(qTypes); bw.u8(posTypes); bw.u8(rotTypes); bw.u8(scaleTypes);
}

void WriteFloatMask(ByteBuf& bw, FloatChannelType ft) {
    // 0x03 = STATIC single f32 — verified as the real Havok encoding against a
    // large loose-animation corpus (every float track uses it). 0x00 = identity.
    // 0x50 = a dynamic (high-nibble) marker matching SplineDecompressor's scalar
    // spline path; no real file uses dynamic floats, so it is self-consistent
    // with the decoder but not validated against Havok.
    std::uint8_t mask = 0x00;
    if (ft == FloatChannelType::Static)  mask = 0x03;
    if (ft == FloatChannelType::Dynamic) mask = 0x50;
    bw.u8(mask);
}

// ── Knot vector (matches the decoder, NOT the C# source) ──────────────────────
// Clamped-uniform knots in FRAME-INDEX units (0..numCtrlPts-1) stored as bytes.
// The decoder evaluates each spline at the integer frame index, so knots must be
// frame indices — the C# source rescaled them to [0,255], which never round-trips.
// numKnots = numCtrlPts + degree + 1; first/last (degree+1) are clamped.
std::vector<std::uint8_t> BuildKnotVector(int numCtrlPts, int degree) {
    int numKnots = numCtrlPts + degree + 1;
    std::vector<std::uint8_t> knots(numKnots, 0);
    for (int i = 0; i < numKnots; ++i) {
        int v;
        if (i <= degree)                         v = 0;
        else if (i >= numKnots - 1 - degree)     v = numCtrlPts - 1;
        else                                     v = i - degree;   // interior 1,2,...
        knots[i] = std::uint8_t(std::clamp(v, 0, 255));
    }
    return knots;
}

// ── Dynamic vector spline (decoder VecCurve layout) ───────────────────────────
// For K component arrays (each `count` samples): one shared spline header, then
// per-component [min,max], then interleaved u16 control points (ctrl-point major,
// component minor). Degree is forced to 1 so the control points ARE the samples
// and evaluation at frame i returns sample i. bpc = 2 (BITS16).
void WriteDynamicComponents(ByteBuf& bw, const std::vector<std::vector<float>>& comps) {
    const int count   = int(comps[0].size());
    const int degree  = 1;
    const int numCtrl = std::max(count, degree + 1);
    const int n       = numCtrl - 1;   // wire stores the MAX ctrl-point index
    const int K       = int(comps.size());

    bw.u16(std::uint16_t(n));
    bw.u8(std::uint8_t(degree));
    for (std::uint8_t k : BuildKnotVector(numCtrl, degree)) bw.u8(k);
    bw.alignTo(4);                       // VecCurve aligns after the knot bytes

    auto val = [&](int j, int i) { return comps[j][std::min(i, count - 1)]; };  // pad with last

    std::vector<float> mn(K), mx(K);
    for (int j = 0; j < K; ++j) {
        mn[j] = mx[j] = val(j, 0);
        for (int i = 1; i < numCtrl; ++i) { float f = val(j, i); mn[j] = std::min(mn[j], f); mx[j] = std::max(mx[j], f); }
        bw.f32(mn[j]); bw.f32(mx[j]);
    }
    for (int i = 0; i < numCtrl; ++i)
        for (int j = 0; j < K; ++j) {
            float range = mx[j] - mn[j];
            if (range < 1e-7f) range = 1e-7f;
            float nrm = (val(j, i) - mn[j]) / range;
            bw.u16(std::uint16_t(std::clamp(int(nrm * 65535.f + 0.5f), 0, 65535)));
        }
}

// Static vector: three raw floats (decoder reads sV[0..2]; no header/knots/ctrl).
void WriteStaticVec3(ByteBuf& bw, float x, float y, float z) {
    bw.f32(x); bw.f32(y); bw.f32(z);
}

// ── THREECOMP40 rotation ──────────────────────────────────────────────────────
int QuantTo12Bit(float v, float halfRange) {
    float normalized = (v + halfRange) / (2.f * halfRange);
    return std::clamp(int(normalized * 4095.f + 0.5f), 0, 4095);
}

void WriteThreeComp40Single(ByteBuf& bw, float rx, float ry, float rz, float rw) {
    float len = std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    if (len < 1e-6f) { rx = 0.f; ry = 0.f; rz = 0.f; rw = 1.f; }
    else { rx /= len; ry /= len; rz /= len; rw /= len; }

    float comps[4] = { rx, ry, rz, rw };
    float absc[4]  = { std::fabs(rx), std::fabs(ry), std::fabs(rz), std::fabs(rw) };
    int dropIdx = 0;
    for (int i = 1; i < 4; ++i) if (absc[i] > absc[dropIdx]) dropIdx = i;

    if (comps[dropIdx] < 0.f) { rx = -rx; ry = -ry; rz = -rz; rw = -rw; }
    comps[0] = rx; comps[1] = ry; comps[2] = rz; comps[3] = rw;

    std::uint8_t signBit = 0;
    std::uint8_t resultShift = std::uint8_t(dropIdx);

    float stored[3];
    int si = 0;
    for (int i = 0; i < 4; ++i) if (i != dropIdx) stored[si++] = comps[i];

    const float kScale = 1.f / 1.41421356f;
    int Va = QuantTo12Bit(stored[0], kScale);
    int Vb = QuantTo12Bit(stored[1], kScale);
    int Vc = QuantTo12Bit(stored[2], kScale);

    std::uint8_t b0 = std::uint8_t(Va & 0xFF);
    std::uint8_t b1 = std::uint8_t(((Va >> 8) & 0xF) | ((Vb & 0xF) << 4));
    std::uint8_t b2 = std::uint8_t((Vb >> 4) & 0xFF);
    std::uint8_t b3 = std::uint8_t(Vc & 0xFF);
    std::uint8_t b4 = std::uint8_t(((Vc >> 8) & 0xF) | ((resultShift & 0x3) << 4) | ((signBit & 0x1) << 6));

    bw.u8(b0); bw.u8(b1); bw.u8(b2); bw.u8(b3); bw.u8(b4);
}

void WriteThreeComp40Spline(ByteBuf& bw, const TransformFrame* frames, int start, int count) {
    const int degree  = 1;
    const int numCtrl = std::max(count, degree + 1);
    const int n       = numCtrl - 1;
    bw.u16(std::uint16_t(n));
    bw.u8(std::uint8_t(degree));
    for (std::uint8_t k : BuildKnotVector(numCtrl, degree)) bw.u8(k);
    // NOTE: TC40 has NO align4 after the knot bytes (unlike VecCurve).
    for (int i = 0; i < numCtrl; ++i) {
        const TransformFrame& f = frames[start + std::min(i, count - 1)];
        WriteThreeComp40Single(bw, f.Rx, f.Ry, f.Rz, f.Rw);
    }
}

// ── Track data writers ────────────────────────────────────────────────────────
void WriteTranslationData(ByteBuf& bw, const TransformFrame* frames, int start, int count, ChannelType type) {
    if (type == ChannelType::Identity) return;
    if (type == ChannelType::Static) {
        WriteStaticVec3(bw, frames[start].Tx, frames[start].Ty, frames[start].Tz);
        return;
    }
    std::vector<std::vector<float>> c(3, std::vector<float>(count));
    for (int i = 0; i < count; ++i) { c[0][i] = frames[start + i].Tx; c[1][i] = frames[start + i].Ty; c[2][i] = frames[start + i].Tz; }
    WriteDynamicComponents(bw, c);
}

void WriteRotationData(ByteBuf& bw, const TransformFrame* frames, int start, int count, ChannelType type) {
    if (type == ChannelType::Identity) return;
    if (type == ChannelType::Static) {
        WriteThreeComp40Single(bw, frames[start].Rx, frames[start].Ry, frames[start].Rz, frames[start].Rw);
        return;
    }
    WriteThreeComp40Spline(bw, frames, start, count);
}

void WriteScaleData(ByteBuf& bw, const TransformFrame* frames, int start, int count, ChannelType type) {
    if (type == ChannelType::Identity) return;
    if (type == ChannelType::Static) {
        WriteStaticVec3(bw, frames[start].Sx, frames[start].Sy, frames[start].Sz);
        return;
    }
    std::vector<std::vector<float>> c(3, std::vector<float>(count));
    for (int i = 0; i < count; ++i) { c[0][i] = frames[start + i].Sx; c[1][i] = frames[start + i].Sy; c[2][i] = frames[start + i].Sz; }
    WriteDynamicComponents(bw, c);
}

void WriteFloatData(ByteBuf& bw, const float* frames, int start, int count, FloatChannelType type) {
    // NOTE: the first-party decoder does not yet decode float tracks, so this
    // layout is UNVERIFIED (no shipping tree exercises it). Written as a
    // 1-component dynamic vec / single static float for forward-compatibility.
    if (type == FloatChannelType::Identity) return;
    if (type == FloatChannelType::Static) { bw.f32(frames[start]); return; }
    std::vector<std::vector<float>> c(1, std::vector<float>(count));
    for (int i = 0; i < count; ++i) c[0][i] = frames[start + i];
    WriteDynamicComponents(bw, c);
}

// ── Block writer ──────────────────────────────────────────────────────────────
void WriteBlock(ByteBuf& bw,
                const std::vector<std::vector<TransformFrame>>& transFrames,
                const std::vector<std::vector<float>>& floatFrames,
                int numTransform, int numFloat, int blockStart, int framesInBlock,
                const AnimCompressionParams& comp) {
    std::vector<ChannelType> posMask(numTransform), rotMask(numTransform), scaleMask(numTransform);
    std::vector<FloatChannelType> floatMask(numFloat);

    for (int t = 0; t < numTransform; ++t) {
        const TransformFrame* fr = transFrames[t].data();
        posMask[t]   = ClassifyTranslation(fr, blockStart, framesInBlock, comp.translationTolerance);
        rotMask[t]   = ClassifyRotation(fr, blockStart, framesInBlock, comp.rotationTolerance);
        scaleMask[t] = ClassifyScale(fr, blockStart, framesInBlock, comp.scaleTolerance);
    }
    for (int t = 0; t < numFloat; ++t)
        floatMask[t] = ClassifyFloat(floatFrames[t].data(), blockStart, framesInBlock);

    for (int t = 0; t < numTransform; ++t) WriteMask(bw, posMask[t], rotMask[t], scaleMask[t]);
    for (int t = 0; t < numFloat; ++t) WriteFloatMask(bw, floatMask[t]);

    bw.alignTo(4);

    // Per-track data with the decoder's alignment: align4 after translation,
    // after rotation, and after scale (the C# source aligned only once).
    for (int t = 0; t < numTransform; ++t) {
        const TransformFrame* fr = transFrames[t].data();
        WriteTranslationData(bw, fr, blockStart, framesInBlock, posMask[t]);
        bw.alignTo(4);
        WriteRotationData(bw, fr, blockStart, framesInBlock, rotMask[t]);
        bw.alignTo(4);
        WriteScaleData(bw, fr, blockStart, framesInBlock, scaleMask[t]);
        bw.alignTo(4);
    }

    for (int t = 0; t < numFloat; ++t)
        WriteFloatData(bw, floatFrames[t].data(), blockStart, framesInBlock, floatMask[t]);
}

} // namespace

// ── Public entry point ────────────────────────────────────────────────────────
CompressedResult CompressAnimation(const AnimationDef& anim, int fps) {
    int numTransform = int(anim.tracks.size());
    int numFloat     = int(anim.floatTracks.size());
    int maxFpb       = anim.compression.maxFramesPerBlock;

    // numFrames = round(Duration*fps)+1. std::nearbyint honours the default
    // round-to-nearest-even mode, matching C#'s Math.Round.
    int numFrames = int(std::nearbyint(double(anim.duration) * fps)) + 1;
    float frameDuration = 1.0f / fps;

    int numBlocks = (numFrames + maxFpb - 2) / (maxFpb - 1);
    if (numBlocks < 1) numBlocks = 1;
    int framesPerBlock = maxFpb;

    float blockDuration = frameDuration * (framesPerBlock - 1);
    float blockInvDuration = 1.0f / blockDuration;

    int maskAndQuantSize = numTransform * 4 + numFloat * 1;

    auto transFrames = SampleTransformTracks(anim, numFrames, frameDuration);
    auto floatFrames = SampleFloatTracks(anim, numFrames, frameDuration);

    ByteBuf bw;
    std::vector<std::uint32_t> blockOffsets;

    for (int b = 0; b < numBlocks; ++b) {
        int blockStart = b * (framesPerBlock - 1);
        int blockEnd = std::min(blockStart + framesPerBlock - 1, numFrames - 1);
        int framesInBlock = blockEnd - blockStart + 1;

        blockOffsets.push_back(std::uint32_t(bw.pos()));
        WriteBlock(bw, transFrames, floatFrames, numTransform, numFloat,
                   blockStart, framesInBlock, anim.compression);
    }

    CompressedResult r;
    r.numFrames               = numFrames;
    r.numBlocks               = numBlocks;
    r.maxFramesPerBlock       = maxFpb;
    r.blockDuration           = blockDuration;
    r.blockInverseDuration    = blockInvDuration;
    r.frameDuration           = frameDuration;
    r.maskAndQuantizationSize = maskAndQuantSize;
    r.blockOffsets            = std::move(blockOffsets);
    r.data                    = std::move(bw.bytes);
    return r;
}

} // namespace havok::anim
