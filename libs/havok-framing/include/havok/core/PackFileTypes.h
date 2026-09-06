#pragma once
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/BinaryWriterEx.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Havok packfile framing — header, sections, fixups, class names. Faithful port
// of HKX2E's PackFileCommon.cs (read + write). The defaults of HKXHeader ARE the
// SkyrimSE preset (hk_2010.2.0-r1, 64-bit, big-endian flag 1, no section pad).

namespace havok {

struct HKXHeader {
    std::uint32_t Magic0 = 0x57E0E057u;
    std::uint32_t Magic1 = 0x10C0C010u;
    std::int32_t  UserTag = 0;
    std::int32_t  FileVersion = 0x08;
    std::uint8_t  PointerSize = 8;
    std::uint8_t  Endian = 1;
    std::uint8_t  PaddingOption = 0;
    std::uint8_t  BaseClass = 1;
    std::int32_t  SectionCount = 3;
    std::int32_t  ContentsSectionIndex = 2;
    std::int32_t  ContentsSectionOffset = 0;
    std::int32_t  ContentsClassNameSectionIndex = 0;
    std::int32_t  ContentsClassNameSectionOffset = 0x4B;
    std::string   ContentsVersionString = "hk_2010.2.0-r1";
    std::int32_t  Flags = 0;
    std::int16_t  MaxPredicate = -1;
    std::int16_t  SectionOffset = -1;
    std::int16_t  Unk40 = 0;
    std::int16_t  Unk42 = 0;
    std::uint32_t Unk44 = 0;
    std::uint32_t Unk48 = 0;
    std::uint32_t Unk4C = 0;

    static HKXHeader SkyrimSE() { return HKXHeader{}; }

    void Write(BinaryWriterEx& bw) const {
        bw.WriteUInt32(Magic0);
        bw.WriteUInt32(Magic1);
        bw.WriteInt32(UserTag);
        bw.WriteInt32(FileVersion);
        bw.WriteByte(PointerSize);
        bw.WriteByte(Endian);
        bw.WriteByte(PaddingOption);
        bw.WriteByte(BaseClass);
        bw.WriteInt32(SectionCount);
        bw.WriteInt32(ContentsSectionIndex);
        bw.WriteInt32(ContentsSectionOffset);
        bw.WriteInt32(ContentsClassNameSectionIndex);
        bw.WriteInt32(ContentsClassNameSectionOffset);
        bw.WriteFixStr(ContentsVersionString, 16, 0xFF);
        bw.WriteInt32(Flags);
        bw.WriteInt16(MaxPredicate);
        bw.WriteInt16(SectionOffset);
        if (SectionOffset != 16) return;
        bw.WriteInt16(Unk40);
        bw.WriteInt16(Unk42);
        bw.WriteUInt32(Unk44);
        bw.WriteUInt32(Unk48);
        bw.WriteUInt32(Unk4C);
    }

