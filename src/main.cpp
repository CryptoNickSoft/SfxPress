#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <climits>
#include <cstdarg>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#ifndef TCM_SETBKCOLOR
#define TCM_SETBKCOLOR (0x1300 + 20)
#endif

enum {
    ID_TAB = 100, ID_STATUS, ID_LOG, ID_CFGVIEW, ID_MODINFO, ID_HINT,
    ID_METHOD, ID_X86, ID_X64, ID_MODDIR, ID_MODDIR_BROWSE,
    ID_PAYLOAD, ID_PAYLOAD_BROWSE, ID_OUT, ID_OUT_BROWSE,
    ID_INSTALLPATH, ID_RUNPROGRAM, ID_DELETE, ID_GUIMODE, ID_SINGLEINSTANCE,
    ID_PASSWORD, ID_SFXAUTHOR, ID_ARCHIVE, ID_PREEXTRACT, ID_EXECUTEONLOAD,
    ID_BEGINPROMPT,
    ID_PAYLOAD_FOLDER_BROWSE,
    ID_BUILD = 200, ID_VERIFY, ID_RUN, ID_OPENFOLDER, ID_LOG_CLEAR,
    ID_FILE_EXIT = 300, ID_FILE_ABOUT, ID_FILE_SAVE, ID_FILE_LOAD,
    ID_HELP_APP = 310, ID_HELP_MODULE, ID_HELP_7ZIP
};

enum { PAGE_MODULE = 0, PAGE_DATA = 1, PAGE_PARAMS = 2, PAGE_CONFIG = 3 };

struct App {
    HINSTANCE hInst;
    HWND hWnd, hTab, hStatus, hLog, hCfgView;
    HWND hMethod, hX86, hX64, hModDir, hModDirBrowse, hModInfo;
    HWND hPayload, hPayloadBrowse, hPayloadFolderBrowse, hOut, hOutBrowse, hHint;
    HWND hInstallPath, hRunProgram, hDelete, hGuiMode, hSingleInstance;
    HWND hPassword, hSfxAuthor, hArchive, hPreExtract, hExecuteOnLoad, hBeginPrompt;
    HFONT hFont, hMono;
    std::vector<HWND> ctls;
    int page;
};

static App g;
static const int EDIT_H = 24;

