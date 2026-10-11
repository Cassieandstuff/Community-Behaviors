#pragma once
#include <cstdint>

namespace CB::engine::codec {

class PackFileSerializer;
class PackFileDeserializer;
class BinaryReaderEx;
class BinaryWriterEx;

/// @brief The packfile serializer's object interface; its one implementation is the schema-driven io::SchemaObject.
/// @note `Signature` is the class CRC (metadata, not a serialized field); `ClassName` feeds the packfile
///       class-name table + the registry.
class IHavokObject {
public:
    virtual ~IHavokObject() = default;
    virtual std::uint32_t Signature() const noexcept = 0;
    virtual const char*   ClassName() const noexcept = 0;
    virtual void Write(PackFileSerializer& s, BinaryWriterEx& bw) const = 0;
    virtual void Read(PackFileDeserializer& des, BinaryReaderEx& br) = 0;
};

} // namespace CB::engine::codec
