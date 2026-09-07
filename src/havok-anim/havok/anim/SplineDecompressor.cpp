// SplineDecompressor — glm-free port of the verified sct-pipeline decoder.
// Arithmetic is identical; glm::vec3/glm::quat became plain structs and
// glm::slerp/normalize/dot were inlined to match glm exactly (so the port is
// bit-faithful — verified by a differential sweep vs the original on real files).

#include "havok/anim/SplineDecompressor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace havok::anim {
namespace {

struct V3 { float x = 0.f, y = 0.f, z = 0.f; };
struct Q4 { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };

float dot(const Q4& a, const Q4& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
Q4 neg(const Q4& a) { return Q4{ -a.x, -a.y, -a.z, -a.w }; }
Q4 normalize(const Q4& a) {
    float len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z + a.w * a.w);
    if (len <= 0.f) return Q4{ 0.f, 0.f, 0.f, 1.f };
    return Q4{ a.x / len, a.y / len, a.z / len, a.w / len };
}
// Exact replica of glm::slerp (no shortest-path flip — the caller flips first,
// as the original decoder does). Returns the raw formula; caller normalizes.
Q4 slerp_raw(const Q4& x, const Q4& y, float a) {
    float cosTheta = dot(x, y);
    if (cosTheta > 1.f - std::numeric_limits<float>::epsilon()) {
        return Q4{ x.x + (y.x - x.x) * a, x.y + (y.y - x.y) * a,
                   x.z + (y.z - x.z) * a, x.w + (y.w - x.w) * a };
    }
    float angle = std::acos(cosTheta);
    float s0 = std::sin((1.f - a) * angle), s1 = std::sin(a * angle), sd = std::sin(angle);
    return Q4{ (s0 * x.x + s1 * y.x) / sd, (s0 * x.y + s1 * y.y) / sd,
               (s0 * x.z + s1 * y.z) / sd, (s0 * x.w + s1 * y.w) / sd };
}

// ── bounds-safe byte reader (copy of sct-pipeline BoundsSafeReader) ────────────
struct Reader {
    const std::uint8_t* p;
    const std::uint8_t* end;
    bool ok;
    Reader(const std::uint8_t* start, const std::uint8_t* e) : p(start), end(e), ok(start <= e) {}
    float F32() { if (p + 4 > end) { ok = false; return 0.f; } float v; std::memcpy(&v, p, 4); p += 4; return v; }
    std::uint16_t U16() { if (p + 2 > end) { ok = false; return 0; } std::uint16_t v; std::memcpy(&v, p, 2); p += 2; return v; }
    std::uint8_t U8() { if (p >= end) { ok = false; return 0; } return *p++; }
    void skip(int n) { if (n < 0 || p + n > end) { ok = false; p = end; return; } p += n; }
    void align4(const std::uint8_t* base) {
        std::uintptr_t off = static_cast<std::uintptr_t>(p - base);
        const std::uint8_t* aligned = base + ((off + 3u) & ~std::uintptr_t(3u));
        if (aligned > end) { ok = false; p = end; return; }
        p = aligned;
    }
};

// The de Boor evaluators work for any B-spline degree; the only fixed-size resource is the working
// array d[deg+1]. Cap the degree so that array can live on the stack (no per-frame heap alloc in the
// hot path) while still covering every real Havok spline — vanilla goes up to degree 6 (some clutter/
// trap "sequence" clips encode a float channel that way). A degree above this cap is malformed/garbage
// and is rejected cleanly (the reader sets ok=false) rather than overrunning the stack.
constexpr int kMaxSplineDegree = 15;

// ── rotation quantization ─────────────────────────────────────────────────────
enum RotQuant { RQ_POLAR32 = 0, RQ_THREECOMP40 = 1, RQ_THREECOMP48 = 2, RQ_THREECOMP24 = 3, RQ_STRAIGHT16 = 4, RQ_UNCOMPRESSED = 5 };

int RotQuantBytes(int q) {
    switch (q) {
        case RQ_POLAR32:      return 4;
        case RQ_THREECOMP40:  return 5;
        case RQ_THREECOMP48:  return 6;
        case RQ_THREECOMP24:  return 3;
        case RQ_STRAIGHT16:   return 2;
        case RQ_UNCOMPRESSED: return 16;
        default:              return 0;
    }
}
bool RotQuantSupported(int q) { return q == RQ_THREECOMP40 || q == RQ_THREECOMP48 || q == RQ_UNCOMPRESSED; }

Q4 AssembleThreeComp(float a, float b, float c, int droppedIdx, bool negate) {
    const float s[3] = { a, b, c };
    float d = std::sqrt(std::max(0.f, 1.f - s[0] * s[0] - s[1] * s[1] - s[2] * s[2]));
    if (negate) d = -d;
    float q[4];
    q[droppedIdx] = d;
    int si = 0;
    for (int i = 0; i < 4; i++) if (i != droppedIdx) q[i] = s[si++];
    return Q4{ q[0], q[1], q[2], q[3] };
}

Q4 DecTC40(const std::uint8_t* b) {
    std::uint32_t Va = b[0] | ((std::uint32_t)(b[1] & 0xF) << 8);
    std::uint32_t Vb = (std::uint32_t)((b[1] >> 4) & 0xF) | ((std::uint32_t)b[2] << 4);
    std::uint32_t Vc = b[3] | ((std::uint32_t)(b[4] & 0xF) << 8);
    const int  rs   = (b[4] >> 4) & 0x3;
    const bool sign = (b[4] >> 6) & 0x1;
    constexpr float kInvSqrt2 = 0.70710678118f;
    auto dq = [&](std::uint32_t q) { return (q / 4095.f) * (2.f * kInvSqrt2) - kInvSqrt2; };
    return AssembleThreeComp(dq(Va), dq(Vb), dq(Vc), rs, sign);
}

Q4 DecTC48(const std::uint8_t* b) {
    const std::uint16_t X = static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    const std::uint16_t Y = static_cast<std::uint16_t>(b[2] | (b[3] << 8));
    const std::uint16_t Z = static_cast<std::uint16_t>(b[4] | (b[5] << 8));
    const int  rs   = ((Y >> 14) & 2) | ((X >> 15) & 1);
    const bool sign = (Z >> 15) != 0;
    constexpr float kInvSqrt2 = 0.70710678118f;
    auto dq = [&](std::uint16_t v) { return ((v & 0x7FFF) / 32767.f) * (2.f * kInvSqrt2) - kInvSqrt2; };
    return AssembleThreeComp(dq(X), dq(Y), dq(Z), rs, sign);
}

Q4 DecQuatRaw(const std::uint8_t* b) {
    float v[4];
    std::memcpy(v, b, sizeof(v));
    return Q4{ v[0], v[1], v[2], v[3] };
}

Q4 DecodeRotation(int quant, const std::uint8_t* b, bool& supported) {
    supported = true;
    switch (quant) {
        case RQ_THREECOMP40:  return DecTC40(b);
        case RQ_THREECOMP48:  return DecTC48(b);
        case RQ_UNCOMPRESSED: return DecQuatRaw(b);
        default:              supported = false; return Q4{ 0.f, 0.f, 0.f, 1.f };
    }
}

// ── vectorized scalar channel (translation or scale) ──────────────────────────
struct VecCurve {
    float          sV[4] = {};
    float          mn[4] = {};
    float          mx[4] = {};
    std::uint8_t   mask = 0;
    int            numDyn = 0;
    int            bpc = 2;
    int            n = 0;
    int            deg = 0;
    const std::uint8_t* K = nullptr;
    const std::uint8_t* C = nullptr;

