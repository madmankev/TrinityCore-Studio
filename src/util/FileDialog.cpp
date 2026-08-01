// Native Win32 open/save file dialogs. See FileDialog.h.

#include "util/FileDialog.h"

#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

#include <vector>

namespace we
{
namespace
{
std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const wchar_t* w)
{
    if (!w || !*w)
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Build the Win32 double-null-terminated filter string.
std::wstring MakeFilter(const std::string& name, const std::string& pattern)
{
    std::wstring wname = Widen(name);
    std::wstring wpat = Widen(pattern);
    std::wstring f;
    f += wname + L" (" + wpat + L")";
    f.push_back(L'\0');
    f += wpat;
    f.push_back(L'\0');
    f += L"All files (*.*)";
    f.push_back(L'\0');
    f += L"*.*";
    f.push_back(L'\0');
    f.push_back(L'\0');
    return f;
}

// Extract a bare extension (e.g. "sql") from a pattern like "*.sql".
std::wstring DefExt(const std::string& pattern)
{
    std::wstring wpat = Widen(pattern);
    size_t dot = wpat.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return {};
    return wpat.substr(dot + 1);
}
} // namespace

std::string OpenFileDialog(const std::string& title, const std::string& filterName,
                           const std::string& filterPattern)
{
    std::wstring filter = MakeFilter(filterName, filterPattern);
    std::wstring wtitle = Widen(title);
    std::vector<wchar_t> file(32768, L'\0');

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn))
        return {};
    return Narrow(file.data());
}

std::string SaveFileDialog(const std::string& title, const std::string& filterName,
                           const std::string& filterPattern, const std::string& defaultName)
{
    std::wstring filter = MakeFilter(filterName, filterPattern);
    std::wstring wtitle = Widen(title);
    std::wstring wext = DefExt(filterPattern);
    std::vector<wchar_t> file(32768, L'\0');

    std::wstring wdef = Widen(defaultName);
    if (!wdef.empty() && wdef.size() < file.size())
        std::copy(wdef.begin(), wdef.end(), file.begin());

    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file.data();
    ofn.nMaxFile = static_cast<DWORD>(file.size());
    ofn.lpstrTitle = wtitle.empty() ? nullptr : wtitle.c_str();
    ofn.lpstrDefExt = wext.empty() ? nullptr : wext.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetSaveFileNameW(&ofn))
        return {};
    return Narrow(file.data());
}

std::string PickFolderDialog(const std::string& title)
{
    std::string result;
    const bool didInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));

    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg))))
    {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        std::wstring wtitle = Widen(title);
        if (!wtitle.empty())
            dlg->SetTitle(wtitle.c_str());
        if (SUCCEEDED(dlg->Show(GetActiveWindow())))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)))
            {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                {
                    result = Narrow(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dlg->Release();
    }

    if (didInit)
        CoUninitialize();
    return result;
}
} // namespace we
