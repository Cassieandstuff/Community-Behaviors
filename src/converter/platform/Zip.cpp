#include "platform/Platform.h"

#include <common/miniz.h>
#include <disk/Disk.h>

#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace CB::converter::platform {

namespace {

/// @brief `p` as miniz opens it: UTF-8 (miniz widens it for _wfopen on Windows), long-path prefixed when needed.
std::string MzPath(const fs::path& p)
{
    const std::u8string u = CB::engine::disk::LongPath(p).u8string();
    return std::string(u.begin(), u.end());
}

}  // namespace

bool UnzipDir(const std::string& zipPath, const std::string& destDir, std::string& err)
{
    namespace disk = CB::engine::disk;
    std::error_code ec;
    const fs::path  zipAbs = fs::absolute(zipPath, ec);
    if (!fs::exists(disk::LongPath(zipAbs), ec)) { err = "archive not found: " + zipPath; return false; }

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, MzPath(zipAbs).c_str(), 0)) {
        err = "cannot open zip: " + zipPath;
        return false;
    }
    const auto fail = [&](std::string why) {
        mz_zip_reader_end(&zip);
        err = std::move(why);
        return false;
    };

    const fs::path root = fs::absolute(destDir, ec);
    const mz_uint  n    = mz_zip_reader_get_num_files(&zip);
    int            extracted = 0;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st)) return fail("zip stat failed");
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        const std::string name = st.m_filename;
        if (name.empty() || name.back() == '/') continue;

        const fs::path rel = fs::path(name).lexically_normal();
        if (rel.is_absolute() || rel.has_root_name() || rel.empty() || *rel.begin() == "..")
            return fail("entry outside the destination: " + name);
        const fs::path out = root / rel;
        std::error_code de;
        fs::create_directories(disk::LongPath(out.parent_path()), de);
        if (!mz_zip_reader_extract_to_file(&zip, i, MzPath(out).c_str(), 0)) return fail("extract failed: " + name);
        ++extracted;
    }
    mz_zip_reader_end(&zip);
    if (extracted == 0) { err = "archive has no files: " + zipPath; return false; }
    return true;
}

}  // namespace CB::converter::platform
