#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <initguid.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <string>
#include "setup_resource.h"

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

// Control IDs
#define IDC_TXT_DIR       3001
#define IDC_BTN_BROWSE    3002
#define IDC_CHK_STARTUP   3003
#define IDC_CHK_DESKTOP   3004
#define IDC_CHK_STARTMENU 3005
#define IDC_BTN_INSTALL   3006
#define IDC_BTN_CANCEL    3007
#define IDC_ST_STATUS     3008

static HBRUSH g_hbrDarkBg = NULL;
static HBRUSH g_hbrEditBg = NULL;

void TerminateETDSelect() {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ETDSelect.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    TerminateProcess(hProc, 0);
                    CloseHandle(hProc);
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

bool CreateShortcut(const wchar_t* targetPath, const wchar_t* shortcutPath, const wchar_t* description) {
    CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr)) {
        psl->SetPath(targetPath);
        psl->SetDescription(description);
        
        wchar_t dir[MAX_PATH];
        wcscpy_s(dir, targetPath);
        PathRemoveFileSpecW(dir);
        psl->SetWorkingDirectory(dir);

        IPersistFile* ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr)) {
            hr = ppf->Save(shortcutPath, TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return SUCCEEDED(hr);
}

bool ExtractResource(HINSTANCE hInst, int resourceID, const wchar_t* targetFilePath) {
    HRSRC hRes = FindResourceW(hInst, MAKEINTRESOURCEW(resourceID), RT_RCDATA);
    if (!hRes) return false;

    HGLOBAL hMem = LoadResource(hInst, hRes);
    if (!hMem) return false;

    DWORD size = SizeofResource(hInst, hRes);
    void* data = LockResource(hMem);
    if (!data || size == 0) return false;

    HANDLE hFile = CreateFileW(targetFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    WriteFile(hFile, data, size, &written, NULL);
    CloseHandle(hFile);

    return (written == size);
}

void SetAutoStartRegistry(const wchar_t* exePath, bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"ETDSelect", 0, REG_SZ, (BYTE*)quoted.c_str(), (DWORD)(quoted.length() + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"ETDSelect");
        }
        RegCloseKey(hKey);
    }
}

void RegisterUninstall(const wchar_t* installDir, const wchar_t* uninstallerPath) {
    HKEY hKey;
    const wchar_t* regPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETDSelect";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        std::wstring displayName = L"ETDSelect Screenshot Tool";
        std::wstring publisher = L"Emir Tuğra Dağ";
        std::wstring version = L"1.0.0";
        std::wstring uninstCmd = L"\"" + std::wstring(uninstallerPath) + L"\" /uninstall";

        std::wstring mainExePath = std::wstring(installDir) + L"\\ETDSelect.exe";
        RegSetValueExW(hKey, L"DisplayIcon", 0, REG_SZ, (BYTE*)mainExePath.c_str(), (DWORD)(mainExePath.length() + 1) * sizeof(wchar_t));

        RegSetValueExW(hKey, L"DisplayName", 0, REG_SZ, (BYTE*)displayName.c_str(), (DWORD)(displayName.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"Publisher", 0, REG_SZ, (BYTE*)publisher.c_str(), (DWORD)(publisher.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"DisplayVersion", 0, REG_SZ, (BYTE*)version.c_str(), (DWORD)(version.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"UninstallString", 0, REG_SZ, (BYTE*)uninstCmd.c_str(), (DWORD)(uninstCmd.length() + 1) * sizeof(wchar_t));
        RegSetValueExW(hKey, L"InstallLocation", 0, REG_SZ, (BYTE*)installDir, (DWORD)(wcslen(installDir) + 1) * sizeof(wchar_t));
        
        DWORD dwordVal = 1;
        RegSetValueExW(hKey, L"NoModify", 0, REG_DWORD, (BYTE*)&dwordVal, sizeof(DWORD));
        RegSetValueExW(hKey, L"NoRepair", 0, REG_DWORD, (BYTE*)&dwordVal, sizeof(DWORD));

        RegCloseKey(hKey);
    }
}

std::wstring GetDefaultInstallPath() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        return std::wstring(path) + L"\\Programs\\ETDSelect";
    }
    return L"C:\\ETDSelect";
}

