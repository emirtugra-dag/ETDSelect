#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <gdiplus.h>
#include <commctrl.h>
#include "app.h"
#include "resource.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Single instance check & shortcut trigger
    HWND hExistingWnd = FindWindowW(L"ETDSelectMainClass", L"ETDSelect");
    if (hExistingWnd) {
        PostMessageW(hExistingWnd, WM_HOTKEY, ID_HOTKEY_CAPTURE, 0);
        return 0;
    }

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"ETDSelect_SingleInstance_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        return 0;
    }

    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, NULL);

    // Initialize Common Controls
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    // Run application
    App& app = App::GetInstance();
    if (app.Init(hInstance)) {
        app.Run();
    }
    app.Cleanup();

    // Shutdown GDI+
    Gdiplus::GdiplusShutdown(gdiplusToken);
    CloseHandle(hMutex);
    return 0;
}