static std::wstring GetText(HWND h)
{
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return std::wstring();
    std::wstring s((size_t)n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize((size_t)n);
    return s;
}

static void SetText(HWND h, const std::wstring& s)
{
    SetWindowTextW(h, s.c_str());
}

static std::wstring FormatSize(unsigned long long v)
{
    wchar_t buf[64];
    if (v >= 1024ull * 1024ull)
        swprintf(buf, 64, L"%llu байт (%.2f МБ)", v, (double)v / (1024.0 * 1024.0));
    else if (v >= 1024ull)
        swprintf(buf, 64, L"%llu байт (%.1f КБ)", v, (double)v / 1024.0);
    else
        swprintf(buf, 64, L"%llu байт", v);
    return buf;
}

static std::wstring g_logText;
static void Log(const std::wstring& line)
{
    g_logText += line + L"\r\n";
    SetWindowTextW(g.hLog, g_logText.c_str());
    SendMessageW(g.hLog, EM_SETSEL, (WPARAM)g_logText.size(), (LPARAM)g_logText.size());
    SendMessageW(g.hLog, EM_SCROLLCARET, 0, 0);
}

static void ClearLog()
{
    g_logText.clear();
    SetWindowTextW(g.hLog, L"");
}

static void SetStatus(const std::wstring& s)
{
    SendMessageW(g.hStatus, SB_SETTEXTW, 0, (LPARAM)s.c_str());
}

static std::wstring EscapeValue(const std::wstring& in)
{
    std::wstring o;
    o.reserve(in.size() * 2);
    for (wchar_t c : in) {
        if (c == L'\\') o += L"\\\\";
        else if (c == L'"') o += L"\\\"";
        else if (c == L'\n') o += L"\\n";
        else if (c == L'\t') o += L"\\t";
        else o += c;
    }
    return o;
}

static std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static int ComboValue(HWND h)
{
    int i = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
    if (i < 0) return 0;
    return (int)SendMessageW(h, CB_GETITEMDATA, (WPARAM)i, 0);
}

static std::wstring ExeDir();

static std::wstring ResolvePath(const std::wstring& p)
{
    if (p.empty()) return p;
    if (p.size() > 1 && p[1] == L':') return p;
    if (p[0] == L'\\' || p[0] == L'/') return p;
    return ExeDir() + p;
}
static std::wstring ModuleFileName()
{
    static const wchar_t* names[] = { L"Base", L"LZMA", L"LZMA2", L"Deflate", L"PPMd", L"All" };
    int m = (int)SendMessageW(g.hMethod, CB_GETCURSEL, 0, 0);
    if (m < 0) m = 1;
    bool x64 = SendMessageW(g.hX64, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring f;
    if (m == 0) {
        f = x64 ? L"7zsd_x64.sfx" : L"7zsd.sfx";
    } else {
        f = L"7zsd_";
        f += names[m];
        if (x64) f += L"_x64";
        f += L".sfx";
    }
    return f;
}

static std::wstring ModulePath()
{
    std::wstring dir = ResolvePath(GetText(g.hModDir));
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + ModuleFileName();
}

static std::wstring ModulePathDisplay()
{
    std::wstring dir = GetText(g.hModDir);
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    return dir + ModuleFileName();
}

static void SetModuleInfo(const std::wstring& text)
{
    SetText(g.hModInfo, text);
    if (g.hTab) {
        RECT rc;
        GetWindowRect(g.hModInfo, &rc);
        POINT pts[2] = { { rc.left, rc.top }, { rc.right, rc.bottom } };
        ScreenToClient(g.hTab, &pts[0]);
        ScreenToClient(g.hTab, &pts[1]);
        RECT r = { pts[0].x, pts[0].y, pts[1].x, pts[1].y };
        InvalidateRect(g.hTab, &r, TRUE);
    }
    InvalidateRect(g.hModInfo, nullptr, TRUE);
}

static void UpdateModuleInfo()
{
    std::wstring p = ModulePath();
    std::wstring shown = ModulePathDisplay();
    DWORD attr = GetFileAttributesW(p.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
        SetModuleInfo(L"Модуль не найден: " + shown);
        return;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fad)) {
        unsigned long long sz = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        SetModuleInfo(L"Найден: " + shown + L"\r\nРазмер: " + FormatSize(sz));
    }
}
static std::wstring ExeDir()
{
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring p(buf, n);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p.erase(pos + 1);
    return p;
}

static bool IsDirectory(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static std::wstring PortableModulesDir()
{
    const wchar_t* names[] = { L"Bin\\Modules", L"Bin\\modules", L"Bin\\SFX", L"Modules", L"SFX" };
    for (const wchar_t* n : names) {
        if (IsDirectory(ExeDir() + n)) return std::wstring(n);
    }
    return std::wstring(L"Bin\\Modules");
}
static std::wstring ToPortablePath(const std::wstring& path)
{
    std::wstring base = ExeDir();
    if (path.size() > base.size() && _wcsnicmp(path.c_str(), base.c_str(), base.size()) == 0)
        return path.substr(base.size());
    return path;
}

static std::wstring FromPortablePath(const std::wstring& path)
{
    if (path.size() > 1 && path[1] == L':' ) return path;
    if (!path.empty() && (path[0] == L'\\' || path[0] == L'/')) return path;
    if (path.empty()) return path;
    return ExeDir() + path;
}
static unsigned long long FileSizeOf(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return 0;
    return ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
}

static int RunProcess(const std::wstring& cmdLine, const std::wstring& workDir)
{
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cl = cmdLine;
    std::vector<wchar_t> buf(cl.begin(), cl.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                             workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static bool FileExists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static std::wstring RegInstallPath(const wchar_t* key)
{
    wchar_t buf[MAX_PATH * 2] = { 0 };
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Path", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS)
        return std::wstring();
    std::wstring p = buf;
    if (!p.empty() && p.back() != L'\\') p += L'\\';
    p += L"7z.exe";
    return FileExists(p) ? p : std::wstring();
}

static std::wstring FindPacker()
{
    std::wstring base = ExeDir();
    const wchar_t* portable[] = {
        L"Bin\\7z.exe", L"Bin\\7za.exe", L"Bin\\7zG.exe",
        L"7z.exe", L"7za.exe"
    };
    for (const wchar_t* n : portable) {
        std::wstring p = base + n;
        if (FileExists(p)) return p;
    }

    wchar_t buf[MAX_PATH * 2];
    const wchar_t* candidates[] = {
        L"%ProgramW6432%\\7-Zip\\7z.exe",
        L"%ProgramFiles%\\7-Zip\\7z.exe",
        L"%ProgramFiles(x86)%\\7-Zip\\7z.exe",
        L"%LOCALAPPDATA%\\Programs\\7-Zip\\7z.exe"
    };
    for (const wchar_t* c : candidates) {
        DWORD n = ExpandEnvironmentStringsW(c, buf, MAX_PATH * 2);
        if (n == 0 || n > MAX_PATH * 2) continue;
        std::wstring p = buf;
        if (p.find(L'%') != std::wstring::npos) continue;
        if (FileExists(p)) return p;
    }

    std::wstring reg = RegInstallPath(L"SOFTWARE\\7-Zip");
    if (!reg.empty()) return reg;
    reg = RegInstallPath(L"SOFTWARE\\WOW6432Node\\7-Zip");
    if (!reg.empty()) return reg;

    wchar_t found[MAX_PATH * 2] = { 0 };
    if (SearchPathW(nullptr, L"7z.exe", nullptr, MAX_PATH * 2, found, nullptr) > 0 && FileExists(found))
        return std::wstring(found);
    return std::wstring();
}

static const wchar_t* MethodCodec(int method)
{
    switch (method) {
    case 2: case 5: return L"lzma2";
    case 3: return L"deflate";
    case 4: return L"ppmd";
    default: return L"lzma";
    }
}

static std::wstring TempArchiveName()
{
    wchar_t tmp[MAX_PATH];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    std::wstring p(tmp, n);
    p += L"sfxctor_";
    p += std::to_wstring(GetCurrentProcessId());
    p += L".7z";
    return p;
}

static bool PackFolder(const std::wstring& folder, const std::wstring& archive, int method, const std::wstring& password, std::wstring* err)
{
    std::wstring packer = FindPacker();
    if (packer.empty()) {
        *err = L"Не найден архиватор: положите 7z.exe (или 7za.exe) в папку Bin рядом с конструктором.";
        return false;
    }
    std::wstring pattern = folder;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*";
    std::wstring cmd = L"\"" + packer + L"\" a -t7z -m0=" + MethodCodec(method) + L" -mx=9 -y ";
    if (!password.empty()) cmd += L"\"-p" + password + L"\" -mhe=on ";
    cmd += L"\"" + archive + L"\" \"" + pattern + L"\"";
    int rc = RunProcess(cmd, folder);
    if (rc != 0) {
        *err = L"7z завершился с кодом " + std::to_wstring(rc);
        return false;
    }
    if (GetFileAttributesW(archive.c_str()) == INVALID_FILE_ATTRIBUTES) {
        *err = L"7z не создал архив.";
        return false;
    }
    return true;
}
static std::wstring BuildConfigText(std::wstring* err)
{
    std::wstring c = L";!@Install@!UTF-8!\r\n";

    auto add = [&](const wchar_t* key, HWND h) {
        std::wstring v = GetText(h);
        if (!v.empty()) {
            c += key;
            c += L"=\"";
            c += EscapeValue(v);
            c += L"\"\r\n";
        }
    };

    int gui = ComboValue(g.hGuiMode);
    wchar_t num[16];
    swprintf(num, 16, L"%d", gui);
    c += L"GUIMode=\"";
    c += num;
    c += L"\"\r\n";

    add(L"InstallPath", g.hInstallPath);
    add(L"Archive", g.hArchive);
    add(L"RunProgram", g.hRunProgram);
    add(L"Delete", g.hDelete);

    int si = ComboValue(g.hSingleInstance);
    if (si > 0) {
        swprintf(num, 16, L"%d", si);
        c += L"SingleInstance=\"";
        c += num;
        c += L"\"\r\n";
    }

    add(L"BeginPrompt", g.hBeginPrompt);
    add(L"Password", g.hPassword);
    add(L"SfxAuthor", g.hSfxAuthor);
    add(L"PreExtract", g.hPreExtract);
    add(L"ExecuteOnLoad", g.hExecuteOnLoad);

    c += L";!@InstallEnd@!\r\n";

    if (si > 0 && GetText(g.hInstallPath).empty()) {
        if (err) *err = L"SingleInstance требует заданного InstallPath.";
        return c;
    }

    if (c.size() > 1024 * 1024) {
        if (err) *err = L"Конфигурация больше 1 МБ: модуль не найдёт ни её, ни архив.";
    }
    return c;
}

static void RefreshConfigView()
{
    std::wstring err;
    std::wstring c = BuildConfigText(&err);
    SetText(g.hCfgView, c);
    if (!err.empty()) Log(L"[!] " + err);
}

static bool BrowseOpen(std::wstring& path, const wchar_t* filter, const wchar_t* defExt)
{
    wchar_t buf[MAX_PATH * 4];
    buf[0] = 0;
    wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hWnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (defExt) ofn.lpstrDefExt = defExt;
    if (!GetOpenFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

static bool BrowseSave(std::wstring& path, const wchar_t* filter, const wchar_t* defExt)
{
    wchar_t buf[MAX_PATH * 4];
    buf[0] = 0;
    wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.hWnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

static bool CopyStreamToFile(FILE* out, const std::wstring& path, unsigned long long* written)
{
    FILE* in = _wfopen(path.c_str(), L"rb");
    if (!in) return false;
    std::vector<char> buf(1 << 20);
    size_t n;
    *written = 0;
    while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) {
        if (fwrite(buf.data(), 1, n, out) != n) {
            fclose(in);
            return false;
        }
        *written += n;
    }
    fclose(in);
    return true;
}

static unsigned long long FindArchiveOffset(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return ULLONG_MAX;
    std::vector<unsigned char> buf(1 << 20);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    const unsigned char sig[6] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
    for (size_t i = 0; i + 6 <= n; i++) {
        if (memcmp(&buf[i], sig, 6) == 0) return (unsigned long long)i;
    }
    return ULLONG_MAX;
}

static void DoBuild(bool verifyOnly)
{
    std::wstring err;
    std::wstring cfg = BuildConfigText(&err);
    if (!err.empty()) {
        Log(L"[ошибка] " + err);
        SetStatus(L"Проверка не пройдена");
        MessageBoxW(g.hWnd, err.c_str(), L"Проверка параметров", MB_ICONWARNING);
        return;
    }

    std::wstring mod = ModulePath();
    std::wstring payloadSrc = ResolvePath(GetText(g.hPayload));
    std::wstring out = ResolvePath(GetText(g.hOut));
    std::wstring payload = payloadSrc;
    std::wstring tempArchive;

    if (verifyOnly) {
        if (out.empty()) {
            Log(L"[ошибка] Не задан путь результата.");
            return;
        }
        unsigned long long off = FindArchiveOffset(out);
        if (off == ULLONG_MAX) {
            Log(L"[проверка] сигнатура 7z не найдена в " + out);
            SetStatus(L"Сигнатура не найдена");
        } else {
            Log(L"[проверка] сигнатура 7z найдена по смещению " + std::to_wstring(off));
            SetStatus(L"Сигнатура найдена: " + std::to_wstring(off));
        }
        return;
    }

    if (mod.empty() || GetFileAttributesW(mod.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"[ошибка] Модуль не найден: " + mod);
        SetStatus(L"Модуль не найден");
        return;
    }
    if (payloadSrc.empty() || GetFileAttributesW(payloadSrc.c_str()) == INVALID_FILE_ATTRIBUTES) {
        Log(L"[ошибка] Payload не найден: " + payloadSrc);
        SetStatus(L"Payload не найден");
        return;
    }
    if (IsDirectory(payloadSrc)) {
        int method = (int)SendMessageW(g.hMethod, CB_GETCURSEL, 0, 0);
        std::wstring pwd = GetText(g.hPassword);
        tempArchive = TempArchiveName();
        DeleteFileW(tempArchive.c_str());
        SetStatus(L"Упаковка папки...");
        Log(L"[упаковка] " + payloadSrc);
        std::wstring perr;
        if (!pwd.empty()) Log(L"  архив шифруется паролем (заголовки тоже)");
        if (!PackFolder(payloadSrc, tempArchive, method, pwd, &perr)) {
            Log(L"[ошибка] " + perr);
            SetStatus(L"Ошибка упаковки");
            return;
        }
        payload = tempArchive;
        Log(L"  архив: " + FormatSize(FileSizeOf(tempArchive)) + L"  " + tempArchive);
    }
    if (!IsDirectory(payloadSrc) && !GetText(g.hPassword).empty())
        Log(L"[пароль] готовый архив не перешифровывается: убедитесь, что он уже зашифрован этим паролем");
    if (out.empty()) {
        Log(L"[ошибка] Не задан путь результата.");
        SetStatus(L"Нет пути результата");
        return;
    }
    if (_wcsicmp(out.c_str(), mod.c_str()) == 0 || _wcsicmp(out.c_str(), payload.c_str()) == 0) {
        Log(L"[ошибка] Результат совпадает с модулем или payload.");
        SetStatus(L"Неверный путь результата");
        return;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad;
    unsigned long long modSize = 0, paySize = 0;
    if (GetFileAttributesExW(mod.c_str(), GetFileExInfoStandard, &fad))
        modSize = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
    if (GetFileAttributesExW(payload.c_str(), GetFileExInfoStandard, &fad))
        paySize = ((unsigned long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;

    std::string cfgUtf8 = ToUtf8(cfg);

    if (modSize + cfgUtf8.size() > 1024 * 1024) {
        Log(L"[ошибка] Модуль + конфигурация больше 1 МБ: архив окажется за границей поиска.");
        SetStatus(L"Слишком большой заголовок");
        return;
    }

    FILE* outFile = _wfopen(out.c_str(), L"wb");
    if (!outFile) {
        Log(L"[ошибка] Не удалось создать файл: " + out);
        SetStatus(L"Ошибка записи");
        return;
    }

    unsigned long long w1 = 0, w2 = 0;
    bool ok = CopyStreamToFile(outFile, mod, &w1);
    if (ok && !cfgUtf8.empty()) {
        ok = fwrite(cfgUtf8.data(), 1, cfgUtf8.size(), outFile) == cfgUtf8.size();
    }
    if (ok) ok = CopyStreamToFile(outFile, payload, &w2);
    fclose(outFile);
    if (!tempArchive.empty()) DeleteFileW(tempArchive.c_str());

    if (!ok) {
        Log(L"[ошибка] Сбой при записи результата.");
        SetStatus(L"Ошибка записи");
        return;
    }

    unsigned long long total = w1 + cfgUtf8.size() + w2;
    Log(L"--- сборка ---");
    Log(L"  модуль:       " + FormatSize(w1) + L"  " + mod);
    Log(L"  конфигурация: " + FormatSize(cfgUtf8.size()) + L" (UTF-8)");
    Log(L"  payload:      " + FormatSize(w2) + L"  " + payload);
    Log(L"  результат:    " + FormatSize(total) + L"  " + out);

    unsigned long long off = FindArchiveOffset(out);
    if (off == ULLONG_MAX) {
        Log(L"[!] сигнатура 7z в результате не найдена");
    } else {
        Log(L"  сигнатура 7z: смещение " + std::to_wstring(off) +
            L" (модуль+конфиг = " + std::to_wstring(w1 + cfgUtf8.size()) + L")");
    }
    SetStatus(L"Собрано: " + FormatSize(total));
}

static void ShowPage(int page)
{
    g.page = page;
    for (HWND h : g.ctls) {
        LONG_PTR p = GetWindowLongPtrW(h, GWLP_USERDATA);
        if (p < 0) continue;
        ShowWindow(h, p == page ? SW_SHOW : SW_HIDE);
    }
    if (page == PAGE_CONFIG) RefreshConfigView();
    if (page == PAGE_MODULE) UpdateModuleInfo();
}

static HWND Ctl(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, int page, DWORD exStyle = 0)
{
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
                             g.hWnd, (HMENU)(INT_PTR)id, g.hInst, nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    SetWindowLongPtrW(c, GWLP_USERDATA, page);
    g.ctls.push_back(c);
    return c;
}

static HWND Label(const wchar_t* text, int x, int y, int w, int h, int page)
{
    return Ctl(L"STATIC", text, SS_LEFT, x, y, w, h, 0, page);
}

static HWND Edit(int x, int y, int w, int id, int page, const wchar_t* text = L"")
{
    return Ctl(L"EDIT", text, WS_BORDER | ES_AUTOHSCROLL, x, y, w, EDIT_H, id, page, WS_EX_CLIENTEDGE);
}

static HWND Button(const wchar_t* text, int x, int y, int w, int h, int id, int page)
{
    return Ctl(L"BUTTON", text, BS_PUSHBUTTON, x, y, w, h, id, page);
}

static HWND Combo(int x, int y, int w, int h, int id, int page)
{
    return Ctl(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, x, y, w, h, id, page, WS_EX_CLIENTEDGE);
}

static void AddComboItem(HWND h, const wchar_t* text, int value)
{
    int i = (int)SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)text);
    SendMessageW(h, CB_SETITEMDATA, (WPARAM)i, (LPARAM)value);
}

static void EnableDpiAwareFont()
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -12;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    g.hFont = CreateFontIndirectW(&lf);

    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -13;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    g.hMono = CreateFontIndirectW(&lf);
}

static void SetComboByValue(HWND combo, int value)
{
    int count = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < count; i++) {
        if ((int)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)i, 0) == value) {
            SendMessageW(combo, CB_SETCURSEL, (WPARAM)i, 0);
            return;
        }
    }
}

static void LoadExample()
{
    std::wstring dir = ExeDir() + L"Example\\";
    if (!IsDirectory(dir)) return;

    std::wstring payload;
    const wchar_t* dirs[] = { L"payload", L"data", L"files" };
    for (const wchar_t* d : dirs) {
        if (IsDirectory(dir + d)) { payload = std::wstring(L"Example\\") + d; break; }
    }
    if (payload.empty()) {
        const wchar_t* files[] = { L"payload.7z", L"data.7z", L"example.7z" };
        for (const wchar_t* f : files) {
            if (FileExists(dir + f)) { payload = std::wstring(L"Example\\") + f; break; }
        }
    }
    if (payload.empty()) return;

    SetText(g.hPayload, payload);
    if (GetText(g.hOut).empty()) SetText(g.hOut, L"Example\\example.exe");

    std::wstring ini = dir + L"example.ini";
    if (FileExists(ini)) {
        auto rd = [&](const wchar_t* key, const wchar_t* def) {
            wchar_t buf[1024];
            buf[0] = 0;
            GetPrivateProfileStringW(L"sfx", key, def, buf, 1024, ini.c_str());
            return std::wstring(buf);
        };
        std::wstring v = rd(L"installpath", L"");
        if (!v.empty()) SetText(g.hInstallPath, v);
        v = rd(L"runprogram", L"");
        if (!v.empty()) SetText(g.hRunProgram, v);
        v = rd(L"delete", L"");
        if (!v.empty()) SetText(g.hDelete, v);
        v = rd(L"method", L"");
        if (!v.empty()) SendMessageW(g.hMethod, CB_SETCURSEL, (WPARAM)_wtoi(v.c_str()), 0);
        v = rd(L"x64", L"0");
        if (_wtoi(v.c_str())) SendMessageW(g.hX64, BM_SETCHECK, BST_CHECKED, 0);
        else SendMessageW(g.hX86, BM_SETCHECK, BST_CHECKED, 0);
        v = rd(L"guimode", L"");
        if (!v.empty()) SetComboByValue(g.hGuiMode, _wtoi(v.c_str()));
        v = rd(L"singleinstance", L"");
        if (!v.empty()) SetComboByValue(g.hSingleInstance, _wtoi(v.c_str()));
    }

    Log(L"[пример] поля заполнены из папки Example");
}
static void CreateUi(HWND hWnd)
{
    g.hWnd = hWnd;

    g.hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                             10, 8, 780, 384, hWnd, (HMENU)(INT_PTR)ID_TAB, g.hInst, nullptr);
    SendMessageW(g.hTab, WM_SETFONT, (WPARAM)g.hFont, TRUE);

    const wchar_t* titles[] = { L"Модуль", L"Данные", L"Параметры", L"Конфигурация" };
    for (int i = 0; i < 4; i++) {
        TCITEMW ti;
        ZeroMemory(&ti, sizeof(ti));
        ti.mask = TCIF_TEXT;
        ti.pszText = (LPWSTR)titles[i];
        TabCtrl_InsertItem(g.hTab, i, &ti);
    }
    Label(L"Метод сжатия:", 26, 50, 120, 20, PAGE_MODULE);
    g.hMethod = Combo(152, 46, 220, 110, ID_METHOD, PAGE_MODULE);
    AddComboItem(g.hMethod, L"Base (Copy + LZMA)", 0);
    AddComboItem(g.hMethod, L"LZMA", 1);
    AddComboItem(g.hMethod, L"LZMA2", 2);
    AddComboItem(g.hMethod, L"Deflate", 3);
    AddComboItem(g.hMethod, L"PPMd", 4);
    AddComboItem(g.hMethod, L"All", 5);
    SendMessageW(g.hMethod, CB_SETCURSEL, 1, 0);

    g.hX86 = Ctl(L"BUTTON", L"", BS_AUTORADIOBUTTON | WS_GROUP, 152, 80, 20, 20, ID_X86, PAGE_MODULE);
    Label(L"x86", 176, 82, 60, 18, PAGE_MODULE);
    g.hX64 = Ctl(L"BUTTON", L"", BS_AUTORADIOBUTTON, 240, 80, 20, 20, ID_X64, PAGE_MODULE);
    Label(L"x64", 264, 82, 60, 18, PAGE_MODULE);
    SendMessageW(g.hX86, BM_SETCHECK, BST_CHECKED, 0);

    Label(L"Папка модулей:", 26, 114, 120, 20, PAGE_MODULE);
    g.hModDir = Edit(152, 110, 470, ID_MODDIR, PAGE_MODULE, PortableModulesDir().c_str());
    g.hModDirBrowse = Button(L"...", 630, 110, 36, EDIT_H, ID_MODDIR_BROWSE, PAGE_MODULE);

    g.hModInfo = Label(L"", 152, 146, 560, 80, PAGE_MODULE);

    Label(L"Payload (.7z):", 26, 50, 120, 20, PAGE_DATA);
    g.hPayload = Edit(152, 46, 320, ID_PAYLOAD, PAGE_DATA);
    g.hPayloadBrowse = Button(L"Файл...", 480, 46, 74, EDIT_H, ID_PAYLOAD_BROWSE, PAGE_DATA);
    g.hPayloadFolderBrowse = Button(L"Папка...", 560, 46, 80, EDIT_H, ID_PAYLOAD_FOLDER_BROWSE, PAGE_DATA);

    Label(L"Результат (.exe):", 26, 86, 120, 20, PAGE_DATA);
    g.hOut = Edit(152, 82, 430, ID_OUT, PAGE_DATA);
    g.hOutBrowse = Button(L"...", 592, 82, 36, EDIT_H, ID_OUT_BROWSE, PAGE_DATA);

    Label(L"Сборка склеивает три части в указанном порядке:\r\n"
          L"  [модуль .sfx] + [текст конфигурации UTF-8] + [архив]\r\n\r\n"
          L"Можно указать готовый .7z или папку: папка упакуется автоматически\r\n"
          L"(через Bin\\7z.exe рядом с конструктором).\r\n\r\n"
          L"Конфигурация и начало архива должны попасть в первые 1 МБ файла.",
          152, 122, 560, 130, PAGE_DATA);

    const int y1 = 50, dy = 36;
    Label(L"InstallPath:", 26, y1, 110, 20, PAGE_PARAMS);
    g.hInstallPath = Edit(142, y1 - 4, 250, ID_INSTALLPATH, PAGE_PARAMS, L"%TEMP%\\app");
    Label(L"RunProgram:", 26, y1 + dy, 110, 20, PAGE_PARAMS);
    g.hRunProgram = Edit(142, y1 + dy - 4, 250, ID_RUNPROGRAM, PAGE_PARAMS, L"app.exe");
    Label(L"Delete:", 26, y1 + dy * 2, 110, 20, PAGE_PARAMS);
    g.hDelete = Edit(142, y1 + dy * 2 - 4, 250, ID_DELETE, PAGE_PARAMS, L"%%T");
    Label(L"GUIMode:", 26, y1 + dy * 3, 110, 20, PAGE_PARAMS);
    g.hGuiMode = Combo(142, y1 + dy * 3 - 4, 250, 96, ID_GUIMODE, PAGE_PARAMS);
    AddComboItem(g.hGuiMode, L"0 — обычное окно", 0);
    AddComboItem(g.hGuiMode, L"1 — только прогресс", 1);
    AddComboItem(g.hGuiMode, L"2 — тихий режим", 2);
    SendMessageW(g.hGuiMode, CB_SETCURSEL, 2, 0);
    Label(L"SingleInstance:", 26, y1 + dy * 4, 110, 20, PAGE_PARAMS);
    g.hSingleInstance = Combo(142, y1 + dy * 4 - 4, 250, 96, ID_SINGLEINSTANCE, PAGE_PARAMS);
    AddComboItem(g.hSingleInstance, L"0 — выключено", 0);
    AddComboItem(g.hSingleInstance, L"1 — вторая копия выходит", 1);
    AddComboItem(g.hSingleInstance, L"2 — активировать первую", 2);
    SendMessageW(g.hSingleInstance, CB_SETCURSEL, 1, 0);

    Label(L"Password:", 404, y1, 100, 20, PAGE_PARAMS);
    g.hPassword = Edit(510, y1 - 4, 250, ID_PASSWORD, PAGE_PARAMS);
    Label(L"SfxAuthor:", 404, y1 + dy, 100, 20, PAGE_PARAMS);
    g.hSfxAuthor = Edit(510, y1 + dy - 4, 250, ID_SFXAUTHOR, PAGE_PARAMS);
    Label(L"Archive:", 404, y1 + dy * 2, 100, 20, PAGE_PARAMS);
    g.hArchive = Edit(510, y1 + dy * 2 - 4, 250, ID_ARCHIVE, PAGE_PARAMS);
    Label(L"PreExtract:", 404, y1 + dy * 3, 100, 20, PAGE_PARAMS);
    g.hPreExtract = Edit(510, y1 + dy * 3 - 4, 250, ID_PREEXTRACT, PAGE_PARAMS);
    Label(L"ExecuteOnLoad:", 404, y1 + dy * 4, 100, 20, PAGE_PARAMS);
    g.hExecuteOnLoad = Edit(510, y1 + dy * 4 - 4, 250, ID_EXECUTEONLOAD, PAGE_PARAMS);

    Label(L"BeginPrompt:", 26, y1 + dy * 5 + 6, 110, 20, PAGE_PARAMS);
    g.hBeginPrompt = Edit(142, y1 + dy * 5 + 2, 618, ID_BEGINPROMPT, PAGE_PARAMS);

    g.hCfgView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 26, 44, 742, 320, hWnd, (HMENU)(INT_PTR)ID_CFGVIEW, g.hInst, nullptr);
    SendMessageW(g.hCfgView, WM_SETFONT, (WPARAM)g.hMono, TRUE);
    SetWindowLongPtrW(g.hCfgView, GWLP_USERDATA, PAGE_CONFIG);
    g.ctls.push_back(g.hCfgView);

    Label(L"Журнал:", 26, 400, 100, 20, -1);
    g.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                             26, 420, 748, 118, hWnd, (HMENU)(INT_PTR)ID_LOG, g.hInst, nullptr);
    SendMessageW(g.hLog, WM_SETFONT, (WPARAM)g.hMono, TRUE);
    SetWindowLongPtrW(g.hLog, GWLP_USERDATA, -1);
    g.ctls.push_back(g.hLog);

    Button(L"Собрать", 26, 548, 120, 32, ID_BUILD, -1);
    Button(L"Проверить", 156, 548, 120, 32, ID_VERIFY, -1);
    Button(L"Запустить", 286, 548, 120, 32, ID_RUN, -1);
    Button(L"Открыть папку", 416, 548, 140, 32, ID_OPENFOLDER, -1);
    Button(L"Очистить журнал", 566, 548, 150, 32, ID_LOG_CLEAR, -1);

    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_STATUS, g.hInst, nullptr);
    SendMessageW(g.hStatus, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    int parts[2] = { 560, -1 };
    SendMessageW(g.hStatus, SB_SETPARTS, 2, (LPARAM)parts);
    SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)L"SfxPress 1.0");

    DragAcceptFiles(hWnd, TRUE);
    LoadExample();
    ShowPage(PAGE_MODULE);
    SetStatus(L"Готово");
}

