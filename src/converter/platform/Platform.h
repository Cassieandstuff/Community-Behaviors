#pragma once
/// @brief The converter's desktop plumbing: native folder/file dialogs, Windows known-folder lookups, and a
///        directory<->zip writer.
/// @note The whole surface of platform/ — the .cpp files keep their impl details (miniz, COM) private.
#include <string>

namespace CB::converter::platform {

// Shows a native folder-selection dialog (Vista+ IFileDialog with
// FOS_PICKFOLDERS — resizable, path-typeable, real folder structure; the legacy
// SHBrowseForFolder / GetOpenFileName are both wrong for picking a directory).
// `initialDir` may be null or empty. Returns false if the user cancelled or the
// dialog could not be shown, leaving outPath untouched. Paths come back UTF-8.
bool PickFolder(const char* title, const char* initialDir, std::string& outPath);

// Shows a native file-open dialog (the same Vista+ IFileDialog as PickFolder, WITHOUT
// FOS_PICKFOLDERS) restricted to a single file-type filter. `filterDesc` is the human label
// (e.g. "Havok/Text") and `filterExt` its pattern list (e.g. "*.hkx;*.txt"); an "All files"
// (*.*) entry is always appended. `initialDir` may be null or empty. Returns false if the user
// cancelled or the dialog could not be shown, leaving outPath untouched. Paths come back UTF-8.
bool PickFile(const char* title, const char* initialDir, const char* filterDesc,
              const char* filterExt, std::string& outPath);

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

// Zip every regular file under `srcDir` into `zipPath` (archive paths are relative
// to srcDir, forward-slash). Overwrites any existing archive at zipPath. Returns
// false with `err` set on failure (missing source, empty tree, or I/O error).
bool ZipDir(const std::string& srcDir, const std::string& zipPath, std::string& err);

// Extract every file entry of `zipPath` into `destDir`, preserving the archive's
// relative paths (forward-slash -> native). Creates destDir + intermediate dirs.
// Returns false with `err` set on failure (missing/corrupt archive, I/O error).
// NOTE: extracts to real files, so `destDir` must be short enough that the deepest
// entry stays under the OS path limit — unpack to a SHORT working dir for editing.
bool UnzipDir(const std::string& zipPath, const std::string& destDir, std::string& err);

}  // namespace CB::converter::platform