    static VecCurve Rd(Reader& r, std::uint8_t mask_, int bytesPerComp, const std::uint8_t* base) {
        VecCurve vc;
        vc.mask = mask_; vc.bpc = bytesPerComp;
        if (mask_ & 0xF0) {
            vc.n   = (int)r.U16();
            vc.deg = (int)r.U8();
            if (vc.deg > kMaxSplineDegree) { r.ok = false; return vc; }   // stack d[deg+1] cap; garbage otherwise
            vc.K   = r.p; r.skip(vc.n + vc.deg + 2);
            r.align4(base);
        }
        for (int i = 0; i < 4; i++) {
            if ((mask_ >> i) & 1) { if (i < 3) vc.sV[i] = r.F32(); else r.skip(4); }
            if ((mask_ >> (i + 4)) & 1) {
                if (i < 3) { vc.mn[i] = r.F32(); vc.mx[i] = r.F32(); }
                else       r.skip(8);
                vc.numDyn++;
            }
        }
        if (mask_ & 0xF0) { vc.C = r.p; r.skip((vc.n + 1) * vc.numDyn * bytesPerComp); }
        return vc;
    }

    V3 Eval(int qt) const {
        V3 out;
        float o[3] = { 0.f, 0.f, 0.f };
        for (int i = 0; i < 3; i++) if ((mask >> i) & 1) o[i] = sV[i];
        if (numDyn == 0 || n < 0) { return V3{ o[0], o[1], o[2] }; }

        int span;
        if      (qt >= (int)K[n + 1]) span = n;
        else if (qt <= (int)K[0])     span = deg;
        else {
            int lo = deg, hi = n;
            while (lo < hi) { int mid = (lo + hi + 1) / 2; if ((int)K[mid] <= qt) lo = mid; else hi = mid - 1; }
            span = lo;
        }

        int di = 0;
        for (int i = 0; i < 3; i++) {
            if (!((mask >> (i + 4)) & 1)) continue;
            const float range = mx[i] - mn[i];
            auto unpack = [&](int ci) -> float {
                ci = std::clamp(ci, 0, n);
                const std::uint8_t* p = C + (ci * numDyn + di) * bpc;
                if (bpc == 1) return mn[i] + (*p / 255.f) * range;
                std::uint16_t v; std::memcpy(&v, p, 2);
                return mn[i] + (v / 65535.f) * range;
            };
            float d[kMaxSplineDegree + 1];
            for (int j = 0; j <= deg; j++) d[j] = unpack(span - deg + j);
            for (int rr = 1; rr <= deg; rr++) {
                for (int j = deg; j >= rr; j--) {
                    const float klo = (float)K[j + span - deg];
                    const float khi = (float)K[j + span - rr + 1];
                    const float a = (khi > klo) ? std::clamp(((float)qt - klo) / (khi - klo), 0.f, 1.f) : 0.f;
                    d[j] = (1.f - a) * d[j - 1] + a * d[j];
                }
            }
            o[i] = d[deg];
            di++;
        }
        return V3{ o[0], o[1], o[2] };
    }
};

// ── scalar float channel (float tracks) ───────────────────────────────────────
// Verified against a large real corpus: every float track uses mask 0x03 =
// STATIC, a single raw f32 (the values are per-animation constants, e.g. MCO
// combat flags). Identity (mask 0) yields 0. A dynamic channel (high nibble set)
// is decoded as the encoder's 1-component BITS16 spline; no real file exercises
// it, so that path is self-consistent with the encoder but unvalidated vs Havok.
struct FloatChan {
    bool                dynamic = false;
    float               sV = 0.f;
    float               mn = 0.f, mx = 0.f;
    int                 n = 0, deg = 0;
    const std::uint8_t* K = nullptr;
    const std::uint8_t* C = nullptr;

