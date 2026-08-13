#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>

enum class ToolType {
    TOOL_NONE = 0,
    TOOL_MOVE,
    TOOL_DRAW,
    TOOL_ARROW,
    TOOL_RECT_HOLLOW,
    TOOL_RECT_FILLED,
    TOOL_TEXT,
    TOOL_MOSAIC,
    TOOL_HIGHLIGHTER,
    TOOL_ERASER,
    TOOL_LASSO
};

enum class ResizeHandle {
    HANDLE_NONE = 0,
    HANDLE_TOP_LEFT,
    HANDLE_TOP,
    HANDLE_TOP_RIGHT,
    HANDLE_LEFT,
    HANDLE_RIGHT,
    HANDLE_BOTTOM_LEFT,
    HANDLE_BOTTOM,
    HANDLE_BOTTOM_RIGHT
};

struct DrawAction {
    ToolType type;
    std::vector<POINT> points;  // For freehand, highlighter, eraser
    POINT startPt;              // For arrow, rects, mosaic
    POINT endPt;
    COLORREF color;
    int thickness;
    std::wstring text;          // For text tool
    POINT textPos;              // Where text is placed
    int alpha;                  // For highlighter transparency
    int mosaicBlockSize;        // For mosaic
};

class Editor {
public:
    static void Start(RECT selection, HBITMAP screenCapture);
    static void Shutdown();
    static bool IsActive() { return s_hwnd != NULL; }
    static int GetPngEncoderClsid(CLSID* pClsid);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static void RegisterWindowClass(HINSTANCE hInstance);
    
    // Rendering
    static void RenderCanvas(Gdiplus::Graphics& g, int screenW, int screenH);
    static void RenderAction(Gdiplus::Graphics& g, const DrawAction& action);
    static void RenderToolbar(Gdiplus::Graphics& g, int screenW, int screenH);
    static void RenderColorPicker(Gdiplus::Graphics& g);
    static void DrawToolIcon(Gdiplus::Graphics& g, ToolType tool, float x, float y, float size, bool selected);
    
    // Resize & Move Helpers
    static ResizeHandle GetHandleAtPoint(int x, int y);
    static HCURSOR GetCursorForHandle(ResizeHandle handle);
    static bool IsPointNearSelectionBorder(int x, int y);
    static bool ShouldMoveSelection(int x, int y);

    // Compositing for save/copy
    static Gdiplus::Bitmap* CompositeFinalImage();
    
    // Actions
    static void SaveScreenshot();
    static void CopyToClipboard(HWND hwnd);
    static void Undo();
    
    // Tool handling
    static void OnMouseDown(int x, int y);
    static void OnMouseMove(int x, int y);
    static void OnMouseUp(int x, int y);
    static void OnToolbarClick(int x, int y);
    
    // State
    static HWND s_hwnd;
    static HBITMAP s_baseBitmap;
    static Gdiplus::Bitmap* s_gdiplusBaseBmp;
    static Gdiplus::Bitmap* s_canvasBmp;
    static HDC s_backBufferDC;
    static HBITMAP s_backBufferBmp;
    static HGDIOBJ s_oldBackBufferObj;
    static RECT s_selectionRect;
    static int s_screenW, s_screenH;
    static int s_originX, s_originY;
    static POINT s_mousePt;

    // Resizing state
    static bool s_isResizing;
    static ResizeHandle s_activeHandle;
    static RECT s_resizeStartRect;
    static POINT s_resizeStartPt;

    // Selection Moving State
    static bool s_isMovingSelection;
    static POINT s_moveStartPt;
    static RECT s_moveStartRect;

    // Toolbar calculated rect
    static RECT s_toolbarRect;
    
    // Tools state (public for subclass access)
public:
    static std::vector<DrawAction> s_actions;
    static ToolType s_currentTool;
    static COLORREF s_currentColor;
    static int s_currentThickness;
    static bool s_isDrawing;
    static DrawAction s_activeAction;
private:
    
    // Color picker state
    static bool s_colorPickerVisible;
    static RECT s_colorPickerRect;
    
    // UI constants
    static const int TOOLBAR_H = 40;
    static const int ICON_SIZE = 28;
    static const int ICON_PAD = 4;
    static const int HANDLE_SIZE = 8;
    static bool s_registered;
};
