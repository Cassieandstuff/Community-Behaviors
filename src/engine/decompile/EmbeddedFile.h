#pragma once
#include <codec/serialization/HavokIo.h>
#include <interface/reflection/HavokSchema.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

/// @file
/// @brief Files of a `kind: embedded` descriptor (an animation unit): told apart by their root key, their
///        schema-described fields parsed and saved alone, and the `view:` hints editors draw those fields by.

namespace CB::engine::decompile {

/// @return the embedded kind `text` is (its first top-level key, past blank and comment lines, is the kind's `root`),
///         or null
const schema::EmbeddedKind* EmbeddedKindOf(const schema::SchemaRegistry& reg, std::string_view text);

/// @brief The kind's class, read from `text`'s root map (ParseEmbeddedObject).
/// @return null with `err` set when the text or the class's fields do not parse
std::shared_ptr<codec::io::SchemaObject> ParseEmbeddedFile(std::string_view text, const schema::EmbeddedKind& kind,
                                                           const schema::SchemaRegistry& reg, const std::string& where,
                                                           std::string& err);

/// @brief `text` with `obj`'s fields written into the root map as EmitEmbeddedObject renders them: every block the
///        root map holds for one of the class's fields is removed, and the rendering goes where the first of them
///        stood, else at the map's end. Every other byte is kept.
/// @note  A file whose fields were last written this way comes back byte-identical when `obj` is unchanged.
/// @return nullopt with `err` set when `text` has no top-level `root:` map
std::optional<std::string> SpliceEmbeddedFile(std::string_view text, const schema::EmbeddedKind& kind,
                                              const codec::io::SchemaObject& obj, const schema::SchemaRegistry& reg,
                                              std::string& err);

/// @brief A `view: curve` object's value at `t`, its keys read by their `view: curvekey` roles: the first key's value
///        before it, the last key's after it, and between two keys the earlier key's interpolation (the enum item
///        named CONSTANT holds the value; any other interpolates linearly).
/// @return nullopt when `curve` is not a curve, its keys are not curve keys, or it has none
std::optional<float> SampleCurve(const codec::io::SchemaObject& curve, float t, const schema::SchemaRegistry& reg);

}  // namespace CB::engine::decompile
