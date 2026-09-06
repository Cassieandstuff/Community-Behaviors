#include "havok/core/BinaryReaderEx.h"

#include <sstream>
#include <stdexcept>

namespace havok {

// ── raw / positioning ───────────────────────────────────────────────────────

void BinaryReaderEx::readRaw(void* out, std::size_t n) {
    if (m_pos + n > m_buf.size())
        throw std::out_of_range("BinaryReaderEx: read past end of stream.");
    std::memcpy(out, m_buf.data() + m_pos, n);
    m_pos += n;
}

std::vector<std::uint8_t> BinaryReaderEx::ReadBytes(std::size_t count) {
    std::vector<std::uint8_t> out(count);
    if (count > 0)
        readRaw(out.data(), count);
    return out;
}

void BinaryReaderEx::StepIn(std::size_t offset) {
    m_steps.push_back(m_pos);
    m_pos = offset;
}

void BinaryReaderEx::StepOut() {
    if (m_steps.empty())
        throw std::logic_error("BinaryReaderEx: already stepped all the way out.");
    m_pos = m_steps.back();
    m_steps.pop_back();
}

void BinaryReaderEx::Pad(int align) {
    const std::size_t a = static_cast<std::size_t>(align);
    if (m_pos % a > 0)
        m_pos += a - (m_pos % a);
}

// ── primitives ──────────────────────────────────────────────────────────────

std::uint8_t BinaryReaderEx::ReadByte() {
    std::uint8_t v;
    readRaw(&v, 1);
    return v;
}

bool BinaryReaderEx::ReadBoolean() {
    const std::uint8_t b = ReadByte();
    if (b == 0) return false;
    if (b == 1) return true;
    std::ostringstream os;
    os << "BinaryReaderEx::ReadBoolean encountered non-boolean value: 0x"
       << std::hex << static_cast<int>(b);
    throw std::runtime_error(os.str());
}

std::int8_t   BinaryReaderEx::ReadSByte()  { return static_cast<std::int8_t>(ReadByte()); }
std::int16_t  BinaryReaderEx::ReadInt16()  { return readScalar<std::int16_t>(); }
std::uint16_t BinaryReaderEx::ReadUInt16() { return readScalar<std::uint16_t>(); }
std::int32_t  BinaryReaderEx::ReadInt32()  { return readScalar<std::int32_t>(); }
std::uint32_t BinaryReaderEx::ReadUInt32() { return readScalar<std::uint32_t>(); }
std::int64_t  BinaryReaderEx::ReadInt64()  { return readScalar<std::int64_t>(); }
std::uint64_t BinaryReaderEx::ReadUInt64() { return readScalar<std::uint64_t>(); }
Half          BinaryReaderEx::ReadHalf()   { return readScalar<Half>(); }
double        BinaryReaderEx::ReadDouble() { return readScalar<double>(); }

float BinaryReaderEx::ReadSingle() {
    // NOTE (M3 parity): HKX2E's reader normalizes floats — NaN -> 0 and
    // Math.Round(value, 6, MidpointRounding.ToEven). havok-core reads the raw
    // bits here so binary IO round-trips EXACTLY (the M0 contract). That
    // normalization is a deserializer-parity concern and will be applied at M3,
    // verified against the documented HKX2E baselines — not baked into the raw
    // primitive (where it would corrupt exact round-trips).
    return readScalar<float>();
}

std::uint64_t BinaryReaderEx::ReadUSize() {
    return USizeLong ? ReadUInt64() : static_cast<std::uint64_t>(ReadUInt32());
}

Vector4 BinaryReaderEx::ReadVector4() {
    Vector4 v;
    v.x = ReadSingle();
    v.y = ReadSingle();
    v.z = ReadSingle();
    v.w = ReadSingle();
    return v;
}

// ── strings ─────────────────────────────────────────────────────────────────

std::string BinaryReaderEx::ReadASCII() {
    std::string s;
    for (std::uint8_t b = ReadByte(); b != 0; b = ReadByte())
        s.push_back(static_cast<char>(b));
    return s;
}

std::string BinaryReaderEx::ReadASCII(std::size_t length) {
    std::vector<std::uint8_t> bytes = ReadBytes(length);
    return std::string(bytes.begin(), bytes.end());
}

std::string BinaryReaderEx::ReadFixStr(std::size_t size) {
    std::vector<std::uint8_t> bytes = ReadBytes(size);
    std::size_t terminator = 0;
    for (; terminator < size; ++terminator)
        if (bytes[terminator] == 0)
            break;
    return std::string(bytes.begin(), bytes.begin() + terminator);
}

// ── assertions ────────────────────────────────────────────────────────────────

template <class T>
T BinaryReaderEx::assertValue(T value, std::initializer_list<T> options, const char* typeName) {
    for (T option : options)
        if (value == option)
            return value;
    std::ostringstream os;
    os << "BinaryReaderEx: read " << typeName << ": 0x" << std::hex
       << static_cast<std::uint64_t>(value) << " | expected one of:";
    for (T option : options)
        os << " 0x" << std::hex << static_cast<std::uint64_t>(option);
    os << " | ending position: 0x" << std::hex << m_pos;
    throw std::runtime_error(os.str());
}

std::uint8_t  BinaryReaderEx::AssertByte(std::initializer_list<std::uint8_t> o)   { return assertValue(ReadByte(), o, "Byte"); }
std::uint16_t BinaryReaderEx::AssertUInt16(std::initializer_list<std::uint16_t> o){ return assertValue(ReadUInt16(), o, "UInt16"); }
std::uint32_t BinaryReaderEx::AssertUInt32(std::initializer_list<std::uint32_t> o){ return assertValue(ReadUInt32(), o, "UInt32"); }
std::uint64_t BinaryReaderEx::AssertUInt64(std::initializer_list<std::uint64_t> o){ return assertValue(ReadUInt64(), o, "UInt64"); }
std::int32_t  BinaryReaderEx::AssertInt32(std::initializer_list<std::int32_t> o)  { return assertValue(ReadInt32(), o, "Int32"); }
std::uint64_t BinaryReaderEx::AssertUSize(std::initializer_list<std::uint64_t> o) { return assertValue(ReadUSize(), o, USizeLong ? "USize64" : "USize32"); }

} // namespace havok
