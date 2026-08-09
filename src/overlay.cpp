#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <algorithm>
#include "overlay.h"
#include "editor.h"
#include "app.h"
#include "resource.h"

using namespace Gdiplus;
using std::min;
using std::max;

HWND Overlay::s_hwnd = NULL;
HBITMAP Overlay::s_screenBitmap = NULL;
int Overlay::s_screenW = 0;
int Overlay::s_screenH = 0;
int Overlay::s_originX = 0;
int Overlay::s_originY = 0;
bool Overlay::s_isSelecting = false;
POINT Overlay::s_startPt = { 0, 0 };
POINT Overlay::s_endPt = { 0, 0 };
bool Overlay::s_registered = false;

HBITMAP Overlay::CaptureScreen(int& width, int& height, int& originX, int& originY) {
    originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    SelectObject(hdcMem, hBitmap);
    BitBlt(hdcMem, 0, 0, width, height, hdcScreen, originX, originY, SRCCOPY);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hBitmap;
}

void Overlay::RegisterWindowClass(HINSTANCE hInstance) {
    if (s_registered) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.lpszClassName = L"ETDSelectOverlayClass";
    RegisterClassW(&wc);
    s_registered = true;
}

void Overlay::Start() {
    if (s_hwnd != NULL) return;

    if (s_screenBitmap) {
        DeleteObject(s_screenBitmap);
        s_screenBitmap = NULL;
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    RegisterWindowClass(hInstance);

    s_screenBitmap = CaptureScreen(s_screenW, s_screenH, s_originX, s_originY);
    s_isSelecting = false;
    s_startPt = { 0, 0 };
    s_endPt = { 0, 0 };

    s_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ETDSelectOverlayClass",
        L"",
        WS_POPUP | WS_VISIBLE,
        s_originX, s_originY, s_screenW, s_screenH,
        NULL, NULL, hInstance, NULL
    );

    if (!s_hwnd) {
        if (s_screenBitmap) {
            DeleteObject(s_screenBitmap);
            s_screenBitmap = NULL;
        }
        return;
    }

    SetForegroundWindow(s_hwnd);
    SetFocus(s_hwnd);
    UpdateWindow(s_hwnd);

    MSG msg;
    while (s_hwnd != NULL && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (s_screenBitmap) {
        DeleteObject(s_screenBitmap);
        s_screenBitmap = NULL;
    }
    s_hwnd = NULL;
}

LRESULT CALLBACK Overlay::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, s_screenW, s_screenH);
        HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

        HDC hdcScreenBmp = CreateCompatibleDC(hdcMem);
        HGDIOBJ hOldScreen = SelectObject(hdcScreenBmp, s_screenBitmap);
        BitBlt(hdcMem, 0, 0, s_screenW, s_screenH, hdcScreenBmp, 0, 0, SRCCOPY);

        {
            Graphics graphics(hdcMem);

            SolidBrush darkBrush(Color(102, 0, 0, 0));
            graphics.FillRectangle(&darkBrush, 0, 0, s_screenW, s_screenH);

            if (s_isSelecting || (s_startPt.x != s_endPt.x && s_startPt.y != s_endPt.y)) {
                int left = min(s_startPt.x, s_endPt.x);
                int top = min(s_startPt.y, s_endPt.y);
                int width = abs((int)(s_startPt.x - s_endPt.x));
                int height = abs((int)(s_startPt.y - s_endPt.y));

                BitBlt(hdcMem, left, top, width, height, hdcScreenBmp, left, top, SRCCOPY);

                Pen cyanPen(Color(255, 0, 255, 255), 2.0f);
                graphics.DrawRectangle(&cyanPen, left, top, width, height);

                WCHAR buf[64];
                wsprintfW(buf, L"%d x %d", width, height);
                FontFamily fontFamily(L"Segoe UI");
                Font font(&fontFamily, 12.0f, FontStyleBold, UnitPixel);
                SolidBrush textBrush(Color(255, 255, 255, 255));
                SolidBrush bgBrush(Color(200, 0, 0, 0));

                REAL textY = (top > 22) ? (REAL)(top - 20) : (REAL)(top + height + 5);
                RectF measureRect;
                graphics.MeasureString(buf, -1, &font, PointF((REAL)left, textY), &measureRect);
                graphics.FillRectangle(&bgBrush, measureRect);
                graphics.DrawString(buf, -1, &font, PointF((REAL)left + 2, textY + 2), &textBrush);
            }
        }

        SelectObject(hdcScreenBmp, hOldScreen);
        DeleteDC(hdcScreenBmp);

        BitBlt(hdc, 0, 0, s_screenW, s_screenH, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
        s_startPt.x = GET_X_LPARAM(lParam);
        s_startPt.y = GET_Y_LPARAM(lParam);
        s_endPt = s_startPt;
        s_isSelecting = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (s_isSelecting) {
            s_endPt.x = GET_X_LPARAM(lParam);
            s_endPt.y = GET_Y_LPARAM(lParam);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONUP:
        if (s_isSelecting) {
            s_isSelecting = false;
            ReleaseCapture();
            s_endPt.x = GET_X_LPARAM(lParam);
            s_endPt.y = GET_Y_LPARAM(lParam);

            int width = abs((int)(s_endPt.x - s_startPt.x));
            int height = abs((int)(s_endPt.y - s_startPt.y));

            if (width > 5 && height > 5) {
                HWND localHwnd = s_hwnd;
                s_hwnd = NULL;

                RECT selRect;
                selRect.left = min(s_startPt.x, s_endPt.x);
                selRect.top = min(s_startPt.y, s_endPt.y);
                selRect.right = max(s_startPt.x, s_endPt.x);
                selRect.bottom = max(s_startPt.y, s_endPt.y);

                HBITMAP hbmScreen = s_screenBitmap;
                s_screenBitmap = NULL;

                DestroyWindow(localHwnd);

                Editor::Start(selRect, hbmScreen);
            } else {
                s_startPt = {0, 0};
                s_endPt = {0, 0};
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;

    case WM_RBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW(hMenu, MF_STRING, ID_MENU_HELP, L"Nasıl Kullanılır?");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_ABOUT, L"Uygulama Hakkında");
        AppendMenuW(hMenu, MF_STRING, ID_MENU_CLOSE_SEL, L"Seçimi Kapat");
        AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(hMenu, MF_STRING, ID_MENU_QUIT_APP, L"Uygulamayı Arka Plandan Kapat");

        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
        DestroyMenu(hMenu);

        if (cmd == ID_MENU_HELP) {
            App::GetInstance().ShowHelp();
        } else if (cmd == ID_MENU_ABOUT) {
            App::GetInstance().ShowAbout();
        } else if (cmd == ID_MENU_CLOSE_SEL) {
            HWND localHwnd = s_hwnd;
            s_hwnd = NULL;
            DestroyWindow(localHwnd);
            if (s_screenBitmap) {
                DeleteObject(s_screenBitmap);
                s_screenBitmap = NULL;
            }
        } else if (cmd == ID_MENU_QUIT_APP) {
            int res = MessageBoxW(hwnd, L"ETDSelect arka plandan tamamen kapatılacak.\r\nOnaylıyor musunuz?", L"Uygulamayı Kapat", MB_YESNO | MB_ICONQUESTION);
            if (res == IDYES) {
                HWND localHwnd = s_hwnd;
                s_hwnd = NULL;
                DestroyWindow(localHwnd);
                App::GetInstance().Cleanup();
                PostQuitMessage(0);
                ExitProcess(0);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            HWND localHwnd = s_hwnd;
            s_hwnd = NULL;
            DestroyWindow(localHwnd);
            if (s_screenBitmap) {
                DeleteObject(s_screenBitmap);
                s_screenBitmap = NULL;
            }
        }
        return 0;

    case WM_DESTROY:
        if (s_screenBitmap) {
            DeleteObject(s_screenBitmap);
            s_screenBitmap = NULL;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