static HMENU BuildMenu()
{
    HMENU m = CreateMenu();

    HMENU f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, ID_FILE_LOAD, L"Загрузить профиль...\tCtrl+O");
    AppendMenuW(f, MF_STRING, ID_FILE_SAVE, L"Сохранить профиль...\tCtrl+S");
    AppendMenuW(f, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(f, MF_STRING, ID_FILE_EXIT, L"Выход");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)f, L"Файл");

    HMENU b = CreatePopupMenu();
    AppendMenuW(b, MF_STRING, ID_BUILD, L"Собрать\tF9");
    AppendMenuW(b, MF_STRING, ID_VERIFY, L"Проверить");
    AppendMenuW(b, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(b, MF_STRING, ID_RUN, L"Запустить результат");
    AppendMenuW(b, MF_STRING, ID_OPENFOLDER, L"Открыть папку результата");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)b, L"Сборка");

    HMENU h = CreatePopupMenu();
    AppendMenuW(h, MF_STRING, ID_HELP_APP, L"Справка SfxPress\tF1");
    AppendMenuW(h, MF_STRING, ID_HELP_MODULE, L"Справка модуля 7zSFX");
    AppendMenuW(h, MF_STRING, ID_HELP_7ZIP, L"Справка 7-Zip");
    AppendMenuW(h, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(h, MF_STRING, ID_FILE_ABOUT, L"О программе");
    AppendMenuW(m, MF_POPUP, (UINT_PTR)h, L"Справка");
    return m;
}
static void SaveProfile()
{
    std::wstring path;
    if (!BrowseSave(path, L"Профиль (*.ini)\0*.ini\0Все файлы\0*.*\0", L"ini")) return;

    auto kv = [&](const wchar_t* k, HWND h) {
        WritePrivateProfileStringW(L"sfx", k, GetText(h).c_str(), path.c_str());
    };
    auto kvp = [&](const wchar_t* k, HWND h) {
        WritePrivateProfileStringW(L"sfx", k, ToPortablePath(GetText(h)).c_str(), path.c_str());
    };
    wchar_t num[16];
    kv(L"moddir", g.hModDir);
    swprintf(num, 16, L"%d", (int)SendMessageW(g.hMethod, CB_GETCURSEL, 0, 0));
    WritePrivateProfileStringW(L"sfx", L"method", num, path.c_str());
    swprintf(num, 16, L"%d", (int)(SendMessageW(g.hX64, BM_GETCHECK, 0, 0) == BST_CHECKED));
    WritePrivateProfileStringW(L"sfx", L"x64", num, path.c_str());
    kvp(L"payload", g.hPayload);
    kvp(L"out", g.hOut);
    kv(L"installpath", g.hInstallPath);
    kv(L"runprogram", g.hRunProgram);
    kv(L"delete", g.hDelete);
    kv(L"password", g.hPassword);
    kv(L"sfxauthor", g.hSfxAuthor);
    kv(L"archive", g.hArchive);
    kv(L"preextract", g.hPreExtract);
    kv(L"executeonload", g.hExecuteOnLoad);
    kv(L"beginprompt", g.hBeginPrompt);
    swprintf(num, 16, L"%d", ComboValue(g.hGuiMode));
    WritePrivateProfileStringW(L"sfx", L"guimode", num, path.c_str());
    swprintf(num, 16, L"%d", ComboValue(g.hSingleInstance));
    WritePrivateProfileStringW(L"sfx", L"singleinstance", num, path.c_str());

    Log(L"[профиль] сохранён: " + path);
}

