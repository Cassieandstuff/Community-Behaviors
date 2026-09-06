// HkxErOracle.cpp — pinned verbatim copy of Engine Relay's HkxErWriter.cpp
// (WriteToMemory + helpers), standalone. See HkxErOracle.h for provenance/intent.
// Logic is byte-for-byte the original; only the namespace, the inlined
// Registration source, and the removal of the PCH/disk-write helpers differ.

#include "HkxErOracle.h"

#include <algorithm>
#include <cstring>
#include <cctype>

namespace hkx_oracle {

// =============================================================================
// Byte-buffer writer
// =============================================================================
struct BufWriter {
    std::vector<std::uint8_t> buf;

    std::uint32_t pos() const { return static_cast<std::uint32_t>(buf.size()); }

    void u8 (std::uint8_t  v) { buf.push_back(v); }
    void u16(std::uint16_t v) { buf.push_back(v & 0xFF); buf.push_back((v >> 8) & 0xFF); }
    void i16(std::int16_t  v) { u16(static_cast<std::uint16_t>(v)); }
    void u32(std::uint32_t v) {
        buf.push_back( v        & 0xFF);
        buf.push_back((v >>  8) & 0xFF);
        buf.push_back((v >> 16) & 0xFF);
        buf.push_back((v >> 24) & 0xFF);
    }
    void i32(std::int32_t  v) { u32(static_cast<std::uint32_t>(v)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)); u32(static_cast<std::uint32_t>(v >> 32)); }
    void f32(float v)         { std::uint32_t b; std::memcpy(&b, &v, 4); u32(b); }

    void zeros(std::uint32_t n) { buf.insert(buf.end(), n, 0x00); }
    void bytes(const void* p, std::uint32_t n) {
        const auto* bp = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), bp, bp + n);
    }
    void cstr(const char* s) { while (*s) buf.push_back(static_cast<std::uint8_t>(*s++)); buf.push_back(0); }

    // Pad to next multiple of 'a'
    void align(std::uint32_t a) {
        const std::uint32_t rem = pos() % a;
        if (rem) zeros(a - rem);
    }
};

// =============================================================================
// Fixup entry types
// =============================================================================
struct LocalFixup  { std::uint32_t src, dst; };
struct GlobalFixup { std::uint32_t src, dstSec, dst; };
struct VirtualFixup{ std::uint32_t src, dstSec, clsNameOff; };

// =============================================================================
// Classnames section
// =============================================================================
struct ClsEntry { std::uint32_t sig; const char* name; };

static constexpr ClsEntry kBaseCls[] = {
    { 0x75585EF6u, "hkClass"                       },
    { 0x5C7EA4C2u, "hkClassMember"                 },
    { 0x8A3609CFu, "hkClassEnum"                   },
    { 0xCE6F8A6Cu, "hkClassEnumItem"               },
    { 0x2772C11Eu, "hkRootLevelContainer"           },
    { 0xB1218F86u, "hkbBehaviorGraph"              },
    { 0x816C1DCBu, "hkbStateMachine"               },
    { 0x0ED7F9D0u, "hkbStateMachineStateInfo"       },
    { 0x333B85B9u, "hkbClipGenerator"              },
    { 0x095ACA5Du, "hkbBehaviorGraphData"           },
    { 0x27812D8Du, "hkbVariableValueSet"            },
    { 0xC713064Eu, "hkbBehaviorGraphStringData"     },
};
static constexpr ClsEntry kClsBRG = { 0x0FCB5423u, "hkbBehaviorReferenceGenerator"    };
static constexpr ClsEntry kClsTIA = { 0xE397B11Eu, "hkbStateMachineTransitionInfoArray" };

struct ClsNameOffs {
    std::uint32_t rlc, bg, sm, si, cg, bgd, vvs, bgsd, brg, tia;
};

