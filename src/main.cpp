#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <richedit.h>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <climits>
#include <cstdarg>

#include "appversion.h"

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
    ID_TAB = 100, ID_STATUS, ID_LOG, ID_CFGVIEW, ID_MODINFO, ID_HINT, ID_PROGRESS,
    ID_METHOD, ID_X86, ID_X64, ID_MODDIR, ID_MODDIR_BROWSE,
    ID_PAYLOAD, ID_PAYLOAD_BROWSE, ID_OUT, ID_OUT_BROWSE,
    ID_INSTALLPATH, ID_RUNPROGRAM, ID_DELETE, ID_GUIMODE, ID_SINGLEINSTANCE,
    ID_PASSWORD, ID_SFXAUTHOR, ID_ARCHIVE, ID_PREEXTRACT, ID_EXECUTEONLOAD,
    ID_BEGINPROMPT, ID_LOGFILE_CHK, ID_LOGFILE, ID_LOGFILE_BROWSE,
    ID_VOLSTYLE, ID_VOLSIZE,
    ID_PAYLOAD_FOLDER_BROWSE,
    ID_PACKER, ID_PACKER_BROWSE, ID_PACKER_INFO,
    ID_PAYLOADCRC, ID_SELFDELETE, ID_CLEANMODE,
    ID_BUILD = 200, ID_VERIFY, ID_RUN, ID_OPENFOLDER, ID_LOG_CLEAR, ID_SAVE_CFG,
    ID_FILE_EXIT = 300, ID_FILE_ABOUT, ID_FILE_SAVE, ID_FILE_LOAD, ID_FILE_VIEWSFX, ID_FILE_GENCFG,
    ID_HELP_APP = 310, ID_HELP_MODULE, ID_HELP_7ZIP
};

enum { PAGE_MODULE = 0, PAGE_DATA = 1, PAGE_PARAMS = 2, PAGE_CONFIG = 3 };

#define IDI_APP 101
#define IDI_SAVE 102
#define IDI_BUILD 103
#define IDI_VERIFY 104
#define IDI_RUN 105
#define IDI_OPENFOLDER 106
#define IDI_CLEARLOG 107

struct App {
    HINSTANCE hInst;
    HWND hWnd, hTab, hStatus, hLog, hCfgView, hProgress, hLogLabel;
    HWND hMethod, hX86, hX64, hModDir, hModDirBrowse, hModInfo;
    HWND hPayload, hPayloadBrowse, hPayloadFolderBrowse, hOut, hOutBrowse, hHint;
    HWND hInstallPath, hRunProgram, hDelete, hGuiMode, hSingleInstance;
    HWND hPassword, hSfxAuthor, hArchive, hPreExtract, hExecuteOnLoad, hBeginPrompt;
    HWND hVolStyle, hVolSize;
    HWND hPacker, hPackerBrowse, hPackerInfo;
    HWND hPayloadCrc, hSelfDelete, hCleanMode;
    HWND hLogFileChk, hLogFile, hLogFileBrowse;
    std::wstring packerOverride;
    HFONT hFont, hMono, hCfgFont;
    std::vector<HWND> ctls;
    int page;
    std::wstring cfgIni;
};

static App g;
static HMODULE g_hRich = nullptr;
static int g_cfgPt = 12;
static const int EDIT_H = 24;

static std::wstring AppTitle()
{
    return std::wstring(APP_NAME_W) + L" " + APP_VERSION_W;
}

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
static void LoadProfileFrom(const std::wstring& path);
static void SaveProfileTo(const std::wstring& path);
static void SetPhase(int phase);
static void PostProgress(int pct);

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

static volatile LONG g_cancel = 0;          // 1 = запрошена отмена сборки
static HANDLE g_childProc = nullptr;        // дочерний 7z.exe (для TerminateProcess)

static int RunProcess(const std::wstring& cmdLine, const std::wstring& workDir)
{
    if (g_cancel) return -2;
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
    // дескриптор процесса оставляем открытым до конца сборки: UI может его завершить
    g_childProc = pi.hProcess;
    CloseHandle(pi.hThread);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (g_cancel) return -2;
    return (int)code;
}

static void PostProgress(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    // диагностика: если задан SFXPRESS_PROGRESS_TRACE=<файл>, пишем значения туда
    static FILE* trace = nullptr;
    static bool traceInit = false;
    if (!traceInit) {
        traceInit = true;
        wchar_t path[MAX_PATH * 2];
        DWORD n = GetEnvironmentVariableW(L"SFXPRESS_PROGRESS_TRACE", path, MAX_PATH * 2);
        if (n > 0 && n < MAX_PATH * 2) trace = _wfopen(path, L"a");
    }
    if (trace) {
        fprintf(trace, "%d\n", pct);
        fflush(trace);
    }

    if (g.hWnd) PostMessageW(g.hWnd, WM_APP + 4, (WPARAM)pct, 0);
}

// PBS_MARQUEE makes PBM_SETPOS a no-op, so the style is toggled at runtime:
// marquee = style on, determinate = style off + SETPOS.
static void ProgressMarquee(HWND h)
{
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if (!(st & PBS_MARQUEE)) SetWindowLongW(h, GWL_STYLE, st | PBS_MARQUEE);
    SendMessageW(h, PBM_SETMARQUEE, TRUE, 30);
    InvalidateRect(h, nullptr, TRUE);
}

static void ProgressDeterminate(HWND h, int pct)
{
    LONG st = GetWindowLongW(h, GWL_STYLE);
    if (st & PBS_MARQUEE) {
        SendMessageW(h, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongW(h, GWL_STYLE, st & ~PBS_MARQUEE);
        SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    SendMessageW(h, PBM_SETRANGE32, 0, 100);
    SendMessageW(h, PBM_SETPOS, (WPARAM)pct, 0);
    InvalidateRect(h, nullptr, TRUE);
}

// Запуск процесса с перехватом вывода: проценты 7z (" 12%") превращаются
// в общий прогресс сборки в диапазоне fromPct..toPct.
static int RunProcessEx(const std::wstring& cmdLine, const std::wstring& workDir, int fromPct, int toPct)
{
    if (g_cancel) return -2;

    HANDLE hRead = nullptr, hWrite = nullptr;
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0))
        return RunProcess(cmdLine, workDir);
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;
    si.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &sa, OPEN_EXISTING, 0, nullptr);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));
    std::wstring cl = cmdLine;
    std::vector<wchar_t> buf(cl.begin(), cl.end());
    buf.push_back(0);
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                             workDir.empty() ? nullptr : workDir.c_str(), &si, &pi);
    if (si.hStdInput != nullptr && si.hStdInput != INVALID_HANDLE_VALUE) CloseHandle(si.hStdInput);
    CloseHandle(hWrite);
    if (!ok) {
        CloseHandle(hRead);
        return -1;
    }

    g_childProc = pi.hProcess;
    CloseHandle(pi.hThread);

    std::string acc;
    char tmp[4096];
    DWORD rd = 0;
    int lastPct = -1;

    for (;;) {
        DWORD avail = 0;
        if (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD toRead = avail > (DWORD)sizeof(tmp) ? (DWORD)sizeof(tmp) : avail;
            if (!ReadFile(hRead, tmp, toRead, &rd, nullptr) || rd == 0) break;
            acc.append(tmp, rd);

            for (size_t i = 0; i < acc.size(); i++) {
                if (acc[i] != '%') continue;
                size_t j = i;
                while (j > 0 && acc[j - 1] >= '0' && acc[j - 1] <= '9') j--;
                if (j == i) continue;
                int v = atoi(acc.substr(j, i - j).c_str());
                if (v < 0 || v > 100 || v == lastPct) continue;
                lastPct = v;
                PostProgress(fromPct + (toPct - fromPct) * v / 100);
            }
            if (acc.size() > 8192) acc.erase(0, acc.size() - 1024);
            continue;
        }

        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            // дочитываем хвост вывода, чтобы 7z не заблокировался на полном пайпе
            while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                DWORD toRead = avail > (DWORD)sizeof(tmp) ? (DWORD)sizeof(tmp) : avail;
                if (!ReadFile(hRead, tmp, toRead, &rd, nullptr) || rd == 0) break;
            }
            break;
        }
    }
    CloseHandle(hRead);

    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    if (g_cancel) return -2;
    return (int)code;
}

