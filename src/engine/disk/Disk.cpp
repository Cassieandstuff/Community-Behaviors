#include <disk/Disk.h>

#include <text/Text.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace CB::engine::disk {

namespace fs = std::filesystem;

fs::path LongPath(const fs::path& p)
{
#ifdef _WIN32
    /// @note 248, not MAX_PATH (260): a DIRECTORY is limited sooner — CreateDirectoryW caps it at 248, and a directory
    ///       iterator searches `<dir>\*`.
    const std::wstring w = p.wstring();
    if (w.size() < 248 || !p.is_absolute() || w.rfind(L"\\\\?\\", 0) == 0) return p;
    if (w.rfind(L"\\\\", 0) == 0) return fs::path(L"\\\\?\\UNC\\" + w.substr(2));
    return fs::path(L"\\\\?\\" + p.lexically_normal().make_preferred().wstring());
#else
    return p;
#endif
}

namespace {

/// @brief Read the whole stream into `out` (a byte container): one sized read when the size is known, else streamed.
template <class Bytes>
bool readAll(std::ifstream& f, Bytes& out)
{
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size >= 0) {
        f.seekg(0, std::ios::beg);
        out.resize(static_cast<std::size_t>(size));
        if (size > 0) f.read(reinterpret_cast<char*>(out.data()), size);
        return static_cast<bool>(f) && f.gcount() == size;
    }
    f.clear();
    f.seekg(0, std::ios::beg);
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return !f.bad();
}

}  // namespace

std::optional<std::string> ReadFile(const fs::path& p, ReadOptions opt)
{
    std::ifstream f(LongPath(p), std::ios::binary);
    if (!f) return std::nullopt;
    std::string s;
    if (!readAll(f, s)) return std::nullopt;
    if (opt.stripBom) {
        const std::string_view v = text::StripBom(s);
        if (v.size() != s.size()) s.erase(0, s.size() - v.size());
    }
    return s;
}

std::optional<std::vector<std::uint8_t>> ReadBytes(const fs::path& p)
{
    std::ifstream f(LongPath(p), std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<std::uint8_t> b;
    if (!readAll(f, b)) return std::nullopt;
    return b;
}

std::optional<std::string> ReadHead(const fs::path& p, std::size_t maxBytes)
{
    std::ifstream f(LongPath(p), std::ios::binary);
    if (!f) return std::nullopt;
    std::string s(maxBytes, '\0');
    if (maxBytes > 0) f.read(s.data(), static_cast<std::streamsize>(maxBytes));
    if (f.bad()) return std::nullopt;
    s.resize(static_cast<std::size_t>(f.gcount()));
    return s;
}

namespace {

bool fail(std::string* err, std::string msg)
{
    if (err) *err = std::move(msg);
    return false;
}

}  // namespace

bool WriteFile(const fs::path& p, std::string_view data, WriteOptions opt, std::string* err)
{
    std::error_code ec;
    if (opt.createDirs && p.has_parent_path()) fs::create_directories(LongPath(p.parent_path()), ec);
    const fs::path target = opt.atomic ? fs::path(p.native() + fs::path::string_type(fs::path(".tmp").native())) : p;
    {
        std::ofstream f(LongPath(target), std::ios::binary | std::ios::trunc);
        if (!f) return fail(err, "cannot open '" + target.string() + "' for writing");
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) return fail(err, "write failed for '" + target.string() + "'");
        f.close();
        if (f.fail()) return fail(err, "close failed for '" + target.string() + "'");
    }
    if (opt.atomic) {
        fs::rename(LongPath(target), LongPath(p), ec);
        if (ec) {
            std::error_code rm;
            fs::remove(LongPath(target), rm);
            return fail(err, "cannot replace '" + p.string() + "': " + ec.message());
        }
    }
    return true;
}

bool WriteFile(const fs::path& p, std::span<const std::uint8_t> data, WriteOptions opt, std::string* err)
{
    return WriteFile(p, std::string_view(reinterpret_cast<const char*>(data.data()), data.size()), opt, err);
}

}  // namespace CB::engine::disk
