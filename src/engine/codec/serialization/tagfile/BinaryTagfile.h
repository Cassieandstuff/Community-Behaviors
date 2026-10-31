#pragma once
/// @file
/// @brief Reads a Havok binary tagfile (magic 0xCAB00D1E 0xD011FACE) into the same SchemaObject graph the
/// packfile path builds.
///
/// A tagfile is a stream, not a memory image: it describes each class it uses (name, version, parent, and every
/// member's name and type) before the objects that use it, and stores an object as presence bits plus the values of
/// its stored members. So it is read by member NAME onto the schema's fields; a field the file doesn't store keeps
/// its Init() default, and a stored member the schema has no field for is an error (never dropped silently).
///
/// The stream (little-endian):
///   - magic, then tags until the end tag: 1 file info (the version), 2 a class, 3 an object, 4 an object that is also
///     remembered, 7 the end. Inside a version 0 pointer, also 5 a back reference (0 = the first object remembered)
///     and 6 null.
///   - an int: bit 0 of the first byte is the sign, bits 1-6 the low six bits; bit 7 of every byte says another
///     byte follows with seven more bits.
///   - a string: an int; > 0 is that many bytes of a new string (remembered), <= 0 is -index into the strings seen so
///     far, which start as "" and null.
///   - a class: name, version, parent (an index into the classes, 0 = none), member count, then per member its name,
///     type, tuple size (if a tuple) and class name (if an object or a struct). A type is a kind in the low nibble (0
///     void, not stored; 1 byte; 2 int; 3 real; 4-7 vec4/8/12/16; 8 object; 9 struct; 10 string), | 0x10 for an array,
///     | 0x20 for a tuple.
///   - an object: its class index, presence bits for every member (the parents' first; LSB first), then each stored
///     value. A struct is an object body without the class index; an array is a count, then its elements.
///   - array elements: an int array starts with the element width (version 3); a vec4 array with the components stored
///     (3 or 4); a struct array is stored by column, presence bits for the struct's members, then each stored member's
///     values for every element.
/// Versions 0 and 3, the two Skyrim SE content uses (Creation Club bgssse001 ships both). They differ in three places:
/// version 3 stores a pointer as an int index into the remembered objects (0 = null, forward references allowed) and
/// an int array's element width; version 0 stores the pointed-to object inline and ends at the end of the file, with
/// no end tag.

#include <codec/serialization/packfile/IHavokObject.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace CB::engine::schema { class SchemaRegistry; }

namespace CB::engine::codec {

/// @brief True when `bytes` start with the binary tagfile magic.
bool IsBinaryTagfile(std::span<const std::uint8_t> bytes) noexcept;

/// @brief Read a binary tagfile into schema objects.
/// @return The file's root object (its first top-level object, the hkRootLevelContainer).
/// @param tolerateUnregistered An object of a class the registry lacks reads as null instead of throwing (the
///        PackFileDeserializer option of the same name).
/// @warning Throws std::runtime_error on a malformed stream, an unsupported version, a class the registry lacks, or
///          a stored member whose class schema has no field of that name.
std::shared_ptr<IHavokObject> ReadBinaryTagfile(std::span<const std::uint8_t> bytes, const schema::SchemaRegistry& reg,
                                                bool tolerateUnregistered = false);

/// @brief The same graph written as a Skyrim SE (64-bit) packfile, through PackFileSerializer.
/// @note  PackFileDeserializer reads a tagfile through this, so every packfile reader (offsets, fixups, read order)
///        takes one unchanged.
/// @param reg nullptr = the shared registry (schema::SharedRegistry()).
/// @warning Throws std::runtime_error as ReadBinaryTagfile does, or when `reg` is null and no shared registry loads.
std::vector<std::uint8_t> BinaryTagfileToPackfile(std::span<const std::uint8_t> bytes,
                                                  const schema::SchemaRegistry* reg = nullptr,
                                                  bool tolerateUnregistered = false);

} ///< namespace CB::engine::codec