static bool FileExists(const std::wstring& path)
{
    return !path.empty() && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool IsChecked(HWND h)
{
    return h != nullptr && SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

// --- CRC32 (совместим с CRC_INIT_VAL / CRC_GET_DIGEST модуля 7zSFX) ---
static unsigned long Crc32Update(unsigned long crc, const void* data, size_t len)
{
    static unsigned long table[256];
    static bool ready = false;
    if (!ready) {
        for (unsigned long i = 0; i < 256; i++) {
            unsigned long c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320ul ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    const unsigned char* p = (const unsigned char*)data;
    unsigned long c = crc;
    for (size_t i = 0; i < len; i++) c = (c >> 8) ^ table[(c ^ p[i]) & 0xFF];
    return c;
}

// CRC32 по набору файлов подряд (для многотомной сборки — все тома по порядку).
// fromPct/toPct задают отображение хода чтения на общей полосе сборки.
static bool Crc32Files(const std::vector<std::wstring>& files, unsigned long* out,
                       int fromPct = 0, int toPct = 0)
{
    unsigned long long total = 0;
    for (const std::wstring& path : files) total += FileSizeOf(path);

    unsigned long c = 0xFFFFFFFFul;
    unsigned long long done = 0;
    std::vector<unsigned char> buf(1 << 20);
    for (const std::wstring& path : files) {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return false;
        size_t n;
        while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) {
            c = Crc32Update(c, buf.data(), n);
            done += n;
            if (total && toPct > fromPct)
                PostProgress(fromPct + (int)((unsigned long long)(toPct - fromPct) * done / total));
        }
        fclose(f);
    }
    *out = c ^ 0xFFFFFFFFul;
    return true;
}

static bool Crc32File(const std::wstring& path, unsigned long* out, int fromPct = 0, int toPct = 0)
{
    std::vector<std::wstring> one(1, path);
    return Crc32Files(one, out, fromPct, toPct);
}

// Версия из ресурса exe (например, "26.06").
static std::wstring PackerVersionText(const std::wstring& exe)
{
    if (exe.empty() || !FileExists(exe)) return std::wstring();
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &dummy);
    if (size == 0) return std::wstring();
    std::vector<BYTE> buf(size);
    if (!GetFileVersionInfoW(exe.c_str(), 0, size, buf.data())) return std::wstring();
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(buf.data(), L"\\", (LPVOID*)&fi, &len) || fi == nullptr) return std::wstring();
    wchar_t s[64];
    swprintf(s, 64, L"%u.%02u", (unsigned)HIWORD(fi->dwProductVersionMS), (unsigned)LOWORD(fi->dwProductVersionMS));
    return s;
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
    if (!g.packerOverride.empty() && FileExists(g.packerOverride)) return g.packerOverride;
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

static void UpdatePackerInfo()
{
    std::wstring p = FindPacker();
    if (p.empty()) {
        SetText(g.hPacker, L"");
        SetText(g.hPackerInfo, L"Не найден: положите 7z.exe (или 7za.exe) в папку Bin рядом с SfxPress\r\n"
                               L"или выберите файл вручную кнопкой «...».");
        return;
    }
    SetText(g.hPacker, ToPortablePath(p));
    std::wstring v = PackerVersionText(p);
    std::wstring t = L"7-Zip " + (v.empty() ? std::wstring(L"(версия не определена)") : v);
    if (g.packerOverride.empty())
        t += L" — найден автоматически";
    else
        t += L" — выбран вручную";
    t += L"\r\n" + p;
    SetText(g.hPackerInfo, t);
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

// Ограничение словаря для LZMA/LZMA2. Модифицированный SFX-модуль не понимает
// архивы, которые 7-Zip 26.x сам создаёт для крупных данных (-mx=9 даёт
// LZMA:166m / LZMA2:192m -> "Метод не поддерживается"). Фиксируем словарь,
// который модуль гарантированно распаковывает. Deflate/PPMd словарь не имеют.
static std::wstring MethodDictParam(int method)
{
    switch (method) {
    case 1: case 2: case 5: return L" -md=32m";
    default: return std::wstring();
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

// Упаковка папки в несколько томов: 7z a -v<N>m base.7z -> base.7z.001, .002, ...
// archiveBase — абсолютный путь без расширения (base => base.7z.001...).
// Выходные тома кладутся в ту же папку, что и base, и возвращаются в vols (полные пути).
static bool PackFolderVolumes(const std::wstring& folder, const std::wstring& archiveBase,
                              int method, const std::wstring& password, int volSizeMB,
                              std::vector<std::wstring>* vols, std::wstring* err)
{
    std::wstring packer = FindPacker();
    if (packer.empty()) {
        *err = L"Не найден архиватор: положите 7z.exe (или 7za.exe) в папку Bin рядом с конструктором.";
        return false;
    }
    std::wstring pattern = folder;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L"*";
    std::wstring cmd = L"\"" + packer + L"\" a -t7z -m0=" + MethodCodec(method) + MethodDictParam(method) + L" -mx=9 -y -bsp1 ";
    if (!password.empty()) cmd += L"\"-p" + password + L"\" -mhe=on ";
    cmd += L"-v" + std::to_wstring(volSizeMB) + L"m \"" + archiveBase + L".7z\" \"" + pattern + L"\"";
    int rc = RunProcessEx(cmd, folder, 0, 70);
    if (rc != 0) {
        *err = L"7z завершился с кодом " + std::to_wstring(rc);
        return false;
    }
    std::wstring pat = archiveBase + L".7z.*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        *err = L"7z не создал тома.";
        return false;
    }
    // полный путь = каталогу archiveBase + имя найденного файла
    size_t slash = archiveBase.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? std::wstring() : archiveBase.substr(0, slash + 1);
    do { vols->push_back(dir + fd.cFileName); } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(vols->begin(), vols->end());
    if (vols->size() < 2) { *err = L"Папка поместилась в один том: для разбиения задайте меньший объём."; return false; }
    return true;
}

// Копирование продолжений томов рядом с результатом (стиль 1).
// out = C:\path\app.exe (уже содержит первый том); продолжения называются app.001, app.002...
// (нумерация с 1, потому что первый кусок уже в exe). Возвращает созданные файлы.
static std::vector<std::wstring> GlueVolumedContinuations(const std::wstring& out,
                                                          const std::vector<std::wstring>& vols)
{
    std::vector<std::wstring> continuations;
    size_t slash = out.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return continuations;
    std::wstring dir = out.substr(0, slash + 1);
    std::wstring stem = out.substr(slash + 1);
    size_t dot = stem.find_last_of(L'.');
    if (dot != std::wstring::npos) stem.erase(dot);
    for (size_t i = 1; i < vols.size(); i++) {
        wchar_t num[8];
        swprintf(num, 8, L"%03d", (int)i);
        std::wstring dst = dir + stem + L"." + num;
        if (CopyFileW(vols[i].c_str(), dst.c_str(), FALSE))
            continuations.push_back(dst);
    }
    return continuations;
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
    std::wstring cmd = L"\"" + packer + L"\" a -t7z -m0=" + MethodCodec(method) + MethodDictParam(method) + L" -mx=9 -y -bsp1 ";
    if (!password.empty()) cmd += L"\"-p" + password + L"\" -mhe=on ";
    cmd += L"\"" + archive + L"\"" + L" \"" + pattern + L"\"";
    int rc = RunProcessEx(cmd, folder, 0, 70);
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
static std::wstring BuildConfigText(std::wstring* err, unsigned long payloadCrc = 0)
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
    // Archive is only for a multi-volume build: the volume set lies next to the exe.
    // For a single-file SFX the archive is embedded, and a stray Archive= makes the
    // module look for an external payload.7z and abort with code 2.
    if (ComboValue(g.hVolStyle) > 0) add(L"Archive", g.hArchive);    add(L"RunProgram", g.hRunProgram);
    add(L"Delete", g.hDelete);

    {
        std::wstring del = GetText(g.hDelete);
        if (del == L"%%T") {
            if (err) *err = L"Delete: одного %%T мало — укажите файл, например %%T\\\\file.tmp.";
        }
    }

    if (IsChecked(g.hSelfDelete)) {
        c += L"SelfDelete=\"1\"\r\n";
    }

    std::wstring cm = GetText(g.hCleanMode);
    if (!cm.empty()) {
        bool digits = true;
        for (size_t i = 0; i < cm.size(); i++)
            if (cm[i] < L'0' || cm[i] > L'9') digits = false;
        int cmv = digits ? _wtoi(cm.c_str()) : -1;
        if (!digits || cmv < 0 || cmv > 15) {
            if (err) *err = L"CleanMode должен быть числом 0..15 (биты 1/2/4/8).";
        } else if (cmv > 0) {
            swprintf(num, 16, L"%d", cmv);
            c += L"CleanMode=\"";
            c += num;
            c += L"\"\r\n";
        }
    }

    int si = ComboValue(g.hSingleInstance);
    if (si > 0) {
        swprintf(num, 16, L"%d", si);
        c += L"SingleInstance=\"";
        c += num;
        c += L"\"\r\n";
    }

    if (IsChecked(g.hLogFileChk)) {
        std::wstring lf = GetText(g.hLogFile);
        if (!lf.empty()) {
            c += L"LogFile=\"";
            c += EscapeValue(lf);
            c += L"\"\r\n";
        }
    }

    int vs = ComboValue(g.hVolStyle);
    if (vs > 0) {
        swprintf(num, 16, L"%d", vs);
        c += L"VolumeNameStyle=\"";
        c += num;
        c += L"\"\r\n";
    }

    add(L"BeginPrompt", g.hBeginPrompt);
    add(L"Password", g.hPassword);
    add(L"SfxAuthor", g.hSfxAuthor);
    add(L"PreExtract", g.hPreExtract);
    add(L"ExecuteOnLoad", g.hExecuteOnLoad);

    if (payloadCrc != 0) {
        swprintf(num, 16, L"0x%08X", payloadCrc);
        c += L"PayloadCRC=\"";
        c += num;
        c += L"\"\r\n";
    }

    c += L";!@InstallEnd@!\r\n";

    if (si > 0 && GetText(g.hInstallPath).empty()) {
        if (err) *err = L"SingleInstance требует заданного InstallPath.";
        return c;
    }

    if (c.size() > 2 * 1024 * 1024) {
        if (err) *err = L"Конфигурация больше 2 МБ: модуль не найдёт ни её, ни архив.";
    }
    return c;
}

// ---- syntax highlighting for the config view (RichEdit only) ----
// light palette in the spirit of GitHub Light: every role gets its own hue and
// weight, SFX variables are italic so they never read as cmd commands
static const COLORREF kClrText    = RGB(31, 35, 40);
static const COLORREF kClrComment = RGB(110, 119, 129);
static const COLORREF kClrSection = RGB(102, 57, 186);
static const COLORREF kClrKey     = RGB(5, 80, 174);
static const COLORREF kClrString  = RGB(10, 48, 105);
static const COLORREF kClrVar     = RGB(149, 56, 0);
static const COLORREF kClrSwitch  = RGB(130, 80, 223);
static const COLORREF kClrOp      = RGB(87, 96, 106);
static const COLORREF kClrKeyword = RGB(207, 34, 46);

static bool IsRichConfigView(HWND h)
{
    if (!g_hRich || !h) return false;
    wchar_t cls[32];
    if (!GetClassNameW(h, cls, 32)) return false;
    return _wcsicmp(cls, L"EDIT") != 0;
}

static void ColorRange(HWND h, int start, int end, COLORREF color, bool bold, bool italic = false)
{
    if (end <= start) return;
    CHARRANGE cr;
    cr.cpMin = start;
    cr.cpMax = end;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&cr);
    CHARFORMAT2W cf;
    ZeroMemory(&cf, sizeof(cf));
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC;
    cf.crTextColor = color;
    cf.dwEffects = (bold ? CFE_BOLD : 0) | (italic ? CFE_ITALIC : 0);
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static bool IsCmdKey(const std::wstring& key)
{
    return _wcsicmp(key.c_str(), L"RunProgram") == 0 ||
           _wcsicmp(key.c_str(), L"PreExtract") == 0 ||
           _wcsicmp(key.c_str(), L"ExecuteOnLoad") == 0 ||
           _wcsicmp(key.c_str(), L"Delete") == 0;
}

static bool IsCmdKeyword(const std::wstring& w)
{
    static const wchar_t* k[] = {
        L"echo", L"if", L"exist", L"not", L"errorlevel", L"else", L"goto", L"call",
        L"start", L"set", L"setlocal", L"endlocal", L"for", L"in", L"do", L"del",
        L"erase", L"copy", L"xcopy", L"move", L"ren", L"rename", L"md", L"mkdir",
        L"rd", L"rmdir", L"cd", L"chdir", L"pushd", L"popd", L"exit", L"type",
        L"attrib", L"reg", L"taskkill", L"msiexec", L"cmd", L"pause", L"choice"
    };
    for (size_t i = 0; i < sizeof(k) / sizeof(k[0]); i++)
        if (_wcsicmp(w.c_str(), k[i]) == 0) return true;
    return false;
}

static void HighlightCmd(HWND h, const std::wstring& s, int base, int from, int to)
{
    int i = from;
    while (i < to) {
        wchar_t ch = s[i];
        if (iswspace(ch)) { i++; continue; }
        if (ch == L'"') {
            int j = i + 1;
            while (j < to) {
                if (s[j] == L'"') {
                    if (j + 1 < to && s[j + 1] == L'"') { j += 2; continue; }
                    break;
                }
                j++;
            }
            ColorRange(h, base + i, base + (j < to ? j + 1 : to), kClrString, false);
            i = (j < to) ? j + 1 : to;
            continue;
        }
        if (ch == L'%') {
            int j = i + 1;
            while (j < to && s[j] != L'%') j++;
            ColorRange(h, base + i, base + (j < to ? j + 1 : to), kClrVar, false, true);
            i = (j < to) ? j + 1 : to;
            continue;
        }
        if ((ch == L'/' || ch == L'-') && i + 1 < to && (iswalnum(s[i + 1]) || s[i + 1] == L'?')) {
            int j = i + 1;
            while (j < to && !iswspace(s[j]) && s[j] != L'"') j++;
            ColorRange(h, base + i, base + j, kClrSwitch, false);
            i = j;
            continue;
        }
        if (ch == L'&' || ch == L'|' || ch == L'>' || ch == L'<') {
            int j = i + 1;
            while (j < to && (s[j] == L'&' || s[j] == L'|' || s[j] == L'>' || s[j] == L'<')) j++;
            ColorRange(h, base + i, base + j, kClrOp, true);
            i = j;
            continue;
        }
        if (iswalpha(ch) || ch == L'_') {
            int j = i;
            while (j < to && (iswalnum(s[j]) || s[j] == L'_' || s[j] == L'.' || s[j] == L'-')) j++;
            std::wstring w(s, i, j - i);
            if (IsCmdKeyword(w)) ColorRange(h, base + i, base + j, kClrKeyword, true);
            i = j;
            continue;
        }
        i++;
    }
}

static void HighlightConfig(HWND h)
{
    if (!IsRichConfigView(h)) return;
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return;
    std::wstring raw((size_t)len + 1, L'\0');
    GetWindowTextW(h, &raw[0], len + 1);
    raw.resize((size_t)len);

    // RichEdit keeps a paragraph break as a single CR, but WM_GETTEXT returns CRLF.
    // Character positions used by EM_EXSETSEL are counted on the CR form, so the
    // offsets must be computed on the normalized copy, otherwise colors drift.
    std::wstring s;
    s.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] == L'\r' && i + 1 < raw.size() && raw[i + 1] == L'\n') continue;
        s.push_back(raw[i]);
    }
    int n = (int)s.size();
    if (n <= 0) return;

    POINTL scroll;
    scroll.x = 0;
    scroll.y = 0;
    SendMessageW(h, EM_GETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(h, WM_SETREDRAW, FALSE, 0);

    CHARRANGE all;
    all.cpMin = 0;
    all.cpMax = n;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&all);
    CHARFORMAT2W def;
    ZeroMemory(&def, sizeof(def));
    def.cbSize = sizeof(def);
    def.dwMask = CFM_COLOR | CFM_BOLD | CFM_SIZE | CFM_FACE | CFM_CHARSET;
    def.crTextColor = kClrText;
    def.dwEffects = 0;
    def.yHeight = g_cfgPt * 20;
    def.bCharSet = DEFAULT_CHARSET;
    def.bPitchAndFamily = FIXED_PITCH | FF_MODERN;
    wcscpy_s(def.szFaceName, L"Consolas");
    SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&def);

    int lineStart = 0;
    while (lineStart <= n) {
        int lineEnd = lineStart;
        while (lineEnd < n && s[lineEnd] != L'\r' && s[lineEnd] != L'\n') lineEnd++;

        int b = lineStart;
        int e = lineEnd;
        while (b < e && iswspace(s[b])) b++;
        while (e > b && iswspace(s[e - 1])) e--;

        if (b < e) {
            if (s[b] == L';') {
                ColorRange(h, b, e, kClrComment, false, true);
            } else if (s[b] == L'[' && s[e - 1] == L']') {
                ColorRange(h, b, e, kClrSection, true);
            } else {
                int eq = -1;
                for (int i = b; i < e; i++) {
                    if (s[i] == L'=') { eq = i; break; }
                    if (s[i] == L';') break;
                }
                if (eq > b) {
                    ColorRange(h, b, eq, kClrKey, true);
                    int vb = eq + 1;
                    while (vb < e && iswspace(s[vb])) vb++;
                    if (vb < e && s[vb] == L'"') {
                        int ve = e - 1;
                        while (ve > vb && s[ve] != L'"') ve--;
                        ColorRange(h, vb, ve + 1, kClrString, false);
                        std::wstring key(s, b, (size_t)(eq - b));
                        if (IsCmdKey(key)) HighlightCmd(h, s, 0, vb + 1, ve);
                    }
                }
            }
        }

        if (lineEnd >= n) break;
        lineStart = lineEnd + 1;
    }

    all.cpMin = 0;
    all.cpMax = 0;
    SendMessageW(h, EM_EXSETSEL, 0, (LPARAM)&all);
    SendMessageW(h, EM_SETSCROLLPOS, 0, (LPARAM)&scroll);
    SendMessageW(h, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(h, nullptr, TRUE);
}

// font size for the config view: Ctrl+plus / Ctrl+minus / Ctrl+0
static void CfgSetFontSize(int pt)
{
    if (pt < 8) pt = 8;
    if (pt > 40) pt = 40;
    if (pt == g_cfgPt && g.hCfgFont) return;
    g_cfgPt = pt;

    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(g_cfgPt, 96, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    HFONT fresh = CreateFontIndirectW(&lf);
    HFONT old = g.hCfgFont;
    g.hCfgFont = fresh;
    if (g.hCfgView) {
        SendMessageW(g.hCfgView, WM_SETFONT, (WPARAM)g.hCfgFont, TRUE);
        HighlightConfig(g.hCfgView);
    }
    if (old) DeleteObject(old);
}

static LRESULT CALLBACK CfgViewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
{
    switch (msg) {
    case WM_KEYDOWN:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            if (wp == VK_OEM_PLUS || wp == VK_ADD) { CfgSetFontSize(g_cfgPt + 1); return 0; }
            if (wp == VK_OEM_MINUS || wp == VK_SUBTRACT) { CfgSetFontSize(g_cfgPt - 1); return 0; }
            if (wp == L'0' || wp == VK_NUMPAD0) { CfgSetFontSize(12); return 0; }
        }
        break;
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) & 0x8000) {
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            CfgSetFontSize(g_cfgPt + (delta > 0 ? 1 : -1));
            return 0;
        }
        break;
    case WM_CONTEXTMENU: {
        HMENU m = CreatePopupMenu();
        if (!m) break;
        CHARRANGE cr;
        SendMessageW(h, EM_EXGETSEL, 0, (LPARAM)&cr);
        BOOL hasSel = cr.cpMax > cr.cpMin;
        AppendMenuW(m, MF_STRING | (hasSel ? MF_ENABLED : MF_GRAYED), 1, L"Копировать");
        AppendMenuW(m, MF_STRING, 2, L"Выделить всё");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 3, L"Шрифт крупнее (Ctrl++)");
        AppendMenuW(m, MF_STRING, 4, L"Шрифт мельче (Ctrl+-)");
        AppendMenuW(m, MF_STRING, 5, L"Шрифт 12 pt (Ctrl+0)");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, 6, L"Очистить окно");
        POINT pt;
        pt.x = (short)LOWORD(lp);
        pt.y = (short)HIWORD(lp);
        if (pt.x == -1 && pt.y == -1) {
            RECT rc;
            GetWindowRect(h, &rc);
            pt.x = rc.left + 20;
            pt.y = rc.top + 20;
        }
        int cmd = (int)TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, h, nullptr);
        DestroyMenu(m);
        if (cmd == 1) SendMessageW(h, WM_COPY, 0, 0);
        else if (cmd == 2) SendMessageW(h, EM_SETSEL, 0, -1);
        else if (cmd == 3) CfgSetFontSize(g_cfgPt + 1);
        else if (cmd == 4) CfgSetFontSize(g_cfgPt - 1);
        else if (cmd == 5) CfgSetFontSize(12);
        else if (cmd == 6) {
            // clear the text inside the editor; the editor itself stays in place
            SendMessageW(h, EM_SETREADONLY, FALSE, 0);
            SendMessageW(h, EM_SETSEL, 0, -1);
            SendMessageW(h, EM_REPLACESEL, FALSE, (LPARAM)L"");
            SendMessageW(h, EM_SETREADONLY, TRUE, 0);
            SendMessageW(h, EM_SETSEL, 0, 0);
            // leftover char format made the caret slanted (looked like a stray "\"): reset it
            CHARFORMAT2W cf;
            ZeroMemory(&cf, sizeof(cf));
            cf.cbSize = sizeof(cf);
            cf.dwMask = CFM_COLOR | CFM_BOLD | CFM_ITALIC;
            cf.crTextColor = kClrText;
            cf.dwEffects = 0;
            SendMessageW(h, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
            ShowWindow(h, SW_SHOW);
            InvalidateRect(h, nullptr, TRUE);
        }
        return 0;
    }
    }
    return DefSubclassProc(h, msg, wp, lp);
}