    static FloatChan Rd(Reader& r, std::uint8_t mask, const std::uint8_t* base) {
        FloatChan c;
        if (mask == 0) return c;               // identity → 0
        if (mask & 0xF0) {                     // dynamic scalar spline
            c.dynamic = true;
            c.n   = static_cast<int>(r.U16());
            c.deg = static_cast<int>(r.U8());
            if (c.deg > kMaxSplineDegree) { r.ok = false; return c; }   // stack d[deg+1] cap; garbage otherwise
            c.K   = r.p; r.skip(c.n + c.deg + 2);
            r.align4(base);
            c.mn = r.F32(); c.mx = r.F32();
            c.C  = r.p; r.skip((c.n + 1) * 2);  // BITS16
        } else {                                // static (0x03) → one raw f32
            c.sV = r.F32();
        }
        return c;
    }

    float Eval(int qt) const {
        if (!dynamic) return sV;
        if (n < 0) return mn;
        int span;
        if      (qt >= (int)K[n + 1]) span = n;
        else if (qt <= (int)K[0])     span = deg;
        else {
            int lo = deg, hi = n;
            while (lo < hi) { int mid = (lo + hi + 1) / 2; if ((int)K[mid] <= qt) lo = mid; else hi = mid - 1; }
            span = lo;
        }
        const float range = mx - mn;
        auto unpack = [&](int ci) -> float {
            ci = std::clamp(ci, 0, n);
            std::uint16_t v; std::memcpy(&v, C + ci * 2, 2);
            return mn + (v / 65535.f) * range;
        };
        float d[kMaxSplineDegree + 1];
        for (int j = 0; j <= deg; j++) d[j] = unpack(span - deg + j);
        for (int rr = 1; rr <= deg; rr++)
            for (int j = deg; j >= rr; j--) {
                const float klo = (float)K[j + span - deg];
                const float khi = (float)K[j + span - rr + 1];
                const float a = (khi > klo) ? std::clamp(((float)qt - klo) / (khi - klo), 0.f, 1.f) : 0.f;
                d[j] = (1.f - a) * d[j - 1] + a * d[j];
            }
        return d[deg];
    }
};

// ── ThreeComp spline channel (rotation) ───────────────────────────────────────
struct TC40 {
    int n;
    int deg;
    const std::uint8_t* k;
    const std::uint8_t* q;
    int quant;
    int bpq;
    bool supported = true;

