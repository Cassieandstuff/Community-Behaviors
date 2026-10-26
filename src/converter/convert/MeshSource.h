#pragma once

#include <codec/bsa/BsaArchive.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CB::converter {

/// @brief The files a game Data folder's `meshes\` resolves to — loose files and BSA entries — read without unpacking.
/// @note Precedence is the engine's: a loose file beats any archive entry, and a later-added archive beats an earlier one.
///       Paths are matched by path identity; each file keeps the spelling of the source that won it (an archive stores
///       its names lowercased, and that case is not recoverable).
class MeshSource {
public:
    /// @brief A regen source folder: its loose `meshes\` tree (when present) plus every `*.bsa` beside it — the base game's
    ///        `Skyrim - *.bsa` first, then the rest, each group in name order (the folder has no plugin list to order
    ///        them by). A folder that is itself named `meshes` is a loose tree alone.
    /// @return nullopt with `err` set when the folder holds neither, or an archive can't be read.
    static std::optional<MeshSource> OpenRegenFolder(const std::filesystem::path& folder, std::string& err);

    /// @brief Add every file under `dataRoot/meshes/`.
    void AddLoose(const std::filesystem::path& dataRoot);
    /// @brief Add every `meshes\` entry of the archive at `path`, over the archives added before it.
    /// @return false with `err` set when the archive can't be read.
    bool AddArchive(const std::filesystem::path& path, std::string& err);

    /// @brief Every file, as "meshes/…" with forward slashes, sorted by path identity.
    const std::vector<std::string>& Files() const;
    /// @brief The whole file at `rel` ("meshes/…", any spelling).
    /// @note Safe to call from several threads at once.
    std::optional<std::vector<std::uint8_t>> Read(const std::string& rel, std::string* err = nullptr) const;
    /// @brief `Read` as text (no transformation), nullopt when absent.
    std::optional<std::string> ReadText(const std::string& rel) const;
    bool Has(const std::string& rel) const;

    /// @brief "<n> files: loose <dir> + <k> archive(s)" plus how many paths a later source overrode.
    std::string Describe() const;

private:
    struct Origin {
        std::string                          rel;           ///< "meshes/…", the winning source's spelling
        std::filesystem::path                loose;         ///< set for a loose file
        std::size_t                          archive = 0;   ///< index into m_archives otherwise
        const CB::engine::codec::bsa::Entry* entry = nullptr;
    };

    std::map<std::string, Origin>                                   m_byKey;   ///< path identity -> origin
    std::vector<std::unique_ptr<CB::engine::codec::bsa::Archive>>   m_archives;
    std::vector<std::filesystem::path>                              m_archivePaths;
    std::vector<std::filesystem::path>                              m_looseRoots;
    std::size_t                                                     m_overridden = 0;
    mutable std::vector<std::string>                                m_files;   ///< Files(), rebuilt after an add
    mutable bool                                                    m_filesStale = true;
};

}  // namespace CB::converter
