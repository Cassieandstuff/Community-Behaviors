#pragma once

// ── sct-config — the public API ───────────────────────────────────────────────
// Shared config discovery + loading for the SKSE plugin family: one
// deterministically-ordered directory scanner plus per-format adapters
// (JSON / YAML / INI), consolidating the config readers each plugin was
// re-cutting (Engine Relay's YAML ConfigLoader, Dialogue Camera / True Flight
// SimpleIni, True Cinematics' SceneLoader, Behavior Relay's resolver).
//
// The scan convention matches how the ecosystem ships *distributed* configs
// (SPID / BDI / SkyPatcher style): a mod drops a file into a known directory,
// every mod's file is read, and load order is lexicographic by path so authors
// control it with filename prefixes (00_, zzz_). That ordering step — the bit a
// raw directory_iterator gets wrong — lives here, correct once.
//
// SINGLE PUBLIC HEADER (CommonLib `Skyrim.h` model): this file is the entire
// cross-project surface — consumers write `#include <sct-config/SctConfig.h>`
// and nothing else. Nothing besides this file may ever live in
// include/internal/sct-config/.
//
// DEP NOTE: because each adapter's Doc holds the parser's own type by value
// (nlohmann::json / CSimpleIniA / c4::yml::Tree), the parser types are part of
// the API and cannot be hidden. Including this umbrella therefore pulls in all
// three parsers (nlohmann/json, SimpleIni, rapidyaml). That is the accepted
// cost of the one-header rule; a TU that only reads one format still compiles
// the other two.
// ------------------------------------------------------------------------------

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <SimpleIni.h>
#include <nlohmann/json.hpp>
#include <RymlInclude.h>   // rapidyaml through the mandatory c4core C++20 shim

namespace sct::config {

// ── Discovery core (pulls in no parser on its own) ────────────────────────────

// One discovered config file.
struct Source {
    std::filesystem::path path;   // absolute path to the file on disk
    std::string           stem;   // filename without extension — the config/mod handle
    std::string           key;    // path relative to its scan root, '/'-separated + lowercased;
                                  //   the sort key, and a stable handle for attribution / dedup
};

// A per-file failure. Loaders collect these instead of throwing, so one bad
// config never sinks the rest of the batch.
struct LoadError {
    Source      source;
    std::string message;
};

// A format loader's result: the docs that parsed + the files that didn't.
template <class Doc>
struct Batch {
    std::vector<Doc>       docs;
    std::vector<LoadError> errors;
};

// What to scan and how.
struct ScanSpec {
    std::vector<std::filesystem::path> roots;        // config dirs; a missing / non-dir root is skipped
    std::vector<std::string>           extensions;   // e.g. {".json"} — case-insensitive, leading dot optional;
                                                     //   empty means "every regular file"
    bool                               recursive = false;   // flat scan by default (the common ecosystem shape)
};

// Discover config files under the spec's roots, returned in a deterministic
// order: lexicographic by `key`. A missing / non-directory root is silently
// skipped (config dirs are optional). Regular files only.
std::vector<Source> Discover(const ScanSpec& spec);

// Read a whole file as text: UTF-8, a leading BOM stripped. std::nullopt on an
// open/read failure — the caller decides whether that is fatal.
std::optional<std::string> ReadText(const std::filesystem::path& path);

// ── INI adapter (SimpleIni) ───────────────────────────────────────────────────
// CSimpleIniA is non-copyable / non-movable, so docs hold it by shared_ptr.

struct IniDoc {
    Source                       source;
    std::shared_ptr<CSimpleIniA> ini;
};

using IniBatch = Batch<IniDoc>;

// Discover (defaults to *.ini) + load each via SimpleIni in Unicode mode (which
// handles the BOM). A load failure is recorded per-file in .errors rather than
// thrown.
IniBatch LoadIni(const ScanSpec& spec);

// ── JSON adapter (nlohmann/json) — the BDI-compatible path ────────────────────
// JSON with // and /* */ comments is accepted (JSONC, common in mod configs).

struct JsonDoc {
    Source         source;
    nlohmann::json json;
};

using JsonBatch = Batch<JsonDoc>;

// Discover (defaults to *.json when spec.extensions is empty) + read + parse each
// file. Comments are ignored; a read/parse failure is recorded per-file in
// .errors rather than thrown.
JsonBatch LoadJson(const ScanSpec& spec);

// ── YAML adapter (rapidyaml) ──────────────────────────────────────────────────

struct YamlDoc {
    Source        source;
    c4::yml::Tree tree;   // parsed into ryml's own arena — self-contained + movable
};

using YamlBatch = Batch<YamlDoc>;

// Discover (defaults to *.yaml / *.yml) + read + parse each file. A read/parse
// failure is recorded per-file in .errors rather than thrown.
YamlBatch LoadYaml(const ScanSpec& spec);

}  // namespace sct::config
