#pragma once
#include <cstdint>

namespace CB::engine::codec {

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
    virtual void Write(PackFileSerializer& s, BinaryWriterEx& bw) const = 0;
    virtual void Read(PackFileDeserializer& des, BinaryReaderEx& br) = 0;
};

} // namespace CB::engine::codec
