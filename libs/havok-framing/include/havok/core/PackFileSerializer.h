#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/BinaryWriterEx.h"
#include "havok/core/HkTypes.h"
#include "havok/core/PackFileTypes.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Faithful port of HKX2E's PackFileSerializer.cs. Walks an object graph from a
// root, emitting the __data__ section + local/global/virtual fixups + the
// __classnames__ section, then assembles the 3-section packfile.
//
// Identity maps key on raw IHavokObject* (reference identity); the serialization
// queues hold shared_ptr so referenced objects stay alive across deferred writes.
// HkxErWriter.cpp is the byte-verified C++ reference for this same algorithm.

namespace havok {

class PackFileSerializer {
public:
    HKXHeader _header;

    void Serialize(const std::shared_ptr<IHavokObject>& rootObject,
                   BinaryWriterEx& bw, const HKXHeader& header) {
        _header = header;
        bw.BigEndian = (_header.Endian == 0);
        _header.Write(bw);

        _localFixups.clear();
        _globalFixups.clear();
        _virtualFixups.clear();
        _globalLookup.clear();
        _virtualLookup.clear();
        _localWriteQueues.clear();
        _serializationQueues.clear();
        _pendingGlobals.clear();
        _pendingVirtuals.clear();
        _serializedObjects.clear();
        _currentLocalWriteQueue = 0;
        _currentSerializationQueue = 0;

        BinaryWriterEx classbw(_header.Endian == 0, _header.PointerSize == 8);
        BinaryWriterEx databw(_header.Endian == 0, _header.PointerSize == 8);

        HKXClassName{0x75585EF6u, "hkClass"}.Write(classbw);
        HKXClassName{0x5C7EA4C2u, "hkClassMember"}.Write(classbw);
        HKXClassName{0x8A3609CFu, "hkClassEnum"}.Write(classbw);
        HKXClassName{0xCE6F8A6Cu, "hkClassEnumItem"}.Write(classbw);

        _serializationQueues.emplace_back();
        _serializationQueues[0].push(rootObject);
        _localWriteQueues.emplace_back();
        _pendingVirtuals.insert(rootObject.get());

        while (_serializationQueues.size() > 1 || !_serializationQueues[0].empty()) {
            while (_serializationQueues.back().empty() && _serializationQueues.size() > 1)
                _serializationQueues.pop_back();
            if (_serializationQueues.back().empty()) continue;

            std::shared_ptr<IHavokObject> obj = _serializationQueues.back().front();
            _serializationQueues.back().pop();
            _currentSerializationQueue = static_cast<int>(_serializationQueues.size()) - 1;

            if (_serializedObjects.count(obj.get())) continue;

            if (_pendingVirtuals.count(obj.get())) {
                _pendingVirtuals.erase(obj.get());
                const std::string classname = obj->ClassName();
                if (!_virtualLookup.count(classname)) {
                    const std::uint32_t offset = static_cast<std::uint32_t>(classbw.Position());
                    HKXClassName{obj->Signature(), classname}.Write(classbw);
                    _virtualLookup[classname] = offset + 5;
                }
                _virtualFixups.push_back(
                    VirtualFixup{static_cast<std::uint32_t>(databw.Position()), 0, _virtualLookup[classname]});
                auto pg = _pendingGlobals.find(obj.get());
                if (pg != _pendingGlobals.end()) {
                    for (std::uint32_t src : pg->second)
                        _globalFixups.push_back(
                            GlobalFixup{src, 2, static_cast<std::uint32_t>(databw.Position())});
                    _pendingGlobals.erase(pg);
                }
                _globalLookup[obj.get()] = static_cast<std::uint32_t>(databw.Position());
            }

            obj->Write(*this, databw);
            _serializedObjects.insert(obj.get());
            databw.Pad(16);

            while (_localWriteQueues.size() > 1 || !_localWriteQueues[0].empty()) {
                while (_localWriteQueues.back().empty() && _localWriteQueues.size() > 1) {
                    _localWriteQueues.pop_back();
                    databw.Pad(16);
                }
                if (_localWriteQueues.back().empty()) {
                    _currentLocalWriteQueue = static_cast<int>(_localWriteQueues.size()) - 1;
                    continue;
                }
                std::function<void()> act = std::move(_localWriteQueues.back().front());
                _localWriteQueues.back().pop();
                _currentLocalWriteQueue = static_cast<int>(_localWriteQueues.size()) - 1;
                act();
            }
            databw.Pad(16);
        }

        HKXSection classNames;
        classNames.SectionID = 0;
        classNames.SectionTag = "__classnames__";
        classNames.SectionData = classbw.Data();
        classNames.ContentsVersionString = _header.ContentsVersionString;

        HKXSection types;
        types.SectionID = 1;
        types.SectionTag = "__types__";
        types.ContentsVersionString = _header.ContentsVersionString;

        HKXSection data;
        data.SectionID = 2;
        data.SectionTag = "__data__";
        data.SectionData = databw.Data();
        data.ContentsVersionString = _header.ContentsVersionString;
        data.LocalFixups = _localFixups;
        std::stable_sort(data.LocalFixups.begin(), data.LocalFixups.end(),
                         [](const LocalFixup& a, const LocalFixup& b) { return a.Dst < b.Dst; });
        data.GlobalFixups = _globalFixups;
        std::stable_sort(data.GlobalFixups.begin(), data.GlobalFixups.end(),
                         [](const GlobalFixup& a, const GlobalFixup& b) { return a.Src < b.Src; });
        data.VirtualFixups = _virtualFixups;

        classNames.WriteHeader(bw);
        types.WriteHeader(bw);
        data.WriteHeader(bw);
        classNames.WriteData(bw);
        types.WriteData(bw);
        data.WriteData(bw);
    }

