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

struct MetaTextWriteOptions {
    /// @brief Refuse a frame over its class's `framelimit`. Off only when the reader is known to take the full count
    ///        (the plugin, after it lifts the engine's 16-bit line counts).
    bool frameLimits = true;
};

/// @brief Write a metadata object back: CRLF after every line (the game's form), counts and frames recomputed, every
///        `Text` value verbatim — so a file read by ReadMetaText writes back byte-identical.
/// @return false with `err` set when a frame exceeds its class's `framelimit` (the engine would read it short) and
///         `opts.frameLimits` is on.
bool WriteMetaText(const SchemaObject& root, std::string& out, std::string& err, const MetaTextWriteOptions& opts = {});

} // namespace CB::engine::codec::io

namespace CB::engine::codec::io {

/// @brief A metadata object as YAML — the reflective, editable form a bundle carries (a speeddata project's tables):
///        every scalar a string scalar (plain when safe, double-quoted otherwise, so its text survives byte-exact),
///        a stringarray a sequence, a recordarray a sequence of maps (a record of scalars only on one flow line).
/// @note The class is not written: the reader names it (ParseMetaYaml's `cls`), as the file's place in the bundle does.
std::string EmitMetaYaml(const SchemaObject& obj);

/// @brief Read EmitMetaYaml's form back into a SchemaObject of metadata class `cls`.
/// @return nullptr with `err` set on malformed YAML or a key the class does not declare (a typo must not vanish).
std::shared_ptr<SchemaObject> ParseMetaYaml(const schema::SchemaRegistry& reg, const std::string& cls, std::string_view yaml,
                                            std::string& err);

} // namespace CB::engine::codec::io

namespace CB::engine::codec::io {

/// @brief The engine's key for a speeddata project: the file stem of its path ("DefaultMaleData\DefaultMale.spd" →
///        "DefaultMale"; the loader drops the directory and extension). Case kept as written.
inline std::string SpeedDataProjectStem(std::string_view path) {
    const auto s = path.find_last_of("\/");
    std::string_view f = s == std::string_view::npos ? path : path.substr(s + 1);
    const auto d = f.find_last_of('.');
    return std::string(d == std::string_view::npos ? f : f.substr(0, d));
}

} // namespace CB::engine::codec::io
