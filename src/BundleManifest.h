#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace CB {

    // Metadata sidecar for one <Mod>.hky bundle:
    //   Data\community_behaviors\plugins\<Mod>.hky\manifest.json
    //
    // The behavior-domain analog of a TES4 plugin header (cf. sctesm::Plugin) —
    // identity + version for display, and the DECLARED master list that turns a
    // node-key collision from "guess" into "checkable": a key that exists in a
    // declared master is an OVERRIDE (load order decides the winner, expected);
    // a key two non-mastering bundles both introduce is a NAMESPACE CLASH (a bug).
    //
    // Purely additive to the runtime. A bundle with no manifest.json (or a broken
    // one) yields a defaulted record (name = bundle stem, no masters) and loads
    // and merges EXACTLY as before — the metadata layer never gates the serve path.
    //
    // Format is JSON (JSONC tolerated on read — nlohmann ignore_comments): read by
    // both BR's C++ runtime here and the MO2 manager plugin (Python stdlib `json`),
    // so it stays plain-JSON-writable with no parser dependency on either side.
    struct BundleManifest {
        std::string              name;            // identity/display; defaults to the bundle stem
        std::string              version;         // author-set, free-form (e.g. "1.4.2"); shown in MO2
        std::string              author;
        std::string              description;
        std::vector<std::string> masters;         // declared deps (bundle stems), file order; Skyrim implicit
        bool                     light   = false; // ESL-analog; reserved for future FormID compaction
        bool                     present = false; // a manifest.json actually existed AND parsed

        // The editor <-> compiler contract stamp (see Havok/SCHEMA.yaml + the schema-version-stamp
        // scheme). Whoever produced this bundle (BR's own compiler, or a third-party editor) copies
        // these in; the compiler gates ingest on schemaVersion.
        std::string              schemaVersion;   // "schema_version": the Havok/ contract this bundle
                                                  // was authored against (empty = legacy/unstamped)
        std::vector<std::string> authoredBy;      // "authored_by": producer CHAIN, origin first,
                                                  // last-toucher last. DIAGNOSTIC-ONLY — never gate on it.

        // Load <bundleDir>\manifest.json. NEVER throws: on a missing file returns a
        // silent defaulted record (name = bundleStem, present = false); on an
        // unreadable/invalid file returns the same default and appends one line to
        // `warnings` (the caller logs them). bundleStem is the load-order handle
        // (bundle filename without .hky, e.g. "BFCO"), used as the default name.
        static BundleManifest Load(const std::filesystem::path& bundleDir,
                                   const std::string&            bundleStem,
                                   std::vector<std::string>&     warnings);

        // Parse manifest JSON from raw text (the shared core of Load). std::nullopt or empty
        // text = no manifest -> defaulted record (name = bundleStem, present = false), never a
        // warning. Bad/non-object JSON appends one warning and returns the default. Used both by
        // Load (disk) and by BR's PACKED-.hky path, which reads the "manifest.json" entry out of
        // the HkyArchive — a packed bundle is a file, not a directory, so the disk Load can't see
        // its manifest and a packed master would silently declare nothing without this.
        static BundleManifest Parse(const std::optional<std::string>& jsonText,
                                    const std::string&                bundleStem,
                                    std::vector<std::string>&         warnings);
    };

}  // namespace CB