    // Serialize ONE object's own struct fields to bytes (for field-level diffing of two
    // packfiles). Pointers become deterministic placeholders and array element data is
    // queued, not flushed — the result is scalars + array sizes + inline structs, laid
    // out identically for any two objects of the same class, so they byte-compare
    // field-for-field. Reads fields the decompiler never emits (that is the point).
    std::vector<std::uint8_t> SerializeStruct(const std::shared_ptr<IHavokObject>& obj, const HKXHeader& header) {
        _header = header;
        _localFixups.clear(); _globalFixups.clear(); _virtualFixups.clear();
        _globalLookup.clear(); _virtualLookup.clear();
        _localWriteQueues.clear(); _serializationQueues.clear();
        _pendingGlobals.clear(); _pendingVirtuals.clear(); _serializedObjects.clear();
        _localWriteQueues.emplace_back();
        _serializationQueues.emplace_back();
        _currentLocalWriteQueue = 0; _currentSerializationQueue = 0;
        BinaryWriterEx bw(_header.Endian == 0, _header.PointerSize == 8);
        obj->Write(*this, bw);
        return bw.Data();
    }

    // Like SerializeStruct, but ALSO flushes the local write queue so the returned
    // blob includes queued local data — string contents, primitive-array contents,
    // and embedded class-array element structs (transitions, bindings, triggers,
    // events). Pointers to OTHER objects stay 0 (serialization queue is not flushed),
    // so the blob is a canonical, pointer-target-independent fingerprint of an
    // object's full self-contained content. Used by the field-differ to compare
    // array ELEMENT contents, not just counts.
    std::vector<std::uint8_t> SerializeStructDeep(const std::shared_ptr<IHavokObject>& obj, const HKXHeader& header) {
        _header = header;
        _localFixups.clear(); _globalFixups.clear(); _virtualFixups.clear();
        _globalLookup.clear(); _virtualLookup.clear();
        _localWriteQueues.clear(); _serializationQueues.clear();
        _pendingGlobals.clear(); _pendingVirtuals.clear(); _serializedObjects.clear();
        _localWriteQueues.emplace_back();
        _serializationQueues.emplace_back();
        _currentLocalWriteQueue = 0; _currentSerializationQueue = 0;
        BinaryWriterEx bw(_header.Endian == 0, _header.PointerSize == 8);
        obj->Write(*this, bw);
        bw.Pad(16);
        while (_localWriteQueues.size() > 1 || !_localWriteQueues[0].empty()) {
            while (_localWriteQueues.back().empty() && _localWriteQueues.size() > 1) {
                _localWriteQueues.pop_back();
                bw.Pad(16);
            }
            if (_localWriteQueues.back().empty()) {
                _currentLocalWriteQueue = static_cast<int>(_localWriteQueues.size()) - 1;
                continue;
            }
            std::function<void()> act = std::move(_localWriteQueues.back().front());
            _localWriteQueues.back().pop();
            _currentLocalWriteQueue = static_cast<int>(_localWriteQueues.size()) - 1;
            act();
        }
        return bw.Data();
    }

