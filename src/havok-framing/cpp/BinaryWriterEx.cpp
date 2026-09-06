#include "havok/core/BinaryWriterEx.h"

#include <stdexcept>

namespace havok {

// ── raw / positioning ───────────────────────────────────────────────────────

void BinaryWriterEx::writeRaw(const void* data, std::size_t n) {
    if (m_pos + n > m_buf.size())
        m_buf.resize(m_pos + n, 0);          // MemoryStream: extend (zero-fill) as needed
    std::memcpy(m_buf.data() + m_pos, data, n);
    m_pos += n;
}

void BinaryWriterEx::WriteBytes(std::span<const std::uint8_t> bytes) {
    writeRaw(bytes.data(), bytes.size());
}

void BinaryWriterEx::StepIn(std::size_t offset) {
    m_steps.push_back(m_pos);
    m_pos = offset;
}

void BinaryWriterEx::StepOut() {
    if (m_steps.empty())
        throw std::logic_error("BinaryWriterEx: already stepped all the way out.");
    m_pos = m_steps.back();
    m_steps.pop_back();
}

void BinaryWriterEx::Pad(int align) {
    while (m_pos % static_cast<std::size_t>(align) > 0)
        WriteByte(0);
}

// ── primitives ──────────────────────────────────────────────────────────────

void BinaryWriterEx::WriteBoolean(bool v)        { std::uint8_t b = v ? 1 : 0; writeRaw(&b, 1); }
void BinaryWriterEx::WriteSByte(std::int8_t v)   { writeRaw(&v, 1); }
void BinaryWriterEx::WriteByte(std::uint8_t v)   { writeRaw(&v, 1); }
void BinaryWriterEx::WriteInt16(std::int16_t v)  { writeScalar(v); }
void BinaryWriterEx::WriteUInt16(std::uint16_t v){ writeScalar(v); }
void BinaryWriterEx::WriteInt32(std::int32_t v)  { writeScalar(v); }
void BinaryWriterEx::WriteUInt32(std::uint32_t v){ writeScalar(v); }
void BinaryWriterEx::WriteInt64(std::int64_t v)  { writeScalar(v); }
void BinaryWriterEx::WriteUInt64(std::uint64_t v){ writeScalar(v); }
void BinaryWriterEx::WriteHalf(Half v)           { writeScalar(v); }
void BinaryWriterEx::WriteSingle(float v)        { writeScalar(v); }
void BinaryWriterEx::WriteDouble(double v)       { writeScalar(v); }

void BinaryWriterEx::WriteUSize(std::uint64_t v) {
    if (USizeLong)
        WriteUInt64(v);
    else
        WriteUInt32(static_cast<std::uint32_t>(v));
}

void BinaryWriterEx::WriteVector4(const Vector4& v) {
    WriteSingle(v.x);
    WriteSingle(v.y);
    WriteSingle(v.z);
    WriteSingle(v.w);
}

// ── strings ─────────────────────────────────────────────────────────────────

void BinaryWriterEx::WriteASCII(std::string_view text, bool terminate) {
    // Matches .NET Encoding.ASCII (C# WriteASCII): every non-ASCII codepoint
    // becomes '?'. ASCII passes through unchanged; the only non-ASCII string that
    // reaches here in practice is the extractor's visible-null placeholder U+2400
    // ('␀', UTF-8 E2 90 80), which C# also emits as '?'.
    std::string ascii;
    ascii.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) { ascii.push_back(static_cast<char>(c)); ++i; }
        else {
            ascii.push_back('?');
            for (++i; i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80; ++i) {}
        }
    }
    if (!ascii.empty())
        writeRaw(ascii.data(), ascii.size());
    if (terminate)
        WriteByte(0);
}

void BinaryWriterEx::WriteFixStr(std::string_view text, int size, std::uint8_t padding) {
    // Mirror of BinaryWriterEx.cs: a `size`-byte field pre-filled with `padding`,
    // then ASCII(text + '\0') copied in from the start, truncated to `size`.
    std::vector<std::uint8_t> value(static_cast<std::size_t>(size), padding);
    std::vector<std::uint8_t> src;
    src.reserve(text.size() + 1);
    for (char c : text)
        src.push_back(static_cast<std::uint8_t>(c));
    src.push_back(0);  // trailing null
    const std::size_t n = std::min(static_cast<std::size_t>(size), src.size());
    if (n > 0)
        std::memcpy(value.data(), src.data(), n);
    writeRaw(value.data(), value.size());
}

