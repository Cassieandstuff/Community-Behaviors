#pragma once
#include <codec/serialization/HavokIo.h>

#include <memory>
#include <string>
#include <string_view>

namespace CB::engine::codec::io {

/// @brief Read a Bethesda line-oriented text file (AnimationDataSingleFile.txt / AnimationSetDataSingleFile.txt)
///        into a SchemaObject of the metadata class `rootClass`, driven entirely by its `kind: metadata` descriptors.
/// @note The grammar the descriptors declare: a `Text` field is one line, verbatim; a `stringarray` is a count line + its
///       lines; a `recordarray` is a count line + records — with `header: f`, the count is followed by every record's
///       `f` (hoisted) before the bodies; with `count: frame`, no count — records run to the end of the frame; fields
///       sharing a `frame` are preceded by ONE line count covering them all; `when: b` gates a field on bool field `b`
///       reading "1"; a `terminator: blank` class ends each record with a blank line. LF or CRLF input.
/// @return nullptr with `err` set when the text does not follow the descriptors (a count, frame, or terminator off).
std::shared_ptr<SchemaObject> ReadMetaText(const schema::SchemaRegistry& reg, const std::string& rootClass,
                                           std::string_view text, std::string& err);

/// @brief Write a metadata object back: CRLF after every line (the game's form), counts and frames recomputed, every
///        `Text` value verbatim — so a file read by ReadMetaText writes back byte-identical.
/// @return false with `err` set when a frame exceeds its class's `framelimit` (the engine would read it short).
bool WriteMetaText(const SchemaObject& root, std::string& out, std::string& err);

} // namespace CB::engine::codec::io
