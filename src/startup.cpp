#include "startup.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <winrt/base.h>

namespace {

std::wstring ShortcutPath() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Startup, KF_FLAG_DEFAULT, nullptr, &folder)))
        return {};

    std::wstring path(folder);
    CoTaskMemFree(folder);
    if (path.empty()) return {};
    return path + L"\\MiniBar.lnk";
}

}  // namespace

namespace startup {

bool Enabled() {
    const std::wstring lnk = ShortcutPath();
    return !lnk.empty() && PathFileExistsW(lnk.c_str());
}

bool Enable() {
    const std::wstring lnk = ShortcutPath();
    if (lnk.empty()) return false;

    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return false;

    winrt::com_ptr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(link.put()))))
        return false;

    link->SetPath(exe);
    link->SetDescription(L"MiniBar");

    // La cartella di lavoro è quella dell'eseguibile: l'INI vive lì accanto, e
    // senza questo un avvio dalla shell lo cercherebbe altrove.
    std::wstring dir = ExeDir();
    if (!dir.empty()) dir.pop_back();  // via il backslash finale
    if (!dir.empty()) link->SetWorkingDirectory(dir.c_str());

    auto file = link.try_as<IPersistFile>();
    return file && SUCCEEDED(file->Save(lnk.c_str(), TRUE));
}

bool Disable() {
    const std::wstring lnk = ShortcutPath();
    if (lnk.empty()) return false;
    if (DeleteFileW(lnk.c_str())) return true;
    return GetLastError() == ERROR_FILE_NOT_FOUND;
}

}  // namespace startup