void RunUninstaller() {
    if (MessageBoxW(NULL, L"ETDSelect bilgisayarınızdan kaldırılacak.\nDevam etmek istiyor musunuz?", L"ETDSelect Kaldırma", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }

    TerminateETDSelect();
    SetAutoStartRegistry(L"", false);

    wchar_t desktopPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopPath))) {
        std::wstring link = std::wstring(desktopPath) + L"\\ETDSelect.lnk";
        DeleteFileW(link.c_str());
    }

    wchar_t startMenuPath[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuPath))) {
        std::wstring link = std::wstring(startMenuPath) + L"\\ETDSelect.lnk";
        DeleteFileW(link.c_str());
    }

    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ETDSelect");

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    wchar_t dir[MAX_PATH];
    wcscpy_s(dir, exePath);
    PathRemoveFileSpecW(dir);

    std::wstring mainExe = std::wstring(dir) + L"\\ETDSelect.exe";
    DeleteFileW(mainExe.c_str());

    wchar_t cmd[MAX_PATH * 2];
    wsprintfW(cmd, L"cmd.exe /c timeout /t 1 /nobreak > NUL & rmdir /s /q \"%s\"", dir);
    
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (pi.hProcess) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    MessageBoxW(NULL, L"ETDSelect başarıyla kaldırıldı.", L"ETDSelect Kaldırıldı", MB_OK | MB_ICONINFORMATION);
}

