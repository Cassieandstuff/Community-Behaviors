#include "platform/Platform.h"

#include <common/miniz.h>
#include <disk/Disk.h>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <thread>

namespace fs = std::filesystem;

namespace CB::converter::platform {

bool ZipDir(const std::string& srcDir, const std::string& zipPath, std::string& err)
{
    std::error_code ec;
    if (!fs::is_directory(srcDir, ec)) { err = "source folder not found: " + srcDir; return false; }

    // Land the archive OUTSIDE the tree we're zipping (else we'd try to add the .zip to
    // itself). Make its parent, and remember its canonical path so the walk skips it even
    // if the caller points it inside srcDir.
    /// @note Written to `<zipPath>.partial` and renamed over `zipPath` only once finalized, so a failed or
    ///       interrupted pack never leaves a truncated archive (or deletes the previous good one) at `zipPath`.
    fs::create_directories(fs::path(zipPath).parent_path(), ec);
    const std::string tmpPath = zipPath + ".partial";
    fs::remove(tmpPath, ec);   // never append to a stale archive
    const fs::path zipCanon = fs::weakly_canonical(zipPath, ec);
    const fs::path tmpCanon = fs::weakly_canonical(tmpPath, ec);

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, tmpPath.c_str(), 0)) {
        err = "cannot create zip: " + tmpPath;
        return false;
    }
    const auto abandon = [&](std::string why) {
        mz_zip_writer_end(&zip);
        std::error_code re;
        fs::remove(tmpPath, re);
        err = std::move(why);
        return false;
    };

    // Manual DFS instead of recursive_directory_iterator: a single unreadable entry only
    // skips that subtree — it can NEVER terminate the whole walk and silently truncate the
    // archive (the old shared-error_code loop did exactly that, dropping base/). Every file
    // reachable under srcDir is added; per-entry failures are counted, not fatal.
    const fs::path root(srcDir);
    int added = 0, skipped = 0;
    std::vector<std::pair<fs::path, std::string>> files;   ///< (file, archive name), in walk order
    std::vector<fs::path> stack{ root };
    while (!stack.empty()) {
        const fs::path dir = std::move(stack.back());
        stack.pop_back();
        std::error_code de;
        /// @note A walk error (unreadable/denied dir, failed stat) skips the rest of that dir but COUNTS as
        ///       skipped, so callers that require a complete archive see it — never a silently short pack.
        fs::directory_iterator di(dir, de), dEnd;
        if (de) { ++skipped; continue; }
        for (; di != dEnd; di.increment(de)) {
            if (de) { ++skipped; break; }
            std::error_code fe;
            const bool isDir = di->is_directory(fe);
            if (fe) { ++skipped; continue; }
            if (isDir) { stack.push_back(di->path()); continue; }
            const bool isFile = di->is_regular_file(fe);
            if (fe) { ++skipped; continue; }
            if (!isFile) continue;
            if (!zipCanon.empty()) {                                                               // never self-add
                const fs::path canon = fs::weakly_canonical(di->path(), fe);
                if (canon == zipCanon || canon == tmpCanon) continue;
            }

            std::string arc = fs::relative(di->path(), root, fe).generic_string();
            if (fe || arc.empty()) { ++skipped; continue; }
            files.emplace_back(di->path(), std::move(arc));
        }
    }

    /// @note Deflate runs in parallel, a batch at a time (bounded memory), into raw-deflate buffers; the entries are then
    ///       written in walk order as pre-compressed data. Default level: on YAML, level 9 costs several times the CPU for
    ///       a few percent of size, and a packed .hky is never byte-compared (diff the unpacked trees).
    struct Packed {
        bool          ok = false;
        std::size_t   size = 0;
        mz_uint32     crc = 0;
        void*         deflated = nullptr;   ///< tdefl heap block (mz_free); null = store uncompressed
        std::size_t   deflatedSize = 0;
        std::string   raw;                  ///< kept only when the file is stored
    };
    const mz_uint   flags     = tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY);
    const unsigned  nThreads  = std::max(1u, std::thread::hardware_concurrency());
    constexpr std::size_t kBatch = 2048;
    std::vector<Packed> batch;
    for (std::size_t start = 0; start < files.size(); start += kBatch) {
        const std::size_t n = std::min(kBatch, files.size() - start);
        batch.assign(n, Packed{});
        std::atomic<std::size_t> next{ 0 };
        std::vector<std::thread> pool;
        for (unsigned t = 0; t < nThreads; ++t)
            pool.emplace_back([&] {
                for (std::size_t i; (i = next++) < n;) {
                    Packed& p = batch[i];
                    auto read = CB::engine::disk::ReadFile(files[start + i].first);
                    if (!read) continue;
                    std::string& buf = *read;
                    p.ok   = true;
                    p.size = buf.size();
                    p.crc  = static_cast<mz_uint32>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(buf.data()), buf.size()));
                    if (!buf.empty()) p.deflated = tdefl_compress_mem_to_heap(buf.data(), buf.size(), &p.deflatedSize, static_cast<int>(flags));
                    if (!p.deflated || p.deflatedSize >= p.size) {
                        if (p.deflated) { mz_free(p.deflated); p.deflated = nullptr; }
                        p.raw = std::move(buf);
                    }
                }
            });
        for (auto& t : pool) t.join();

        std::string failed;
        for (std::size_t i = 0; i < n; ++i) {
            Packed& p = batch[i];
            const std::string& arc = files[start + i].second;
            if (!p.ok) { ++skipped; continue; }
            const bool good = p.deflated
                ? mz_zip_writer_add_mem_ex(&zip, arc.c_str(), p.deflated, p.deflatedSize, nullptr, 0,
                                           MZ_ZIP_FLAG_COMPRESSED_DATA, p.size, p.crc)
                : mz_zip_writer_add_mem(&zip, arc.c_str(), p.raw.data(), p.raw.size(), MZ_NO_COMPRESSION);
            if (p.deflated) { mz_free(p.deflated); p.deflated = nullptr; }
            if (!good) { failed = "failed adding to zip: " + arc; break; }
            ++added;
        }
        if (!failed.empty()) {
            for (auto& p : batch) if (p.deflated) mz_free(p.deflated);
            return abandon(std::move(failed));
        }
    }
    if (added == 0) return abandon("nothing to zip (run Convert first)");
    if (!mz_zip_writer_finalize_archive(&zip)) return abandon("zip finalize failed");
    mz_zip_writer_end(&zip);
    fs::rename(tmpPath, zipPath, ec);
    if (ec) {
        std::error_code re;
        fs::remove(tmpPath, re);
        err = "cannot replace '" + zipPath + "': " + ec.message();
        return false;
    }
    if (skipped > 0) err = "warning: " + std::to_string(skipped) + " unreadable file(s)/folder(s) skipped";  // non-fatal
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

}  // namespace CB::converter::platform
