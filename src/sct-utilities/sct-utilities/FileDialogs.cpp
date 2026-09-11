#include <sct-utilities/SctUtilities.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#endif

namespace sct::ui {

#if defined(_WIN32)

namespace {

std::wstring Widen(const char* utf8)
{
    if (!utf8 || !*utf8) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w.data(), n);
    return w;
}

std::string Narrow(const wchar_t* wide)
{
    if (!wide || !*wide) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// COM is normally already initialised as STA by GLFW's Win32 backend. Calling
// again is fine and returns S_FALSE; the rule is that every call returning
// S_OK or S_FALSE must be balanced by CoUninitialize, and RPC_E_CHANGED_MODE
// (someone already picked MTA) must not be. This RAII wrapper encodes that —
// getting it wrong unbalances the process-wide COM refcount.
class ScopedCom {
public:
    ScopedCom()
    {
        const HRESULT hr = CoInitializeEx(nullptr,
                                          COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_owns   = SUCCEEDED(hr);           // S_OK or S_FALSE
        m_usable = m_owns || hr == RPC_E_CHANGED_MODE;
    }
    ~ScopedCom() { if (m_owns) CoUninitialize(); }

    ScopedCom(const ScopedCom&)            = delete;
    ScopedCom& operator=(const ScopedCom&) = delete;

    bool Usable() const { return m_usable; }

private:
    bool m_owns   = false;
    bool m_usable = false;
};

}  // namespace

bool PickFolder(const char* title, const char* initialDir, std::string& outPath)
{
    ScopedCom com;
    if (!com.Usable()) return false;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return false;

    bool picked = false;

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                           FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);

    if (title && *title) {
        const std::wstring wTitle = Widen(title);
        dialog->SetTitle(wTitle.c_str());
    }

    // Seed the starting location. SetFolder (not SetDefaultFolder) so re-opening
    // the dialog returns to the previously chosen path rather than the shell
    // default. Failure here is cosmetic — the dialog still opens.
    if (initialDir && *initialDir) {
        const std::wstring wDir = Widen(initialDir);
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item))) && item) {
            dialog->SetFolder(item);
            item->Release();
        }
    }

    // Parent to the active window so the dialog is modal to the app; passing null
    // would let the user keep clicking the ImGui frame behind it.
    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &wpath)) && wpath) {
                outPath = Narrow(wpath);
                picked  = !outPath.empty();
                CoTaskMemFree(wpath);
            }
            result->Release();
        }
    }

    dialog->Release();
    return picked;
}

bool PickFile(const char* title, const char* initialDir, const char* filterDesc,
              const char* filterExt, std::string& outPath)
{
    ScopedCom com;
    if (!com.Usable()) return false;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return false;

    bool picked = false;

    // A real file pick: FORCEFILESYSTEM + FILEMUSTEXIST, and NO FOS_PICKFOLDERS (the one bit that
    // separates this from PickFolder).
    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options)))
        dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                           FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);

    // The caller's type filter, plus an always-present "All files" catch-all. The wide strings must
    // outlive SetFileTypes (it copies on Show, but keep them in scope until the call returns).
    const std::wstring wDesc = Widen((filterDesc && *filterDesc) ? filterDesc : "Files");
    const std::wstring wExt  = Widen((filterExt && *filterExt) ? filterExt : "*.*");
    const COMDLG_FILTERSPEC specs[] = {
        { wDesc.c_str(), wExt.c_str() },
        { L"All files",  L"*.*" },
    };
    dialog->SetFileTypes(2, specs);

    if (title && *title) {
        const std::wstring wTitle = Widen(title);
        dialog->SetTitle(wTitle.c_str());
    }

    // Seed the starting location (SetFolder, as in PickFolder — return to the last chosen path).
    if (initialDir && *initialDir) {
        const std::wstring wDir = Widen(initialDir);
        IShellItem* item = nullptr;
        if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), nullptr,
                                                  IID_PPV_ARGS(&item))) && item) {
            dialog->SetFolder(item);
            item->Release();
        }
    }

    if (SUCCEEDED(dialog->Show(GetActiveWindow()))) {
        IShellItem* result = nullptr;
        if (SUCCEEDED(dialog->GetResult(&result)) && result) {
            PWSTR wpath = nullptr;
            if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &wpath)) && wpath) {
                outPath = Narrow(wpath);
                picked  = !outPath.empty();
                CoTaskMemFree(wpath);
            }
            result->Release();
        }
    }

    dialog->Release();
    return picked;
}

#else  // !_WIN32

bool PickFolder(const char*, const char*, std::string&) { return false; }
bool PickFile(const char*, const char*, const char*, const char*, std::string&) { return false; }

#endif

}  // namespace sct::ui