// reflow: tab and config view grow, the log and the button row stay pinned to the bottom
static volatile LONG g_busy = 0; // 1 while a build thread is running

static void LayoutUi(HWND hWnd)
{
    if (!hWnd) return;
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right;
    int h = rc.bottom;
    if (w <= 0 || h <= 0) return;

    if (g.hStatus) SendMessageW(g.hStatus, WM_SIZE, 0, 0);
    int statusH = 22;
    if (g.hStatus) {
        RECT rs;
        GetWindowRect(g.hStatus, &rs);
        statusH = rs.bottom - rs.top;
    }

    int progressY = h - statusH - 8;
    if (progressY < 0) progressY = 0;
    // push the strip to the bottom of the z-order so it can never paint over the buttons
    if (g.hProgress) SetWindowPos(g.hProgress, HWND_BOTTOM, 4, progressY, w - 8, 6, 0);
    // idle: the strip must never be on screen while nothing is being built
    if (g.hProgress && !g_busy) ShowWindow(g.hProgress, SW_HIDE);

    // keep a clear gap between the button row and the progress strip
    int buttonsY = progressY - 6 - 10 - 32;
    const int logH = 118;
    int logY = buttonsY - 10 - logH;
    if (logY < 240) logY = 240;
    int logLabelY = logY - 20;
    int tabH = logLabelY - 16;
    if (tabH < 220) tabH = 220;

    if (g.hTab) SetWindowPos(g.hTab, nullptr, 10, 8, w - 20, tabH, SWP_NOZORDER);
    if (g.hCfgView) SetWindowPos(g.hCfgView, nullptr, 26, 44, w - 52, tabH - 64, SWP_NOZORDER);
    if (g.hLogLabel) SetWindowPos(g.hLogLabel, nullptr, 26, logLabelY, 120, 18, SWP_NOZORDER);
    if (g.hLog) SetWindowPos(g.hLog, nullptr, 26, logY, w - 52, logH, SWP_NOZORDER);

    struct { int id; int x; int w; } btns[] = {
        { ID_SAVE_CFG, 26, 108 }, { ID_BUILD, 142, 108 }, { ID_VERIFY, 258, 108 },
        { ID_RUN, 374, 108 }, { ID_OPENFOLDER, 490, 124 }, { ID_LOG_CLEAR, 622, 140 }
    };
    for (int i = 0; i < 6; i++) {
        HWND b = GetDlgItem(hWnd, btns[i].id);
        if (b) SetWindowPos(b, nullptr, btns[i].x, buttonsY, btns[i].w, 32, SWP_NOZORDER);
    }

    if (g.hStatus) {
        int parts[2] = { w - 150, -1 };
        SendMessageW(g.hStatus, SB_SETPARTS, 2, (LPARAM)parts);
    }

    // controls were moved: repaint the window and every child, otherwise the
    // old pixels stay behind (blank owner-draw buttons, leftover strip)
    RedrawWindow(hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// non-empty while the config of an already built SFX is shown instead of the generated one
static std::wstring g_viewSrc;

static void RefreshConfigView()
{
    if (!g_viewSrc.empty()) return; // читаем конфиг готового SFX, генерируемый не трогаем
    std::wstring err;
    unsigned long crc = 0;
    bool wantCrc = IsChecked(g.hPayloadCrc);
    if (wantCrc) {
        std::wstring p = ResolvePath(GetText(g.hPayload));
        if (!p.empty() && !IsDirectory(p) && FileExists(p)) {
            if (!Crc32File(p, &crc)) crc = 0;
        }
    }
    std::wstring c = BuildConfigText(&err, crc);
    if (wantCrc && crc == 0) {
        size_t pos = c.find(L";!@InstallEnd@!");
        if (pos != std::wstring::npos)
            c.insert(pos, L"; PayloadCRC будет вычислен из упакованного архива при сборке\r\n");
    }
    SetText(g.hCfgView, c);
    HighlightConfig(g.hCfgView);
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

static unsigned long long g_copyDone = 0;
static unsigned long long g_copyTotal = 0;

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
        g_copyDone += n;
        if (g_copyTotal)
            PostProgress(85 + (int)((unsigned long long)15 * g_copyDone / g_copyTotal));
    }
    fclose(in);
    return true;
}

// A finished 7zSFX file is [module][config text][7z archive]. The module is a DLL that also
// carries a copy of the 7z signature and of the marker strings, so everything is searched
// backwards: the real configuration sits closest to the archive.

static size_t FindLastSig(const std::vector<unsigned char>& buf, size_t limit)
{
    static const unsigned char sig[6] = { 0x37, 0x7A, 0xBC, 0xAF, 0x27, 0x1C };
    if (limit > buf.size()) limit = buf.size();
    size_t found = (size_t)-1;
    for (size_t i = 0; i + 6 <= limit; i++)
        if (memcmp(&buf[i], sig, 6) == 0) found = i;
    return found;
}

static unsigned long long FindArchiveOffset(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return ULLONG_MAX;
    std::vector<unsigned char> buf(2 << 20);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    buf.resize(n);
    size_t pos = FindLastSig(buf, n);
    return pos == (size_t)-1 ? ULLONG_MAX : (unsigned long long)pos;
}

// ---- reading the configuration back out of a finished SFX ----

static bool ReadFileHead(const std::wstring& path, size_t maxBytes,
                         std::vector<unsigned char>& buf, std::wstring& err)
{
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) {
        err = L"Не удалось открыть файл: " + path;
        return false;
    }
    buf.resize(maxBytes);
    size_t n = fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    buf.resize(n);
    if (n == 0) {
        err = L"Файл пуст: " + path;
        return false;
    }
    return true;
}

