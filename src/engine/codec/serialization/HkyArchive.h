#pragma once

#include <interface/UnitSource.h>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CB::engine::disk { class ReadOnlyFile; }

namespace CB::engine::codec {

using CB::engine::common::IUnitSource;   // the unit backing-store interface lives in the interface layer

/// @brief A .hky (a zip of the bundle's YAML tree) opened as an INDEX: loading reads only the zip's central directory;
///        each file is read and inflated when asked for. Keys are path keys (codec::resource::PathKey), so lookups
///        match the loader's unit-relative reads and the Resolver's serve keys.
/// @note  Streaming, not a one-time unpack: memory holds the index (the master's 76k entries are a few MB), never the
///        6 GB the YAML inflates to. The file stays open for the archive's lifetime; reads from any number of threads
///        are safe, and each inflate runs on the thread that asked for the file.
class HkyArchive : public std::enable_shared_from_this<HkyArchive> {
public:
    enum class UnitKind { Behavior, Character, Project, Skeleton };
    struct Unit {
        std::string prefix;   // serve key, e.g. "meshes/actors/character/behaviors/0_master.hkx"
        UnitKind    kind;
    };

    /// @brief Open a .hky: read its index and classify its units (each unit's graph.yaml is read for that); nullptr
    ///        with `err` set on failure.
    static std::shared_ptr<HkyArchive> LoadFromFile(const std::string& hkyPath, std::string& err);
    ~HkyArchive();

    /// @brief Pack every regular file under `dir` into `outHkyPath` (a deflate zip), each entry named by its path
    ///        relative to `dir`, forward-slashed and case kept — the layout LoadFromFile reads back. THE .hky / zip
    ///        packer: hky-tool, the master regen, every converter bundle and the GUI's packaged zip.
    /// @return false with `err` set on ANY failure — a walk, stat or read error included: an incomplete archive is
    ///         never a success.
    /// @note Entries are sorted (deterministic order). Deflate is parallel, at the default level. The archive is
    ///       written to `<outHkyPath>.partial` and renamed over `outHkyPath` once finalized; an output inside `dir`
    ///       is never added to itself. Paths past MAX_PATH are handled.
    static bool PackDirectory(const std::string& dir, const std::string& outHkyPath, std::string& err);

    /// @brief A reflective unit's kind from its graph.yaml: the root's named variant class (hkbCharacterData,
    ///        hkbProjectData, hkaAnimationContainer); anything else — or no graph.yaml — is a Behavior. (A skeleton
    ///        unit is bonelist.yaml + bones/, not reflective: see units().)
    /// @note  A mod's patch unit names its base's variant class too; pair with IsPatchGraphYaml.
    static UnitKind KindOfGraphYaml(std::string_view graphYaml);
    /// @brief True when graph.yaml is a PATCH's: its root `id:` is a FormID owned by a master (`0001x…`), not by the
    ///        unit itself (`0000x…`). A patch unit is incomplete alone — it only overlays its master's unit.
    static bool IsPatchGraphYaml(std::string_view graphYaml);

    // Every unit in the archive — every "<prefix>.hkx/" subtree. `prefix` doubles as the serve key.
    const std::vector<Unit>& units() const { return m_units; }

    /// @brief A read-only IUnitSource rooted at `unitPrefix` (a merge layer: ParseHkyReflective / MergeHkyPatch).
    std::shared_ptr<const IUnitSource> source(const std::string& unitPrefix) const;

    /// @brief A file's bytes by its path key, read and inflated now; nullopt when the archive has no such file.
    /// @throws std::runtime_error when the entry can't be read back intact (I/O error, bad header, inflate failure, CRC
    ///         mismatch) — a damaged file is never passed off as an absent one.
    std::optional<std::string> file(const std::string& path) const;

    // Every file path under a prefix (normalized lowercase/forward-slash), returned NORMALIZED. This is
    // what unit keying and serve keys want; most callers re-lowercase anything they extract anyway.
    std::vector<std::string> filesUnder(const std::string& prefix) const;

    // Same range, but returns the ORIGINAL-CASE (forward-slashed) paths. Use ONLY where a case-sensitive
    // NAME is derived from the path — the animationdata clip records, whose name the engine matches to the
    // mixed-case behaviour clip generator (lowercasing it silently kills that clip's root motion). PathKey()
    // never changes length, so substr offsets from the normalized prefix still line up; read-back
    // re-normalizes, so the original case is safe to hand out.
    std::vector<std::string> filesUnderOrig(const std::string& prefix) const;

private:
    HkyArchive() = default;   // built only via LoadFromFile (needs shared ownership)
    /// @brief Where one file lives in the zip, from its central-directory record.
    struct Entry {
        std::string   orig;          ///< original-case, forward-slashed path (case preserved)
        std::uint64_t headerOffset;  ///< the entry's local header; its data follows the header's name + extra fields
        std::uint64_t packedSize;
        std::uint64_t size;
        std::uint32_t crc;
        bool          deflated;      ///< deflate, else stored
    };
    std::string read(const Entry& e) const;

    std::string                         m_path;
    std::unique_ptr<disk::ReadOnlyFile> m_file;
    std::map<std::string, Entry>        m_files;   ///< path key -> entry; ordered -> deterministic
    std::vector<Unit>                   m_units;
};

} // namespace CB::engine::codec
