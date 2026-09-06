#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/BinaryReaderEx.h"
#include "havok/core/HavokRegistry.h"
#include "havok/core/HkTypes.h"
#include "havok/core/PackFileTypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

// Faithful port of HKX2E's PackFileDeserializer.cs. Reads a packfile back into an
// object graph: parse header + 3 sections + class names, then walk the __data__
// section from offset 0 via virtual fixups (class name -> registry factory ->
// Read), resolving pointers/arrays/strings through the section fixup maps.

namespace havok {

class PackFileDeserializer {
public:
    HKXHeader _header;

    // The raw __data__ bytes, valid after DeserializePartially. Callers that
    // drive ConstructAllOfClass need this to build the section reader.
    const std::vector<std::uint8_t>& DataSectionBytes() const { return _dataSection.SectionData; }

    // Source offsets of every local (pointer) fixup — the byte positions in the data
    // section that hold pointers. Lets a caller mask pointer noise when byte-diffing
    // raw object data (field-level compare of two packfiles). Valid after DeserializePartially.
    std::vector<std::uint32_t> LocalFixupSources() const {
        std::vector<std::uint32_t> out; out.reserve(_dataSection._localMap.size());
        for (const auto& kv : _dataSection._localMap) out.push_back(kv.first);
        return out;
    }

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

    // havok-io (Stage 2): override object construction. When set, ConstructVirtualClass asks this
    // factory (name -> object) instead of the typed HavokRegistry, so the SAME framing walk builds a
    // generic schema-driven object graph. nullptr from the factory is treated as "unregistered".
    std::function<std::shared_ptr<IHavokObject>(const std::string&)> ObjectFactory;

    std::shared_ptr<IHavokObject> Deserialize(BinaryReaderEx& br) {
        DeserializePartially(br);
        _deserializedObjects.clear();
        BinaryReaderEx br2(_header.Endian == 0, _header.PointerSize == 8, _dataSection.SectionData);
        return ConstructVirtualClass(br2, 0);
    }

    void DeserializePartially(BinaryReaderEx& br) {
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

        std::shared_ptr<IHavokObject> ret = ObjectFactory ? ObjectFactory(cn->second)
                                                          : HavokRegistry::Create(cn->second);
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
        _readStack.push_back(offset);
        ret->Read(*this, br);
        _readStack.pop_back();
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

    // Per-object outgoing references (target offsets) in Read/field order, keyed by
    // owning object offset. Populated by a full construction walk (see above).
    const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& RefsInReadOrder() const {
        return _refsInReadOrder;
    }

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
    std::string ReadStringPointer(BinaryReaderEx& br) {
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
    Vector4 ReadVector4(BinaryReaderEx& br) { return br.ReadVector4(); }
    // Quaternion: 4 floats x,y,z,w — inverse of WriteQuaternion (mirrors C#
    // PackFileDeserializer.ReadQuaternion).
    Quaternion ReadQuaternion(BinaryReaderEx& br) {
        Quaternion q;
        q.x = br.ReadSingle();
        q.y = br.ReadSingle();
        q.z = br.ReadSingle();
        q.w = br.ReadSingle();
        return q;
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
    QSTransform ReadQSTransform(BinaryReaderEx& br) {
        QSTransform q;
        q.translation.x = br.ReadSingle(); q.translation.y = br.ReadSingle();
        q.translation.z = br.ReadSingle(); q.translation.w = br.ReadSingle();
        q.rotation.x = br.ReadSingle(); q.rotation.y = br.ReadSingle();
        q.rotation.z = br.ReadSingle(); q.rotation.w = br.ReadSingle();
        q.scale.x = br.ReadSingle(); q.scale.y = br.ReadSingle();
        q.scale.z = br.ReadSingle(); q.scale.w = br.ReadSingle();
        return q;
    }
    template <std::size_t N>
    std::array<bool, N> ReadBooleanCStyleArray(BinaryReaderEx& br) {
        std::array<bool, N> a{};
        for (std::size_t i = 0; i < N; ++i) a[i] = br.ReadBoolean();
        return a;
    }
    template <class T>
    std::shared_ptr<T> ReadClassPointer(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        const std::uint32_t key = static_cast<std::uint32_t>(br.Position());
        br.AssertUSize({0});
        auto it = _dataSection._globalMap.find(key);
        if (it == _dataSection._globalMap.end()) return nullptr;
        // Record this reference against the object currently being read, in Read
        // (field, inline-array) order — the structural-alignment oracle matches
        // these positionally against the tagfile's per-object #NNNN references.
        if (!_readStack.empty()) _refsInReadOrder[_readStack.back()].push_back(it->second.Dst);
        return std::dynamic_pointer_cast<T>(ConstructVirtualClass(br, it->second.Dst));
    }
    template <class T>
    std::vector<T> ReadClassArray(BinaryReaderEx& br) {
        return ReadArrayBase<T>([this](BinaryReaderEx& b) { T c; c.Read(*this, b); return c; }, br);
    }
    template <class T>
    std::vector<std::shared_ptr<T>> ReadClassPointerArray(BinaryReaderEx& br) {
        return ReadArrayBase<std::shared_ptr<T>>(
            [this](BinaryReaderEx& b) { return ReadClassPointer<T>(b); }, br);
    }
    std::vector<std::string> ReadStringPointerArray(BinaryReaderEx& br) {
        return ReadArrayBase<std::string>([this](BinaryReaderEx& b) { return ReadStringPointer(b); }, br);
    }
    std::vector<float> ReadSingleArray(BinaryReaderEx& br) {
        return ReadArrayBase<float>([](BinaryReaderEx& b) { return b.ReadSingle(); }, br);
    }
    std::vector<std::int16_t> ReadInt16Array(BinaryReaderEx& br) {
        return ReadArrayBase<std::int16_t>([](BinaryReaderEx& b) { return b.ReadInt16(); }, br);
    }
    std::vector<std::int32_t> ReadInt32Array(BinaryReaderEx& br) {
        return ReadArrayBase<std::int32_t>([](BinaryReaderEx& b) { return b.ReadInt32(); }, br);
    }
    std::vector<Vector4> ReadVector4Array(BinaryReaderEx& br) {
        return ReadArrayBase<Vector4>([](BinaryReaderEx& b) { return b.ReadVector4(); }, br);
    }
    std::vector<std::uint32_t> ReadUInt32Array(BinaryReaderEx& br) {
        return ReadArrayBase<std::uint32_t>([](BinaryReaderEx& b) { return b.ReadUInt32(); }, br);
    }
    std::vector<std::uint8_t> ReadByteArray(BinaryReaderEx& br) {
        return ReadArrayBase<std::uint8_t>([](BinaryReaderEx& b) { return b.ReadByte(); }, br);
    }
    std::vector<QSTransform> ReadQSTransformArray(BinaryReaderEx& br) {
        return ReadArrayBase<QSTransform>(
            [this](BinaryReaderEx& b) { return ReadQSTransform(b); }, br);
    }

    // havok-io generic array readers (Stage 2). Same framing as the typed readers above, but the
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

    // Consumes a pointer slot WITHOUT resolving it. For pointers to classes that
    // are not ported: ReadClassPointer would reach ConstructVirtualClass and
    // throw, and ReadEmptyPointer asserts the slot is null. This just advances
    // the stream so the fields after it stay aligned, dropping the reference.
    void SkipPointer(BinaryReaderEx& br) {
        PadToPointerSizeIfPaddingOption(br);
        br.ReadUSize();
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
    std::vector<std::uint32_t>                                       _readStack;
    bool                                                             _tolerateUnregistered = false;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>    _refsInReadOrder;
};

} // namespace havok