static size_t FindBytes(const std::vector<unsigned char>& hay, const char* needle, size_t from = 0)
{
    size_t len = strlen(needle);
    if (len == 0 || hay.size() < len) return (size_t)-1;
    for (size_t i = from; i + len <= hay.size(); i++)
        if (memcmp(&hay[i], needle, len) == 0) return i;
    return (size_t)-1;
}

// last occurrence that starts before `limit`: the module holds stray copies of the markers
static size_t FindBytesBack(const std::vector<unsigned char>& hay, const char* needle, size_t limit)
{
    size_t len = strlen(needle);
    if (len == 0 || hay.size() < len) return (size_t)-1;
    if (limit > hay.size() - len + 1) limit = hay.size() - len + 1;
    size_t found = (size_t)-1;
    for (size_t i = 0; i < limit; i++)
        if (memcmp(&hay[i], needle, len) == 0) found = i;
    return found;
}

// UTF-8 first, then CP1251 for old ANSI builds
static bool DecodeTextBlock(const unsigned char* p, size_t n, bool utf8Only, std::wstring& out)
{
    if (n == 0) return false;
    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)p, (int)n, nullptr, 0);
    if (need > 0) {
        out.resize(need);
        MultiByteToWideChar(CP_UTF8, 0, (const char*)p, (int)n, &out[0], need);
        return true;
    }
    if (utf8Only) return false;
    need = MultiByteToWideChar(1251, 0, (const char*)p, (int)n, nullptr, 0);
    if (need <= 0) return false;
    out.resize(need);
    MultiByteToWideChar(1251, 0, (const char*)p, (int)n, &out[0], need);
    return true;
}

static bool ExtractSfxConfig(const std::wstring& path, std::wstring& cfg,
                             std::wstring& err, std::wstring* how = nullptr)
{
    std::vector<unsigned char> buf;
    if (!ReadFileHead(path, 4u << 20, buf, err)) return false;

    size_t scanLimit = buf.size() < (2u << 20) ? buf.size() : (2u << 20);
    const char* endMarker = ";!@InstallEnd@!";
    const char* markers[] = { ";!@Install@!UTF-8!", ";!@Install@!ANSI!" };

    size_t e = FindBytesBack(buf, endMarker, scanLimit);
    if (e != (size_t)-1) {
        for (const char* sm : markers) {
            size_t b = FindBytesBack(buf, sm, e);
            if (b == (size_t)-1) continue;
            size_t len = e + strlen(endMarker) - b;
            bool utf8 = (strstr(sm, "UTF-8") != nullptr);
            std::wstring text;
            if (!DecodeTextBlock(&buf[b], len, utf8, text)) continue;
            if (text.find(L'=') == std::wstring::npos) continue; // the markers alone, no parameters
            cfg = text;
            if (how) *how = utf8 ? L"маркер ;!@Install@!UTF-8!" : L"маркер ;!@Install@!ANSI! (CP1251)";
            return true;
        }
    }

    // no usable markers: the text block right before the archive signature
    size_t sigPos = FindLastSig(buf, scanLimit == buf.size() ? buf.size() : scanLimit);
    if (sigPos == (size_t)-1) {
        err = L"Маркеры конфигурации не найдены, сигнатуры 7z в первых 2 МБ нет — это не 7zSFX-инсталлятор.";
        return false;
    }
    size_t b = sigPos;
    while (b > 0 && buf[b - 1] != 0) b--;
    size_t n = sigPos - b;
    if (n < 8) {
        err = L"Перед архивом нет текста конфигурации.";
        return false;
    }
    std::wstring text;
    if (!DecodeTextBlock(&buf[b], n, true, text) && !DecodeTextBlock(&buf[b], n, false, text)) {
        err = L"Блок перед архивом не похож на текст конфигурации.";
        return false;
    }
    if (text.find(L'=') == std::wstring::npos) {
        err = L"Блок перед архивом не содержит параметров конфигурации.";
        return false;
    }
    cfg = text;
    if (how) *how = L"текст перед сигнатурой 7z (маркеров нет)";
    return true;
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
    std::vector<std::wstring> vols;          // тома при многотомной сборке
    bool volumed = false;
    int volSizeMB = _wtoi(GetText(g.hVolSize).c_str());
    if (volSizeMB < 0) volSizeMB = 0;
    if (volSizeMB > 4096) {
        Log(L"[ошибка] Объём тома больше 4096 МБ — уменьшите значение.");
        SetStatus(L"Неверный объём тома");
        return;
    }

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
        if (volSizeMB > 0) {
            // многотомная сборка: тома во временной папке, первый склеивается в exe
            std::wstring workBase = TempArchiveName();      // ...\sfxctor_<pid>.7z
            workBase.erase(workBase.size() - 3);            // убрать ".7z"
            workBase += L"_v";
            std::wstring perr;
            SetPhase(1);
            SetStatus(L"Упаковка папки в тома...");
            Log(L"[тома] папка: " + payloadSrc + L", том " + std::to_wstring(volSizeMB) + L" МБ");
            if (!PackFolderVolumes(payloadSrc, workBase, method, pwd, volSizeMB, &vols, &perr)) {
                if (g_cancel) {
                    Log(L"[сборка] отменена.");
                    SetStatus(L"Сборка отменена");
                    return;
                }
                Log(L"[ошибка] " + perr);
                SetStatus(L"Ошибка упаковки томов");
                return;
            }
            payload = vols[0];
            volumed = true;
            // используем vols[0] как первый том для склейки
        } else {
            tempArchive = TempArchiveName();
            DeleteFileW(tempArchive.c_str());
            SetPhase(1);
            SetStatus(L"Упаковка папки...");
            Log(L"[упаковка] " + payloadSrc);
            std::wstring perr;
            if (!pwd.empty()) Log(L"  архив шифруется паролем (заголовки тоже)");
            if (!PackFolder(payloadSrc, tempArchive, method, pwd, &perr)) {
                if (g_cancel) {
                    Log(L"[сборка] отменена.");
                    SetStatus(L"Сборка отменена");
                    DeleteFileW(tempArchive.c_str());
                    return;
                }
                Log(L"[ошибка] " + perr);
                SetStatus(L"Ошибка упаковки");
                return;
            }
            payload = tempArchive;
            Log(L"  архив: " + FormatSize(FileSizeOf(tempArchive)) + L"  " + tempArchive);
        }
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

    SetPhase(2);
    if (IsChecked(g.hPayloadCrc)) {
        unsigned long payloadCrc = 0;
        bool crcOk = volumed ? Crc32Files(vols, &payloadCrc, 70, 85) : Crc32File(payload, &payloadCrc, 70, 85);
        if (!crcOk || payloadCrc == 0) {
            Log(L"[ошибка] Не удалось вычислить PayloadCRC.");
            SetStatus(L"Ошибка вычисления CRC");
            return;
        }
        std::wstring crcErr;
        cfg = BuildConfigText(&crcErr, payloadCrc);
        if (!crcErr.empty()) {
            Log(L"[ошибка] " + crcErr);
            SetStatus(L"Проверка не пройдена");
            return;
        }
        wchar_t crcBuf[32];
        swprintf(crcBuf, 32, L"0x%08X", payloadCrc);
        Log(L"  PayloadCRC:  " + std::wstring(crcBuf) + L"  (CRC32 полезной нагрузки)");
    }

    {
        std::wstring cmTxt = GetText(g.hCleanMode);
        int cmVal = cmTxt.empty() ? 0 : _wtoi(cmTxt.c_str());
        if ((cmVal & 1) && !IsChecked(g.hSelfDelete))
            Log(L"[!] CleanMode=1 без SelfDelete=1: самоудаление не сработает.");
    }

    std::string cfgUtf8 = ToUtf8(cfg);

    if (modSize + cfgUtf8.size() > 2 * 1024 * 1024) {
        Log(L"[ошибка] Модуль + конфигурация больше 2 МБ: архив окажется за границей поиска.");
        SetStatus(L"Слишком большой заголовок");
        return;
    }

    SetPhase(3);
    g_copyDone = 0;
    g_copyTotal = modSize + (unsigned long long)cfgUtf8.size() + paySize;
    if (volumed) {
        for (size_t vi = 1; vi < vols.size(); vi++) g_copyTotal += FileSizeOf(vols[vi]);
    }
    std::vector<std::wstring> continuations;
    unsigned long long w1 = 0, w2 = 0;
    bool ok = false;
    if (volumed) {
        // многотомная сборка: модуль + конфиг + первый том в exe,
        // продолжения (vols[1..]) копируются рядом как <stem>.001...
        unsigned long long tmp1 = 0;
        FILE* vf = _wfopen(out.c_str(), L"wb");
        if (vf) {
            ok = CopyStreamToFile(vf, mod, &tmp1);
            if (ok && !cfgUtf8.empty())
                ok = fwrite(cfgUtf8.data(), 1, cfgUtf8.size(), vf) == cfgUtf8.size();
            if (ok) ok = CopyStreamToFile(vf, payload, &w2);
            fclose(vf);
        }
        w1 = tmp1;
        if (ok) continuations = GlueVolumedContinuations(out, vols);
    } else {
        FILE* outFile = _wfopen(out.c_str(), L"wb");
        if (outFile) {
            ok = CopyStreamToFile(outFile, mod, &w1);
            if (ok && !cfgUtf8.empty())
                ok = fwrite(cfgUtf8.data(), 1, cfgUtf8.size(), outFile) == cfgUtf8.size();
            if (ok) ok = CopyStreamToFile(outFile, payload, &w2);
            fclose(outFile);
        }
    }
    // уборка временных архивов (и томов во временной папке)
    if (!tempArchive.empty()) DeleteFileW(tempArchive.c_str());
    for (const std::wstring& v : vols) DeleteFileW(v.c_str());

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
    if (volumed) {
        for (const std::wstring& c : continuations)
            Log(L"  том:         " + FormatSize(FileSizeOf(c)) + L"  " + c);
    }

    unsigned long long off = FindArchiveOffset(out);
    if (off == ULLONG_MAX) {
        Log(L"[!] сигнатура 7z в результате не найдена");
    } else {
        Log(L"  сигнатура 7z: смещение " + std::to_wstring(off) +
            L" (модуль+конфиг = " + std::to_wstring(w1 + cfgUtf8.size()) + L")");
    }
    SetStatus(L"Собрано: " + FormatSize(total));
}