    static TC40 Rd(Reader& r, int quant) {
        TC40 c = {};
        c.quant = quant;
        c.bpq = RotQuantBytes(quant);
        c.supported = RotQuantSupported(quant);
        if (c.bpq == 0) { r.ok = false; return c; }
        c.n   = (int)r.U16();
        c.deg = (int)r.U8();
        if (c.deg > kMaxSplineDegree) { r.ok = false; return c; }   // stack Q4 d[deg+1] cap; garbage otherwise
        c.k   = r.p; r.skip(c.n + c.deg + 2);
        c.q   = r.p; r.skip((c.n + 1) * c.bpq);
        return c;
    }

    Q4 Pt(int idx) const { bool ok = true; return DecodeRotation(quant, q + (std::ptrdiff_t)idx * bpq, ok); }

    Q4 Eval(int fi) const {
        if (n < 0) return Pt(0);
        if (fi <= (int)(std::uint8_t)k[0])     return Pt(0);
        if (fi >= (int)(std::uint8_t)k[n + 1]) return Pt(n);
        int span;
        {
            int lo = deg, hi = n;
            while (lo < hi) { int mid = (lo + hi + 1) / 2; if ((int)(std::uint8_t)k[mid] <= fi) lo = mid; else hi = mid - 1; }
            span = lo;
        }
        Q4 d[kMaxSplineDegree + 1];
        for (int j = 0; j <= deg; j++) { int idx = std::clamp(span - deg + j, 0, n); d[j] = Pt(idx); }
        for (int rr = 1; rr <= deg; rr++) {
            for (int j = deg; j >= rr; j--) {
                const float klo = (float)(std::uint8_t)k[j + span - deg];
                const float khi = (float)(std::uint8_t)k[j + span - rr + 1];
                const float a = (khi > klo) ? std::clamp(((float)fi - klo) / (khi - klo), 0.f, 1.f) : 0.f;
                if (dot(d[j - 1], d[j]) < 0.f) d[j] = neg(d[j]);
                d[j] = normalize(slerp_raw(d[j - 1], d[j], a));
            }
        }
        return d[deg];
    }
};

// ── decompress one block ──────────────────────────────────────────────────────
bool DecompBlock(const std::uint8_t* base, std::size_t dataLen, std::uint32_t offset,
                 int nTracks, int nFloatTracks, int maskQSize,
                 int blockStart, int framesInBlock,
                 std::vector<DecodedPose>& out, int& unsupportedQuant,
                 std::vector<float>* floatOut) {
    if (static_cast<std::size_t>(offset) >= dataLen) return false;
    const std::uint8_t* blockBase = base + offset;
    const std::uint8_t* dataEnd = base + dataLen;
    const int totalFrames = (int)(out.size() / nTracks);

    std::vector<std::uint8_t> quatT(nTracks), posT(nTracks), rotT(nTracks), scaleT(nTracks);
    {
        Reader r(blockBase, dataEnd);
        for (int i = 0; i < nTracks; i++) { quatT[i] = r.U8(); posT[i] = r.U8(); rotT[i] = r.U8(); scaleT[i] = r.U8(); }
        if (!r.ok) return false;
    }

    // The encoder always align4's after the raw mask bytes (nTracks*4 + nFloatTracks),
    // and maskAndQuantizationSize is that RAW size — so the track data starts at the
    // next 4-boundary. Align unconditionally: a no-op when the mask size is already
    // aligned (e.g. 4 floats → 400), but essential when nFloatTracks % 4 != 0.
    Reader rSeq(blockBase + (maskQSize > 0 ? maskQSize : nTracks * 4 + nFloatTracks), dataEnd);
    rSeq.align4(base);

    for (int tk = 0; tk < nTracks; tk++) {
        const std::uint8_t* tkDataStart = rSeq.p;
        if (tkDataStart >= dataEnd && (posT[tk] | rotT[tk] | scaleT[tk]) != 0) return false;

        Reader r(tkDataStart, dataEnd);

        const int transBpc = ((quatT[tk] & 0x03) == 0) ? 1 : 2;
        VecCurve pos = VecCurve::Rd(r, posT[tk], transBpc, base);
        if (!r.ok) return false;

        r.align4(base);

        const int rotQ = (quatT[tk] >> 2) & 0x0F;
        const int rotBy = RotQuantBytes(rotQ);
        if (rotBy == 0) return false;

        Q4 sR{ 0.f, 0.f, 0.f, 1.f };
        TC40 rc{}; bool dR = false;
        if (rotT[tk] & 0xF0) {
            rc = TC40::Rd(r, rotQ);
            if (!r.ok) return false;
            dR = true;
        } else if (rotT[tk] != 0) {
            if (r.p + rotBy > dataEnd) return false;
            bool sup = true;
            sR = DecodeRotation(rotQ, r.p, sup);
            r.skip(rotBy);
            if (!sup) unsupportedQuant = rotQ;
        }

        r.align4(base);

        const int scaleBpc = (((quatT[tk] >> 6) & 0x03) == 0) ? 1 : 2;
        VecCurve scale = VecCurve::Rd(r, scaleT[tk], scaleBpc, base);
        if (!r.ok) return false;

        r.align4(base);
        rSeq.p = r.p;

        for (int fi = 0; fi < framesInBlock; fi++) {
            const int af = blockStart + fi;
            if (af >= totalFrames) break;
            DecodedPose& ch = out[(std::size_t)af * nTracks + tk];
            V3 t = pos.Eval(fi);
            ch.t[0] = t.x; ch.t[1] = t.y; ch.t[2] = t.z;
            Q4 q = dR ? rc.Eval(fi) : sR;
            ch.q[0] = q.x; ch.q[1] = q.y; ch.q[2] = q.z; ch.q[3] = q.w;
            // Identity scale channels carry no data (mask == 0) and VecCurve::Eval
            // would return (0,0,0); leave the pose's (1,1,1) default in that case.
            if (scaleT[tk] != 0) {
                V3 s = scale.Eval(fi);
                ch.s[0] = s.x; ch.s[1] = s.y; ch.s[2] = s.z;
            }
        }
        if (dR && !rc.supported) unsupportedQuant = rotQ;
    }

    // Float tracks follow all transform tracks; their masks live in the mask
    // section right after the transform masks (nTracks*4 .. +nFloatTracks).
    if (nFloatTracks > 0 && floatOut) {
        const std::uint8_t* fmask = blockBase + nTracks * 4;
        Reader r(rSeq.p, dataEnd);
        for (int ftk = 0; ftk < nFloatTracks; ftk++) {
            const std::uint8_t m = (fmask + ftk < dataEnd) ? fmask[ftk] : 0;
            FloatChan fc = FloatChan::Rd(r, m, base);
            if (!r.ok) return false;
            for (int fi = 0; fi < framesInBlock; fi++) {
                const int af = blockStart + fi;
                if (af >= totalFrames) break;
                (*floatOut)[(std::size_t)af * nFloatTracks + ftk] = fc.Eval(fi);
            }
        }
    }
    return true;
}

} // namespace

