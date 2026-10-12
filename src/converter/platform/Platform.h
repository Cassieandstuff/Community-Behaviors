#pragma once
/// @brief The converter's desktop plumbing: native folder dialogs, Windows known-folder lookups, and a zip extractor.
///        (Packing a directory is codec::HkyArchive::PackDirectory — the one packer.)
/// @note The whole surface of platform/ — the .cpp files keep their impl details (miniz, COM) private.
#include <string>

namespace CB::converter::platform {

// Shows a native folder-selection dialog (Vista+ IFileDialog with
// FOS_PICKFOLDERS — resizable, path-typeable, real folder structure; the legacy
// SHBrowseForFolder / GetOpenFileName are both wrong for picking a directory).
// `initialDir` may be null or empty. Returns false if the user cancelled or the
// dialog could not be shown, leaving outPath untouched. Paths come back UTF-8.
bool PickFolder(const char* title, const char* initialDir, std::string& outPath);

}  // namespace CB::converter::platform

namespace CB::converter::platform {

// The current user's Downloads folder (FOLDERID_Downloads), UTF-8. Empty on failure.
// A convenient default target for "package to" flows; the caller may let the user
// override it (e.g. point it at their mod manager's downloads directory).
std::string DownloadsFolder();

/// @brief The current user's Documents folder (FOLDERID_Documents), UTF-8; empty on failure.
/// @note Follows OneDrive/folder redirection, unlike %USERPROFILE%\Documents — this is where Skyrim's
///   "My Games" INIs (and thus the archive lists) actually live.
std::string DocumentsFolder();

/// @brief Extract every file entry of `zipPath` into `destDir` at its archive path (case kept), streaming one entry at
///        a time; creates the directories.
/// @return false with `err` set on any failure — missing/corrupt archive, an entry naming a path outside `destDir`,
///         or an I/O error.
/// @note Paths past MAX_PATH are handled, but other tools may not open them: unpack to a SHORT dir for editing.
bool UnzipDir(const std::string& zipPath, const std::string& destDir, std::string& err);

}  // namespace CB::converter::platform