LRESULT CALLBACK SetupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            g_hbrDarkBg = CreateSolidBrush(RGB(26, 26, 46));
            g_hbrEditBg = CreateSolidBrush(RGB(40, 40, 60));

            HINSTANCE hInst = GetModuleHandle(NULL);

            CreateWindowExW(0, L"STATIC", L"ETDSelect Kurulum Sihirbazı",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 15, 360, 25, hwnd, NULL, hInst, NULL);

            CreateWindowExW(0, L"STATIC", L"Yapımcı: Emir Tuğra Dağ",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 40, 360, 20, hwnd, NULL, hInst, NULL);

            CreateWindowExW(0, L"STATIC", L"Kurulum Dizini:",
                WS_CHILD | WS_VISIBLE,
                20, 75, 360, 20, hwnd, NULL, hInst, NULL);

            std::wstring defaultDir = GetDefaultInstallPath();
            HWND hEditDir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", defaultDir.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                20, 95, 360, 24, hwnd, (HMENU)(UINT_PTR)IDC_TXT_DIR, hInst, NULL);

            HWND hStartup = CreateWindowExW(0, L"BUTTON", L"Windows başlangıcında otomatik çalıştır",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 135, 360, 22, hwnd, (HMENU)(UINT_PTR)IDC_CHK_STARTUP, hInst, NULL);
            SendMessageW(hStartup, BM_SETCHECK, BST_CHECKED, 0);

            HWND hDesktop = CreateWindowExW(0, L"BUTTON", L"Masaüstüne kısayol simgesi ekle",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 162, 360, 22, hwnd, (HMENU)(UINT_PTR)IDC_CHK_DESKTOP, hInst, NULL);
            SendMessageW(hDesktop, BM_SETCHECK, BST_CHECKED, 0);

            HWND hStartMenu = CreateWindowExW(0, L"BUTTON", L"Başlat menüsüne kısayol ekle",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                20, 189, 360, 22, hwnd, (HMENU)(UINT_PTR)IDC_CHK_STARTMENU, hInst, NULL);
            SendMessageW(hStartMenu, BM_SETCHECK, BST_CHECKED, 0);

            CreateWindowExW(0, L"STATIC", L"",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 220, 360, 20, hwnd, (HMENU)(UINT_PTR)IDC_ST_STATUS, hInst, NULL);

            CreateWindowExW(0, L"BUTTON", L"Kur",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                80, 250, 110, 32, hwnd, (HMENU)(UINT_PTR)IDC_BTN_INSTALL, hInst, NULL);

            CreateWindowExW(0, L"BUTTON", L"İptal",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                210, 250, 110, 32, hwnd, (HMENU)(UINT_PTR)IDC_BTN_CANCEL, hInst, NULL);

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(26, 26, 46));
            return (LRESULT)g_hbrDarkBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkColor(hdc, RGB(40, 40, 60));
            return (LRESULT)g_hbrEditBg;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            if (id == IDC_BTN_INSTALL) {
                wchar_t installDir[MAX_PATH];
                GetWindowTextW(GetDlgItem(hwnd, IDC_TXT_DIR), installDir, MAX_PATH);

                if (wcslen(installDir) == 0) {
                    MessageBoxW(hwnd, L"Lütfen geçerli bir kurulum dizini belirtin.", L"Hata", MB_OK | MB_ICONERROR);
                    return 0;
                }

                SetWindowTextW(GetDlgItem(hwnd, IDC_ST_STATUS), L"Kuruluyor...");
                EnableWindow(GetDlgItem(hwnd, IDC_BTN_INSTALL), FALSE);

                TerminateETDSelect();
                SHCreateDirectoryExW(NULL, installDir, NULL);

                std::wstring mainExePath = std::wstring(installDir) + L"\\ETDSelect.exe";
                if (!ExtractResource(GetModuleHandle(NULL), IDR_ETDSELECT_EXE, mainExePath.c_str())) {
                    MessageBoxW(hwnd, L"ETDSelect.exe dosyası çıkartılamadı!", L"Kurulum Hatası", MB_OK | MB_ICONERROR);
                    EnableWindow(GetDlgItem(hwnd, IDC_BTN_INSTALL), TRUE);
                    SetWindowTextW(GetDlgItem(hwnd, IDC_ST_STATUS), L"");
                    return 0;
                }

                wchar_t selfPath[MAX_PATH];
                GetModuleFileNameW(NULL, selfPath, MAX_PATH);
                std::wstring uninstPath = std::wstring(installDir) + L"\\Uninstall.exe";
                CopyFileW(selfPath, uninstPath.c_str(), FALSE);

                bool chkDesktop = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_DESKTOP), BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (chkDesktop) {
                    wchar_t desktopDir[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktopDir))) {
                        std::wstring shortcut = std::wstring(desktopDir) + L"\\ETDSelect.lnk";
                        CreateShortcut(mainExePath.c_str(), shortcut.c_str(), L"ETDSelect Ekran Alıntısı Aracı");
                    }
                }

                bool chkStartMenu = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_STARTMENU), BM_GETCHECK, 0, 0) == BST_CHECKED);
                if (chkStartMenu) {
                    wchar_t startMenuDir[MAX_PATH];
                    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROGRAMS, NULL, 0, startMenuDir))) {
                        std::wstring shortcut = std::wstring(startMenuDir) + L"\\ETDSelect.lnk";
                        CreateShortcut(mainExePath.c_str(), shortcut.c_str(), L"ETDSelect Ekran Alıntısı Aracı");
                    }
                }

                bool chkStartup = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED);
                SetAutoStartRegistry(mainExePath.c_str(), chkStartup);

                RegisterUninstall(installDir, uninstPath.c_str());

                SetWindowTextW(GetDlgItem(hwnd, IDC_ST_STATUS), L"Kurulum Tamamlandı!");

                int answer = MessageBoxW(hwnd, 
                    L"ETDSelect başarıyla kuruldu!\nUygulamayı şimdi başlatmak ister misiniz?", 
                    L"Kurulum Tamamlandı", MB_YESNO | MB_ICONINFORMATION);

                if (answer == IDYES) {
                    ShellExecuteW(NULL, L"open", mainExePath.c_str(), NULL, NULL, SW_SHOWNORMAL);
                }

                DestroyWindow(hwnd);
            } else if (id == IDC_BTN_CANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_hbrDarkBg) DeleteObject(g_hbrDarkBg);
            if (g_hbrEditBg) DeleteObject(g_hbrEditBg);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int) {
    if (wcsstr(lpCmdLine, L"/uninstall") != NULL) {
        RunUninstaller();
        return 0;
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = SetupWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ETDSelectSetupClass";
    wc.hbrBackground = CreateSolidBrush(RGB(26, 26, 46));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc);

    int w = 415, h = 330;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"ETDSelectSetupClass",
        L"ETDSelect - Kurulum Sihirbazı",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}