// --- фоновая сборка: тяжёлая упаковка не блокирует интерфейс ---
static volatile LONG g_exitAfterBuild = 0;
static bool g_verifyAfterBuild = false;
static HANDLE g_buildThread = nullptr;
static volatile LONG g_phase = 0;
static std::wstring g_busyText;
static volatile LONG g_progressKnown = 0; // 1 = status already shows a real percent, dots must not overwrite it

static void SetPhase(int phase)
{
    InterlockedExchange(&g_phase, phase);
    if (g.hWnd) PostMessageW(g.hWnd, WM_APP + 3, (WPARAM)phase, 0);
}

static DWORD WINAPI BuildThreadProc(LPVOID)
{
    DoBuild(false);

    if (g_cancel) {
        Log(L"[сборка] отменена пользователем.");
        SetStatus(L"Сборка отменена");
    } else if (g_verifyAfterBuild) {
        std::wstring out = ResolvePath(GetText(g.hOut));
        if (!out.empty() && FileExists(out)) {
            Log(L"[проверка] запуск тестового проекта: " + out);
            int rc = RunProcess(L"\"" + out + L"\" -y", ExeDir());
            Log(L"[проверка] код возврата: " + std::to_wstring(rc));
        }
    }

    PostMessageW(g.hWnd, WM_APP + 2, 0, 0);
    return 0;
}

static void SetBuildButtonsEnabled(BOOL enabled)
{
    EnableWindow(GetDlgItem(g.hWnd, ID_VERIFY), enabled);
    SetWindowTextW(GetDlgItem(g.hWnd, ID_BUILD), enabled ? L"Собрать" : L"Отмена");
}

static void StartBuild(bool verify)
{
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) {
        Log(L"[!] Сборка уже выполняется — дождитесь завершения.");
        return;
    }
    InterlockedExchange(&g_cancel, 0);
    InterlockedExchange(&g_progressKnown, 0);
    g_verifyAfterBuild = verify;
    SetBuildButtonsEnabled(FALSE);
    g_busyText = verify ? L"Сборка и проверка" : L"Сборка";
    SetStatus(g_busyText + L"...");

    if (g.hProgress) {
        ShowWindow(g.hProgress, SW_SHOW);
        // start determinate at 0: marquee would show a block and then jump back
        ProgressDeterminate(g.hProgress, 0);
    }

    if (g_buildThread) {
        CloseHandle(g_buildThread);
        g_buildThread = nullptr;
    }
    g_buildThread = CreateThread(nullptr, 0, BuildThreadProc, nullptr, 0, nullptr);
    if (!g_buildThread) {
        InterlockedExchange(&g_busy, 0);
        SetBuildButtonsEnabled(TRUE);
        if (g.hProgress) ShowWindow(g.hProgress, SW_HIDE);
        Log(L"[ошибка] не удалось запустить поток сборки.");
        SetStatus(L"Ошибка запуска потока");
    }
}

static void RequestCancel()
{
    if (!g_busy) return;
    InterlockedExchange(&g_cancel, 1);
    if (g_childProc) TerminateProcess(g_childProc, 1);
    Log(L"[!] Запрошена отмена сборки...");
    SetStatus(L"Отмена сборки...");
}