    // ── Write helpers invoked by each class's Write(s, bw) ─────────────────────

    void WriteVoidPointer(BinaryWriterEx& bw) {
        PadToPointerSizeIfPaddingOption(bw);
        bw.WriteUSize(0);
    }
    void WriteVoidArray(BinaryWriterEx& bw) {
        WriteVoidPointer(bw);
        bw.WriteUInt32(0);
        bw.WriteUInt32(0u | (static_cast<std::uint32_t>(0x80) << 24));
    }

    void WriteClassPointer(BinaryWriterEx& bw, const std::shared_ptr<IHavokObject>& d) {
        PadToPointerSizeIfPaddingOption(bw);
        const std::uint32_t pos = static_cast<std::uint32_t>(bw.Position());
        bw.WriteUSize(0);
        if (!d) return;
        IHavokObject* key = d.get();
        auto gl = _globalLookup.find(key);
        if (gl != _globalLookup.end()) {
            _globalFixups.push_back(GlobalFixup{pos, 2, gl->second});
            return;
        }
        if (!_pendingGlobals.count(key)) {
            _pendingGlobals.emplace(key, std::vector<std::uint32_t>{});
            PushSerializationQueue();
            _serializationQueues[_currentSerializationQueue].push(d);
            PopSerializationQueue();
            _pendingVirtuals.insert(key);
        }
        _pendingGlobals[key].push_back(pos);
    }

    void WriteStringPointer(BinaryWriterEx& bw, const std::string& d, int padding = 16) {
        PadToPointerSizeIfPaddingOption(bw);
        const std::uint32_t src = static_cast<std::uint32_t>(bw.Position());
        bw.WriteUSize(0);
        const std::size_t idx = _localFixups.size();
        _localFixups.push_back(LocalFixup{src, 0});
        const std::string copy = d;
        _localWriteQueues[_currentLocalWriteQueue].push([this, &bw, idx, copy, padding]() {
            _localFixups[idx].Dst = static_cast<std::uint32_t>(bw.Position());
            bw.WriteASCII(copy, /*terminate*/ true);
            bw.Pad(padding);
        });
    }

    void WriteVector4(BinaryWriterEx& bw, const Vector4& d) { bw.WriteVector4(d); }

    // Quaternion: 4 floats x,y,z,w — byte-identical to WriteVector4 (mirrors C#
    // PackFileSerializer.WriteQuaternion -> bw.WriteSingle X/Y/Z/W).
    void WriteQuaternion(BinaryWriterEx& bw, const Quaternion& d) {
        bw.WriteSingle(d.x);
        bw.WriteSingle(d.y);
        bw.WriteSingle(d.z);
        bw.WriteSingle(d.w);
    }