    void Read(BinaryReaderEx& br) {
        Magic0 = br.AssertUInt32({0x57E0E057u});
        Magic1 = br.AssertUInt32({0x10C0C010u});
        UserTag = br.ReadInt32();
        FileVersion = br.AssertInt32({0x0B, 0x08});
        PointerSize = br.AssertByte({4, 8});
        Endian = br.AssertByte({0, 1});
        PaddingOption = br.AssertByte({0, 1});
        BaseClass = br.AssertByte({1});
        SectionCount = br.AssertInt32({3});
        ContentsSectionIndex = br.ReadInt32();
        ContentsSectionOffset = br.ReadInt32();
        ContentsClassNameSectionIndex = br.ReadInt32();
        ContentsClassNameSectionOffset = br.ReadInt32();
        ContentsVersionString = br.ReadFixStr(16);
        Flags = br.ReadInt32();
        MaxPredicate = br.ReadInt16();
        SectionOffset = br.ReadInt16();
        if (SectionOffset != 16) return;
        Unk40 = br.ReadInt16();
        Unk42 = br.ReadInt16();
        Unk44 = br.ReadUInt32();
        Unk48 = br.ReadUInt32();
        Unk4C = br.ReadUInt32();
    }
};

struct LocalFixup {
    std::uint32_t Src = 0;
    std::uint32_t Dst = 0;
    void Write(BinaryWriterEx& bw) const { bw.WriteUInt32(Src); bw.WriteUInt32(Dst); }
};
struct GlobalFixup {
    std::uint32_t Src = 0, DstSectionIndex = 0, Dst = 0;
    void Write(BinaryWriterEx& bw) const { bw.WriteUInt32(Src); bw.WriteUInt32(DstSectionIndex); bw.WriteUInt32(Dst); }
};
struct VirtualFixup {
    std::uint32_t Src = 0, DstSectionIndex = 0, Dst = 0;
    void Write(BinaryWriterEx& bw) const { bw.WriteUInt32(Src); bw.WriteUInt32(DstSectionIndex); bw.WriteUInt32(Dst); }
};

struct HKXClassName {
    std::uint32_t Signature = 0;
    std::string   ClassName;
    void Write(BinaryWriterEx& bw) const {
        bw.WriteUInt32(Signature);
        bw.WriteByte(0x09);
        bw.WriteASCII(ClassName, true);
    }
};

// Parsed __classnames__ section: maps the offset of each name string to its name.
struct HKXClassNames {
    std::unordered_map<std::uint32_t, std::string> OffsetClassNamesMap;
    void Read(BinaryReaderEx& br) {
        while (true) {
            if (br.Position() >= br.Length() || br.Position() + 5 >= br.Length()) break;
            br.ReadUInt32();                    // signature
            const std::uint8_t sep = br.ReadByte();
            if (sep != 0x09) break;
            br.SetPosition(br.Position() - 5);
            const std::uint32_t stringStart = static_cast<std::uint32_t>(br.Position()) + 5;
            br.ReadUInt32();                    // signature (again)
            br.ReadByte();                      // 0x09
            OffsetClassNamesMap[stringStart] = br.ReadASCII();
            if (br.Position() == br.Length()) break;
        }
    }
};

struct HKXSection {
    int                       SectionID = 0;
    std::string               SectionTag;
    std::vector<std::uint8_t> SectionData;
    std::vector<LocalFixup>   LocalFixups;
    std::vector<GlobalFixup>  GlobalFixups;
    std::vector<VirtualFixup> VirtualFixups;
    std::string               ContentsVersionString = "hk_2010.2.0-r1";

    std::unordered_map<std::uint32_t, LocalFixup>   _localMap;
    std::unordered_map<std::uint32_t, GlobalFixup>  _globalMap;
    std::unordered_map<std::uint32_t, VirtualFixup> _virtualMap;

    void WriteHeader(BinaryWriterEx& bw) const {
        const std::string id = std::to_string(SectionID);
        bw.WriteFixStr(SectionTag, 19);
        bw.WriteByte(0xFF);
        bw.ReserveUInt32("absoffset" + id);
        bw.ReserveUInt32("locoffset" + id);
        bw.ReserveUInt32("globoffset" + id);
        bw.ReserveUInt32("virtoffset" + id);
        bw.ReserveUInt32("expoffset" + id);
        bw.ReserveUInt32("impoffset" + id);
        bw.ReserveUInt32("endoffset" + id);
        if (ContentsVersionString == "hk_2010.2.0-r1") return;
        bw.WriteUInt32(0xFFFFFFFFu); bw.WriteUInt32(0xFFFFFFFFu);
        bw.WriteUInt32(0xFFFFFFFFu); bw.WriteUInt32(0xFFFFFFFFu);
    }