static void ShowPage(int page)
{
    // leaving the config tab drops the "view a built SFX" mode: the tab shows the live config again
    if (page != PAGE_CONFIG && !g_viewSrc.empty()) {
        g_viewSrc.clear();
        SetStatus(L"Конфигурация из параметров");
    }
    g.page = page;
    for (HWND h : g.ctls) {
        LONG_PTR p = GetWindowLongPtrW(h, GWLP_USERDATA);
        if (p < 0) continue;
        ShowWindow(h, p == page ? SW_SHOW : SW_HIDE);
    }
    if (page == PAGE_CONFIG) RefreshConfigView();
    if (page == PAGE_MODULE) UpdateModuleInfo();
    RedrawWindow(g.hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// show the config of a finished SFX in the config tab (read-only, highlighted)
static bool ShowSfxConfigFile(const std::wstring& path, bool silent = false)
{
    std::wstring cfg, err, how;
    if (!ExtractSfxConfig(path, cfg, err, &how)) {
        Log(L"[просмотр] " + err);
        SetStatus(L"Конфигурация не найдена");
        if (!silent) MessageBoxW(g.hWnd, err.c_str(), L"Просмотр конфигурации", MB_ICONWARNING);
        return false;
    }
    g_viewSrc = path;
    SetText(g.hCfgView, cfg);
    HighlightConfig(g.hCfgView);

    int idx = TabCtrl_GetItemCount(g.hTab);
    for (int i = 0; i < idx; i++) {
        wchar_t buf[64] = { 0 };
        TCITEMW it; ZeroMemory(&it, sizeof(it));
        it.mask = TCIF_TEXT; it.pszText = buf; it.cchTextMax = 63;
        TabCtrl_GetItem(g.hTab, i, &it);
        if (wcscmp(buf, L"Конфигурация") == 0) { TabCtrl_SetCurSel(g.hTab, i); break; }
    }
    ShowPage(PAGE_CONFIG);
    SetText(g.hCfgView, cfg);          // ShowPage may have refreshed it
    HighlightConfig(g.hCfgView);

    std::wstring name = path;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    Log(L"[просмотр] " + name + L": конфигурация " + std::to_wstring(cfg.size()) + L" симв. (" + how + L")");
    SetStatus(L"Конфигурация из SFX: " + name);
    return true;
}

static void ShowGeneratedConfig()
{
    g_viewSrc.clear();
    RefreshConfigView();
    SetStatus(L"Конфигурация из параметров");
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

static HWND AddTooltip(HWND parent, HWND btn, const wchar_t* tip)
{
    HWND tt = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr,
                              WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                              CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                              parent, nullptr, g.hInst, nullptr);
    if (!tt) return nullptr;
    TOOLINFOW ti;
    ZeroMemory(&ti, sizeof(ti));
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)btn;
    ti.lpszText = const_cast<wchar_t*>(tip);
    SendMessageW(tt, TTM_ADDTOOLW, 0, (LPARAM)&ti);
    // активнее: показ почти сразу
    SendMessageW(tt, TTM_SETDELAYTIME, TTDT_INITIAL, 300);
    SendMessageW(tt, TTM_SETDELAYTIME, TTDT_AUTOPOP, 8000);
    return tt;
}

// Кнопка с иконкой и текстом. Owner-draw: рисуем иконку слева, текст справа
// (обычный BS_PUSHBUTTON заменяет текст иконкой). Иконка грузится из ресурсов
// exe и хранится в собственном свойстве окна "Icon"; подсказка — в свойстве "Tip".
static HWND IconButton(const wchar_t* text, int x, int y, int w, int h, int id, int page, int iconId)
{
    HWND b = Ctl(L"BUTTON", text, BS_OWNERDRAW, x, y, w, h, id, page);
    HICON hIcon = (HICON)LoadImageW(g.hInst, MAKEINTRESOURCEW(iconId), IMAGE_ICON, 16, 16, LR_DEFAULTSIZE);
    if (hIcon) SetPropW(b, L"Icon", hIcon);
    return b;
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

    // 12 pt monospace font for the config view
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight = -MulDiv(g_cfgPt, 96, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, L"Consolas");
    g.hCfgFont = CreateFontIndirectW(&lf);
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
    // full key set, the same one a profile load uses: otherwise logfile/password/... stay empty
    if (FileExists(ini)) LoadProfileFrom(ini);

    Log(L"[пример] поля заполнены из папки Example");
}
static void SaveLoadedConfig()
{
    if (g.cfgIni.empty()) {
        Log(L"[конфиг] не загружен: назначьте файл через «Загрузить профиль» или папку Example");
        SetStatus(L"Конфиг не назначен");
        return;
    }
    auto kv = [&](const wchar_t* k, HWND h) {
        WritePrivateProfileStringW(L"sfx", k, GetText(h).c_str(), g.cfgIni.c_str());
    };
    auto kvp = [&](const wchar_t* k, HWND h) {
        WritePrivateProfileStringW(L"sfx", k, ToPortablePath(GetText(h)).c_str(), g.cfgIni.c_str());
    };
    wchar_t num[16];
    kv(L"moddir", g.hModDir);
    swprintf(num, 16, L"%d", (int)SendMessageW(g.hMethod, CB_GETCURSEL, 0, 0));
    WritePrivateProfileStringW(L"sfx", L"method", num, g.cfgIni.c_str());
    swprintf(num, 16, L"%d", (int)(SendMessageW(g.hX64, BM_GETCHECK, 0, 0) == BST_CHECKED));
    WritePrivateProfileStringW(L"sfx", L"x64", num, g.cfgIni.c_str());
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
    WritePrivateProfileStringW(L"sfx", L"guimode", num, g.cfgIni.c_str());
    swprintf(num, 16, L"%d", ComboValue(g.hSingleInstance));
    WritePrivateProfileStringW(L"sfx", L"singleinstance", num, g.cfgIni.c_str());
    swprintf(num, 16, L"%d", ComboValue(g.hVolStyle));
    WritePrivateProfileStringW(L"sfx", L"volstyle", num, g.cfgIni.c_str());
    WritePrivateProfileStringW(L"sfx", L"volsize", GetText(g.hVolSize).c_str(), g.cfgIni.c_str());
    Log(L"[конфиг] сохранён: " + g.cfgIni);
    SetStatus(L"Конфиг сохранён");
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

    g.hModInfo = Label(L"", 152, 146, 560, 40, PAGE_MODULE);

    Label(L"Архиватор:", 26, 196, 120, 20, PAGE_MODULE);
    g.hPacker = Edit(152, 192, 470, ID_PACKER, PAGE_MODULE);
    g.hPackerBrowse = Button(L"...", 630, 192, 36, EDIT_H, ID_PACKER_BROWSE, PAGE_MODULE);
    g.hPackerInfo = Label(L"", 152, 222, 560, 44, PAGE_MODULE);

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
          L"Конфигурация и начало архива должны попасть в первые 2 МБ файла.",
          152, 122, 560, 130, PAGE_DATA);

    const int y1 = 50, dy = 36;
    Label(L"InstallPath:", 26, y1, 110, 20, PAGE_PARAMS);
    g.hInstallPath = Edit(142, y1 - 4, 250, ID_INSTALLPATH, PAGE_PARAMS, L"%TEMP%\\app");
    Label(L"RunProgram:", 26, y1 + dy, 110, 20, PAGE_PARAMS);
    g.hRunProgram = Edit(142, y1 + dy - 4, 250, ID_RUNPROGRAM, PAGE_PARAMS, L"app.exe");
    Label(L"Delete:", 26, y1 + dy * 2, 110, 20, PAGE_PARAMS);
    g.hDelete = Edit(142, y1 + dy * 2 - 4, 250, ID_DELETE, PAGE_PARAMS);
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

    Label(L"VolumeNameStyle:", 26, y1 + dy * 5 + 6, 110, 20, PAGE_PARAMS);
    g.hVolStyle = Combo(142, y1 + dy * 5 + 2, 250, 96, ID_VOLSTYLE, PAGE_PARAMS);
    AddComboItem(g.hVolStyle, L"0 — не задан (по имени exe)", 0);
    AddComboItem(g.hVolStyle, L"1 — exe + внешние .001…", 1);
    SendMessageW(g.hVolStyle, CB_SETCURSEL, 0, 0);
    Label(L"Объём тома (МБ):", 404, y1 + dy * 5 + 6, 104, 20, PAGE_PARAMS);
    g.hVolSize = Edit(510, y1 + dy * 5 + 2, 250, ID_VOLSIZE, PAGE_PARAMS, L"");
    Label(L"При 0 — один архив без разбиения на тома.", 510, y1 + dy * 5 + 26, 300, 18, PAGE_PARAMS);

    Label(L"SelfDelete:", 26, y1 + dy * 7, 110, 20, PAGE_PARAMS);
    g.hSelfDelete = Ctl(L"BUTTON", L"удалять exe после работы", BS_AUTOCHECKBOX | WS_TABSTOP,
                        142, y1 + dy * 7 - 2, 240, 20, ID_SELFDELETE, PAGE_PARAMS);

    Label(L"CleanMode:", 404, y1 + dy * 7, 100, 20, PAGE_PARAMS);
    g.hCleanMode = Edit(510, y1 + dy * 7 - 2, 80, ID_CLEANMODE, PAGE_PARAMS);
    Label(L"биты 1/2/4/8", 596, y1 + dy * 7, 90, 20, PAGE_PARAMS);

    g.hPayloadCrc = Ctl(L"BUTTON", L"PayloadCRC (CRC32 архива)", BS_AUTOCHECKBOX | WS_TABSTOP,
                        26, y1 + dy * 8, 300, 20, ID_PAYLOADCRC, PAGE_PARAMS);

    // журнал работы модуля (LogFile): путь к файлу, куда модуль пишет шаги распаковки
    g.hLogFileChk = Ctl(L"BUTTON", L"LogFile", BS_AUTOCHECKBOX | WS_TABSTOP,
                        404, y1 + dy * 8, 82, 20, ID_LOGFILE_CHK, PAGE_PARAMS);
    g.hLogFile = Edit(490, y1 + dy * 8 - 2, 200, ID_LOGFILE, PAGE_PARAMS);
    g.hLogFileBrowse = Button(L"...", 694, y1 + dy * 8 - 2, 36, EDIT_H, ID_LOGFILE_BROWSE, PAGE_PARAMS);
    EnableWindow(g.hLogFile, FALSE);
    EnableWindow(g.hLogFileBrowse, FALSE);
    AddTooltip(hWnd, g.hLogFile, L"Журнал модуля, путь от папки SFX");

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

    Label(L"BeginPrompt:", 26, y1 + dy * 6 + 6, 110, 20, PAGE_PARAMS);
    g.hBeginPrompt = Edit(142, y1 + dy * 6 + 2, 618, ID_BEGINPROMPT, PAGE_PARAMS);

    const wchar_t* cfgClass = L"EDIT";
    g_hRich = LoadLibraryW(L"Msftedit.dll");
    if (g_hRich) {
        cfgClass = L"RICHEDIT50W";
    } else {
        g_hRich = LoadLibraryW(L"Riched20.dll");
        if (g_hRich) cfgClass = L"RICHEDIT20W";
    }
    g.hCfgView = CreateWindowExW(WS_EX_CLIENTEDGE, cfgClass, L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL | WS_TABSTOP,
                                 26, 44, 742, 320, hWnd, (HMENU)(INT_PTR)ID_CFGVIEW, g.hInst, nullptr);
    SendMessageW(g.hCfgView, WM_SETFONT, (WPARAM)g.hCfgFont, TRUE);
    SetWindowSubclass(g.hCfgView, CfgViewProc, 1, 0);
    SetWindowLongPtrW(g.hCfgView, GWLP_USERDATA, PAGE_CONFIG);
    g.ctls.push_back(g.hCfgView);

    g.hLogLabel = Label(L"Журнал:", 26, 400, 100, 20, -1);
    g.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                             26, 420, 748, 118, hWnd, (HMENU)(INT_PTR)ID_LOG, g.hInst, nullptr);
    SendMessageW(g.hLog, WM_SETFONT, (WPARAM)g.hMono, TRUE);
    SetWindowLongPtrW(g.hLog, GWLP_USERDATA, -1);
    g.ctls.push_back(g.hLog);

    IconButton(L"Сохранить", 26, 548, 108, 32, ID_SAVE_CFG, -1, IDI_SAVE);
    IconButton(L"Собрать", 142, 548, 108, 32, ID_BUILD, -1, IDI_BUILD);
    IconButton(L"Проверить", 258, 548, 108, 32, ID_VERIFY, -1, IDI_VERIFY);
    IconButton(L"Запустить", 374, 548, 108, 32, ID_RUN, -1, IDI_RUN);
    IconButton(L"Открыть папку", 490, 548, 124, 32, ID_OPENFOLDER, -1, IDI_OPENFOLDER);
    IconButton(L"Очистить журнал", 622, 548, 140, 32, ID_LOG_CLEAR, -1, IDI_CLEARLOG);

    AddTooltip(hWnd, GetDlgItem(hWnd, ID_SAVE_CFG), L"Сохранить текущую конфигурацию в загруженный INI-файл");
    AddTooltip(hWnd, GetDlgItem(hWnd, ID_BUILD), L"Собрать SFX-инсталлятор (модуль + конфигурация + архив)");
    AddTooltip(hWnd, GetDlgItem(hWnd, ID_VERIFY), L"Собрать и сразу запустить для проверки");
    AddTooltip(hWnd, GetDlgItem(hWnd, ID_RUN), L"Запустить собранный инсталлятор");
    AddTooltip(hWnd, GetDlgItem(hWnd, ID_OPENFOLDER), L"Открыть папку с результатом в проводнике");
    AddTooltip(hWnd, GetDlgItem(hWnd, ID_LOG_CLEAR), L"Очистить журнал операций");

    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                0, 0, 0, 0, hWnd, (HMENU)(INT_PTR)ID_STATUS, g.hInst, nullptr);
    SendMessageW(g.hStatus, WM_SETFONT, (WPARAM)g.hFont, TRUE);
    int parts[2] = { 560, -1 };
    SendMessageW(g.hStatus, SB_SETPARTS, 2, (LPARAM)parts);
    {
        std::wstring ver = AppTitle();
        SendMessageW(g.hStatus, SB_SETTEXTW, 1, (LPARAM)ver.c_str());
    }

    // тонкая полоса прогресса над статус-баром (неопределённое состояние = анимация)
    g.hProgress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD,
                                  0, 0, 10, 6, hWnd, (HMENU)(INT_PTR)ID_PROGRESS, g.hInst, nullptr);
    ShowWindow(g.hProgress, SW_HIDE);

    DragAcceptFiles(hWnd, TRUE);
    UpdatePackerInfo();
    LoadExample();
    {
        std::wstring lastIni = ExeDir() + L"SfxPress.last.ini";
        if (FileExists(lastIni)) LoadProfileFrom(lastIni);
    }
    ShowPage(PAGE_MODULE);
    LayoutUi(g.hWnd);
    SetTimer(hWnd, 1, 200, nullptr);
    SetStatus(L"Готово");
}