static ClsNameOffs BuildClassnames(BufWriter& out, bool hasRegs)
{
    ClsNameOffs r{};
    auto writeOne = [&](const ClsEntry& e) -> std::uint32_t {
        out.u32(e.sig);
        out.u8(0x09);
        const auto nameOff = out.pos();
        out.cstr(e.name);
        return nameOff;
    };

    for (std::size_t i = 0; i < std::size(kBaseCls); ++i) {
        const auto no = writeOne(kBaseCls[i]);
        switch (i) {
            case 4: r.rlc  = no; break;
            case 5: r.bg   = no; break;
            case 6: r.sm   = no; break;
            case 7: r.si   = no; break;
            case 8: r.cg   = no; break;
            case 9: r.bgd  = no; break;
            case 10: r.vvs = no; break;
            case 11: r.bgsd= no; break;
        }
    }
    if (hasRegs) {
        r.brg = writeOne(kClsBRG);
        r.tia = writeOne(kClsTIA);
    }
    // Pad with 0xFF to 16-byte boundary (matches baseline)
    const auto rem = out.pos() % 16;
    if (rem) for (std::uint32_t i = 0; i < (16 - rem); ++i) out.u8(0xFF);
    return r;
}

// =============================================================================
// Data section field writers
// =============================================================================
static void WriteRefObj(BufWriter& d) { d.zeros(16); }

static void WriteBindable(BufWriter& d) {
    d.zeros(8);           // variableBindingSet = null
    d.zeros(8);           // cachedBindables.data = null
    d.u32(0);             // cachedBindables.size = 0
    d.u32(0x80000000u);   // cachedBindables.cap  = 0x80000000
    d.zeros(8);           // areBindablesCached(1)+7pad
}

static std::uint32_t WriteNode(BufWriter& d) {
    d.u64(0);                          // userData = 0
    const auto nameOff = d.pos();
    d.zeros(8);                        // name ptr (LocalFixup target)
    d.u16(0); d.u8(0); d.u8(0); d.u32(0); // id + cloneState + pads
    return nameOff;
}

static void WriteVoidArr(BufWriter& d) {
    d.zeros(8); d.u32(0); d.u32(0x80000000u);
}

static std::uint32_t WriteArrHdr(BufWriter& d, std::uint32_t n) {
    const auto off = d.pos();
    d.zeros(8);
    d.u32(n);
    d.u32(n | 0x80000000u);
    return off;
}

static std::uint32_t WritePtr(BufWriter& d) {
    const auto off = d.pos();
    d.zeros(8);
    return off;
}

static std::uint32_t WriteStr(BufWriter& d, const char* s) {
    const auto off = d.pos();
    d.cstr(s);
    d.align(16);
    return off;
}

static std::string San(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (char c : name)
        out.push_back((std::isalnum(static_cast<unsigned char>(c)) || c == '_') ? c : '_');
    if (!out.empty() && std::isdigit(static_cast<unsigned char>(out[0])))
        out.insert(out.begin(), '_');
    return out;
}