    void WriteData(BinaryWriterEx& bw) const {
        const std::string id = std::to_string(SectionID);
        const std::uint32_t abs = static_cast<std::uint32_t>(bw.Position());
        bw.FillUInt32("absoffset" + id, abs);
        bw.WriteBytes(SectionData);
        while (bw.Position() % 16 != 0) bw.WriteByte(0xFF);
        bw.FillUInt32("locoffset" + id, static_cast<std::uint32_t>(bw.Position()) - abs);
        for (const auto& f : LocalFixups) f.Write(bw);
        while (bw.Position() % 16 != 0) bw.WriteByte(0xFF);
        bw.FillUInt32("globoffset" + id, static_cast<std::uint32_t>(bw.Position()) - abs);
        for (const auto& f : GlobalFixups) f.Write(bw);
        while (bw.Position() % 16 != 0) bw.WriteByte(0xFF);
        bw.FillUInt32("virtoffset" + id, static_cast<std::uint32_t>(bw.Position()) - abs);
        for (const auto& f : VirtualFixups) f.Write(bw);
        while (bw.Position() % 16 != 0) bw.WriteByte(0xFF);
        const std::uint32_t end = static_cast<std::uint32_t>(bw.Position()) - abs;
        bw.FillUInt32("expoffset" + id, end);
        bw.FillUInt32("impoffset" + id, end);
        bw.FillUInt32("endoffset" + id, end);
    }

    void Read(BinaryReaderEx& br, const std::string& ver) {
        ContentsVersionString = ver;
        SectionTag = br.ReadFixStr(19);
        br.AssertByte({0xFF});
        const std::uint32_t absStart = br.ReadUInt32();
        const std::uint32_t locOff   = br.ReadUInt32();
        const std::uint32_t globOff  = br.ReadUInt32();
        const std::uint32_t virtOff  = br.ReadUInt32();
        const std::uint32_t expOff   = br.ReadUInt32();
        br.ReadUInt32();  // imports
        br.ReadUInt32();  // end

        br.StepIn(absStart);
        SectionData = br.ReadBytes(locOff);
        br.StepOut();

        br.StepIn(absStart + locOff);
        for (std::uint32_t i = 0; i < (globOff - locOff) / 8; ++i) {
            if (br.ReadUInt32() != 0xFFFFFFFFu) {
                br.SetPosition(br.Position() - 4);
                LocalFixup f; f.Src = br.ReadUInt32(); f.Dst = br.ReadUInt32();
                _localMap[f.Src] = f; LocalFixups.push_back(f);
            }
        }
        br.StepOut();

        br.StepIn(absStart + globOff);
        for (std::uint32_t i = 0; i < (virtOff - globOff) / 12; ++i) {
            if (br.ReadUInt32() != 0xFFFFFFFFu) {
                br.SetPosition(br.Position() - 4);
                GlobalFixup f; f.Src = br.ReadUInt32(); f.DstSectionIndex = br.ReadUInt32(); f.Dst = br.ReadUInt32();
                _globalMap[f.Src] = f; GlobalFixups.push_back(f);
            }
        }
        br.StepOut();

        br.StepIn(absStart + virtOff);
        for (std::uint32_t i = 0; i < (expOff - virtOff) / 12; ++i) {
            if (br.ReadUInt32() != 0xFFFFFFFFu) {
                br.SetPosition(br.Position() - 4);
                VirtualFixup f; f.Src = br.ReadUInt32(); f.DstSectionIndex = br.ReadUInt32(); f.Dst = br.ReadUInt32();
                _virtualMap[f.Src] = f; VirtualFixups.push_back(f);
            }
        }
        br.StepOut();
        // hk_2010.2.0-r1 has no trailing section-header padding.
    }

    HKXClassNames ReadClassnames(bool bigEndian, bool uSizeLong) const {
        BinaryReaderEx br(bigEndian, uSizeLong, SectionData);
        HKXClassNames cn;
        cn.Read(br);
        return cn;
    }
};

} // namespace havok