static HMENU BuildMenu()
{
    HMENU m = CreateMenu();

    HMENU f = CreatePopupMenu();
    AppendMenuW(f, MF_STRING, ID_FILE_LOAD, L"Загрузить профиль...\tCtrl+O");
    AppendMenuW(f, MF_STRING, ID_FILE_SAVE, L"Сохранить профиль...\tCtrl+S");
    AppendMenuW(f, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(f, MF_STRING, ID_FILE_VIEWSFX, L"Показать конфигурацию из SFX...");
    AppendMenuW(f, MF_STRING, ID_FILE_GENCFG, L"Вернуться к генерируемой конфигурации");
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
static void SaveProfileTo(const std::wstring& path)
{
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
    swprintf(num, 16, L"%d", ComboValue(g.hVolStyle));
    WritePrivateProfileStringW(L"sfx", L"volstyle", num, path.c_str());
    WritePrivateProfileStringW(L"sfx", L"volsize", GetText(g.hVolSize).c_str(), path.c_str());
    WritePrivateProfileStringW(L"sfx", L"selfdelete", IsChecked(g.hSelfDelete) ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"sfx", L"cleanmode", GetText(g.hCleanMode).c_str(), path.c_str());
    WritePrivateProfileStringW(L"sfx", L"payloadcrc", IsChecked(g.hPayloadCrc) ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"sfx", L"logfilechk", IsChecked(g.hLogFileChk) ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"sfx", L"logfile", GetText(g.hLogFile).c_str(), path.c_str());
    WritePrivateProfileStringW(L"sfx", L"packer", ToPortablePath(g.packerOverride).c_str(), path.c_str());

    Log(L"[профиль] сохранён: " + path);
}

static void SaveProfile()
{
    std::wstring path;
    if (!BrowseSave(path, L"Профиль (*.ini)\0*.ini\0Все файлы\0*.*\0", L"ini")) return;
    SaveProfileTo(path);
}

static void LoadProfileFrom(const std::wstring& path)
{
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
    GetPrivateProfileStringW(L"sfx", L"volstyle", L"0", num, 32, path.c_str());
    SendMessageW(g.hVolStyle, CB_SETCURSEL, (WPARAM)_wtoi(num), 0);
    kv(L"volsize", g.hVolSize);

    {
        wchar_t flag[16];
        GetPrivateProfileStringW(L"sfx", L"selfdelete", L"0", flag, 16, path.c_str());
        SendMessageW(g.hSelfDelete, BM_SETCHECK, _wtoi(flag) ? BST_CHECKED : BST_UNCHECKED, 0);
        GetPrivateProfileStringW(L"sfx", L"cleanmode", L"", flag, 16, path.c_str());
        SetText(g.hCleanMode, flag);
        GetPrivateProfileStringW(L"sfx", L"payloadcrc", L"0", flag, 16, path.c_str());
        SendMessageW(g.hPayloadCrc, BM_SETCHECK, _wtoi(flag) ? BST_CHECKED : BST_UNCHECKED, 0);
        GetPrivateProfileStringW(L"sfx", L"logfilechk", L"0", flag, 16, path.c_str());
        SendMessageW(g.hLogFileChk, BM_SETCHECK, _wtoi(flag) ? BST_CHECKED : BST_UNCHECKED, 0);
        wchar_t lfp[4096];
        lfp[0] = 0;
        GetPrivateProfileStringW(L"sfx", L"logfile", L"", lfp, 4096, path.c_str());
        SetText(g.hLogFile, lfp);
        EnableWindow(g.hLogFile, IsChecked(g.hLogFileChk));
        EnableWindow(g.hLogFileBrowse, IsChecked(g.hLogFileChk));
        wchar_t pk[4096];
        pk[0] = 0;
        GetPrivateProfileStringW(L"sfx", L"packer", L"", pk, 4096, path.c_str());
        if (pk[0]) g.packerOverride = FromPortablePath(pk);
        else g.packerOverride.clear();
    }

    UpdateModuleInfo();
    UpdatePackerInfo();
    RefreshConfigView();
    g.cfgIni = path;
    Log(L"[профиль] загружен: " + path);
}

static void LoadProfile()
{
    std::wstring path;
    if (!BrowseOpen(path, L"Профиль (*.ini)\0*.ini\0Все файлы\0*.*\0", nullptr)) return;
    LoadProfileFrom(path);
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

// ask before closing while a build runs; true = cancel the build and exit
static bool AskExitDuringBuild(HWND owner)
{
    typedef HRESULT (WINAPI *PFN_TASKDIALOG)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    PFN_TASKDIALOG fn = nullptr;
    HMODULE com = GetModuleHandleW(L"comctl32.dll");
    if (com) fn = (PFN_TASKDIALOG)GetProcAddress(com, "TaskDialogIndirect");
    if (!fn) {
        return MessageBoxW(owner,
            L"Идёт сборка. Выйти — отменить сборку и закрыть программу?",
            L"SfxPress", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
    }
    TASKDIALOG_BUTTON buttons[2];
    buttons[0].nButtonID = 1;
    buttons[0].pszButtonText = L"Выйти";
    buttons[1].nButtonID = 2;
    buttons[1].pszButtonText = L"Отмена";
    TASKDIALOGCONFIG cfg = {};
    cfg.cbSize = sizeof(cfg);
    cfg.hwndParent = owner;
    cfg.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    cfg.pszWindowTitle = L"SfxPress";
    cfg.pszMainInstruction = L"Идёт сборка";
    cfg.pszContent = L"Выйти — отменить сборку и закрыть программу.\r\nОтмена — оставить сборку и продолжить.";
    cfg.cButtons = 2;
    cfg.pButtons = buttons;
    cfg.nDefaultButton = 2;
    int pressed = 0;
    if (FAILED(fn(&cfg, &pressed, nullptr, nullptr))) return false;
    return pressed == 1;
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


    case WM_SIZE:
        LayoutUi(hWnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lParam;
        // never let the user shrink the window below the designed client area,
        // otherwise the fixed-position controls on the tabs get clipped
        RECT rc = { 0, 0, 800, 620 };
        AdjustWindowRectEx(&rc, (DWORD)GetWindowLongW(hWnd, GWL_STYLE), TRUE,
                           (DWORD)GetWindowLongW(hWnd, GWL_EXSTYLE));
        mm->ptMinTrackSize.x = rc.right - rc.left;
        mm->ptMinTrackSize.y = rc.bottom - rc.top;
        if (mm->ptMaxTrackSize.x < mm->ptMinTrackSize.x) mm->ptMaxTrackSize.x = mm->ptMinTrackSize.x;
        if (mm->ptMaxTrackSize.y < mm->ptMinTrackSize.y) mm->ptMaxTrackSize.y = mm->ptMinTrackSize.y;
        return 0;
    }

    case WM_TIMER:
        if (wParam == 1) {
            if (g.hTab) {
                int sel = TabCtrl_GetCurSel(g.hTab);
                if (sel >= 0 && sel != g.page) ShowPage(sel);
            }
            if (g_busy) {
                static int dots = 0;
                static int noPctTicks = 0;
                if (g_progressKnown) {
                    noPctTicks = 0;
                } else {
                    // no percent for ~1 s (5 x 200 ms): fall back to marquee
                    if (++noPctTicks == 5 && g.hProgress) ProgressMarquee(g.hProgress);
                    dots = (dots + 1) % 4;
                    SetStatus(g_busyText + std::wstring(dots, L'.'));
                }
            }
        }
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
        case ID_BUILD:
            if (g_busy) RequestCancel();
            else StartBuild(false);
            return 0;
        case ID_SAVE_CFG: SaveLoadedConfig(); return 0;
        case ID_VERIFY: StartBuild(true); return 0;
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
        case ID_FILE_VIEWSFX: {
            std::wstring p;
            const wchar_t* filter = L"Готовый SFX (*.exe)\0*.exe\0Все файлы (*.*)\0*.*\0";
            if (BrowseOpen(p, filter, L"exe")) ShowSfxConfigFile(p);
            return 0;
        }
        case ID_FILE_GENCFG: ShowGeneratedConfig(); return 0;
        case ID_FILE_EXIT: DestroyWindow(hWnd); return 0;
        case ID_HELP_APP: OpenHelp(L"Bin\\SfxPress.chm"); return 0;
        case ID_HELP_MODULE: OpenHelp(L"Bin\\Modules\\7zSD_RU_plus.chm"); return 0;
        case ID_HELP_7ZIP: OpenHelp(L"Bin\\7-zip.chm"); return 0;
        case ID_FILE_ABOUT: {
            std::wstring about = AppTitle() + L"\n\n" +
                L"Конструктор SFX-инсталляторов для модифицированного SFX-модуля.\n"
                L"Чистый C++ / Win32, без сторонних библиотек.\n\n"
                L"Возможности:\n"
                L"  • PayloadCRC — CRC32 архива и проверка целостности модулем;\n"
                L"  • CleanMode и SelfDelete — мало-следовый режим и самоудаление;\n"
                L"  • выбор архиватора 7z с показом версии;\n"
                L"  • запоминание путей, валидация конфигурации, коды возврата в журнале;\n"
                L"  • фоновая сборка с индикатором прогресса и отменой.\n\n"
                L"Сборка: модуль .sfx + конфигурация UTF-8 + архив payload.7z";
            MessageBoxW(hWnd, about.c_str(), L"О программе", MB_ICONINFORMATION);
            return 0;
        }
        case ID_METHOD:
        case ID_X86:
        case ID_X64:
        case ID_GUIMODE:
        case ID_SINGLEINSTANCE:
        case ID_VOLSTYLE:
            if (code == CBN_SELCHANGE || code == BN_CLICKED) {
                UpdateModuleInfo();
                if (g.page == PAGE_CONFIG) RefreshConfigView();
            }
            return 0;
        case ID_PAYLOADCRC:
        case ID_SELFDELETE:
            if (code == BN_CLICKED && g.page == PAGE_CONFIG) RefreshConfigView();
            return 0;
        case ID_PACKER_BROWSE: {
            std::wstring p = g.packerOverride.empty() ? FindPacker() : g.packerOverride;
            if (BrowseOpen(p, L"Архиватор 7-Zip (7z.exe)\07z.exe;7za.exe\0Программы\0*.exe\0Все файлы\0*.*\0", L"exe")) {
                g.packerOverride = p;
                UpdatePackerInfo();
                Log(L"[архиватор] выбран: " + p);
            }
            return 0;
        }
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
            BROWSEINFOW bi;
            ZeroMemory(&bi, sizeof(bi));
            bi.hwndOwner = g.hWnd;
            bi.lpszTitle = L"Выберите папку с модулями";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            std::wstring cur = GetText(g.hModDir);
            LPITEMIDLIST root = nullptr;
            if (!cur.empty()) {
                if (SHParseDisplayName(cur.c_str(), nullptr, &root, 0, nullptr) != S_OK) root = nullptr;
                if (root) bi.pidlRoot = root;
            }
            LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
            if (pidl) {
                wchar_t path[MAX_PATH * 4];
                if (SHGetPathFromIDListW(pidl, path)) {
                    SetText(g.hModDir, path);
                    UpdateModuleInfo();
                }
                CoTaskMemFree(pidl);
            }
            if (root) CoTaskMemFree(root);
            return 0;
        }
        case ID_INSTALLPATH:
        case ID_RUNPROGRAM:
        case ID_DELETE:
            if (code == EN_CHANGE) {
                if (g.page == PAGE_CONFIG) RefreshConfigView();
            }
            return 0;
        case ID_LOGFILE:
            if (code == EN_CHANGE && g.page == PAGE_CONFIG) RefreshConfigView();
            return 0;
        case ID_LOGFILE_CHK: {
            bool on = IsChecked(g.hLogFileChk);
            EnableWindow(g.hLogFile, on);
            EnableWindow(g.hLogFileBrowse, on);
            if (on && GetText(g.hLogFile).empty()) {
                // relative name: the module resolves it against the folder of the SFX itself
                std::wstring out = GetText(g.hOut);
                size_t slash = out.find_last_of(L"\\/");
                std::wstring name = (slash == std::wstring::npos) ? out : out.substr(slash + 1);
                size_t dot = name.find_last_of(L'.');
                if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
                if (!name.empty()) SetText(g.hLogFile, name + L".log");
            }
            if (g.page == PAGE_CONFIG) RefreshConfigView();
            return 0;
        }
        case ID_LOGFILE_BROWSE: {
            std::wstring p = GetText(g.hLogFile);
            const wchar_t* filter = L"Журнал (*.log)\0*.log\0Все файлы (*.*)\0*.*\0";
            if (BrowseSave(p, filter, L"log")) {
                SetText(g.hLogFile, p);
                SendMessageW(g.hLogFileChk, BM_SETCHECK, BST_CHECKED, 0);
                EnableWindow(g.hLogFile, TRUE);
                EnableWindow(g.hLogFileBrowse, TRUE);
                if (g.page == PAGE_CONFIG) RefreshConfigView();
            }
            return 0;
        }
        case ID_PASSWORD:
        case ID_SFXAUTHOR:
        case ID_ARCHIVE:
        case ID_PREEXTRACT:
        case ID_EXECUTEONLOAD:
        case ID_BEGINPROMPT:
        case ID_VOLSIZE:
            if (code == EN_CHANGE && g.page == PAGE_CONFIG) RefreshConfigView();
            return 0;
        default:
            break;
        }
        return 0;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType != ODT_BUTTON) break;
        HWND btn = dis->hwndItem;
        HICON hIcon = (HICON)GetPropW(btn, L"Icon");
        HDC dc = dis->hDC;
        RECT rc = dis->rcItem;
        bool pressed = (dis->itemState & ODS_SELECTED) != 0;
        bool focused = (dis->itemState & ODS_FOCUS) != 0;
        // фон: штатный цвет кнопки
        SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
        ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &rc, L"", 0, nullptr);
        // рамка + внутренние смещения
        RECT frame = rc;
        DrawEdge(dc, &frame, pressed ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
        // текст кнопки
        wchar_t txt[256];
        GetWindowTextW(btn, txt, 256);
        // текст справа от иконки
        int iconW = hIcon ? 16 : 0;
        int gap = hIcon ? 4 : 0;
        RECT tr = rc;
        tr.left += 6 + iconW + gap;
        tr.right -= 4;
        tr.top += 2; tr.bottom -= 2;
        if (pressed) { tr.left += 1; tr.top += 1; }
        SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        // иконка
        if (hIcon) {
            int iconY = rc.top + (rc.bottom - rc.top) / 2 - iconW / 2 + (pressed ? 1 : 0);
            DrawIconEx(dc, rc.left + 5 + (pressed ? 1 : 0), iconY, hIcon, iconW, iconW, 0, nullptr, DI_NORMAL);
        }
        // фокус-пунктир
        if (focused) {
            RECT fr = rc;
            fr.left += 2; fr.top += 2; fr.right -= 2; fr.bottom -= 2;
            DrawFocusRect(dc, &fr);
        }
        return TRUE;
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
            std::wstring dropped = path;
            size_t dot = dropped.find_last_of(L'.');
            std::wstring ext = (dot == std::wstring::npos) ? std::wstring() : dropped.substr(dot);
            for (auto& ch : ext) ch = towlower(ch);
            if (ext == L".exe") {
                // dropped a finished installer: show its config instead of treating it as payload
                ShowSfxConfigFile(dropped);
                return 0;
            }
            SetText(g.hPayload, path);
            Log(L"[drop] " + std::wstring(path));
        }
        return 0;
    }

    case WM_APP + 2:
        InterlockedExchange(&g_busy, 0);
        InterlockedExchange(&g_progressKnown, 0);
        SetBuildButtonsEnabled(TRUE);
        if (g.hProgress) {
            SendMessageW(g.hProgress, PBM_SETMARQUEE, FALSE, 0);
            ShowWindow(g.hProgress, SW_HIDE);
        }
        if (g_childProc) {
            CloseHandle(g_childProc);
            g_childProc = nullptr;
        }
        if (g_exitAfterBuild) { DestroyWindow(hWnd); return 0; }
        return 0;

    case WM_APP + 3:
        InterlockedExchange(&g_progressKnown, 0);
        switch ((int)wParam) {
        case 1: g_busyText = L"Упаковка"; break;
        case 2: g_busyText = L"CRC"; break;
        case 3: g_busyText = L"Запись"; break;
        default: g_busyText = L"Сборка"; break;
        }
        SetStatus(g_busyText + L"...");
        return 0;

    case WM_APP + 4:
        InterlockedExchange(&g_progressKnown, 1);
        if (g.hProgress) {
            ProgressDeterminate(g.hProgress, (int)wParam);
        }
        if (g_busy) {
            wchar_t pct[16];
            swprintf(pct, 16, L" %d%%", (int)wParam);
            SetStatus(g_busyText + pct);
        }
        return 0;

    case WM_CLOSE:
        if (g_busy) {
            if (AskExitDuringBuild(hWnd)) {
                g_exitAfterBuild = true;
                RequestCancel();
                if (!g_busy) { DestroyWindow(hWnd); return 0; } // build finished while the dialog was open
                Log(L"[!] Закрытие: сборка отменяется, программа закроется после остановки.");
                SetStatus(L"Остановка сборки и выход...");
            }
            return 0;
        }
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        SaveProfileTo(ExeDir() + L"SfxPress.last.ini");
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
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS;
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

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    RECT rc = { 0, 0, 800, 620 };
    AdjustWindowRectEx(&rc, style, TRUE, 0);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, AppTitle().c_str(),
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
    if (cmdline.find(L"--show-config") != std::wstring::npos) {
        std::wstring p = ArgValue(cmdline, L"--show-config");
        std::wstring cfg, err, how;
        bool ok = !p.empty() && ExtractSfxConfig(p, cfg, err, &how);
        AttachConsole(ATTACH_PARENT_PROCESS);
        HANDLE con = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD written = 0;
        if (con && con != INVALID_HANDLE_VALUE) {
            std::string out = ok ? ToUtf8(cfg + L"\r\n")
                                 : ToUtf8(L"SfxPress: " + (err.empty() ? std::wstring(L"не задан файл: --show-config=<exe>") : err) + L"\r\n");
            WriteFile(con, out.data(), (DWORD)out.size(), &written, nullptr);
        }
        DestroyWindow(hWnd);
        return ok ? 0 : 3;
    }
    if (cmdline.find(L"--build") != std::wstring::npos) {
        std::wstring v;
        v = ArgValue(cmdline, L"--payload"); if (!v.empty()) SetText(g.hPayload, v);
        v = ArgValue(cmdline, L"--out");     if (!v.empty()) SetText(g.hOut, v);
        v = ArgValue(cmdline, L"--install"); if (!v.empty()) SetText(g.hInstallPath, v);
        v = ArgValue(cmdline, L"--run");     if (!v.empty()) SetText(g.hRunProgram, v);
        v = ArgValue(cmdline, L"--password"); if (!v.empty()) SetText(g.hPassword, v);
        v = ArgValue(cmdline, L"--style"); if (!v.empty()) SendMessageW(g.hVolStyle, CB_SETCURSEL, (WPARAM)_wtoi(v.c_str()), 0);
        v = ArgValue(cmdline, L"--volsize"); if (!v.empty()) SetText(g.hVolSize, v);
        v = ArgValue(cmdline, L"--guimode"); if (!v.empty()) SetComboByValue(g.hGuiMode, _wtoi(v.c_str()));
        v = ArgValue(cmdline, L"--cleanmode"); if (!v.empty()) SetText(g.hCleanMode, v);
        v = ArgValue(cmdline, L"--moddir"); if (!v.empty()) SetText(g.hModDir, v);
        v = ArgValue(cmdline, L"--packer"); if (!v.empty()) { g.packerOverride = v; UpdatePackerInfo(); }
        if (cmdline.find(L"--selfdelete") != std::wstring::npos)
            SendMessageW(g.hSelfDelete, BM_SETCHECK, BST_CHECKED, 0);
        if (cmdline.find(L"--crc") != std::wstring::npos)
            SendMessageW(g.hPayloadCrc, BM_SETCHECK, BST_CHECKED, 0);
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