bool DecodeSpline(const std::uint8_t* data, std::size_t dataLen,
                  int numFrames, int numBlocks, int maxFramesPerBlock,
                  int maskAndQuantizationSize,
                  const std::uint32_t* blockOffsets, int numBlockOffsets,
                  int numTracks, int numFloatTracks,
                  std::vector<DecodedPose>& out, std::string* warn,
                  std::vector<float>* floatOut) {
    if (numTracks <= 0 || numFrames <= 0 || numBlocks <= 0 || maxFramesPerBlock <= 1) return false;
    if (numBlockOffsets < numBlocks) return false;

    out.assign((std::size_t)numFrames * numTracks, DecodedPose{});
    if (floatOut) floatOut->assign((std::size_t)numFrames * std::max(numFloatTracks, 0), 0.f);
    int unsupportedQuant = -1;

    for (int b = 0; b < numBlocks; b++) {
        const int bs = b * (maxFramesPerBlock - 1);
        const int be = std::min(bs + maxFramesPerBlock - 1, numFrames - 1);
        if (!DecompBlock(data, dataLen, blockOffsets[b], numTracks, numFloatTracks,
                         maskAndQuantizationSize, bs, be - bs + 1, out, unsupportedQuant, floatOut))
            return false;
    }

    if (unsupportedQuant >= 0 && warn) {
        static const char* kNames[] = { "POLAR32", "THREECOMP40", "THREECOMP48", "THREECOMP24", "STRAIGHT16", "UNCOMPRESSED" };
        const char* nm = (unsupportedQuant >= 0 && unsupportedQuant < 6) ? kNames[unsupportedQuant] : "?";
        *warn = std::string("rotation quantization ") + std::to_string(unsupportedQuant) + " (" + nm + ") not decoded — identity rotations for those tracks";
    }
    return true;
}

} // namespace havok::anim
