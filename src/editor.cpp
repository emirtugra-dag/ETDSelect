#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>
#include <ctime>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "editor.h"
#include "app.h"
#include "resource.h"
#include "i18n.h"

using namespace Gdiplus;
using std::min;
using std::max;

HWND Editor::s_hwnd = NULL;
HBITMAP Editor::s_baseBitmap = NULL;
RECT Editor::s_selectionRect = {0, 0, 0, 0};
int Editor::s_screenW = 0;
int Editor::s_screenH = 0;
int Editor::s_originX = 0;
int Editor::s_originY = 0;
POINT Editor::s_mousePt = {0, 0};

bool Editor::s_isResizing = false;
ResizeHandle Editor::s_activeHandle = ResizeHandle::HANDLE_NONE;
RECT Editor::s_resizeStartRect = {0, 0, 0, 0};
POINT Editor::s_resizeStartPt = {0, 0};

bool Editor::s_isMovingSelection = false;
POINT Editor::s_moveStartPt = {0, 0};
RECT Editor::s_moveStartRect = {0, 0, 0, 0};

RECT Editor::s_toolbarRect = {0, 0, 0, 0};

std::vector<DrawAction> Editor::s_actions;
ToolType Editor::s_currentTool = ToolType::TOOL_DRAW;
COLORREF Editor::s_currentColor = RGB(255, 0, 0);
int Editor::s_currentThickness = 3;
bool Editor::s_isDrawing = false;
DrawAction Editor::s_activeAction = {};

DrawAction& g_editorActiveAction = Editor::s_activeAction;
bool& g_editorIsDrawing = Editor::s_isDrawing;

bool Editor::s_colorPickerVisible = false;
RECT Editor::s_colorPickerRect = {0, 0, 0, 0};

bool Editor::s_registered = false;

static HWND s_editHwnd = NULL;
static HBRUSH s_editBgBrush = NULL;
static HFONT s_editFont = NULL;

static void CommitTextEdit(HWND parentHwnd) {
    if (!s_editHwnd) return;
    int len = GetWindowTextLengthW(s_editHwnd);
    if (len > 0) {
        std::vector<wchar_t> buf(len + 1, 0);
        GetWindowTextW(s_editHwnd, buf.data(), len + 1);
        g_editorActiveAction.text = buf.data();
        g_editorIsDrawing = false;
        Editor::s_actions.push_back(g_editorActiveAction);
    } else {
        g_editorIsDrawing = false;
    }
    DestroyWindow(s_editHwnd);
    s_editHwnd = NULL;
    if (s_editFont) { DeleteObject(s_editFont); s_editFont = NULL; }
    if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
    InvalidateRect(parentHwnd, NULL, FALSE);
}

static void CancelTextEdit(HWND parentHwnd) {
    if (!s_editHwnd) return;
    g_editorIsDrawing = false;
    DestroyWindow(s_editHwnd);
    s_editHwnd = NULL;
    if (s_editFont) { DeleteObject(s_editFont); s_editFont = NULL; }
    if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
    InvalidateRect(parentHwnd, NULL, FALSE);
}

