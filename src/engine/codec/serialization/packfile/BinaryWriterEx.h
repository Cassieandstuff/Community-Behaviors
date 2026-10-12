#pragma once
#include <common/HkTypes.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CB::engine::codec {
using namespace CB::engine::common;

// Faithful C++ port of HKX2E's BinaryWriterEx.cs.
//
// Backed by an in-memory byte buffer with a movable write cursor. Writing past
// the end grows the buffer (MemoryStream semantics); writing before the end
// overwrites — this is what powers the reserve/fill back-patching used to emit
// section headers and forward offsets.
//
// Host is assumed little-endian (SSE x64). `BigEndian` reverses scalar bytes.
class BinaryWriterEx {
public:
    BinaryWriterEx() = default;
    BinaryWriterEx(bool bigEndian, bool uSizeLong)
        : BigEndian(bigEndian), USizeLong(uSizeLong) {}

    bool BigEndian = false;
    bool USizeLong = false;

    // ── stream state ────────────────────────────────────────────────────────
    std::size_t Position() const noexcept { return m_pos; }
    void        SetPosition(std::size_t pos) noexcept { m_pos = pos; }
    std::size_t Length() const noexcept { return m_buf.size(); }
    const std::vector<std::uint8_t>& Data() const noexcept { return m_buf; }
    std::vector<std::uint8_t>        Take() noexcept { return std::move(m_buf); }

    // ── raw / positioning ───────────────────────────────────────────────────
    void WriteBytes(std::span<const std::uint8_t> bytes);
    void StepIn(std::size_t offset);
    void StepOut();
    void Pad(int align);

    // ── primitives ──────────────────────────────────────────────────────────
    void WriteByte(std::uint8_t v);
    void WriteInt16(std::int16_t v);
    void WriteInt32(std::int32_t v);
    void WriteUInt32(std::uint32_t v);
    void WriteUInt64(std::uint64_t v);
    void WriteUSize(std::uint64_t v);

    // ── strings ─────────────────────────────────────────────────────────────
    void WriteASCII(std::string_view text, bool terminate = false);
    void WriteFixStr(std::string_view text, int size, std::uint8_t padding = 0);

    // ── reservations (back-patching) ─────────────────────────────────────────
    void ReserveUInt32(const std::string& name);

    void FillUInt32(const std::string& name, std::uint32_t v);

private:
    void        writeRaw(const void* data, std::size_t n);
    void        reserve(const std::string& name, const char* typeName, int length);
    std::size_t fill(const std::string& name, const char* typeName);

    template <class T>
    void writeScalar(T v) {
        std::uint8_t bytes[sizeof(T)];
        std::memcpy(bytes, &v, sizeof(T));
        if (BigEndian)
            std::reverse(std::begin(bytes), std::end(bytes));
        writeRaw(bytes, sizeof(T));
    }

    std::vector<std::uint8_t>                     m_buf;
    std::size_t                                   m_pos = 0;
    std::unordered_map<std::string, std::size_t>  m_reservations;
    std::vector<std::size_t>                      m_steps;
};

} // namespace CB::engine::codec