static void LoadProfile()
{
    std::wstring path;
    if (!BrowseOpen(path, L"Профиль (*.ini)\0*.ini\0Все файлы\0*.*\0", nullptr)) return;

    auto kv = [&](const wchar_t* k, HWND h) {
        wchar_t buf[4096];
        buf[0] = 0;
        GetPrivateProfileStringW(L"sfx", k, L"", buf, 4096, path.c_str());
        if (buf[0]) SetText(h, buf);
    };
    auto kvp = [&](const wchar_t* k, HWND h) {
        wchar_t buf[4096];
        buf[0] = 0;
        GetPrivateProfileStringW(L"sfx", k, L"", buf, 4096, path.c_str());
        if (buf[0]) SetText(h, FromPortablePath(buf));
    };
    kv(L"moddir", g.hModDir);
    wchar_t num[32];
    GetPrivateProfileStringW(L"sfx", L"method", L"1", num, 32, path.c_str());
    SendMessageW(g.hMethod, CB_SETCURSEL, (WPARAM)_wtoi(num), 0);
    GetPrivateProfileStringW(L"sfx", L"x64", L"0", num, 32, path.c_str());
    if (_wtoi(num)) SendMessageW(g.hX64, BM_SETCHECK, BST_CHECKED, 0);
    else SendMessageW(g.hX86, BM_SETCHECK, BST_CHECKED, 0);
    kvp(L"payload", g.hPayload);
    kvp(L"out", g.hOut);
    kv(L"installpath", g.hInstallPath);
    kv(L"runprogram", g.hRunProgram);
    kv(L"delete", g.hDelete);
    kv(L"password", g.hPassword);
    kv(L"sfxauthor", g.hSfxAuthor);
    kv(L"archive", g.hArchive);
    kv(L"preextract", g.hPreExtract);
    kv(L"executeonload", g.hExecuteOnLoad);
    kv(L"beginprompt", g.hBeginPrompt);
    GetPrivateProfileStringW(L"sfx", L"guimode", L"2", num, 32, path.c_str());
    SendMessageW(g.hGuiMode, CB_SETCURSEL, (WPARAM)_wtoi(num), 0);
    GetPrivateProfileStringW(L"sfx", L"singleinstance", L"1", num, 32, path.c_str());
    SendMessageW(g.hSingleInstance, CB_SETCURSEL, (WPARAM)_wtoi(num), 0);

    UpdateModuleInfo();
    RefreshConfigView();
    Log(L"[профиль] загружен: " + path);
}