LRESULT CALLBACK InlineEditProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    if (uMsg == WM_KEYDOWN && wParam == VK_ESCAPE) {
        CancelTextEdit(GetParent(hWnd));
        return 0;
    }
    if (uMsg == WM_CHAR || uMsg == WM_KEYDOWN) {
        // After typing, auto-resize the edit control to fit content
        LRESULT result = DefSubclassProc(hWnd, uMsg, wParam, lParam);
        // Auto-resize height based on line count
        int lineCount = (int)SendMessageW(hWnd, EM_GETLINECOUNT, 0, 0);
        if (lineCount < 1) lineCount = 1;
        HDC hdc = GetDC(hWnd);
        HFONT hOldFont = (HFONT)SelectObject(hdc, s_editFont);
        TEXTMETRICW tm;
        GetTextMetricsW(hdc, &tm);
        int lineH = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(hdc, hOldFont);
        ReleaseDC(hWnd, hdc);
        int newH = lineCount * lineH + 8;
        // Also auto-resize width based on longest line
        int maxLineW = 150;
        for (int i = 0; i < lineCount; i++) {
            int lineLen = (int)SendMessageW(hWnd, EM_LINELENGTH, SendMessageW(hWnd, EM_LINEINDEX, i, 0), 0);
            if (lineLen > 0) {
                std::vector<wchar_t> lineBuf(lineLen + 2, 0);
                *(WORD*)lineBuf.data() = (WORD)(lineLen + 1);
                SendMessageW(hWnd, EM_GETLINE, i, (LPARAM)lineBuf.data());
                lineBuf[lineLen] = 0;
                HDC hdc2 = GetDC(hWnd);
                HFONT hOldFont2 = (HFONT)SelectObject(hdc2, s_editFont);
                SIZE sz;
                GetTextExtentPoint32W(hdc2, lineBuf.data(), lineLen, &sz);
                SelectObject(hdc2, hOldFont2);
                ReleaseDC(hWnd, hdc2);
                if (sz.cx + 16 > maxLineW) maxLineW = sz.cx + 16;
            }
        }
        RECT rc;
        GetWindowRect(hWnd, &rc);
        POINT pos = { rc.left, rc.top };
        ScreenToClient(GetParent(hWnd), &pos);
        SetWindowPos(hWnd, NULL, pos.x, pos.y, maxLineW, newH, SWP_NOZORDER);
        InvalidateRect(GetParent(hWnd), NULL, FALSE);
        return result;
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void Editor::RegisterWindowClass(HINSTANCE hInstance) {
    if (s_registered) return;
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"ETDSelectEditorClass";
    RegisterClassW(&wc);
    s_registered = true;
}

void Editor::Start(RECT selection, HBITMAP screenCapture) {
    if (s_hwnd != NULL) {
        if (screenCapture) DeleteObject(screenCapture);
        return;
    }

    if (s_baseBitmap) {
        DeleteObject(s_baseBitmap);
        s_baseBitmap = NULL;
    }

    HINSTANCE hInstance = GetModuleHandle(NULL);
    RegisterWindowClass(hInstance);

    s_originX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    s_originY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    s_screenW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s_screenH = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    s_selectionRect = selection;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    s_baseBitmap = CreateCompatibleBitmap(hdcScreen, s_screenW, s_screenH);
    SelectObject(hdcMem, s_baseBitmap);

    HDC hdcCap = CreateCompatibleDC(hdcScreen);
    SelectObject(hdcCap, screenCapture);
    BitBlt(hdcMem, 0, 0, s_screenW, s_screenH, hdcCap, 0, 0, SRCCOPY);
    DeleteDC(hdcCap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    if (screenCapture) {
        DeleteObject(screenCapture);
    }

    s_actions.clear();
    s_currentTool = ToolType::TOOL_DRAW;
    s_currentColor = RGB(255, 0, 0);
    s_currentThickness = 3;
    s_isDrawing = false;
    s_colorPickerVisible = false;
    s_isResizing = false;
    s_isMovingSelection = false;

    s_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"ETDSelectEditorClass",
        L"",
        WS_POPUP | WS_VISIBLE,
        s_originX, s_originY, s_screenW, s_screenH,
        NULL, NULL, hInstance, NULL
    );

    SetForegroundWindow(s_hwnd);
    SetFocus(s_hwnd);
    UpdateWindow(s_hwnd);

    HICON hIconBig = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    SendMessageW(s_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(s_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

    MSG msg;
    while (s_hwnd != NULL && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void Editor::Shutdown() {
    if (s_editHwnd) {
        DestroyWindow(s_editHwnd);
        s_editHwnd = NULL;
    }
    if (s_editFont) { DeleteObject(s_editFont); s_editFont = NULL; }
    if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
    if (s_hwnd) {
        HWND localHwnd = s_hwnd;
        s_hwnd = NULL;
        DestroyWindow(localHwnd);
    }
    if (s_baseBitmap) {
        DeleteObject(s_baseBitmap);
        s_baseBitmap = NULL;
    }
    s_isDrawing = false;
    s_colorPickerVisible = false;
    s_isResizing = false;
    s_isMovingSelection = false;
}

ResizeHandle Editor::GetHandleAtPoint(int x, int y) {
    int l = min((int)s_selectionRect.left, (int)s_selectionRect.right);
    int t = min((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int r = max((int)s_selectionRect.left, (int)s_selectionRect.right);
    int b = max((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int mx = (l + r) / 2;
    int my = (t + b) / 2;

    int hs = HANDLE_SIZE;

    auto inRect = [&](int cx, int cy) {
        return (x >= cx - hs && x <= cx + hs && y >= cy - hs && y <= cy + hs);
    };

    if (inRect(l, t)) return ResizeHandle::HANDLE_TOP_LEFT;
    if (inRect(mx, t)) return ResizeHandle::HANDLE_TOP;
    if (inRect(r, t)) return ResizeHandle::HANDLE_TOP_RIGHT;
    if (inRect(l, my)) return ResizeHandle::HANDLE_LEFT;
    if (inRect(r, my)) return ResizeHandle::HANDLE_RIGHT;
    if (inRect(l, b)) return ResizeHandle::HANDLE_BOTTOM_LEFT;
    if (inRect(mx, b)) return ResizeHandle::HANDLE_BOTTOM;
    if (inRect(r, b)) return ResizeHandle::HANDLE_BOTTOM_RIGHT;

    return ResizeHandle::HANDLE_NONE;
}

HCURSOR Editor::GetCursorForHandle(ResizeHandle handle) {
    switch (handle) {
        case ResizeHandle::HANDLE_TOP_LEFT:
        case ResizeHandle::HANDLE_BOTTOM_RIGHT:
            return LoadCursor(NULL, IDC_SIZENWSE);
        case ResizeHandle::HANDLE_TOP_RIGHT:
        case ResizeHandle::HANDLE_BOTTOM_LEFT:
            return LoadCursor(NULL, IDC_SIZENESW);
        case ResizeHandle::HANDLE_TOP:
        case ResizeHandle::HANDLE_BOTTOM:
            return LoadCursor(NULL, IDC_SIZENS);
        case ResizeHandle::HANDLE_LEFT:
        case ResizeHandle::HANDLE_RIGHT:
            return LoadCursor(NULL, IDC_SIZEWE);
        default:
            return LoadCursor(NULL, IDC_ARROW);
    }
}

bool Editor::IsPointNearSelectionBorder(int x, int y) {
    int l = min((int)s_selectionRect.left, (int)s_selectionRect.right);
    int t = min((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int r = max((int)s_selectionRect.left, (int)s_selectionRect.right);
    int b = max((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int margin = 12;

    bool nearLeft   = (x >= l - margin && x <= l + margin && y >= t - margin && y <= b + margin);
    bool nearRight  = (x >= r - margin && x <= r + margin && y >= t - margin && y <= b + margin);
    bool nearTop    = (y >= t - margin && y <= t + margin && x >= l - margin && x <= r + margin);
    bool nearBottom = (y >= b - margin && y <= b + margin && x >= l - margin && x <= r + margin);

    return (nearLeft || nearRight || nearTop || nearBottom);
}

bool Editor::ShouldMoveSelection(int x, int y) {
    RECT selNorm;
    selNorm.left = min((int)s_selectionRect.left, (int)s_selectionRect.right);
    selNorm.top = min((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    selNorm.right = max((int)s_selectionRect.left, (int)s_selectionRect.right);
    selNorm.bottom = max((int)s_selectionRect.top, (int)s_selectionRect.bottom);

    POINT pt = { x, y };

    if (!PtInRect(&selNorm, pt) && !IsPointNearSelectionBorder(x, y)) {
        return false;
    }

    if (s_currentTool == ToolType::TOOL_MOVE) return true;
    if (GetKeyState(VK_SPACE) & 0x8000) return true;
    if (IsPointNearSelectionBorder(x, y)) return true;

    return false;
}

LRESULT CALLBACK Editor::WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hbmMem = CreateCompatibleBitmap(hdc, s_screenW, s_screenH);
            HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);

            {
                Graphics g(hdcMem);
                
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetCompositingQuality(CompositingQualityHighSpeed);
                g.SetInterpolationMode(InterpolationModeBilinear);
                g.SetPixelOffsetMode(PixelOffsetModeHighSpeed);

                RenderCanvas(g, s_screenW, s_screenH);
                RenderToolbar(g, s_screenW, s_screenH);
                if (s_colorPickerVisible) {
                    RenderColorPicker(g);
                }
            }

            BitBlt(hdc, 0, 0, s_screenW, s_screenH, hdcMem, 0, 0, SRCCOPY);

            SelectObject(hdcMem, hOld);
            DeleteObject(hbmMem);
            DeleteDC(hdcMem);

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);

            ResizeHandle handle = GetHandleAtPoint(pt.x, pt.y);
            if (handle != ResizeHandle::HANDLE_NONE) {
                SetCursor(GetCursorForHandle(handle));
                return TRUE;
            }
            if (PtInRect(&s_toolbarRect, pt)) {
                SetCursor(LoadCursor(NULL, IDC_ARROW));
                return TRUE;
            }

            if (ShouldMoveSelection(pt.x, pt.y)) {
                SetCursor(LoadCursor(NULL, IDC_SIZEALL));
                return TRUE;
            }
            break;
        }

        case WM_MBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            RECT selNorm;
            selNorm.left = min(s_selectionRect.left, s_selectionRect.right);
            selNorm.top = min(s_selectionRect.top, s_selectionRect.bottom);
            selNorm.right = max(s_selectionRect.left, s_selectionRect.right);
            selNorm.bottom = max(s_selectionRect.top, s_selectionRect.bottom);

            POINT pt = { x, y };
            if (PtInRect(&selNorm, pt) || IsPointNearSelectionBorder(x, y)) {
                s_isMovingSelection = true;
                s_moveStartPt = pt;
                s_moveStartRect = selNorm;
                SetCapture(hwnd);
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (s_editHwnd) {
                // Check if click is outside the edit control
                RECT editRect;
                GetWindowRect(s_editHwnd, &editRect);
                POINT editPt = { x, y };
                ClientToScreen(hwnd, &editPt);
                if (!PtInRect(&editRect, editPt)) {
                    CommitTextEdit(hwnd);
                    return 0;
                }
                break; // Let click pass to edit control
            }

            POINT pt = { x, y };

            if (s_colorPickerVisible) {
                OnToolbarClick(x, y);
                return 0;
            }

            if (PtInRect(&s_toolbarRect, pt)) {
                OnToolbarClick(x, y);
                return 0;
            }

            ResizeHandle handle = GetHandleAtPoint(x, y);
            if (handle != ResizeHandle::HANDLE_NONE) {
                s_isResizing = true;
                s_activeHandle = handle;
                s_resizeStartRect = s_selectionRect;
                s_resizeStartPt = { x, y };
                SetCapture(hwnd);
                return 0;
            }

            // Selection Dragging / Moving check
            if (ShouldMoveSelection(x, y)) {
                RECT selNorm;
                selNorm.left = min(s_selectionRect.left, s_selectionRect.right);
                selNorm.top = min(s_selectionRect.top, s_selectionRect.bottom);
                selNorm.right = max(s_selectionRect.left, s_selectionRect.right);
                selNorm.bottom = max(s_selectionRect.top, s_selectionRect.bottom);

                s_isMovingSelection = true;
                s_moveStartPt = pt;
                s_moveStartRect = selNorm;
                SetCapture(hwnd);
                return 0;
            }

            OnMouseDown(x, y);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            s_mousePt = { x, y };

            if (s_isMovingSelection) {
                int dx = x - s_moveStartPt.x;
                int dy = y - s_moveStartPt.y;

                int w = s_moveStartRect.right - s_moveStartRect.left;
                int h = s_moveStartRect.bottom - s_moveStartRect.top;

                int newLeft = max(0, min((int)(s_moveStartRect.left + dx), s_screenW - w));
                int newTop = max(0, min((int)(s_moveStartRect.top + dy), s_screenH - h));

                s_selectionRect = { newLeft, newTop, newLeft + w, newTop + h };
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (s_isResizing) {
                int dx = x - s_resizeStartPt.x;
                int dy = y - s_resizeStartPt.y;

                RECT r = s_resizeStartRect;

                if (s_activeHandle == ResizeHandle::HANDLE_TOP_LEFT ||
                    s_activeHandle == ResizeHandle::HANDLE_TOP ||
                    s_activeHandle == ResizeHandle::HANDLE_TOP_RIGHT) {
                    r.top = min(r.bottom - 20, r.top + dy);
                }
                if (s_activeHandle == ResizeHandle::HANDLE_BOTTOM_LEFT ||
                    s_activeHandle == ResizeHandle::HANDLE_BOTTOM ||
                    s_activeHandle == ResizeHandle::HANDLE_BOTTOM_RIGHT) {
                    r.bottom = max(r.top + 20, r.bottom + dy);
                }
                if (s_activeHandle == ResizeHandle::HANDLE_TOP_LEFT ||
                    s_activeHandle == ResizeHandle::HANDLE_LEFT ||
                    s_activeHandle == ResizeHandle::HANDLE_BOTTOM_LEFT) {
                    r.left = min(r.right - 20, r.left + dx);
                }
                if (s_activeHandle == ResizeHandle::HANDLE_TOP_RIGHT ||
                    s_activeHandle == ResizeHandle::HANDLE_RIGHT ||
                    s_activeHandle == ResizeHandle::HANDLE_BOTTOM_RIGHT) {
                    r.right = max(r.left + 20, r.right + dx);
                }

                s_selectionRect = r;
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            OnMouseMove(x, y);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MBUTTONUP:
        case WM_LBUTTONUP: {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            if (s_isMovingSelection) {
                s_isMovingSelection = false;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            if (s_isResizing) {
                s_isResizing = false;
                s_activeHandle = ResizeHandle::HANDLE_NONE;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            OnMouseUp(x, y);
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_MENU_HELP, I18n::Get("MENU_HELP"));
            AppendMenuW(hMenu, MF_STRING, ID_MENU_ABOUT, I18n::Get("MENU_ABOUT"));
            AppendMenuW(hMenu, MF_STRING, ID_MENU_CLOSE_SEL, I18n::Get("MENU_CLOSE_SEL"));
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_MENU_QUIT_APP, I18n::Get("MENU_EXIT"));

            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);

            if (cmd == ID_MENU_HELP) {
                App::GetInstance().ShowHelp();
            } else if (cmd == ID_MENU_ABOUT) {
                App::GetInstance().ShowAbout();
            } else if (cmd == ID_MENU_CLOSE_SEL) {
                Shutdown();
            } else if (cmd == ID_MENU_QUIT_APP) {
                int lang = App::GetInstance().GetSettings().GetEffectiveLanguage();
                const wchar_t* msgStr = (lang == 1) ? L"ETDSelect arka plandan tamamen kapatılacak.\r\nOnaylıyor musunuz?" : L"ETDSelect will be closed completely.\r\nAre you sure?";
                const wchar_t* titleStr = (lang == 1) ? L"Uygulamayı Kapat" : L"Exit Application";
                int res = MessageBoxW(hwnd, msgStr, titleStr, MB_YESNO | MB_ICONQUESTION);
                if (res == IDYES) {
                    Shutdown();
                    App::GetInstance().Cleanup();
                    PostQuitMessage(0);
                    ExitProcess(0);
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                Shutdown();
            } else if (wParam == 'Z' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                Undo();
            } else if (wParam == 'C' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                CopyToClipboard(hwnd);
                Shutdown();
            } else if (wParam == 'S' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                SaveScreenshot();
                Shutdown();
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (delta > 0 && s_currentThickness < 20) s_currentThickness++;
            else if (delta < 0 && s_currentThickness > 1) s_currentThickness--;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_APP + 10:
            CommitTextEdit(hwnd);
            return 0;

        case WM_APP + 11:
            CancelTextEdit(hwnd);
            return 0;

        case WM_CTLCOLOREDIT: {
            if ((HWND)lParam == s_editHwnd) {
                HDC hdcEdit = (HDC)wParam;
                SetTextColor(hdcEdit, s_currentColor);
                SetBkMode(hdcEdit, TRANSPARENT);
                if (!s_editBgBrush) {
                    s_editBgBrush = CreateSolidBrush(RGB(255, 255, 255));
                }
                return (LRESULT)s_editBgBrush;
            }
            break;
        }

        case WM_DESTROY:
            if (s_baseBitmap) {
                DeleteObject(s_baseBitmap);
                s_baseBitmap = NULL;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

void Editor::RenderCanvas(Graphics& g, int screenW, int screenH) {
    if (!s_baseBitmap) return;

    Bitmap baseImage(s_baseBitmap, NULL);

    g.DrawImage(&baseImage, 0, 0, screenW, screenH);

    Region fullRegion(Rect(0, 0, screenW, screenH));

    int selLeft = min((int)s_selectionRect.left, (int)s_selectionRect.right);
    int selTop = min((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int selW = abs((int)(s_selectionRect.right - s_selectionRect.left));
    int selH = abs((int)(s_selectionRect.bottom - s_selectionRect.top));

    Region selRegion(Rect(selLeft, selTop, selW, selH));
    fullRegion.Exclude(&selRegion);

    SolidBrush darkOverlayBrush(Color(120, 0, 0, 0));
    g.FillRegion(&darkOverlayBrush, &fullRegion);

    Pen borderPen(Color(255, 0, 255, 255), 2.0f);
    g.DrawRectangle(&borderPen, selLeft, selTop, selW, selH);

    auto drawHandle = [&](int cx, int cy) {
        int hs = HANDLE_SIZE;
        SolidBrush hBrush(Color(255, 255, 255, 255));
        Pen hPen(Color(255, 0, 120, 215), 1.5f);
        g.FillRectangle(&hBrush, cx - hs/2, cy - hs/2, hs, hs);
        g.DrawRectangle(&hPen, cx - hs/2, cy - hs/2, hs, hs);
    };

    int l = selLeft, t = selTop, r = selLeft + selW, b = selTop + selH;
    int mx = (l + r) / 2, my = (t + b) / 2;

    drawHandle(l, t);   drawHandle(mx, t);  drawHandle(r, t);
    drawHandle(l, my);                      drawHandle(r, my);
    drawHandle(l, b);   drawHandle(mx, b);  drawHandle(r, b);

    WCHAR buf[64];
    wsprintfW(buf, L"%d x %d", selW, selH);
    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 12.0f, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 255, 255, 255));
    SolidBrush bgBrush(Color(200, 0, 0, 0));

    REAL textY = (selTop > 22) ? (REAL)(selTop - 20) : (REAL)(selTop + selH + 5);
    RectF measureRect;
    g.MeasureString(buf, -1, &font, PointF((REAL)selLeft, textY), &measureRect);
    g.FillRectangle(&bgBrush, measureRect);
    g.DrawString(buf, -1, &font, PointF((REAL)selLeft + 2, textY + 2), &textBrush);

    for (const auto& action : s_actions) {
        RenderAction(g, action);
    }

    if (s_isDrawing) {
        RenderAction(g, s_activeAction);
    }
}

void Editor::RenderAction(Graphics& g, const DrawAction& action) {
    Color col(action.alpha > 0 ? action.alpha : 255, GetRValue(action.color), GetGValue(action.color), GetBValue(action.color));
    SolidBrush brush(col);
    Pen pen(col, (REAL)action.thickness);
    pen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
    pen.SetLineJoin(LineJoinRound);

    switch (action.type) {
        case ToolType::TOOL_DRAW:
        case ToolType::TOOL_HIGHLIGHTER: {
            if (action.points.size() > 1) {
                if (action.type == ToolType::TOOL_HIGHLIGHTER) {
                    Color hCol(80, GetRValue(action.color), GetGValue(action.color), GetBValue(action.color));
                    Pen hPen(hCol, (REAL)action.thickness * 3);
                    hPen.SetLineCap(LineCapSquare, LineCapSquare, DashCapFlat);
                    hPen.SetLineJoin(LineJoinMiter);
                    std::vector<Point> pts;
                    for (auto pt : action.points) pts.push_back(Point(pt.x, pt.y));
                    g.DrawLines(&hPen, pts.data(), (INT)pts.size());
                } else {
                    std::vector<Point> pts;
                    for (auto pt : action.points) pts.push_back(Point(pt.x, pt.y));
                    g.DrawCurve(&pen, pts.data(), (INT)pts.size());
                }
            }
            break;
        }
        case ToolType::TOOL_ARROW: {
            pen.SetEndCap(LineCapArrowAnchor);
            g.DrawLine(&pen, (INT)action.startPt.x, (INT)action.startPt.y, (INT)action.endPt.x, (INT)action.endPt.y);
            break;
        }
        case ToolType::TOOL_RECT_HOLLOW: {
            int x = min((int)action.startPt.x, (int)action.endPt.x);
            int y = min((int)action.startPt.y, (int)action.endPt.y);
            int w = abs((int)(action.startPt.x - action.endPt.x));
            int h = abs((int)(action.startPt.y - action.endPt.y));
            g.DrawRectangle(&pen, x, y, w, h);
            break;
        }
        case ToolType::TOOL_RECT_FILLED: {
            int x = min((int)action.startPt.x, (int)action.endPt.x);
            int y = min((int)action.startPt.y, (int)action.endPt.y);
            int w = abs((int)(action.startPt.x - action.endPt.x));
            int h = abs((int)(action.startPt.y - action.endPt.y));
            Color fCol(255, GetRValue(action.color), GetGValue(action.color), GetBValue(action.color));
            SolidBrush fBrush(fCol);
            g.FillRectangle(&fBrush, x, y, w, h);
            g.DrawRectangle(&pen, x, y, w, h);
            break;
        }
        case ToolType::TOOL_TEXT: {
            FontFamily fontFamily(L"Arial");
            Font font(&fontFamily, (REAL)(action.thickness * 4 + 10), FontStyleRegular, UnitPixel);
            g.DrawString(action.text.c_str(), -1, &font, PointF((REAL)action.textPos.x, (REAL)action.textPos.y), &brush);
            break;
        }
        case ToolType::TOOL_MOSAIC: {
            if (s_baseBitmap) {
                int x = min((int)action.startPt.x, (int)action.endPt.x);
                int y = min((int)action.startPt.y, (int)action.endPt.y);
                int w = abs((int)(action.startPt.x - action.endPt.x));
                int h = abs((int)(action.startPt.y - action.endPt.y));
                if (w > 0 && h > 0) {
                    Bitmap baseImage(s_baseBitmap, NULL);
                    int blockSize = max(8, (int)action.mosaicBlockSize);

                    for (int by = y; by < y + h; by += blockSize) {
                        for (int bx = x; bx < x + w; bx += blockSize) {
                            int bw = min(blockSize, x + w - bx);
                            int bh = min(blockSize, y + h - by);

                            int sampleXs[] = { bx + bw/4, bx + bw*3/4, bx + bw/2, bx + bw/4, bx + bw*3/4 };
                            int sampleYs[] = { by + bh/4, by + bh/4, by + bh/2, by + bh*3/4, by + bh*3/4 };
                            int r = 0, gv = 0, b = 0, count = 0;

                            for (int i = 0; i < 5; i++) {
                                if (sampleXs[i] >= 0 && sampleXs[i] < s_screenW && sampleYs[i] >= 0 && sampleYs[i] < s_screenH) {
                                    Color c;
                                    baseImage.GetPixel(sampleXs[i], sampleYs[i], &c);
                                    r += c.GetR();
                                    gv += c.GetG();
                                    b += c.GetB();
                                    count++;
                                }
                            }

                            if (count > 0) {
                                r /= count; gv /= count; b /= count;
                                SolidBrush mBrush(Color(255, r, gv, b));
                                g.FillRectangle(&mBrush, bx, by, bw, bh);
                            }
                        }
                    }

                    if (s_isDrawing && &action == &s_activeAction) {
                        Pen dashPen(Color(255, 0, 255, 255), 1.5f);
                        dashPen.SetDashStyle(DashStyleDash);
                        g.DrawRectangle(&dashPen, x, y, w, h);
                    }
                }
            }
            break;
        }
        case ToolType::TOOL_ERASER: {
            if (s_baseBitmap && action.points.size() > 0) {
                Bitmap baseImage(s_baseBitmap, NULL);
                GraphicsPath path;
                int radius = max(8, action.thickness * 4);

                if (action.points.size() == 1) {
                    path.AddEllipse(action.points[0].x - radius, action.points[0].y - radius, radius * 2, radius * 2);
                } else {
                    std::vector<Point> pts;
                    for (auto pt : action.points) {
                        pts.push_back(Point(pt.x, pt.y));
                    }
                    path.AddLines(pts.data(), (INT)pts.size());
                    Pen pathPen(Color(255, 0, 0, 0), (REAL)(radius * 2));
                    pathPen.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
                    pathPen.SetLineJoin(LineJoinRound);
                    path.Widen(&pathPen);
                }

                Region eraserRegion(&path);
                g.SetClip(&eraserRegion, CombineModeReplace);
                g.DrawImage(&baseImage, Rect(0, 0, s_screenW, s_screenH), 0, 0, s_screenW, s_screenH, UnitPixel);
                g.ResetClip();
            }
            break;
        }
        case ToolType::TOOL_LASSO: {
            Pen dashPen(Color(255, 0, 0, 0), 1.0f);
            dashPen.SetDashStyle(DashStyleDash);
            std::vector<Point> pts;
            for (auto pt : action.points) pts.push_back(Point(pt.x, pt.y));
            if (pts.size() > 1) {
                g.DrawPolygon(&dashPen, pts.data(), (INT)pts.size());
            }
            break;
        }
    }
}

void Editor::DrawToolIcon(Graphics& g, ToolType tool, float x, float y, float size, bool selected) {
    bool isDark = App::GetInstance().GetSettings().isDarkMode;

    if (selected) {
        SolidBrush selBrush(isDark ? Color(255, 0, 255, 255) : Color(255, 0, 120, 215));
        g.FillRectangle(&selBrush, x, y, size, size);
    }
    
    Color iconColNormal = isDark ? Color(255, 192, 192, 192) : Color(255, 50, 50, 50);
    Color iconColSelected = isDark ? Color(255, 0, 0, 0) : Color(255, 255, 255, 255);

    Pen pen(selected ? iconColSelected : iconColNormal, 2.0f);
    SolidBrush brush(selected ? iconColSelected : iconColNormal);

    float cx = x + size/2;
    float cy = y + size/2;
    float p = 4.0f;

    switch (tool) {
        case ToolType::TOOL_MOVE: {
            g.DrawLine(&pen, cx - 6, cy, cx + 6, cy);
            g.DrawLine(&pen, cx, cy - 6, cx, cy + 6);
            g.DrawLine(&pen, cx - 6, cy, cx - 3, cy - 3);
            g.DrawLine(&pen, cx - 6, cy, cx - 3, cy + 3);
            g.DrawLine(&pen, cx + 6, cy, cx + 3, cy - 3);
            g.DrawLine(&pen, cx + 6, cy, cx + 3, cy + 3);
            g.DrawLine(&pen, cx, cy - 6, cx - 3, cy - 3);
            g.DrawLine(&pen, cx, cy - 6, cx + 3, cy - 3);
            g.DrawLine(&pen, cx, cy + 6, cx - 3, cy + 3);
            g.DrawLine(&pen, cx, cy + 6, cx + 3, cy + 3);
            break;
        }
        case ToolType::TOOL_DRAW: {
            PointF pts[] = {
                PointF(x+p, cy), PointF(x+size/3, cy-4), 
                PointF(x+size*2/3, cy+4), PointF(x+size-p, cy)
            };
            g.DrawCurve(&pen, pts, 4);
            break;
        }
        case ToolType::TOOL_ARROW: {
            pen.SetEndCap(LineCapArrowAnchor);
            g.DrawLine(&pen, x+p, y+size-p, x+size-p, y+p);
            break;
        }
        case ToolType::TOOL_RECT_HOLLOW: {
            g.DrawRectangle(&pen, x+p, y+p, size-2*p, size-2*p);
            break;
        }
        case ToolType::TOOL_RECT_FILLED: {
            g.FillRectangle(&brush, x+p, y+p, size-2*p, size-2*p);
            break;
        }
        case ToolType::TOOL_TEXT: {
            FontFamily ff(L"Segoe UI");
            Font f(&ff, size - 2*p, FontStyleBold, UnitPixel);
            StringFormat sf;
            sf.SetAlignment(StringAlignmentCenter);
            sf.SetLineAlignment(StringAlignmentCenter);
            g.DrawString(L"T", 1, &f, PointF(cx, cy), &sf, &brush);
            break;
        }
        case ToolType::TOOL_MOSAIC: {
            float s = (size - 2*p) / 2;
            g.DrawRectangle(&pen, x+p, y+p, s, s);
            g.DrawRectangle(&pen, cx, y+p, s, s);
            g.DrawRectangle(&pen, x+p, cy, s, s);
            g.DrawRectangle(&pen, cx, cy, s, s);
            break;
        }
        case ToolType::TOOL_HIGHLIGHTER: {
            Color hIconCol = selected ? iconColSelected : iconColNormal;
            Pen hPen(hIconCol, 4.0f);
            hPen.SetLineCap(LineCapSquare, LineCapSquare, DashCapFlat);
            g.DrawLine(&hPen, x+p, y+size-p, x+size-p, y+p);
            break;
        }
        case ToolType::TOOL_ERASER: {
            g.DrawRectangle(&pen, x+p, y+size/4, size-2*p, size/2);
            g.DrawLine(&pen, x+p, y+size/4, x+size-p, y+size*3/4);
            g.DrawLine(&pen, x+p, y+size*3/4, x+size-p, y+size*3/4);
            break;
        }
    }
}

void Editor::RenderToolbar(Graphics& g, int screenW, int screenH) {
    bool isDark = App::GetInstance().GetSettings().isDarkMode;

    int numItems = 9 + 1 + 1 + 3 + 1 + 3; 
    int totalTbW = numItems * (ICON_SIZE + ICON_PAD) + ICON_PAD * 6 + 40;

    int preferredX = s_selectionRect.right - totalTbW;
    int preferredY = s_selectionRect.bottom + 8;

    if (preferredY + TOOLBAR_H > screenH - 10) {
        preferredY = s_selectionRect.top - TOOLBAR_H - 8;
    }

    int tbX = max(10, min(preferredX, screenW - totalTbW - 10));
    int tbY = max(10, min(preferredY, screenH - TOOLBAR_H - 10));

    s_toolbarRect = { tbX, tbY, tbX + totalTbW, tbY + TOOLBAR_H };

    Color bgCol = isDark ? Color(255, 26, 26, 46) : Color(255, 240, 242, 248);
    Color borderCol = isDark ? Color(255, 64, 64, 96) : Color(255, 180, 190, 210);
    Color txtCol = isDark ? Color(255, 220, 220, 220) : Color(255, 30, 30, 30);

    SolidBrush bgBrush(bgCol);
    g.FillRectangle(&bgBrush, tbX, tbY, totalTbW, TOOLBAR_H);
    Pen borderPen(borderCol, 1.0f);
    g.DrawRectangle(&borderPen, tbX, tbY, totalTbW, TOOLBAR_H);

    float cx = (float)tbX + ICON_PAD;
    float cy = (float)tbY + (TOOLBAR_H - ICON_SIZE) / 2.0f;

    ToolType tools[] = {
        ToolType::TOOL_MOVE, ToolType::TOOL_DRAW, ToolType::TOOL_ARROW, ToolType::TOOL_RECT_HOLLOW,
        ToolType::TOOL_RECT_FILLED, ToolType::TOOL_TEXT, ToolType::TOOL_MOSAIC,
        ToolType::TOOL_HIGHLIGHTER, ToolType::TOOL_ERASER
    };

    int hoveredIndex = -1;

    for (int i = 0; i < 9; i++) {
        DrawToolIcon(g, tools[i], cx, cy, (float)ICON_SIZE, s_currentTool == tools[i]);
        if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) {
            hoveredIndex = i;
        }
        cx += ICON_SIZE + ICON_PAD;
    }

    Pen sepPen(borderCol, 2.0f);
    g.DrawLine(&sepPen, cx + ICON_PAD, cy, cx + ICON_PAD, cy + ICON_SIZE);
    cx += ICON_PAD * 3;

    s_colorPickerRect = { (long)cx, (long)cy, (long)(cx + ICON_SIZE), (long)(cy + ICON_SIZE) };
    SolidBrush colBrush(Color(255, GetRValue(s_currentColor), GetGValue(s_currentColor), GetBValue(s_currentColor)));
    g.FillEllipse(&colBrush, (REAL)(cx + 2), (REAL)(cy + 2), (REAL)(ICON_SIZE - 4), (REAL)(ICON_SIZE - 4));
    Pen colBorder(isDark ? Color(255, 255, 255, 255) : Color(255, 50, 50, 50), 1.5f);
    g.DrawEllipse(&colBorder, (REAL)(cx + 2), (REAL)(cy + 2), (REAL)(ICON_SIZE - 4), (REAL)(ICON_SIZE - 4));

    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) {
        hoveredIndex = 9;
    }
    cx += ICON_SIZE + ICON_PAD * 2;

    FontFamily ff(L"Segoe UI");
    Font f(&ff, 13, FontStyleBold, UnitPixel);
    SolidBrush txtBrush(txtCol);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    
    g.DrawString(L"-", 1, &f, PointF(cx + ICON_SIZE/2, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 10;
    cx += ICON_SIZE;

    wchar_t tStr[10];
    swprintf(tStr, 10, L"%d", s_currentThickness);
    g.DrawString(tStr, -1, &f, PointF(cx + ICON_SIZE/2, cy + ICON_SIZE/2), &sf, &txtBrush);
    cx += ICON_SIZE;

    g.DrawString(L"+", 1, &f, PointF(cx + ICON_SIZE/2, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx - ICON_SIZE && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 10;
    cx += ICON_SIZE + ICON_PAD;
    
    g.DrawLine(&sepPen, cx + ICON_PAD, cy, cx + ICON_PAD, cy + ICON_SIZE);
    cx += ICON_PAD * 3;

    g.DrawString(L"⚙", 1, &f, PointF(cx + ICON_SIZE/2, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 11;
    cx += ICON_SIZE + ICON_PAD;

    g.DrawLine(&sepPen, cx + ICON_PAD, cy, cx + ICON_PAD, cy + ICON_SIZE);
    cx += ICON_PAD * 3;

    g.DrawString(L"Save", -1, &f, PointF(cx + ICON_SIZE, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE*2 && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 12;
    cx += ICON_SIZE * 2;
    
    g.DrawString(L"Copy", -1, &f, PointF(cx + ICON_SIZE, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE*2 && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 13;
    cx += ICON_SIZE * 2;

    g.DrawString(L"X", 1, &f, PointF(cx + ICON_SIZE/2, cy + ICON_SIZE/2), &sf, &txtBrush);
    if (s_mousePt.x >= cx && s_mousePt.x <= cx + ICON_SIZE && s_mousePt.y >= cy && s_mousePt.y <= cy + ICON_SIZE) hoveredIndex = 14;

    // Dual Language Tooltip Box (TR / EN)
    if (hoveredIndex >= 0) {
        const wchar_t* trText = L"";
        const wchar_t* enText = L"";

        switch (hoveredIndex) {
            case 0:  trText = L"Seçimi Taşı (Uzay Çubuğu / Sürükle)"; enText = L"Move Selection (Spacebar / Drag)"; break;
            case 1:  trText = L"Kalem (Serbest Çizim)"; enText = L"Pen (Freehand Draw)"; break;
            case 2:  trText = L"Ok Çıkarma Aracı"; enText = L"Arrow Tool"; break;
            case 3:  trText = L"Boş Dikdörtgen"; enText = L"Hollow Rectangle"; break;
            case 4:  trText = L"Dolu Dikdörtgen"; enText = L"Solid Filled Rectangle"; break;
            case 5:  trText = L"Yazı Ekleme"; enText = L"Text Insertion"; break;
            case 6:  trText = L"Mozaik / Gizleme"; enText = L"Mosaic / Blur"; break;
            case 7:  trText = L"Vurgulayıcı Kalem"; enText = L"Highlighter Pen"; break;
            case 8:  trText = L"Pürüzsüz Silgi"; enText = L"Smooth Eraser"; break;
            case 9:  trText = L"Renk Seçimi"; enText = L"Color Picker"; break;
            case 10: trText = L"Çizim Kalınlığı"; enText = L"Line Thickness"; break;
            case 11: trText = L"Ayarlar ve Kısayol"; enText = L"Settings & Hotkey"; break;
            case 12: trText = L"Resmi Kaydet (Ctrl+S)"; enText = L"Save Image (Ctrl+S)"; break;
            case 13: trText = L"Panoya Kopyala (Ctrl+C)"; enText = L"Copy to Clipboard (Ctrl+C)"; break;
            case 14: trText = L"Seçimi Kapat (ESC)"; enText = L"Cancel Selection (ESC)"; break;
        }

        Font fTr(&ff, 11, FontStyleBold, UnitPixel);
        Font fEn(&ff, 10, FontStyleRegular, UnitPixel);

        PointF pTr((REAL)(tbX + 12), (REAL)(tbY - 38));
        PointF pEn((REAL)(tbX + 12), (REAL)(tbY - 22));

        RectF rectTr, rectEn;
        g.MeasureString(trText, -1, &fTr, pTr, &rectTr);
        g.MeasureString(enText, -1, &fEn, pEn, &rectEn);

        float tipW = max(rectTr.Width, rectEn.Width) + 24;
        float tipH = 42;
        float tipX = (REAL)max(10, min((int)(s_mousePt.x - tipW / 2), (int)(screenW - tipW - 10)));
        float tipY = (REAL)(tbY - tipH - 6);
        if (tipY < 10) tipY = (REAL)(tbY + TOOLBAR_H + 6);

        SolidBrush tipBg(isDark ? Color(240, 20, 20, 35) : Color(240, 255, 255, 255));
        Pen tipBorder(isDark ? Color(255, 0, 255, 255) : Color(255, 0, 120, 215), 1.0f);
        SolidBrush txtTrBrush(isDark ? Color(255, 255, 255, 255) : Color(255, 20, 20, 20));
        SolidBrush txtEnBrush(isDark ? Color(255, 0, 255, 255) : Color(255, 0, 102, 204));

        g.FillRectangle(&tipBg, tipX, tipY, tipW, tipH);
        g.DrawRectangle(&tipBorder, tipX, tipY, tipW, tipH);

        g.DrawString(trText, -1, &fTr, PointF(tipX + 12, tipY + 6), &txtTrBrush);
        g.DrawString(enText, -1, &fEn, PointF(tipX + 12, tipY + 22), &txtEnBrush);
    }
}

void Editor::RenderColorPicker(Graphics& g) {
    bool isDark = App::GetInstance().GetSettings().isDarkMode;
    int pw = 4 * 28 + 8;
    int ph = 3 * 28 + 36;
    int px = s_colorPickerRect.left;
    int py = s_colorPickerRect.top - ph - 4;
    if (py < 10) py = s_colorPickerRect.bottom + 4;
    
    SolidBrush bgBrush(isDark ? Color(255, 40, 40, 60) : Color(255, 240, 240, 245));
    g.FillRectangle(&bgBrush, px, py, pw, ph);
    Pen border(isDark ? Color(255, 100, 100, 120) : Color(255, 180, 180, 190), 1.0f);
    g.DrawRectangle(&border, px, py, pw, ph);

    COLORREF colors[] = {
        RGB(255, 0, 0),     RGB(0, 255, 0),     RGB(0, 0, 255),     RGB(255, 255, 0),
        RGB(255, 128, 0),   RGB(128, 0, 255),   RGB(0, 255, 255),   RGB(255, 0, 255),
        RGB(255, 255, 255), RGB(0, 0, 0),       RGB(128, 128, 128), RGB(255, 192, 203)
    };

    for (int i = 0; i < 12; i++) {
        int r = i / 4;
        int c = i % 4;
        int cx = px + 4 + c * 28;
        int cy = py + 4 + r * 28;
        
        SolidBrush sb(Color(255, GetRValue(colors[i]), GetGValue(colors[i]), GetBValue(colors[i])));
        g.FillRectangle(&sb, cx, cy, 24, 24);
        Pen p(Color(255, 100, 100, 100), 1.0f);
        g.DrawRectangle(&p, cx, cy, 24, 24);
    }

    FontFamily ff(L"Segoe UI");
    Font f(&ff, 11, FontStyleRegular, UnitPixel);
    SolidBrush txtBrush(isDark ? Color(255, 200, 200, 200) : Color(255, 30, 30, 30));
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    g.DrawString(L"Özel...", -1, &f, PointF((REAL)(px + pw/2), (REAL)(py + 3 * 28 + 16)), &sf, &txtBrush);
}

Bitmap* Editor::CompositeFinalImage() {
    int selLeft = min((int)s_selectionRect.left, (int)s_selectionRect.right);
    int selTop = min((int)s_selectionRect.top, (int)s_selectionRect.bottom);
    int selW = abs((int)(s_selectionRect.right - s_selectionRect.left));
    int selH = abs((int)(s_selectionRect.bottom - s_selectionRect.top));

    if (selW <= 0 || selH <= 0 || !s_baseBitmap) return NULL;

    Bitmap* result = new Bitmap(selW, selH, PixelFormat32bppARGB);
    Graphics g(result);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    Bitmap baseImage(s_baseBitmap, NULL);
    g.DrawImage(&baseImage, Rect(0, 0, selW, selH), selLeft, selTop, selW, selH, UnitPixel);

    g.TranslateTransform((REAL)(-selLeft), (REAL)(-selTop));

    for (const auto& action : s_actions) {
        RenderAction(g, action);
    }

    return result;
}

int Editor::GetPngEncoderClsid(CLSID* pClsid) {
    UINT num = 0, size = 0;
    GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)(malloc(size));
    if (pImageCodecInfo == NULL) return -1;
    GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, L"image/png") == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

void Editor::SaveScreenshot() {
    Bitmap* bmp = CompositeFinalImage();
    if (!bmp) return;

    CLSID pngClsid;
    if (GetPngEncoderClsid(&pngClsid) < 0) {
        delete bmp;
        return;
    }

    std::wstring dir = App::GetInstance().GetSettings().GetSavePath();
    SHCreateDirectoryExW(NULL, dir.c_str(), NULL);

    time_t t = time(NULL);
    struct tm tmInfo;
    localtime_s(&tmInfo, &t);
    wchar_t filename[128];
    wcsftime(filename, 128, L"\\ETDSelect_%Y%m%d_%H%M%S.png", &tmInfo);

    std::wstring fullPath = dir + filename;
    bmp->Save(fullPath.c_str(), &pngClsid, NULL);
    delete bmp;

    App::GetInstance().ShowNotification(L"ETDSelect", I18n::Get("NOTIF_SAVE"));
}

static HGLOBAL BitmapToHGlobalPNG(Bitmap* bmp) {
    if (!bmp) return NULL;

    CLSID pngClsid;
    if (Editor::GetPngEncoderClsid(&pngClsid) < 0) return NULL;

    IStream* pStream = NULL;
    if (CreateStreamOnHGlobal(NULL, TRUE, &pStream) != S_OK) return NULL;

    if (bmp->Save(pStream, &pngClsid, NULL) != Ok) {
        pStream->Release();
        return NULL;
    }

    STATSTG statstg;
    pStream->Stat(&statstg, STATFLAG_NONAME);
    ULONG pngSize = statstg.cbSize.LowPart;

    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, pngSize);
    if (!hGlobal) {
        pStream->Release();
        return NULL;
    }

    BYTE* pDst = (BYTE*)GlobalLock(hGlobal);
    if (pDst) {
        LARGE_INTEGER li = {0};
        pStream->Seek(li, STREAM_SEEK_SET, NULL);
        ULONG bytesRead = 0;
        pStream->Read(pDst, pngSize, &bytesRead);
        GlobalUnlock(hGlobal);
    } else {
        GlobalFree(hGlobal);
        hGlobal = NULL;
    }

    pStream->Release();
    return hGlobal;
}

static HGLOBAL BitmapToHGlobalDIB(Bitmap* bmp) {
    if (!bmp) return NULL;

    INT w = bmp->GetWidth();
    INT h = bmp->GetHeight();
    if (w <= 0 || h <= 0) return NULL;

    BitmapData bmpData;
    Rect rect(0, 0, w, h);
    if (bmp->LockBits(&rect, ImageLockModeRead, PixelFormat32bppARGB, &bmpData) != Ok) {
        return NULL;
    }

    DWORD dwBmpSize = w * h * 4;
    DWORD dwHeaderSize = sizeof(BITMAPINFOHEADER);
    DWORD dwTotalSize = dwHeaderSize + dwBmpSize;

    HGLOBAL hGlobal = GlobalAlloc(GHND | GMEM_SHARE, dwTotalSize);
    if (!hGlobal) {
        bmp->UnlockBits(&bmpData);
        return NULL;
    }

    BYTE* pData = (BYTE*)GlobalLock(hGlobal);
    if (!pData) {
        GlobalFree(hGlobal);
        bmp->UnlockBits(&bmpData);
        return NULL;
    }

    BITMAPINFOHEADER* bmi = (BITMAPINFOHEADER*)pData;
    ZeroMemory(bmi, sizeof(BITMAPINFOHEADER));
    bmi->biSize = sizeof(BITMAPINFOHEADER);
    bmi->biWidth = w;
    bmi->biHeight = h;
    bmi->biPlanes = 1;
    bmi->biBitCount = 32;
    bmi->biCompression = BI_RGB;
    bmi->biSizeImage = dwBmpSize;

    BYTE* pDst = pData + dwHeaderSize;
    BYTE* pSrc = (BYTE*)bmpData.Scan0;

    int stride = bmpData.Stride;
    for (int y = h - 1; y >= 0; y--) {
        BYTE* pRowSrc = pSrc + y * stride;
        memcpy(pDst, pRowSrc, w * 4);
        pDst += w * 4;
    }

    GlobalUnlock(hGlobal);
    bmp->UnlockBits(&bmpData);
    return hGlobal;
}

void Editor::CopyToClipboard(HWND hwnd) {
    Bitmap* bmp = CompositeFinalImage();
    if (!bmp) return;

    HGLOBAL hPng = BitmapToHGlobalPNG(bmp);
    HGLOBAL hDib = BitmapToHGlobalDIB(bmp);
    HBITMAP hbm = NULL;
    bmp->GetHBITMAP(Color(255, 255, 255, 255), &hbm);
    delete bmp;

    UINT cfPng = RegisterClipboardFormatW(L"PNG");
    UINT cfImagePng = RegisterClipboardFormatW(L"image/png");

    bool opened = false;
    for (int i = 0; i < 10; i++) {
        if (OpenClipboard(NULL)) {
            opened = true;
            break;
        }
        Sleep(10);
    }

    if (opened) {
        EmptyClipboard();

        if (hPng && cfPng) {
            SetClipboardData(cfPng, hPng);
        }
        if (hPng && cfImagePng) {
            SIZE_T sz = GlobalSize(hPng);
            HGLOBAL hPngDup = GlobalAlloc(GHND | GMEM_SHARE, sz);
            if (hPngDup) {
                void* pSrc = GlobalLock(hPng);
                void* pDst = GlobalLock(hPngDup);
                if (pSrc && pDst) {
                    memcpy(pDst, pSrc, sz);
                }
                if (pSrc) GlobalUnlock(hPng);
                if (pDst) GlobalUnlock(hPngDup);
                SetClipboardData(cfImagePng, hPngDup);
            }
        }
        if (hDib) {
            SetClipboardData(CF_DIB, hDib);
        }
        if (hbm) {
            SetClipboardData(CF_BITMAP, hbm);
        }

        CloseClipboard();
        App::GetInstance().ShowNotification(L"ETDSelect", I18n::Get("NOTIF_COPY"));
    } else {
        if (hPng) GlobalFree(hPng);
        if (hDib) GlobalFree(hDib);
        if (hbm) DeleteObject(hbm);
    }
}

void Editor::Undo() {
    if (!s_actions.empty()) {
        s_actions.pop_back();
        if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
    }
}

void Editor::OnMouseDown(int x, int y) {
    s_isDrawing = true;
    s_activeAction = {};
    s_activeAction.type = s_currentTool;
    s_activeAction.color = s_currentColor;
    s_activeAction.thickness = s_currentThickness;
    s_activeAction.startPt = { x, y };
    s_activeAction.endPt = { x, y };
    s_activeAction.mosaicBlockSize = 12;

    if (s_currentTool == ToolType::TOOL_DRAW || 
        s_currentTool == ToolType::TOOL_HIGHLIGHTER || 
        s_currentTool == ToolType::TOOL_ERASER ||
        s_currentTool == ToolType::TOOL_LASSO) {
        s_activeAction.points.push_back({ x, y });
    }

    if (s_currentTool == ToolType::TOOL_TEXT) {
        s_activeAction.textPos = { x, y };
        
        if (s_editHwnd) {
            CommitTextEdit(s_hwnd);
        }
        
        // Calculate font size matching the render output
        int fontSize = s_currentThickness * 4 + 10;
        
        if (s_editFont) { DeleteObject(s_editFont); s_editFont = NULL; }
        s_editFont = CreateFontW(
            fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Arial"
        );
        
        if (s_editBgBrush) { DeleteObject(s_editBgBrush); s_editBgBrush = NULL; }
        s_editBgBrush = CreateSolidBrush(RGB(255, 255, 255));
        
        HINSTANCE hInst = GetModuleHandle(NULL);
        s_editHwnd = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            x, y, 200, fontSize + 8,
            s_hwnd, NULL, hInst, NULL
        );
        
        SendMessageW(s_editHwnd, WM_SETFONT, (WPARAM)s_editFont, TRUE);
        SetWindowSubclass(s_editHwnd, InlineEditProc, 1, 0);
        SetFocus(s_editHwnd);
    }
}

void Editor::OnMouseMove(int x, int y) {
    if (!s_isDrawing) return;

    s_activeAction.endPt = { x, y };

    if (s_currentTool == ToolType::TOOL_DRAW || 
        s_currentTool == ToolType::TOOL_HIGHLIGHTER || 
        s_currentTool == ToolType::TOOL_ERASER ||
        s_currentTool == ToolType::TOOL_LASSO) {
        s_activeAction.points.push_back({ x, y });
    }

    if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
}

void Editor::OnMouseUp(int x, int y) {
    if (!s_isDrawing) return;

    if (s_currentTool != ToolType::TOOL_TEXT) {
        s_isDrawing = false;
        s_activeAction.endPt = { x, y };
        s_actions.push_back(s_activeAction);
    }

    if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
}

void Editor::OnToolbarClick(int x, int y) {
    if (s_colorPickerVisible) {
        int pw = 4 * 28 + 8;
        int ph = 3 * 28 + 36;
        int px = s_colorPickerRect.left;
        int py = s_colorPickerRect.top - ph - 4;
        if (py < 10) py = s_colorPickerRect.bottom + 4;

        if (x >= px && x <= px + pw && y >= py && y <= py + ph) {
            COLORREF colors[] = {
                RGB(255, 0, 0),     RGB(0, 255, 0),     RGB(0, 0, 255),     RGB(255, 255, 0),
                RGB(255, 128, 0),   RGB(128, 0, 255),   RGB(0, 255, 255),   RGB(255, 0, 255),
                RGB(255, 255, 255), RGB(0, 0, 0),       RGB(128, 128, 128), RGB(255, 192, 203)
            };

            for (int i = 0; i < 12; i++) {
                int r = i / 4;
                int c = i % 4;
                int cx = px + 4 + c * 28;
                int cy = py + 4 + r * 28;
                if (x >= cx && x <= cx + 24 && y >= cy && y <= cy + 24) {
                    s_currentColor = colors[i];
                    s_colorPickerVisible = false;
                    InvalidateRect(s_hwnd, NULL, FALSE);
                    return;
                }
            }

            if (y >= py + 3 * 28 + 4) {
                CHOOSECOLORW cc = { sizeof(CHOOSECOLORW) };
                static COLORREF custColors[16] = {0};
                cc.hwndOwner = s_hwnd;
                cc.lpCustColors = custColors;
                cc.rgbResult = s_currentColor;
                cc.Flags = CC_FULLOPEN | CC_RGBINIT;
                if (ChooseColorW(&cc)) {
                    s_currentColor = cc.rgbResult;
                }
                s_colorPickerVisible = false;
                InvalidateRect(s_hwnd, NULL, FALSE);
                return;
            }
            return;
        } else {
            s_colorPickerVisible = false;
        }
    }

    int tbX = s_toolbarRect.left;
    int tbY = s_toolbarRect.top;
    float cy = (float)tbY + (TOOLBAR_H - ICON_SIZE) / 2.0f;
    float cx = (float)tbX + ICON_PAD;

    ToolType tools[] = {
        ToolType::TOOL_MOVE, ToolType::TOOL_DRAW, ToolType::TOOL_ARROW, ToolType::TOOL_RECT_HOLLOW,
        ToolType::TOOL_RECT_FILLED, ToolType::TOOL_TEXT, ToolType::TOOL_MOSAIC,
        ToolType::TOOL_HIGHLIGHTER, ToolType::TOOL_ERASER
    };

    for (int i = 0; i < 9; i++) {
        if (x >= cx && x <= cx + ICON_SIZE) {
            s_currentTool = tools[i];
            s_colorPickerVisible = false;
            InvalidateRect(s_hwnd, NULL, FALSE);
            return;
        }
        cx += ICON_SIZE + ICON_PAD;
    }

    cx += ICON_PAD * 3;

    if (x >= cx && x <= cx + ICON_SIZE) {
        s_colorPickerVisible = !s_colorPickerVisible;
        InvalidateRect(s_hwnd, NULL, FALSE);
        return;
    }
    cx += ICON_SIZE + ICON_PAD * 2;

    if (x >= cx && x <= cx + ICON_SIZE) {
        if (s_currentThickness > 1) s_currentThickness--;
        InvalidateRect(s_hwnd, NULL, FALSE);
        return;
    }
    cx += ICON_SIZE * 2;

    if (x >= cx && x <= cx + ICON_SIZE) {
        if (s_currentThickness < 20) s_currentThickness++;
        InvalidateRect(s_hwnd, NULL, FALSE);
        return;
    }
    cx += ICON_SIZE + ICON_PAD * 4;

    if (x >= cx && x <= cx + ICON_SIZE) {
        PostMessageW(App::GetInstance().GetHWND(), WM_APP_SHOW_SETTINGS, 0, 0);
        return;
    }
    cx += ICON_SIZE + ICON_PAD * 4;

    if (x >= cx && x <= cx + ICON_SIZE * 2) {
        SaveScreenshot();
        Shutdown();
        return;
    }
    cx += ICON_SIZE * 2;

    if (x >= cx && x <= cx + ICON_SIZE * 2) {
        CopyToClipboard(s_hwnd);
        Shutdown();
        return;
    }
    cx += ICON_SIZE * 2;

    if (x >= cx && x <= cx + ICON_SIZE) {
        Shutdown();
        return;
    }
}