    // C-string pointer. Same packfile shape as WriteStringPointer EXCEPT: an
    // empty string emits the null pointer with NO local fixup (mirrors C#
    // PackFileSerializer.WriteCString, which skips the fixup for "" / null /
    // U+2400). Used by TYPE_CSTRING members.
    void WriteCString(BinaryWriterEx& bw, const std::string& d, int padding = 16) {
        PadToPointerSizeIfPaddingOption(bw);
        const std::uint32_t src = static_cast<std::uint32_t>(bw.Position());
        bw.WriteUSize(0);
        if (d.empty()) return;
        const std::size_t idx = _localFixups.size();
        _localFixups.push_back(LocalFixup{src, 0});
        const std::string copy = d;
        _localWriteQueues[_currentLocalWriteQueue].push([this, &bw, idx, copy, padding]() {
            _localFixups[idx].Dst = static_cast<std::uint32_t>(bw.Position());
            bw.WriteASCII(copy, /*terminate*/ true);
            bw.Pad(padding);
        });
    }

    void WriteQSTransform(BinaryWriterEx& bw, const QSTransform& d) {
        // 12 floats: translation, rotation (quaternion), scale.
        bw.WriteSingle(d.translation.x); bw.WriteSingle(d.translation.y);
        bw.WriteSingle(d.translation.z); bw.WriteSingle(d.translation.w);
        bw.WriteSingle(d.rotation.x);    bw.WriteSingle(d.rotation.y);
        bw.WriteSingle(d.rotation.z);    bw.WriteSingle(d.rotation.w);
        bw.WriteSingle(d.scale.x);       bw.WriteSingle(d.scale.y);
        bw.WriteSingle(d.scale.z);       bw.WriteSingle(d.scale.w);
    }

    template <std::size_t N>
    void WriteBooleanCStyleArray(BinaryWriterEx& bw, const std::array<bool, N>& d) {
        for (bool b : d) bw.WriteBoolean(b);
    }

    // ── array writers ─────────────────────────────────────────────────────────
    template <class T>
    void WriteClassPointerArray(BinaryWriterEx& bw, const std::vector<std::shared_ptr<T>>& d) {
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { WriteClassPointer(bw, d[i]); });
    }
    template <class T>
    void WriteClassArray(BinaryWriterEx& bw, const std::vector<T>& d) {
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { d[i].Write(*this, bw); }, /*pad*/ true);
    }
    void WriteStringPointerArray(BinaryWriterEx& bw, const std::vector<std::string>& d) {
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { WriteStringPointer(bw, d[i], 2); });
    }
    void WriteSingleArray(BinaryWriterEx& bw, const std::vector<float>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteSingle(d[i]); });
    }
    void WriteInt16Array(BinaryWriterEx& bw, const std::vector<std::int16_t>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteInt16(d[i]); });
    }
    void WriteInt32Array(BinaryWriterEx& bw, const std::vector<std::int32_t>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteInt32(d[i]); });
    }
    void WriteUInt32Array(BinaryWriterEx& bw, const std::vector<std::uint32_t>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteUInt32(d[i]); });
    }
    void WriteByteArray(BinaryWriterEx& bw, const std::vector<std::uint8_t>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteByte(d[i]); });
    }
    void WriteQSTransformArray(BinaryWriterEx& bw, const std::vector<QSTransform>& d) {
        // 48-byte elements, already 16-byte aligned — same shape as Vector4Array.
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { WriteQSTransform(bw, d[i]); });
    }
    void WriteVector4Array(BinaryWriterEx& bw, const std::vector<Vector4>& d) {
        WriteArrayBase(bw, d.size(), [&bw, &d](std::size_t i) { bw.WriteVector4(d[i]); });
    }

    // ── havok-io generic array writers (Stage 2) — inverse of the deserializer's generic readers ──
    // Raw fixed-width scalar array (ScalarArray / Vec4Array / QsTransformArray): `bytes` is the flat
    // element buffer, `elemWidth` its stride; count = bytes.size()/elemWidth.
    void WriteRawArray(BinaryWriterEx& bw, const std::vector<std::uint8_t>& bytes, int elemWidth) {
        const std::size_t count = elemWidth > 0 ? bytes.size() / static_cast<std::size_t>(elemWidth) : 0;
        WriteArrayBase(bw, count, [&bw, &bytes, elemWidth](std::size_t i) {
            bw.WriteBytes(std::span<const std::uint8_t>(bytes.data() + i * elemWidth, elemWidth));
        });
    }
    void WriteClassPointerArrayGeneric(BinaryWriterEx& bw,
                                       const std::vector<std::shared_ptr<IHavokObject>>& d) {
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { WriteClassPointer(bw, d[i]); });
    }
    void WriteStructArrayGeneric(BinaryWriterEx& bw,
                                 const std::vector<std::shared_ptr<IHavokObject>>& d) {
        WriteArrayBase(bw, d.size(), [this, &bw, &d](std::size_t i) { d[i]->Write(*this, bw); }, /*pad*/ true);
    }