// =============================================================================
// Main writer
// =============================================================================
std::vector<std::uint8_t> WriteToMemory(const std::vector<Registration>& registrations,
                                        std::string_view                 graphName)
{
    const bool hasRegs = !registrations.empty();
    const auto N = static_cast<std::uint32_t>(registrations.size());

    BufWriter cls;
    const ClsNameOffs clsOffs = BuildClassnames(cls, hasRegs);

    BufWriter d;

    std::vector<LocalFixup>   LF;
    std::vector<VirtualFixup> VF;

    std::uint32_t g_rlcVariant   = 0;
    std::uint32_t g_bgRoot       = 0;
    std::uint32_t g_bgData       = 0;
    std::uint32_t g_smWild       = 0;
    std::vector<std::uint32_t> g_smStates;
    std::uint32_t g_idleSIgen    = 0;
    std::vector<std::uint32_t> g_regSIgen;
    std::uint32_t g_bgdVVS       = 0;
    std::uint32_t g_bgdBGSD      = 0;

    std::uint32_t off_RLC   = 0;
    std::uint32_t off_BG    = 0;
    std::uint32_t off_SM    = 0;
    std::uint32_t off_IdleSI = 0;
    std::vector<std::uint32_t> off_RegSI;
    std::uint32_t off_IdleCG = 0;
    std::vector<std::uint32_t> off_BRG;
    std::uint32_t off_BGD   = 0;
    std::uint32_t off_VVS   = 0;
    std::uint32_t off_BGSD  = 0;
    std::uint32_t off_TIA   = 0;

    // [1] hkRootLevelContainer (no hkReferencedObject header — just array body)
    off_RLC = d.pos();
    VF.push_back({off_RLC, 0, clsOffs.rlc});
    {
        const auto arrPtrOff = WritePtr(d);
        d.u32(1);
        d.u32(0x80000001u);

        const auto arrDataOff = d.pos();
        LF.push_back({arrPtrOff, arrDataOff});

        const auto nvNamePtr  = WritePtr(d);
        const auto nvClsPtr   = WritePtr(d);
        g_rlcVariant          = WritePtr(d);

        const auto nvNameStrOff = WriteStr(d, "hkbBehaviorGraph");
        const auto nvClsStrOff  = WriteStr(d, "hkbBehaviorGraph");
        LF.push_back({nvNamePtr, nvNameStrOff});
        LF.push_back({nvClsPtr,  nvClsStrOff});
    }
    d.align(16);

    // [2] hkbBehaviorGraph (body 0x130 = 304)
    off_BG = d.pos();
    VF.push_back({off_BG, 0, clsOffs.bg});
    {
        WriteRefObj(d);
        WriteBindable(d);
        const auto namePtr = WriteNode(d);

        d.u8(0); d.zeros(7);     // variableMode + 7 pad
        WriteVoidArr(d);         // uniqueIdPool
        d.zeros(8);              // idToStateMachineTemplateMap
        WriteVoidArr(d);         // mirroredExternalIdMap
        d.zeros(8);              // pseudoRandomGenerator
        g_bgRoot = WritePtr(d);  // rootGenerator → SM
        g_bgData = WritePtr(d);  // data → BGD
        for (int i = 0; i < 14; ++i) d.zeros(8);  // 14 void ptrs
        d.u32(0); d.u32(0);      // numIntermediateOutputs + pad
        WriteVoidArr(d);         // jobs
        WriteVoidArr(d);         // allPartitionMemory
        d.u16(0); d.u16(0); d.u8(0); d.u8(0); d.u8(0); d.u8(0);  // numStaticNodes..bools

        d.align(16);
        const std::string graphNameStr(graphName);
        const auto nameStrOff = WriteStr(d, graphNameStr.c_str());
        LF.push_back({namePtr, nameStrOff});
    }
    d.align(16);

    // [3] hkbStateMachine (body 0x108 = 264)
    off_SM = d.pos();
    VF.push_back({off_SM, 0, clsOffs.sm});
    std::uint32_t sm_namePtr = 0;
    {
        WriteRefObj(d);
        WriteBindable(d);
        sm_namePtr = WriteNode(d);

        d.i32(-1); d.u32(0); d.zeros(8); d.zeros(8);  // hkbEvent inline
        d.zeros(8);                                    // startStateChooser
        d.i32(0);                                      // startStateId
        d.i32(-1); d.i32(-1); d.i32(-1); d.i32(-1); d.i32(-1);  // 5 event ids
        d.i32(0);                                      // currentStateId (ignored)
        d.u8(0);    // wrapAroundStateId
        d.u8(32);   // maxSimultaneousTransitions
        d.u8(0);    // startStateMode
        d.u8(0);    // selfTransitionMode
        d.u8(0);    // isActive (ignored)
        d.zeros(3);
        d.zeros(4);
        const auto statesArrPtrOff = WriteArrHdr(d, N + 1);
        g_smWild = WritePtr(d);
        d.zeros(8);  // stateIdToIndexMap
        WriteVoidArr(d); WriteVoidArr(d); WriteVoidArr(d); WriteVoidArr(d);
        d.f32(0.0f); d.f32(0.0f); d.i32(0); d.i32(0);
        d.u8(0); d.u8(0); d.u16(0); d.u32(0);

        d.align(16);
        const auto nameStrOff = WriteStr(d, "ER_SwitchboardSM");
        LF.push_back({sm_namePtr, nameStrOff});

        const auto statesArrDataOff = d.pos();
        LF.push_back({statesArrPtrOff, statesArrDataOff});
        for (std::uint32_t i = 0; i < N + 1; ++i) {
            g_smStates.push_back(d.pos());
            d.zeros(8);
        }
        d.align(16);
    }
    d.align(16);

    // [4] hkbStateMachineStateInfo — ER_IdleState (body 0x78 = 120)
    off_IdleSI = d.pos();
    VF.push_back({off_IdleSI, 0, clsOffs.si});
    {
        WriteRefObj(d);
        WriteBindable(d);
        WriteArrHdr(d, 0);   // listeners
        d.zeros(8);          // enterNotifyEvents
        d.zeros(8);          // exitNotifyEvents
        d.zeros(8);          // transitions
        g_idleSIgen = WritePtr(d);  // generator → CG
        const auto namePtr = WritePtr(d);
        d.i32(0); d.f32(1.0f); d.u8(1); d.zeros(7);  // stateId, prob, enable

        d.align(16);
        const auto nameStrOff = WriteStr(d, "ER_IdleState");
        LF.push_back({namePtr, nameStrOff});
    }
    d.align(16);

    // [5] per-registration StateInfo
    for (std::uint32_t i = 0; i < N; ++i) {
        const auto& reg = registrations[i];
        const auto stateName = std::string("BSB_") + San(reg.modName) + "_State";

        off_RegSI.push_back(d.pos());
        VF.push_back({d.pos(), 0, clsOffs.si});

        WriteRefObj(d);
        WriteBindable(d);
        WriteArrHdr(d, 0);
        d.zeros(8);
        d.zeros(8);
        d.zeros(8);
        g_regSIgen.push_back(WritePtr(d));
        const auto namePtr = WritePtr(d);
        d.i32(static_cast<std::int32_t>(i + 1));
        d.f32(1.0f); d.u8(1); d.zeros(7);

        d.align(16);
        const auto nameStrOff = WriteStr(d, stateName.c_str());
        LF.push_back({namePtr, nameStrOff});
        d.align(16);
    }

    // [6] hkbClipGenerator — ER_Idle (body 0x110 = 272)
    off_IdleCG = d.pos();
    VF.push_back({off_IdleCG, 0, clsOffs.cg});
    {
        WriteRefObj(d);
        WriteBindable(d);
        const auto nodeNamePtr  = WriteNode(d);
        const auto animNamePtr  = WritePtr(d);
        d.zeros(8);  // triggers
        d.f32(0.0f); d.f32(0.0f); d.f32(0.0f); d.f32(1.0f); d.f32(0.0f); d.f32(0.0f);  // 6 floats
        d.i16(-1); d.u8(1); d.u8(0); d.u32(0);  // bindingIndex, mode=LOOPING, flags, pad
        WriteVoidArr(d);  // animDatas
        for (int i = 0; i < 5; ++i) d.zeros(8);  // 5 void ptrs
        d.zeros(48);  // QSTransform extractedMotion
        WriteVoidArr(d);  // echos
        d.f32(0.0f); d.f32(0.0f); d.f32(0.0f); d.i32(0); d.i32(0);
        d.u8(0); d.u8(0); d.u8(0); d.zeros(9);

        static constexpr const char kAnimPath[] = "Animations\\EngineRelay\\ER_Idle.hkx";
        d.align(16);
        const auto nodeNameStrOff = WriteStr(d, "ER_Idle");
        const auto animNameStrOff = WriteStr(d, kAnimPath);
        LF.push_back({nodeNamePtr, nodeNameStrOff});
        LF.push_back({animNamePtr, animNameStrOff});
    }
    d.align(16);

    // [7] per-registration hkbBehaviorReferenceGenerator (body 0x58 = 88)
    for (std::uint32_t i = 0; i < N; ++i) {
        const auto& reg = registrations[i];
        const auto brgName = std::string("BSB_") + San(reg.modName) + "BRG";

        off_BRG.push_back(d.pos());
        VF.push_back({d.pos(), 0, clsOffs.brg});

        WriteRefObj(d);
        WriteBindable(d);
        const auto nodeNamePtr  = WriteNode(d);
        const auto behNamePtr   = WritePtr(d);
        d.zeros(8);  // behavior void ptr

        d.align(16);
        const auto nodeNameStrOff = WriteStr(d, brgName.c_str());
        const auto behNameStrOff  = WriteStr(d, reg.behaviorPath.c_str());
        LF.push_back({nodeNamePtr, nodeNameStrOff});
        LF.push_back({behNamePtr,  behNameStrOff});
        d.align(16);
    }

    // [8] hkbBehaviorGraphData (body 0x80 = 128)
    off_BGD = d.pos();
    VF.push_back({off_BGD, 0, clsOffs.bgd});
    std::uint32_t bgd_evtArrPtr = 0;
    {
        WriteRefObj(d);
        WriteVoidArr(d);  // attributeDefaults
        WriteVoidArr(d);  // variableInfos
        WriteVoidArr(d);  // characterPropertyInfos
        bgd_evtArrPtr = WriteArrHdr(d, N);  // eventInfos
        WriteVoidArr(d);  // wordMinVariableValues
        WriteVoidArr(d);  // wordMaxVariableValues
        g_bgdVVS  = WritePtr(d);
        g_bgdBGSD = WritePtr(d);

        if (N > 0) {
            d.align(16);
            const auto evtDataOff = d.pos();
            LF.push_back({bgd_evtArrPtr, evtDataOff});
            for (std::uint32_t i = 0; i < N; ++i) d.u32(0);
            d.align(16);
        }
    }
    d.align(16);

    // [9] hkbVariableValueSet (body 0x40 = 64)
    off_VVS = d.pos();
    VF.push_back({off_VVS, 0, clsOffs.vvs});
    {
        WriteRefObj(d);
        WriteVoidArr(d);
        WriteVoidArr(d);
        WriteVoidArr(d);
    }
    d.align(16);

    // [10] hkbBehaviorGraphStringData (body 0x50 = 80)
    off_BGSD = d.pos();
    VF.push_back({off_BGSD, 0, clsOffs.bgsd});
    {
        WriteRefObj(d);
        const auto evtNameArrPtr = WriteArrHdr(d, N);
        WriteVoidArr(d);
        WriteVoidArr(d);
        WriteVoidArr(d);

        if (N > 0) {
            d.align(16);
            const auto evtPtrArrOff = d.pos();
            LF.push_back({evtNameArrPtr, evtPtrArrOff});

            std::vector<std::uint32_t> evtPtrSlots;
            for (std::uint32_t i = 0; i < N; ++i) {
                evtPtrSlots.push_back(d.pos());
                d.zeros(8);
            }
            d.align(16);

            for (std::uint32_t i = 0; i < N; ++i) {
                const auto strOff = WriteStr(d, registrations[i].eventName.c_str());
                LF.push_back({evtPtrSlots[i], strOff});
            }
        }
    }
    d.align(16);

    // [11] hkbStateMachineTransitionInfoArray (only if N>0, body 0x20 = 32)
    if (hasRegs) {
        off_TIA = d.pos();
        VF.push_back({off_TIA, 0, clsOffs.tia});

        WriteRefObj(d);
        const auto tiaArrPtr = WriteArrHdr(d, N);

        d.align(16);
        const auto tiaDataOff = d.pos();
        LF.push_back({tiaArrPtr, tiaDataOff});

        for (std::uint32_t i = 0; i < N; ++i) {
            d.i32(-1); d.i32(-1); d.f32(0.0f); d.f32(0.0f);  // triggerInterval
            d.i32(-1); d.i32(-1); d.f32(0.0f); d.f32(0.0f);  // initiateInterval
            d.zeros(8);  // transition effect ptr
            d.zeros(8);  // condition ptr
            d.i32(static_cast<std::int32_t>(i));      // eventId
            d.i32(static_cast<std::int32_t>(i + 1));  // toStateId
            d.i32(0);    // fromNestedStateId
            d.i32(0);    // toNestedStateId
            d.i16(0);    // priority
            d.i16(static_cast<std::int16_t>(0x0900)); // flags
            d.u32(0);    // pad
        }
        d.align(16);
    }

    // Build GlobalFixups
    std::vector<GlobalFixup> GF;
    GF.push_back({g_rlcVariant, 2, off_BG});
    GF.push_back({g_bgRoot, 2, off_SM});
    GF.push_back({g_bgData, 2, off_BGD});
    if (hasRegs)
        GF.push_back({g_smWild, 2, off_TIA});
    GF.push_back({g_smStates[0], 2, off_IdleSI});
    for (std::uint32_t i = 0; i < N; ++i)
        GF.push_back({g_smStates[i + 1], 2, off_RegSI[i]});
    GF.push_back({g_idleSIgen, 2, off_IdleCG});
    for (std::uint32_t i = 0; i < N; ++i)
        GF.push_back({g_regSIgen[i], 2, off_BRG[i]});
    GF.push_back({g_bgdVVS, 2, off_VVS});
    GF.push_back({g_bgdBGSD, 2, off_BGSD});

    // Assemble
    const std::uint32_t clsSize  = cls.pos();
    const std::uint32_t dataSz   = d.pos();

    const std::uint32_t localSz  = (static_cast<std::uint32_t>(LF.size()) + 1u) * 8u;
    const std::uint32_t globalSz = (static_cast<std::uint32_t>(GF.size()) + 1u) * 12u;
    const std::uint32_t virtSz   =  static_cast<std::uint32_t>(VF.size())        * 12u;

    const std::uint32_t localOff  = dataSz;
    const std::uint32_t globalOff = localOff  + localSz;
    const std::uint32_t virtOff   = globalOff + globalSz;
    const std::uint32_t expOff    = virtOff   + virtSz;
    const std::uint32_t endOff    = expOff;

    constexpr std::uint32_t kFileHdrSz  = 64u;
    constexpr std::uint32_t kSecHdrSz   = 48u;
    constexpr std::uint32_t kNSections  = 3u;
    const std::uint32_t absClsStart  = kFileHdrSz + kNSections * kSecHdrSz;
    const std::uint32_t absDataStart = absClsStart + clsSize;

    BufWriter out;

    static constexpr std::uint8_t kMagic[] = { 0x57,0xE0,0xE0,0x57, 0x10,0xC0,0xC0,0x10 };
    out.bytes(kMagic, 8);
    out.u32(0);           // UserTag
    out.u32(8);           // FileVersion
    out.u8(8);            // PointerSize
    out.u8(1);            // Endian
    out.u8(0);            // PaddingOption
    out.u8(1);            // BaseClass
    out.u32(kNSections);  // SectionCount
    out.u32(2);           // ContentsSectionIndex
    out.u32(0);           // ContentsSectionOffset
    out.u32(0);           // ContentsClassNameSectionIndex
    out.u32(clsOffs.rlc); // ContentsClassNameSectionOffset
    {
        static const char kVer[] = "hk_2010.2.0-r1";
        out.bytes(kVer, 15);
        out.u8(0xFF);
    }
    out.u32(0);           // Flags
    out.i16(-1);          // MaxPredicate
    out.i16(-1);          // SectionOffset

    auto writeSecHdr = [&](const char* tag,
                            std::uint32_t absStart,
                            std::uint32_t lr, std::uint32_t gr, std::uint32_t vr,
                            std::uint32_t er, std::uint32_t ir, std::uint32_t endr)
    {
        // Explicit zero-fill copy, NOT strncpy_s.
        //
        // strncpy_s fills the destination past the terminator with
        // _SECURECRT_FILL_BUFFER_PATTERN (0xFE) in Debug builds and leaves it
        // alone in Release. Since all 19 bytes are emitted, the oracle produced
        // DIFFERENT BYTES per build configuration: "__classnames__" is 14 chars,
        // so tb[15..18] came out 0xFE in Debug and 0x00 in Release.
        //
        // That made the N=0 byte-exact check fail in Debug only, and it was the
        // oracle that was wrong — real Bethesda .hkx files and the deployed
        // enginerelay.hkx both pad this field with 0x00, which is what
        // havok-core emits.
        char tb[20] = {};
        const std::size_t tagLen = (std::min)(std::strlen(tag), std::size_t{19});
        std::memcpy(tb, tag, tagLen);
        out.bytes(tb, 19);
        out.u8(0xFF);
        out.u32(absStart);
        out.u32(lr); out.u32(gr); out.u32(vr);
        out.u32(er); out.u32(ir); out.u32(endr);
    };

    writeSecHdr("__classnames__", absClsStart,
        clsSize, clsSize, clsSize, clsSize, clsSize, clsSize);
    writeSecHdr("__types__", absDataStart,
        0, 0, 0, 0, 0, 0);
    writeSecHdr("__data__", absDataStart,
        localOff, globalOff, virtOff, expOff, expOff, endOff);

    out.bytes(cls.buf.data(), clsSize);
    out.bytes(d.buf.data(), dataSz);

    for (const auto& lf : LF) { out.u32(lf.src); out.u32(lf.dst); }
    out.u32(0xFFFFFFFFu); out.u32(0xFFFFFFFFu);

    for (const auto& gf : GF) { out.u32(gf.src); out.u32(gf.dstSec); out.u32(gf.dst); }
    out.u32(0xFFFFFFFFu); out.u32(0xFFFFFFFFu); out.u32(0xFFFFFFFFu);

    for (const auto& vf : VF) { out.u32(vf.src); out.u32(vf.dstSec); out.u32(vf.clsNameOff); }

    return out.buf;
}

}  // namespace hkx_oracle
