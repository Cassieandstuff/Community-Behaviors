// havok-core — M0 exit check: BinaryReaderEx / BinaryWriterEx round-trip tests.
//
// Dependency-free harness (no GoogleTest). Build target `havok-core-tests`;
// run the exe — exit code 0 = all green. This validates the binary-IO plumbing
// (primitives, endianness, USize 32/64, strings, padding, step in/out, and the
// reserve/fill back-patching) that every later milestone builds on.

#include "havok/core/BinaryReaderEx.h"
#include "havok/core/BinaryWriterEx.h"
#include "test_harness.h"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>

using havok::BinaryReaderEx;
using havok::BinaryWriterEx;
using havok::Vector4;

static void test_primitives() {
    BinaryWriterEx w;
    w.WriteBoolean(true);
    w.WriteBoolean(false);
    w.WriteSByte(static_cast<std::int8_t>(-5));
    w.WriteByte(static_cast<std::uint8_t>(0xAB));
    w.WriteInt16(static_cast<std::int16_t>(-12345));
    w.WriteUInt16(static_cast<std::uint16_t>(54321));
    w.WriteInt32(-123456789);
    w.WriteUInt32(0xDEADBEEFu);
    w.WriteInt64(-1234567890123LL);
    w.WriteUInt64(0xFEEDFACECAFEBEEFull);
    w.WriteHalf(static_cast<havok::Half>(0x3C00));  // raw bits of half 1.0
    w.WriteSingle(1.5f);
    w.WriteSingle(-2.25f);
    w.WriteSingle(0.0f);
    w.WriteDouble(2.5);
    w.WriteVector4(Vector4{1.0f, -2.0f, 0.25f, 100.0f});

    BinaryReaderEx r(w.Data());
    CHECK(r.ReadBoolean() == true);
    CHECK(r.ReadBoolean() == false);
    CHECK(r.ReadSByte() == static_cast<std::int8_t>(-5));
    CHECK(r.ReadByte() == 0xAB);
    CHECK(r.ReadInt16() == static_cast<std::int16_t>(-12345));
    CHECK(r.ReadUInt16() == 54321);
    CHECK(r.ReadInt32() == -123456789);
    CHECK(r.ReadUInt32() == 0xDEADBEEFu);
    CHECK(r.ReadInt64() == -1234567890123LL);
    CHECK(r.ReadUInt64() == 0xFEEDFACECAFEBEEFull);
    CHECK(r.ReadHalf() == 0x3C00);
    CHECK(r.ReadSingle() == 1.5f);
    CHECK(r.ReadSingle() == -2.25f);
    CHECK(r.ReadSingle() == 0.0f);
    CHECK(r.ReadDouble() == 2.5);
    CHECK(r.ReadVector4() == (Vector4{1.0f, -2.0f, 0.25f, 100.0f}));
    CHECK(r.Position() == w.Length());
}

static void test_reservation() {
    BinaryWriterEx w;
    w.WriteUInt32(0x11111111u);
    w.ReserveUInt32("count");     // placeholder occupies offset 4..7
    w.WriteUInt32(0x22222222u);
    CHECK(w.Data()[4] == 0xFE);   // placeholder byte before fill
    w.FillUInt32("count", 0xABCDEF01u);
    CHECK(w.Position() == w.Length());  // StepOut restored cursor to the end

    BinaryReaderEx r(w.Data());
    CHECK(r.ReadUInt32() == 0x11111111u);
    CHECK(r.ReadUInt32() == 0xABCDEF01u);  // the back-patched value
    CHECK(r.ReadUInt32() == 0x22222222u);  // untouched by the fill
}

static void test_pad() {
    BinaryWriterEx w;
    w.WriteByte(0xAA);
    w.Pad(4);
    CHECK(w.Position() == 4);
    CHECK(w.Length() == 4);
    CHECK(w.Data()[1] == 0 && w.Data()[2] == 0 && w.Data()[3] == 0);

    BinaryReaderEx r(w.Data());
    (void)r.ReadByte();
    r.Pad(4);
    CHECK(r.Position() == 4);
}

static void test_fixstr() {
    BinaryWriterEx w;
    w.WriteFixStr("TEST", 8);       // "TEST" + 4 null padding
    w.WriteFixStr("EXACTLY8", 8);   // exactly fills the field, no terminator
    w.WriteFixStr("TOOLONGFORFIELD", 8);  // truncated to 8 bytes

    BinaryReaderEx r(w.Data());
    CHECK(r.ReadFixStr(8) == "TEST");
    CHECK(r.ReadFixStr(8) == "EXACTLY8");
    CHECK(r.ReadFixStr(8) == "TOOLONGF");
}

static void test_ascii() {
    BinaryWriterEx w;
    w.WriteASCII("hello", true);  // null-terminated
    w.WriteUInt16(0xBEEFu);

    BinaryReaderEx r(w.Data());
    CHECK(r.ReadASCII() == "hello");
    CHECK(r.ReadUInt16() == 0xBEEFu);
}

static void test_usize() {
    {  // 32-bit USize
        BinaryWriterEx w(false, false);
        w.WriteUSize(0x12345678ull);
        CHECK(w.Length() == 4);
        BinaryReaderEx r(false, false, w.Data());
        CHECK(r.ReadUSize() == 0x12345678ull);
    }
    {  // 64-bit USize
        BinaryWriterEx w(false, true);
        w.WriteUSize(0x123456789ABCull);
        CHECK(w.Length() == 8);
        BinaryReaderEx r(false, true, w.Data());
        CHECK(r.ReadUSize() == 0x123456789ABCull);
    }
}

static void test_bigendian() {
    BinaryWriterEx w(true, false);  // big-endian
    w.WriteUInt32(0x01020304u);
    CHECK(w.Data()[0] == 0x01 && w.Data()[1] == 0x02 &&
          w.Data()[2] == 0x03 && w.Data()[3] == 0x04);
    BinaryReaderEx r(true, false, w.Data());
    CHECK(r.ReadUInt32() == 0x01020304u);
}

static void test_stepinout() {
    BinaryWriterEx w;
    w.WriteUInt32(0u);            // to be overwritten via StepIn
    w.WriteUInt32(0x99999999u);
    w.StepIn(0);
    w.WriteUInt32(0x12345678u);
    w.StepOut();
    CHECK(w.Position() == 8);     // cursor restored to the end

    BinaryReaderEx r(w.Data());
    CHECK(r.ReadUInt32() == 0x12345678u);
    CHECK(r.ReadUInt32() == 0x99999999u);
}

static void test_assert() {
    BinaryWriterEx w;
    w.WriteUInt32(0x57E0E057u);

    BinaryReaderEx r(w.Data());
    CHECK(r.AssertUInt32({0x57E0E057u}) == 0x57E0E057u);

    bool threw = false;
    BinaryReaderEx r2(w.Data());
    try {
        r2.AssertUInt32({0xDEADBEEFu});
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

void run_binaryio_tests() {
    test_primitives();
    test_reservation();
    test_pad();
    test_fixstr();
    test_ascii();
    test_usize();
    test_bigendian();
    test_stepinout();
    test_assert();
}
