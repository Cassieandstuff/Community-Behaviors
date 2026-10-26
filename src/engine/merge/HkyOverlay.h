#pragma once
#include <merge/LoadOrderDocument.h>

#include <vfs/Vfs.h>

#include <memory>

/// @file
/// @brief The .hky load order streamed over the Data folder: a VFS overlay that shows every bundle's content at the
///        game path it stands for, so a tool browses and reads it as part of Data.

namespace CB::engine::merge {

/// @brief A LoadOrderDocument as a vfs::Overlay: each unit at its game path (`<path>.hkx`, read as its packfile,
///        GraphBytes), each other file a bundle carries at its own path, read as stored (an animation reads as its
///        YAML: CB compiles it in game).
/// @note  Two scopes split one document so VFS ranks and names them apart: Scope::Project holds what the project
///        bundle (LoadOrderDocument::SetProject) wins, Scope::LoadOrder what any other bundle provides. Set the
///        project overlay above the load order's. Both read the document's winner: the load order's overlay is only
///        read where the project supplies nothing.
/// @note  Not carried: a bundle's root files (manifest.json) and its animationnames/ roster drops — bundle metadata,
///        not Data paths.
/// @warning The document must not change while a Read runs; refresh the overlays (vfs::FileSystem::RefreshOverlay)
///          after it changes.
class HkyOverlay final : public vfs::Overlay {
public:
    enum class Scope { LoadOrder, Project };

    HkyOverlay(std::shared_ptr<const LoadOrderDocument> doc, Scope scope) : m_doc(std::move(doc)), m_scope(scope) {}

    std::vector<std::string>                 Paths() const override;
    std::optional<std::vector<std::uint8_t>> Read(std::string_view path, std::string* err) const override;

private:
    std::shared_ptr<const LoadOrderDocument> m_doc;
    Scope                                    m_scope;
};

}  // namespace CB::engine::merge
