#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CB::core::anim {

/// @brief A character's animation roster RECIPE (`animations.yaml`): the roster is BUILT by scanning asset folders,
///        never stored, so a mod adds animations by shipping the files — no roster edits.
/// @note  Paths are relative to the character's PROJECT folder (the folder above `characters[ x]\`), backslashed,
///        folders end in `\`. `sources` are scanned recursively. An `ignore` entry (folder, or a single file) is
///        RANKED LAST, never dropped: its file fills a slot only when no other source has that filename. That keeps
///        characters sharing a graph slot-aligned by construction (defaultmale ignores `Animations\female\`,
///        defaultfemale `Animations\male\`: both get every filename, each its own variant).
struct AnimationScan {
    std::vector<std::string> sources;
    std::vector<std::string> ignore;
};

std::string                  EmitAnimationScanYaml(const AnimationScan& scan);
std::optional<AnimationScan> ParseAnimationScanYaml(std::string_view text, std::string& err);

/// @brief Every animation file the load order holds: lowercase '/' data path ("meshes/actors/character/animations/
///        x.hkx") -> that path in its ORIGINAL case (Havok is case-sensitive; the roster keeps the file's own case).
using AnimationIndex = std::map<std::string, std::string>;

/// @brief The engine's roster order: by file STEM, case-insensitive, folder ignored, '_' before digits and letters
///        (vanilla rosters are exactly this sort — the HBT project enumeration).
std::string AnimationSortKey(std::string_view rosterPath);

/// @brief True for an animation replacer's tree (OpenAnimationReplacer / DynamicAnimationReplacer): replacers reuse the
///        original filenames by design, so they are never roster sources.
bool IsReplacerPath(std::string_view path);

/// @brief Build a roster from `scan`. `projectDir` is the character's project folder as a lowercase '/' data path
///        ("meshes/actors/character"). One entry per FILENAME (the engine's slot key): a non-ignored file wins over
///        an ignored one; two non-ignored files with one filename keep the first (source order, then path) and are
///        reported in `collisions` (a recipe needs a file-level `ignore` for the other).
std::vector<std::string> ExpandAnimationScan(const AnimationScan& scan, const std::string& projectDir,
                                             const AnimationIndex& index, std::vector<std::string>* collisions = nullptr);

/// @brief Add every animation file among `originalPaths` (a bundle's file list, original case) to `index`: a path
///        ending in `.hkx` that is not inside a unit folder (`<x>.hkx/...`).
void IndexAnimationFiles(AnimationIndex& index, const std::vector<std::string>& originalPaths);

/// @brief The recipe that rebuilds `roster` (a character's vanilla list, project-relative) from `index`: each roster
///        folder climbs to its highest ancestor whose every animation folder is either a roster folder or adds no
///        filename the roster folders lack (those become `ignore`, ranked last — e.g. `Animations\female\` for
///        defaultmale). Two sourced files sharing a filename keep the one `roster` lists; the other is ignored.
/// @note  The scan also takes the unlisted files in those folders (vanilla leftovers no clip references) and drops the
///        roster's file-less entries — the roster IS the folders now.
AnimationScan DeriveAnimationScan(const std::vector<std::string>& roster, const std::string& projectDir,
                                  const AnimationIndex& index);

/// @brief `rel` ("..\SharedKillMoves\X\") resolved against `projectDir`: a lowercase '/' data path, ".." collapsed.
std::string ResolveProjectPath(const std::string& projectDir, std::string_view rel);

}  // namespace CB::core::anim
