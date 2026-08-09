#pragma once
#include <windows.h>
#include <gdiplus.h>

class Overlay {
public:
    static void Start();
    static bool IsActive() { return s_hwnd != NULL; }
private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void RegisterWindowClass(HINSTANCE hInstance);
    static HBITMAP CaptureScreen(int& width, int& height, int& originX, int& originY);

    static HWND s_hwnd;
    static HBITMAP s_screenBitmap;
    static int s_screenW, s_screenH;
    static int s_originX, s_originY;
    static bool s_isSelecting;
    static POINT s_startPt, s_endPt;
    static bool s_registered;
};
