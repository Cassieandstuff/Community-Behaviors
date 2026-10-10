#include "platform/Platform.h"

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>   // SHGetKnownFolderPath, FOLDERID_Downloads
#endif

namespace CB::converter::platform {

#if defined(_WIN32)

namespace {
std::string KnownFolderUtf8(const KNOWNFOLDERID& id)
{
    PWSTR w = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &w)) && w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            out.resize(static_cast<size_t>(n - 1));
            WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), n, nullptr, nullptr);
        }
    }
    if (w) CoTaskMemFree(w);
    return out;
}
}  // namespace

std::string DownloadsFolder() { return KnownFolderUtf8(FOLDERID_Downloads); }
std::string DocumentsFolder() { return KnownFolderUtf8(FOLDERID_Documents); }

#else  // !_WIN32

std::string DownloadsFolder() { return {}; }
std::string DocumentsFolder() { return {}; }

#endif

}  // namespace CB::converter::platform