// ── reservations (back-patching) ─────────────────────────────────────────────

void BinaryWriterEx::reserve(const std::string& name, const char* typeName, int length) {
    const std::string key = name + ":" + typeName;
    if (m_reservations.find(key) != m_reservations.end())
        throw std::invalid_argument("BinaryWriterEx: key already reserved: " + key);
    m_reservations[key] = m_pos;
    for (int i = 0; i < length; ++i)
        WriteByte(0xFE);
}

std::size_t BinaryWriterEx::fill(const std::string& name, const char* typeName) {
    const std::string key = name + ":" + typeName;
    auto it = m_reservations.find(key);
    if (it == m_reservations.end())
        throw std::invalid_argument("BinaryWriterEx: key is not reserved: " + key);
    const std::size_t jump = it->second;
    m_reservations.erase(it);
    return jump;
}

void BinaryWriterEx::ReserveBoolean(const std::string& name) { reserve(name, "Boolean", 1); }
void BinaryWriterEx::ReserveByte(const std::string& name)    { reserve(name, "Byte", 1); }
void BinaryWriterEx::ReserveSByte(const std::string& name)   { reserve(name, "SByte", 1); }
void BinaryWriterEx::ReserveInt16(const std::string& name)   { reserve(name, "Int16", 2); }
void BinaryWriterEx::ReserveUInt16(const std::string& name)  { reserve(name, "UInt16", 2); }
void BinaryWriterEx::ReserveInt32(const std::string& name)   { reserve(name, "Int32", 4); }
void BinaryWriterEx::ReserveUInt32(const std::string& name)  { reserve(name, "UInt32", 4); }
void BinaryWriterEx::ReserveInt64(const std::string& name)   { reserve(name, "Int64", 8); }
void BinaryWriterEx::ReserveUInt64(const std::string& name)  { reserve(name, "UInt64", 8); }
void BinaryWriterEx::ReserveHalf(const std::string& name)    { reserve(name, "Half", 2); }
void BinaryWriterEx::ReserveSingle(const std::string& name)  { reserve(name, "Single", 4); }
void BinaryWriterEx::ReserveDouble(const std::string& name)  { reserve(name, "Double", 8); }

void BinaryWriterEx::FillBoolean(const std::string& name, bool v)         { StepIn(fill(name, "Boolean")); WriteBoolean(v); StepOut(); }
void BinaryWriterEx::FillByte(const std::string& name, std::uint8_t v)    { StepIn(fill(name, "Byte"));    WriteByte(v);    StepOut(); }
void BinaryWriterEx::FillSByte(const std::string& name, std::int8_t v)    { StepIn(fill(name, "SByte"));   WriteSByte(v);   StepOut(); }
void BinaryWriterEx::FillInt16(const std::string& name, std::int16_t v)   { StepIn(fill(name, "Int16"));   WriteInt16(v);   StepOut(); }
void BinaryWriterEx::FillUInt16(const std::string& name, std::uint16_t v) { StepIn(fill(name, "UInt16"));  WriteUInt16(v);  StepOut(); }
void BinaryWriterEx::FillInt32(const std::string& name, std::int32_t v)   { StepIn(fill(name, "Int32"));   WriteInt32(v);   StepOut(); }
void BinaryWriterEx::FillUInt32(const std::string& name, std::uint32_t v) { StepIn(fill(name, "UInt32"));  WriteUInt32(v);  StepOut(); }
void BinaryWriterEx::FillInt64(const std::string& name, std::int64_t v)   { StepIn(fill(name, "Int64"));   WriteInt64(v);   StepOut(); }
void BinaryWriterEx::FillUInt64(const std::string& name, std::uint64_t v) { StepIn(fill(name, "UInt64"));  WriteUInt64(v);  StepOut(); }
void BinaryWriterEx::FillHalf(const std::string& name, Half v)            { StepIn(fill(name, "Half"));    WriteHalf(v);    StepOut(); }
void BinaryWriterEx::FillSingle(const std::string& name, float v)         { StepIn(fill(name, "Single"));  WriteSingle(v);  StepOut(); }
void BinaryWriterEx::FillDouble(const std::string& name, double v)        { StepIn(fill(name, "Double"));  WriteDouble(v);  StepOut(); }

} // namespace havok
