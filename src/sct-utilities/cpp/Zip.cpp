#include <sct-utilities/SctUtilities.h>

#include "miniz.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs = std::filesystem;

namespace sct::util {

bool ZipDir(const std::string& srcDir, const std::string& zipPath, std::string& err)
{
    std::error_code ec;
    if (!fs::is_directory(srcDir, ec)) { err = "source folder not found: " + srcDir; return false; }

    // Land the archive OUTSIDE the tree we're zipping (else we'd try to add the .zip to
    // itself). Make its parent, and remember its canonical path so the walk skips it even
    // if the caller points it inside srcDir.
    fs::create_directories(fs::path(zipPath).parent_path(), ec);
    fs::remove(zipPath, ec);   // never append to a stale archive
    const fs::path zipCanon = fs::weakly_canonical(zipPath, ec);

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, zipPath.c_str(), 0)) {
        err = "cannot create zip: " + zipPath;
        return false;
    }

    // Manual DFS instead of recursive_directory_iterator: a single unreadable entry only
    // skips that subtree — it can NEVER terminate the whole walk and silently truncate the
    // archive (the old shared-error_code loop did exactly that, dropping base/). Every file
    // reachable under srcDir is added; per-entry failures are counted, not fatal.
    const fs::path root(srcDir);
    int added = 0, skipped = 0;
    std::vector<fs::path> stack{ root };
    while (!stack.empty()) {
        const fs::path dir = std::move(stack.back());
        stack.pop_back();
        std::error_code de;
        for (fs::directory_iterator di(dir, fs::directory_options::skip_permission_denied, de), dEnd;
             di != dEnd; di.increment(de)) {
            if (de) { de.clear(); break; }   // this dir went bad — skip the rest of it, keep the walk
            std::error_code fe;
            if (di->is_directory(fe) && !fe) { stack.push_back(di->path()); continue; }
            if (!di->is_regular_file(fe) || fe) continue;
            if (!zipCanon.empty() && fs::weakly_canonical(di->path(), fe) == zipCanon) continue;  // never self-add

            const std::string arc = fs::relative(di->path(), root, fe).generic_string();
            if (fe || arc.empty()) { ++skipped; continue; }
            std::ifstream f(di->path(), std::ios::binary);
            if (!f) { ++skipped; continue; }
            const std::vector<char> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (!mz_zip_writer_add_mem(&zip, arc.c_str(), buf.data(), buf.size(),
                                       static_cast<mz_uint>(MZ_BEST_COMPRESSION))) {
                mz_zip_writer_end(&zip);
                err = "failed adding to zip: " + arc;
                return false;
            }
            ++added;
        }
    }
    if (!mz_zip_writer_finalize_archive(&zip)) { mz_zip_writer_end(&zip); err = "zip finalize failed"; return false; }
    mz_zip_writer_end(&zip);
    if (added == 0) { err = "nothing to zip (run Convert first)"; return false; }
    if (skipped > 0) err = "warning: " + std::to_string(skipped) + " unreadable file(s) skipped";  // non-fatal
    return true;
}

bool UnzipDir(const std::string& zipPath, const std::string& destDir, std::string& err)
{
    std::error_code ec;
    if (!fs::exists(zipPath, ec)) { err = "archive not found: " + zipPath; return false; }

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, zipPath.c_str(), 0)) {
        err = "cannot open zip: " + zipPath;
        return false;
    }

    fs::create_directories(destDir, ec);
    const fs::path root(destDir);
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    int extracted = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) { mz_zip_reader_end(&zip); err = "zip stat failed"; return false; }
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        const std::string name = st.m_filename;
        if (name.empty() || name.back() == '/') continue;

        const fs::path out = root / fs::path(name);
        fs::create_directories(out.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&zip, i, out.string().c_str(), 0)) {
            mz_zip_reader_end(&zip);
            err = "extract failed (path too long?): " + name;
            return false;
        }
        ++extracted;
    }
    mz_zip_reader_end(&zip);
    if (extracted == 0) { err = "archive has no files: " + zipPath; return false; }
    return true;
}

}  // namespace sct::util
