#include <sct-utilities/SctUtilities.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath, FOLDERID_Downloads
#endif

namespace sct::util {

#if defined(_WIN32)

std::string DownloadsFolder()
{
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &w)) && w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            out.resize(static_cast<size_t>(n - 1));
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
        }
    }
    if (w) CoTaskMemFree(w);
    return out;
}

#else  // !_WIN32

std::string DownloadsFolder() { return {}; }

#endif

}  // namespace sct::util