static void OpenHelp(const std::wstring& relPath)
{
    std::wstring path = ExeDir() + relPath;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring msg = L"Файл справки не найден:\n" + path;
        MessageBoxW(g.hWnd, msg.c_str(), L"Справка", MB_ICONINFORMATION);
        return;
    }
    ShellExecuteW(g.hWnd, L"open", path.c_str(), nullptr, ExeDir().c_str(), SW_SHOWNORMAL);
    Log(L"[справка] " + path);
}
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        CreateUi(hWnd);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_F1) { OpenHelp(L"Bin\\SfxPress.chm"); return 0; }
        break;

    case WM_NOTIFY: {
        LPNMHDR nh = (LPNMHDR)lParam;
        if (nh->idFrom == ID_TAB && nh->code == TCN_SELCHANGE)
            ShowPage(TabCtrl_GetCurSel(g.hTab));
        return 0;
    }


    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
        case ID_BUILD: DoBuild(false); return 0;
        case ID_VERIFY: {
            DoBuild(false);
            std::wstring out = ResolvePath(GetText(g.hOut));
            if (!out.empty() && GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES) {
                Log(L"[проверка] запуск тестового проекта: " + out);
                SetStatus(L"Проверка: запущен тестовый проект");
                ShellExecuteW(hWnd, L"open", out.c_str(), nullptr, ExeDir().c_str(), SW_SHOWNORMAL);
            } else {
                Log(L"[проверка] тестовый проект не собран");
                SetStatus(L"Проверка: сборка не удалась");
            }
            return 0;
        }
        case ID_RUN: {
            std::wstring out = ResolvePath(GetText(g.hOut));
            if (out.empty()) { Log(L"[!] Сначала задайте путь результата."); return 0; }
            ShellExecuteW(hWnd, L"open", out.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case ID_LOG_CLEAR: ClearLog(); SetStatus(L"Журнал очищен"); return 0;
        case ID_OPENFOLDER: {
            std::wstring out = ResolvePath(GetText(g.hOut));
            if (out.empty()) { Log(L"[!] Сначала задайте путь результата."); return 0; }
            std::wstring args = L"/select,\"" + out + L"\"";
            ShellExecuteW(hWnd, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        case ID_FILE_SAVE: SaveProfile(); return 0;
        case ID_FILE_LOAD: LoadProfile(); return 0;
        case ID_FILE_EXIT: DestroyWindow(hWnd); return 0;
        case ID_HELP_APP: OpenHelp(L"Bin\\SfxPress.chm"); return 0;
        case ID_HELP_MODULE: OpenHelp(L"Bin\\Modules\\7zSD_RU_plus.chm"); return 0;
        case ID_HELP_7ZIP: OpenHelp(L"Bin\\7-zip.chm"); return 0;
        case ID_FILE_ABOUT:
            MessageBoxW(hWnd,
                        L"SfxPress 1.0\n\n"
                        L"Конструктор SFX-инсталляторов для модифицированного SFX-модуля.\n"
                        L"Чистый C++ / Win32, без сторонних библиотек и внешних утилит.\n\n"
                        L"Сборка: модуль .sfx + конфигурация UTF-8 + архив payload.7z",
                        L"О программе", MB_ICONINFORMATION);
            return 0;
        case ID_METHOD:
        case ID_X86:
        case ID_X64:
        case ID_GUIMODE:
        case ID_SINGLEINSTANCE:
            if (code == CBN_SELCHANGE || code == BN_CLICKED) {
                UpdateModuleInfo();
                if (g.page == PAGE_CONFIG) RefreshConfigView();
            }
            return 0;
        case ID_PAYLOAD_BROWSE: {
            std::wstring p = GetText(g.hPayload);
            if (BrowseOpen(p, L"Архив 7z (*.7z)\0*.7z\0Все файлы\0*.*\0", L"7z")) SetText(g.hPayload, p);
            return 0;
        }
        case ID_OUT_BROWSE: {
            std::wstring p = GetText(g.hOut);
            if (BrowseSave(p, L"Исполняемый файл (*.exe)\0*.exe\0Все файлы\0*.*\0", L"exe")) SetText(g.hOut, p);
            return 0;
        }
        case ID_PAYLOAD_FOLDER_BROWSE: {
            BROWSEINFOW bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.hwndOwner = g.hWnd;
            bi.lpszTitle = L"Выберите папку с файлами для payload";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH * 4];
                if (SHGetPathFromIDListW(pidl, path)) SetText(g.hPayload, path);
                CoTaskMemFree(pidl);
            }
            return 0;
        }
        case ID_MODDIR_BROWSE: {
            std::wstring p = GetText(g.hModDir);
            if (BrowseOpen(p, L"Все файлы\0*.*\0", nullptr)) {
                size_t pos = p.find_last_of(L"\\/");
                if (pos != std::wstring::npos) p.erase(pos);
                SetText(g.hModDir, p);
                UpdateModuleInfo();
            }
            return 0;
        }
        case ID_INSTALLPATH:
        case ID_RUNPROGRAM:
        case ID_DELETE:
        case ID_PASSWORD:
        case ID_SFXAUTHOR:
        case ID_ARCHIVE:
        case ID_PREEXTRACT:
        case ID_EXECUTEONLOAD:
        case ID_BEGINPROMPT:
            if (code == EN_CHANGE && g.page == PAGE_CONFIG) RefreshConfigView();
            return 0;
        default:
            break;
        }
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        wchar_t cls[32] = { 0 };
        GetClassNameW((HWND)lParam, cls, 32);
        if (_wcsicmp(cls, L"Edit") == 0) {
            SetBkColor((HDC)wParam, GetSysColor(COLOR_WINDOW));
            SetTextColor((HDC)wParam, GetSysColor(COLOR_WINDOWTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_DROPFILES: {
        HDROP hd = (HDROP)wParam;
        wchar_t path[MAX_PATH * 4];
        UINT count = DragQueryFileW(hd, 0, path, MAX_PATH * 4);
        DragFinish(hd);
        if (count > 0) {
            SetText(g.hPayload, path);
            Log(L"[drop] " + std::wstring(path));
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static std::wstring ArgValue(const std::wstring& cmd, const wchar_t* name)
{
    std::wstring key = std::wstring(L" ") + name + L"=";
    size_t p = cmd.find(key);
    if (p == std::wstring::npos) return std::wstring();
    p += key.size();
    std::wstring val;
    if (p < cmd.size() && cmd[p] == L'"') {
        ++p;
        while (p < cmd.size() && cmd[p] != L'"') val += cmd[p++];
    } else {
        while (p < cmd.size() && cmd[p] != L' ' && cmd[p] != L'\t') val += cmd[p++];
    }
    return val;
}

#define IDI_APP 101

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    EnableDpiAwareFont();
    g.hInst = hInst;

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SfxPressMain";
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, 800, 620 };
    AdjustWindowRectEx(&rc, style, TRUE, 0);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"SfxPress",
                                style, CW_USEDEFAULT, CW_USEDEFAULT,
                                rc.right - rc.left, rc.bottom - rc.top,
                                nullptr, BuildMenu(), hInst, nullptr);
    if (!hWnd) return 1;

    SendMessageW(hWnd, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED));
    SendMessageW(hWnd, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    std::wstring cmdline = GetCommandLineW();
    if (cmdline.find(L"--build") != std::wstring::npos) {
        std::wstring v;
        v = ArgValue(cmdline, L"--payload"); if (!v.empty()) SetText(g.hPayload, v);
        v = ArgValue(cmdline, L"--out");     if (!v.empty()) SetText(g.hOut, v);
        v = ArgValue(cmdline, L"--install"); if (!v.empty()) SetText(g.hInstallPath, v);
        v = ArgValue(cmdline, L"--run");     if (!v.empty()) SetText(g.hRunProgram, v);
        v = ArgValue(cmdline, L"--password"); if (!v.empty()) SetText(g.hPassword, v);
        DoBuild(false);
        std::wstring out = ResolvePath(GetText(g.hOut));
        bool ok = !out.empty() && GetFileAttributesW(out.c_str()) != INVALID_FILE_ATTRIBUTES;
        DestroyWindow(hWnd);
        return ok ? 0 : 2;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}