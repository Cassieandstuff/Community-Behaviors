#pragma once
#include <codec/serialization/packfile/IHavokObject.h>
#include <codec/serialization/packfile/BinaryReaderEx.h>
#include <common/HkTypes.h>
#include <codec/serialization/packfile/PackFileTypes.h>
#include <codec/serialization/tagfile/BinaryTagfile.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Faithful port of HKX2E's PackFileDeserializer.cs. Reads a packfile back into an
// object graph: parse header + 3 sections + class names, then walk the __data__
// section from offset 0 via virtual fixups (class name -> registry factory ->
// Read), resolving pointers/arrays/strings through the section fixup maps.
// A binary tagfile is transcoded to a packfile first (BinaryTagfile.h).

namespace CB::engine::codec {
using namespace CB::engine::common;

class PackFileDeserializer {
public:
    HKXHeader _header;

    // The raw __data__ bytes, valid after DeserializePartially. Callers that
    // drive ConstructAllOfClass need this to build the section reader.
    const std::vector<std::uint8_t>& DataSectionBytes() const { return _dataSection.SectionData; }

    // Every root object in __data__ as (offset, className), ascending by offset.
    // Diagnostic surface: when a Read body drifts, knowing an object's exact
    // start offset is what turns "it threw somewhere" into a byte-level answer.
    std::vector<std::pair<std::uint32_t, std::string>> ListObjects() const {
        std::vector<std::pair<std::uint32_t, std::string>> out;
        for (const auto& [src, fixup] : _dataSection._virtualMap) {
            const auto cn = _classnames.OffsetClassNamesMap.find(fixup.Dst);
            out.emplace_back(src, cn == _classnames.OffsetClassNamesMap.end()
                                      ? std::string("<unknown>") : cn->second);
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    // Outgoing object->object references as (sourceOffset, targetOffset). The
    // source is the byte position of the pointer field inside its owning object,
    // so sorting a single object's fixups by source yields its references in
    // serialization (field) order — what the tagfile #NNNN post-order walks.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> GlobalFixups() const {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
        out.reserve(_dataSection._globalMap.size());
        for (const auto& [src, fx] : _dataSection._globalMap) out.emplace_back(src, fx.Dst);
        return out;
    }

    // Count of __classnames__ entries — the tagfile numbers those first, so a
    // __data__ object's #NNNN = ClassnamesCount() + its data-order index.
    std::size_t ClassnamesCount() const { return _classnames.OffsetClassNamesMap.size(); }

    // Opt-in: skip (return null for) an unregistered class instead of throwing. Only the skeleton
    // physics read enables this (creature ragdoll bodies with exotic, unported collision shapes).
    void SetTolerateUnregistered(bool b) { _tolerateUnregistered = b; }

    /// @brief Builds each object by class name (io::MakeSchemaFactory: the generic schema-driven graph).
    /// @note Unset, or returning nullptr, means "unregistered" (see SetTolerateUnregistered).
    std::function<std::shared_ptr<IHavokObject>(const std::string&)> ObjectFactory;

    std::shared_ptr<IHavokObject> Deserialize(BinaryReaderEx& br) {
        DeserializePartially(br);
        _deserializedObjects.clear();
        BinaryReaderEx br2(_header.Endian == 0, _header.PointerSize == 8, _dataSection.SectionData);
        return ConstructVirtualClass(br2, 0);
    }

    /// @brief The registry a binary tagfile input is read through (nullptr = the shared registry).
    const schema::SchemaRegistry* TagfileSchema = nullptr;

    void DeserializePartially(BinaryReaderEx& br) {
        /// @note A binary tagfile is read into schema objects and written as a packfile, so everything below (and every
        ///       caller of the sections, fixups and offsets) reads it unchanged.
        if (br.Length() >= 8) {
            br.StepIn(0);
            const std::vector<std::uint8_t> head = br.ReadBytes(8);
            br.StepOut();
            if (IsBinaryTagfile(head)) {
                br.StepIn(0);
                const std::vector<std::uint8_t> tagfile = br.ReadBytes(br.Length());
                br.StepOut();
                BinaryReaderEx packfile(false, true, BinaryTagfileToPackfile(tagfile, TagfileSchema, _tolerateUnregistered));
                DeserializePartially(packfile);
                return;
            }
        }
        br.StepIn(0x11);
        br.BigEndian = (br.ReadByte() == 0x0);
        br.StepOut();
        _header.Read(br);
        br.SetPosition(_header.SectionOffset == -1
                           ? 0x40
                           : static_cast<std::size_t>(_header.SectionOffset) + 0x40);
        _classSection = HKXSection{}; _classSection.SectionID = 0;
        _classSection.Read(br, _header.ContentsVersionString);
        _typeSection = HKXSection{}; _typeSection.SectionID = 1;
        _typeSection.Read(br, _header.ContentsVersionString);
        _dataSection = HKXSection{}; _dataSection.SectionID = 2;
        _dataSection.Read(br, _header.ContentsVersionString);
        _classnames = _classSection.ReadClassnames(_header.Endian == 0, _header.PointerSize == 8);
    }

    // Constructs every object in __data__ whose class name == `className`, in
    // ascending file offset (so [0] is the first one in the file, matching the
    // old XML path's "first hkobject of this class" behaviour).
    //
    // Exists because Deserialize() walks the whole graph from the root, and a
    // real Skyrim skeleton.hkx root container also references ragdoll and
    // physics variants whose classes are not ported — ConstructVirtualClass
    // throws on an unregistered class, so a full walk cannot survive those
    // files. Targeting one class touches only what the caller actually needs.
    //
    // Call DeserializePartially(br) first; `br` must be a reader over
    // _dataSection.SectionData.
    std::vector<std::shared_ptr<IHavokObject>>
    ConstructAllOfClass(BinaryReaderEx& br, const std::string& className) {
        std::vector<std::uint32_t> offsets;
        for (const auto& [src, fixup] : _dataSection._virtualMap) {
            const auto cn = _classnames.OffsetClassNamesMap.find(fixup.Dst);
            if (cn != _classnames.OffsetClassNamesMap.end() && cn->second == className)
                offsets.push_back(src);
        }
        std::sort(offsets.begin(), offsets.end());

        std::vector<std::shared_ptr<IHavokObject>> out;
        out.reserve(offsets.size());
        for (const std::uint32_t off : offsets)
            if (auto obj = ConstructVirtualClass(br, off)) out.push_back(std::move(obj));
        return out;
    }

    std::shared_ptr<IHavokObject> ConstructVirtualClass(BinaryReaderEx& br, std::uint32_t offset) {
        auto cached = _deserializedObjects.find(offset);
        if (cached != _deserializedObjects.end()) return cached->second;

        auto vf = _dataSection._virtualMap.find(offset);
        if (vf == _dataSection._virtualMap.end())
            throw std::runtime_error("ConstructVirtualClass: no virtual fixup at offset");
        auto cn = _classnames.OffsetClassNamesMap.find(vf->second.Dst);
        if (cn == _classnames.OffsetClassNamesMap.end())
            throw std::runtime_error("ConstructVirtualClass: no class name at fixup dst");

        std::shared_ptr<IHavokObject> ret = ObjectFactory ? ObjectFactory(cn->second) : nullptr;
        if (!ret) {
            // Unregistered class. The walk is per-object-seek (StepIn/StepOut below), so returning
            // null here leaves every SIBLING object valid — only the referencing field goes null.
            // Opt-in leniency for the skeleton PHYSICS read: some creature ragdoll bodies use exotic
            // collision shapes (hkpBoxShape/ConvexVertices/ConvexTranslate/ListShape) we don't port;
            // skipping the shape lets the body/joint/mass still read, and the compiler derives a
            // default capsule. Strict everywhere else (behaviour graphs must not silently drop a node).
            if (_tolerateUnregistered) return nullptr;
            throw std::runtime_error("Havok class not registered: " + cn->second);
        }

        br.StepIn(offset);
        ret->Read(*this, br);
        br.StepOut();
        _deserializedObjects[offset] = ret;
        // Read completes AFTER every reference it makes has been constructed (each
        // ReadClassPointer resolves its target inline), so appending here yields a
        // post-order of the reference graph in field order (arrays inline) — the
        // exact order the tagfile #NNNN uses (root ends up last; caller special-cases it).
        _readCompletionOrder.push_back(offset);
        return ret;
    }

    // Offsets in read-completion (post-order) order — populated by a full
    // Deserialize / ConstructVirtualClass-from-root walk. Empty after a bare
    // DeserializePartially. See ConstructVirtualClass for why this is post-order.
    const std::vector<std::uint32_t>& ReadCompletionOrder() const { return _readCompletionOrder; }

    // Constructed objects keyed by their __data__ byte offset. Populated by a full
    // construction walk; lets a caller map an offset (e.g. an oracle's #NNNN->offset)
    // back to the live shared_ptr so it can mutate it in place (the patch-merge path).
    const std::unordered_map<std::uint32_t, std::shared_ptr<IHavokObject>>& DeserializedObjects() const {
        return _deserializedObjects;
    }

    // ── Read helpers (inverse of the serializer's Write helpers) ──────────────
    void ReadEmptyPointer(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        br.AssertUSize({0});
    }
    void ReadEmptyArray(BinaryReaderEx& br) {
        ReadEmptyPointer(br);
        const std::uint32_t size = br.ReadUInt32();
        br.AssertUInt32({size | (static_cast<std::uint32_t>(0x80) << 24)});
    }
    std::string ReadStringPointer(BinaryReaderEx& br) { return ReadOptionalStringPointer(br).value_or(std::string()); }
    /// @brief ReadStringPointer that keeps a null pointer (no local fixup: no string at all) apart from "".
    std::optional<std::string> ReadOptionalStringPointer(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        auto it = _dataSection._localMap.find(key);
        if (it == _dataSection._localMap.end()) return std::nullopt;
        br.StepIn(it->second.Dst);
        std::string ret = br.ReadASCII();
        br.StepOut();
        return ret;
    }
    // Inverse of WriteCString. Same read path as ReadStringPointer (a missing
    // local fixup -> empty string). Mirrors C# PackFileDeserializer.ReadCString;
    // like the C++ ReadStringPointer it does not trim.
    std::string ReadCString(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        auto it = _dataSection._localMap.find(key);
        if (it == _dataSection._localMap.end()) return std::string();
        br.StepIn(it->second.Dst);
        std::string ret = br.ReadASCII();
        br.StepOut();
        return ret;
    }
    template <class T>
    std::shared_ptr<T> ReadClassPointer(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        auto it = _dataSection._globalMap.find(key);
        if (it == _dataSection._globalMap.end()) return nullptr;
        return std::dynamic_pointer_cast<T>(ConstructVirtualClass(br, it->second.Dst));
    }
    template <class T>
    std::vector<std::shared_ptr<T>> ReadClassPointerArray(BinaryReaderEx& br) {
        return ReadArrayBase<std::shared_ptr<T>>(
            [this](BinaryReaderEx& b) { return ReadClassPointer<T>(b); }, br);
    }
    std::vector<std::string> ReadStringPointerArray(BinaryReaderEx& br) {
        return ReadArrayBase<std::string>([this](BinaryReaderEx& b) { return ReadStringPointer(b); }, br);
    }

    // Generic (schema-driven) array readers. Same framing as the templated readers above, but the
    // element type is data-driven rather than a template parameter.
    //
    // Raw fixed-width scalar array (ScalarArray / Vec4Array / QsTransformArray): reads `count`
    // elements of `elemWidth` bytes each into a flat byte buffer (endian-preserving — the bytes are
    // written back verbatim). Returns the raw bytes; count == bytes.size()/elemWidth.
    std::vector<std::uint8_t> ReadRawArray(BinaryReaderEx& br, int elemWidth) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        const std::uint32_t size = br.ReadUInt32();
        br.AssertUInt32({size | (static_cast<std::uint32_t>(0x80) << 24)});
        if (size == 0) return {};
        auto it = _dataSection._localMap.find(key);
        if (it == _dataSection._localMap.end()) return {};
        br.StepIn(it->second.Dst);
        std::vector<std::uint8_t> bytes = br.ReadBytes(static_cast<std::size_t>(size) * elemWidth);
        br.StepOut();
        return bytes;
    }
    // Inline struct array (hkArray<T> by value): builds each element via `make()` and reads it inline.
    std::vector<std::shared_ptr<IHavokObject>>
    ReadStructArrayGeneric(BinaryReaderEx& br, const std::function<std::shared_ptr<IHavokObject>()>& make) {
        return ReadArrayBase<std::shared_ptr<IHavokObject>>(
            [this, &make](BinaryReaderEx& b) { auto e = make(); e->Read(*this, b); return e; }, br);
    }

private:
    void PadToPointerSizeIfPaddingOption(BinaryReaderEx& br) {
        if (_header.PaddingOption == 1) br.Pad(_header.PointerSize);
    }
    template <class T, class Fn>
    std::vector<T> ReadArrayBase(Fn func, BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        const std::uint32_t size = br.ReadUInt32();
        br.AssertUInt32({size | (static_cast<std::uint32_t>(0x80) << 24)});
        std::vector<T> res;
        if (size == 0) return res;
        auto it = _dataSection._localMap.find(key);
        if (it == _dataSection._localMap.end()) return res;
        br.StepIn(it->second.Dst);
        for (std::uint32_t i = 0; i < size; ++i) res.push_back(func(br));
        br.StepOut();
        return res;
    }

    HKXSection    _classSection, _typeSection, _dataSection;
    HKXClassNames _classnames;
    std::unordered_map<std::uint32_t, std::shared_ptr<IHavokObject>> _deserializedObjects;
    std::vector<std::uint32_t>                                       _readCompletionOrder;
    bool                                                             _tolerateUnregistered = false;
};

} // namespace CB::engine::codec
