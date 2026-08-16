#ifndef GUI_WIDGETS_H
#define GUI_WIDGETS_H

#if _WIN32
// 与 main_gui.cpp 保持一致：确保 Vista+ 的 COM 接口（IFileOpenDialog 等）
// 不被 ncnn 的 Windows XP 目标版本定义隐藏。
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif
#endif

#include <string>
#include <vector>
#include <cctype>
#include "filesystem_utils.h"

#if _WIN32
#include <windows.h>
#include <shlobj.h>
#include <commdlg.h>
#include <shobjidl_core.h>

inline bool browse_folder(path_t& out_path, HWND hwnd = NULL)
{
    // 使用现代 IFileOpenDialog 选择文件夹。
    // 旧的 SHBrowseForFolderW 在“按显示器感知(Per-Monitor V2)”DPI 模式下
    // 会卡死不弹窗，所以这里改用 COM 版的文件夹选择器。
    IFileOpenDialog* pfd = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&pfd));
    if (FAILED(hr) || !pfd)
        return false;

    DWORD options = 0;
    pfd->GetOptions(&options);
    pfd->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    pfd->SetTitle(L"选择文件夹");

    hr = pfd->Show(hwnd ? hwnd : NULL);
    if (FAILED(hr))
    {
        pfd->Release();
        return false;
    }

    IShellItem* item = nullptr;
    hr = pfd->GetResult(&item);
    pfd->Release();
    if (FAILED(hr) || !item)
        return false;

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    item->Release();
    if (FAILED(hr) || !path)
        return false;

    out_path = path_t(path);
    CoTaskMemFree(path);
    return true;
}

inline bool browse_files(std::vector<path_t>& out_paths, HWND hwnd = NULL)
{
    wchar_t buf[MAX_PATH * 100] = {};

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd ? hwnd : GetActiveWindow();
    ofn.lpstrFilter = L"图片文件 (*.jpg;*.jpeg;*.png;*.webp)\0*.jpg;*.jpeg;*.png;*.webp\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = sizeof(buf) / sizeof(wchar_t);
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

    if (!GetOpenFileNameW(&ofn))
        return false;

    out_paths.clear();

    wchar_t* p = buf;
    std::wstring dir(p);

    p += dir.size() + 1;
    if (*p == L'\0')
    {
        // Single file selected
        out_paths.push_back(path_t(dir));
        return true;
    }

    // Multiple files: dir contains the directory, subsequent entries are filenames
    while (*p)
    {
        std::wstring filename(p);
        out_paths.push_back(path_t(dir + L"\\" + filename));
        p += filename.size() + 1;
    }

    return !out_paths.empty();
}

// UTF-8 <-> wide path conversion (paths containing Chinese characters are
// stored in the settings file as UTF-8, not raw bytes).
inline std::string path_to_utf8(const path_t& p)
{
    if (p.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return std::string();
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, p.c_str(), (int)p.size(), &s[0], n, NULL, NULL);
    return s;
}

inline path_t utf8_to_path(const std::string& s)
{
    if (s.empty()) return path_t();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    if (n <= 0) return path_t();
    path_t p(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &p[0], n);
    return p;
}

// Filter: return true if filename has an image extension
inline bool is_image_file(const path_t& filepath)
{
    size_t dot = filepath.rfind(PATHSTR('.'));
    if (dot == path_t::npos)
        return false;

    path_t ext = filepath.substr(dot);
    // lowercase
    for (auto& ch : ext)
        ch = (wchar_t)towlower((wint_t)ch);

    return ext == PATHSTR(".jpg") || ext == PATHSTR(".jpeg") ||
           ext == PATHSTR(".png") || ext == PATHSTR(".webp");
}

// 递归创建目录（父目录不存在时逐级创建）
inline bool create_directory_recursive(const path_t& path)
{
    if (path.empty())
        return false;
    if (path_is_directory(path))
        return true;

    path_t cur;
    size_t i = 0;
    if (path.size() >= 2 && path[1] == PATHSTR(':'))
    {
        cur = path.substr(0, 2); // 保留盘符，如 C:
        i = 2;
    }

    while (i <= path.size())
    {
        size_t slash = path.find_first_of(PATHSTR("/\\"), i);
        path_t seg = (slash == path_t::npos) ? path.substr(i) : path.substr(i, slash - i);
        if (!seg.empty())
        {
            if (!cur.empty() && cur.back() != PATHSTR('\\') && cur.back() != PATHSTR('/'))
                cur += PATHSTR('\\');
            cur += seg;
            if (!path_is_directory(cur) && !CreateDirectoryW(cur.c_str(), NULL))
            {
                if (GetLastError() != ERROR_ALREADY_EXISTS)
                    return false;
            }
        }
        if (slash == path_t::npos)
            break;
        i = slash + 1;
    }

    return path_is_directory(path);
}

#else // !_WIN32

inline std::string path_to_utf8(const path_t& p) { return p; }
inline path_t utf8_to_path(const std::string& s) { return s; }

// Filter: return true if filename has an image extension
inline bool is_image_file(const path_t& filepath)
{
    size_t dot = filepath.rfind('.');
    if (dot == path_t::npos)
        return false;

    std::string ext = filepath.substr(dot);
    for (auto& ch : ext)
        ch = (char)tolower((unsigned char)ch);

    return ext == ".jpg" || ext == ".jpeg" ||
           ext == ".png" || ext == ".webp";
}

#endif // _WIN32

#endif // GUI_WIDGETS_H
