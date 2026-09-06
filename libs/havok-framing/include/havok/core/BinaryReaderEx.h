#pragma once
#include "havok/core/HkTypes.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>
#include <string>
#include <vector>

namespace havok {

// Faithful C++ port of HKX2E's BinaryReaderEx.cs. Owns a copy of the input
// bytes and reads them through a movable cursor with a step stack (for
// fixup-driven jumps). Host is assumed little-endian (SSE x64).
class BinaryReaderEx {
public:
    BinaryReaderEx() = default;
    explicit BinaryReaderEx(std::vector<std::uint8_t> input)
        : m_buf(std::move(input)) {}
    BinaryReaderEx(bool bigEndian, bool uSizeLong, std::vector<std::uint8_t> input)
        : BigEndian(bigEndian), USizeLong(uSizeLong), m_buf(std::move(input)) {}
    BinaryReaderEx(bool bigEndian, bool uSizeLong, std::span<const std::uint8_t> input)
        : BigEndian(bigEndian), USizeLong(uSizeLong), m_buf(input.begin(), input.end()) {}

    bool BigEndian = false;
    bool USizeLong = false;

    // ── stream state ────────────────────────────────────────────────────────
    std::size_t Position() const noexcept { return m_pos; }
    void        SetPosition(std::size_t p) noexcept { m_pos = p; }
    void        Skip(std::size_t n) noexcept { m_pos += n; }
    std::size_t Length() const noexcept { return m_buf.size(); }

    // ── raw / positioning ───────────────────────────────────────────────────
    std::vector<std::uint8_t> ReadBytes(std::size_t count);
    void StepIn(std::size_t offset);
    void StepOut();
    void Pad(int align);

    // ── primitives ──────────────────────────────────────────────────────────
    bool          ReadBoolean();
    std::int8_t   ReadSByte();
    std::uint8_t  ReadByte();
    std::int16_t  ReadInt16();
    std::uint16_t ReadUInt16();
    std::int32_t  ReadInt32();
    std::uint32_t ReadUInt32();
    std::int64_t  ReadInt64();
    std::uint64_t ReadUInt64();
    Half          ReadHalf();
    float         ReadSingle();
    double        ReadDouble();
    std::uint64_t ReadUSize();
    Vector4       ReadVector4();

    // ── strings ─────────────────────────────────────────────────────────────
    std::string ReadASCII();                  // null-terminated
    std::string ReadASCII(std::size_t length);
    std::string ReadFixStr(std::size_t size);

    // ── assertions (used by the deserializer) ────────────────────────────────
    std::uint8_t  AssertByte(std::initializer_list<std::uint8_t> options);
    std::uint16_t AssertUInt16(std::initializer_list<std::uint16_t> options);
    std::uint32_t AssertUInt32(std::initializer_list<std::uint32_t> options);
    std::uint64_t AssertUInt64(std::initializer_list<std::uint64_t> options);
    std::int32_t  AssertInt32(std::initializer_list<std::int32_t> options);
    std::uint64_t AssertUSize(std::initializer_list<std::uint64_t> options);

private:
    void readRaw(void* out, std::size_t n);

    template <class T>
    T readScalar() {
        std::uint8_t bytes[sizeof(T)];
        readRaw(bytes, sizeof(T));
        if (BigEndian)
            std::reverse(std::begin(bytes), std::end(bytes));
        T v;
        std::memcpy(&v, bytes, sizeof(T));
        return v;
    }

    template <class T>
    T assertValue(T value, std::initializer_list<T> options, const char* typeName);

    std::vector<std::uint8_t> m_buf;
    std::size_t               m_pos = 0;
    std::vector<std::size_t>  m_steps;
};

} // namespace havok
