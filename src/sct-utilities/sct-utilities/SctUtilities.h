#pragma once
// ── sct-utilities — the public API ────────────────────────────────────────────
// Shared desktop-app plumbing for the SCT tools (Scene Editor, Behavior
// Converter, and whatever comes next): native folder/file dialogs, Windows
// known-folder lookups, and a directory<->zip writer. Consolidates the copies
// each app was re-cutting (both apps shipped a byte-identical FileDialogs; the
// converter carried its own Zip + Downloads lookup).
//
// SINGLE PUBLIC HEADER (CommonLib `Skyrim.h` model): this file is the entire
// cross-project surface — consumers write `#include <sct-utilities/SctUtilities.h>`
// and nothing else. Nothing besides this file may ever be added to
// include/internal/sct-utilities/. Impl details (miniz) stay private to the .cpp.
//
// Two namespaces by design: dialog UI lives in `sct::ui`, the non-UI helpers in
// `sct::util`.
// ------------------------------------------------------------------------------
#include <string>

namespace sct::ui {

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

}  // namespace sct::ui

namespace sct::util {

// The current user's Downloads folder (FOLDERID_Downloads), UTF-8. Empty on failure.
// A convenient default target for "package to" flows; the caller may let the user
// override it (e.g. point it at their mod manager's downloads directory).
std::string DownloadsFolder();

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

}  // namespace sct::util
