#pragma once
#include <cstdint>

namespace havok {

// Forward declarations for the (de)serializer. Read/Write join this interface
// at M2 alongside the PackFileSerializer; for M1 the class model is pure data
// (fields + identity), which is all Tier-B's builder and the editor need.
class PackFileSerializer;
class PackFileDeserializer;
class BinaryReaderEx;
class BinaryWriterEx;

// Base of every object in the Havok model. Faithful port of HKX2E's
// IHavokObject. `Signature` is the class CRC (metadata, not a serialized field);
// `ClassName` feeds the packfile class-name table + the registry.
//
// Value conventions used by the ported classes (HKX2E -> C++):
//   - inline struct  (TYPE_STRUCT)        -> member by value
//   - pointer        (TYPE_POINTER)       -> std::shared_ptr<T>   (T may be incomplete)
//   - array of pointers                   -> std::vector<std::shared_ptr<T>>
//   - array of structs                    -> std::vector<T>
//   - C-style inline array (arrSize > 0)  -> std::array<T, N>
//   - string         (TYPE_STRINGPTR)     -> std::string
//   - void-typed SERIALIZE_IGNORED fields -> omitted (serializer emits empty
//                                            slots positionally; they carry no value)
//   - typed   SERIALIZE_IGNORED fields    -> kept (they occupy bytes and a value
//                                            that must survive a round-trip)
class IHavokObject {
public:
    virtual ~IHavokObject() = default;
    virtual std::uint32_t Signature() const noexcept = 0;
    virtual const char*   ClassName() const noexcept = 0;
    // Serialize this object's fields into `bw` via the serializer `s` (defined
    // per class in ClassWrite.cpp); read them back via `des` (ClassRead.cpp).
    virtual void Write(PackFileSerializer& s, BinaryWriterEx& bw) const = 0;
    virtual void Read(PackFileDeserializer& des, BinaryReaderEx& br) = 0;
};

} // namespace havok

// Emit the Signature()/ClassName()/Write/Read overrides' DECLARATIONS for a
// ported class. Write bodies live in ClassWrite.cpp, Read in ClassRead.cpp.
// Defined here so every class header needs only IHavokObject.h (the serializer
// and deserializer are forward-declared).
#define HK_CLASS_ID(sig, name)                                              \
    std::uint32_t Signature() const noexcept override { return (sig); }     \
    const char*   ClassName() const noexcept override { return (name); }    \
    void Write(::havok::PackFileSerializer& s,                              \
               ::havok::BinaryWriterEx& bw) const override;                 \
    void Read(::havok::PackFileDeserializer& des,                          \
              ::havok::BinaryReaderEx& br) override;