private:
    void PadToPointerSizeIfPaddingOption(BinaryWriterEx& bw) {
        if (_header.PaddingOption == 1) bw.Pad(_header.PointerSize);
    }
    void PushLocalWriteQueue() {
        _currentLocalWriteQueue++;
        if (static_cast<std::size_t>(_currentLocalWriteQueue) == _localWriteQueues.size())
            _localWriteQueues.emplace_back();
    }
    void PopLocalWriteQueue() { _currentLocalWriteQueue--; }
    void PushSerializationQueue() {
        _currentSerializationQueue++;
        if (static_cast<std::size_t>(_currentSerializationQueue) == _serializationQueues.size())
            _serializationQueues.emplace_back();
    }
    void PopSerializationQueue() { _currentSerializationQueue--; }

    template <class Fn>
    void WriteArrayBase(BinaryWriterEx& bw, std::size_t count, Fn perElement, bool pad = false) {
        PadToPointerSizeIfPaddingOption(bw);
        const std::uint32_t src = static_cast<std::uint32_t>(bw.Position());
        bw.WriteUSize(0);
        bw.WriteUInt32(static_cast<std::uint32_t>(count));
        bw.WriteUInt32(static_cast<std::uint32_t>(count) | (static_cast<std::uint32_t>(0x80) << 24));
        if (count == 0) return;
        const std::size_t idx = _localFixups.size();
        _localFixups.push_back(LocalFixup{src, 0});
        _localWriteQueues[_currentLocalWriteQueue].push([this, &bw, count, perElement, idx]() {
            bw.Pad(16);
            _localFixups[idx].Dst = static_cast<std::uint32_t>(bw.Position());
            PushLocalWriteQueue();
            for (std::size_t i = 0; i < count; ++i) perElement(i);
            PopLocalWriteQueue();
        });
        if (pad)
            _localWriteQueues[_currentLocalWriteQueue].push([&bw]() { bw.Pad(16); });
    }

    int _currentLocalWriteQueue = 0;
    int _currentSerializationQueue = 0;
    std::vector<LocalFixup>   _localFixups;
    std::vector<GlobalFixup>  _globalFixups;
    std::vector<VirtualFixup> _virtualFixups;
    std::unordered_map<IHavokObject*, std::uint32_t>              _globalLookup;
    std::unordered_map<IHavokObject*, std::vector<std::uint32_t>> _pendingGlobals;
    std::unordered_set<IHavokObject*> _pendingVirtuals;
    std::unordered_set<IHavokObject*> _serializedObjects;
    std::unordered_map<std::string, std::uint32_t> _virtualLookup;
    std::vector<std::queue<std::function<void()>>>             _localWriteQueues;
    std::vector<std::queue<std::shared_ptr<IHavokObject>>>     _serializationQueues;
};

} // namespace havok
