#include "app.h"
#include "resource.h"
#include "overlay.h"
#include "editor.h"
#include "i18n.h"
#include <commctrl.h>
#include <string>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

App& App::GetInstance() {
    static App instance;
    return instance;
}

void App::ShowNotification(const wchar_t* title, const wchar_t* msg) {
    NOTIFYICONDATAW nid = { sizeof(NOTIFYICONDATAW) };
    nid.hWnd = m_hwnd;
    nid.uID = 1001;
    nid.uFlags = NIF_INFO | NIF_ICON | NIF_TIP;
    nid.hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APPICON));
    if (!nid.hIcon) {
        nid.hIcon = LoadIconW(NULL, IDI_INFORMATION);
    }
    wcscpy_s(nid.szTip, L"ETDSelect");
    wcscpy_s(nid.szInfoTitle, title);
    wcscpy_s(nid.szInfo, msg);
    nid.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;

    Shell_NotifyIconW(NIM_ADD, &nid);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

bool App::Init(HINSTANCE hInstance) {
    m_hInst = hInstance;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.lpszClassName = L"ETDSelectMainClass";
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        0, L"ETDSelectMainClass", L"ETDSelect",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, NULL, hInstance, NULL
    );

    if (!m_hwnd) return false;

    HICON hIconBig = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    HICON hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    SendMessageW(m_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(m_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

    if (m_settings.IsFirstRun()) {
        m_settings.ApplyAutostartRegistry();
        ShowSettings(true);
        ShowHelp();
        m_settings.Save();
    } else {
        m_settings.ApplyAutostartRegistry();
        PostMessageW(m_hwnd, WM_HOTKEY, ID_HOTKEY_CAPTURE, 0);
    }

    RegisterAppHotkey();
    return true;
}

void App::Run() {
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void App::Cleanup() {
    UnregisterAppHotkey();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
}

bool App::RegisterAppHotkey() {
    UnregisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE);
    UINT mods = m_settings.GetModifiers();
    UINT vkey = m_settings.GetVirtualKey();
    if (vkey == 0) {
        m_settings.ctrl = true;
        m_settings.alt = false;
        m_settings.shift = true;
        m_settings.vkey = 'S';
        mods = m_settings.GetModifiers();
        vkey = 'S';
    }

    BOOL res = RegisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE, mods | MOD_NOREPEAT, vkey);
    if (!res) {
        res = RegisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE, mods, vkey);
    }

    if (!res) {
        m_settings.ctrl = true;
        m_settings.alt = false;
        m_settings.shift = true;
        m_settings.vkey = 'S';
        m_settings.Save();
        mods = m_settings.GetModifiers();
        vkey = 'S';
        res = RegisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE, mods | MOD_NOREPEAT, vkey);
        if (!res) {
            res = RegisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE, mods, vkey);
        }
    }

    return (res != FALSE);
}

void App::UnregisterAppHotkey() {
    UnregisterHotKey(m_hwnd, ID_HOTKEY_CAPTURE);
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_HOTKEY:
            if (wParam == ID_HOTKEY_CAPTURE) {
                if (!Overlay::IsActive() && !Editor::IsActive()) {
                    Overlay::Start();
                }
            }
            break;
        case WM_APP_SHOW_SETTINGS:
            App::GetInstance().ShowSettings(false);
            break;
        case WM_APP_SHOW_ABOUT:
            App::GetInstance().ShowAbout();
            break;
        case WM_APP_SHOW_HELP:
            App::GetInstance().ShowHelp();
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- Settings Dialog ----

#define IDC_TXT_HOTKEY_INPUT  2001
#define IDC_CHK_AUTOSTART     2002
#define IDC_CHK_DARKMODE      2003
#define IDC_BTN_SAVE_DLG      2004
#define IDC_BTN_CANCEL_DLG    2005
#define IDC_BTN_QUIT_APP      2006

static HBRUSH g_hbrBg = NULL;
static HBRUSH g_hbrEditBg = NULL;

static bool g_tempCtrl = false;
static bool g_tempAlt = false;
static bool g_tempShift = false;
static UINT g_tempVkey = 0;
static WNDPROC g_origHotkeyEditProc = NULL;

static std::wstring GetHotkeyDisplayString(bool ctrl, bool alt, bool shift, UINT vkey) {
    std::wstring str = L"";
    if (ctrl) str += L"Ctrl + ";
    if (alt) str += L"Alt + ";
    if (shift) str += L"Shift + ";

    if (vkey >= 'A' && vkey <= 'Z') {
        str += (wchar_t)vkey;
    } else if (vkey >= '0' && vkey <= '9') {
        str += (wchar_t)vkey;
    } else if (vkey >= VK_F1 && vkey <= VK_F24) {
        wchar_t buf[16];
        wsprintfW(buf, L"F%d", vkey - VK_F1 + 1);
        str += buf;
    } else if (vkey == VK_SNAPSHOT) {
        str += L"PrintScreen";
    } else if (vkey == VK_SPACE) {
        str += L"Space";
    } else if (vkey == VK_INSERT) {
        str += L"Insert";
    } else if (vkey == VK_DELETE) {
        str += L"Delete";
    } else if (vkey == VK_HOME) {
        str += L"Home";
    } else if (vkey == VK_END) {
        str += L"End";
    } else if (vkey == VK_PRIOR) {
        str += L"PageUp";
    } else if (vkey == VK_NEXT) {
        str += L"PageDown";
    } else {
        wchar_t keyName[64] = {0};
        UINT scanCode = MapVirtualKeyW(vkey, MAPVK_VK_TO_VSC);
        LONG lParamVal = (scanCode << 16);
        if (GetKeyNameTextW(lParamVal, keyName, 64) > 0) {
            str += keyName;
        } else if (vkey != 0) {
            wchar_t buf[16];
            wsprintfW(buf, L"Key 0x%X", vkey);
            str += buf;
        } else {
            str += L"(Tuşa basın)";
        }
    }
    return str;
}

LRESULT CALLBACK HotkeySubclassProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN) {
        UINT vkey = (UINT)wParam;

        if (vkey == VK_CONTROL || vkey == VK_LCONTROL || vkey == VK_RCONTROL ||
            vkey == VK_MENU || vkey == VK_LMENU || vkey == VK_RMENU ||
            vkey == VK_SHIFT || vkey == VK_LSHIFT || vkey == VK_RSHIFT) {
            return 0;
        }

        g_tempCtrl  = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        g_tempAlt   = (GetKeyState(VK_MENU) & 0x8000) != 0;
        g_tempShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        g_tempVkey  = vkey;

        std::wstring str = GetHotkeyDisplayString(g_tempCtrl, g_tempAlt, g_tempShift, g_tempVkey);
        SetWindowTextW(hWnd, str.c_str());
        return 0;
    }
    if (uMsg == WM_CHAR || uMsg == WM_SYSCHAR) {
        return 0;
    }
    return CallWindowProcW(g_origHotkeyEditProc, hWnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK App::SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            App* app = &App::GetInstance();
            Settings& settings = app->GetSettings();
            
            bool firstRun = (((LPCREATESTRUCT)lParam)->lpCreateParams != NULL);

            COLORREF bgCol = settings.isDarkMode ? RGB(26, 26, 46) : RGB(240, 242, 245);
            COLORREF editBgCol = settings.isDarkMode ? RGB(40, 40, 60) : RGB(255, 255, 255);

            g_hbrBg = CreateSolidBrush(bgCol);
            g_hbrEditBg = CreateSolidBrush(editBgCol);

            g_tempCtrl  = settings.ctrl;
            g_tempAlt   = settings.alt;
            g_tempShift = settings.shift;
            g_tempVkey  = settings.vkey;

            HINSTANCE hInst = app->GetHInstance();
            int yOff = 15;

            if (firstRun) {
                CreateWindowExW(0, L"STATIC", 
                    L"ETDSelect'e Hoş Geldiniz!\r\nLütfen kısayol ve tercihlerinizi ayarlayın.",
                    WS_CHILD | WS_VISIBLE | SS_CENTER,
                    20, yOff, 350, 36, hwnd, NULL, hInst, NULL);
                yOff += 45;
            }

            CreateWindowExW(0, L"STATIC", I18n::Get("SET_HOTKEY_LABEL"),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                15, yOff, 360, 20, hwnd, NULL, hInst, NULL);
            yOff += 25;

            HWND hEditKey = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_READONLY,
                40, yOff, 310, 32, hwnd, (HMENU)(UINT_PTR)IDC_TXT_HOTKEY_INPUT, hInst, NULL);
            
            HFONT hFont = CreateFontW(18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, 
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, 
                                      DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            SendMessageW(hEditKey, WM_SETFONT, (WPARAM)hFont, TRUE);

            std::wstring initialKeyStr = GetHotkeyDisplayString(g_tempCtrl, g_tempAlt, g_tempShift, g_tempVkey);
            SetWindowTextW(hEditKey, initialKeyStr.c_str());

            g_origHotkeyEditProc = (WNDPROC)SetWindowLongPtrW(hEditKey, GWLP_WNDPROC, (LONG_PTR)HotkeySubclassProc);

            yOff += 40;

            // Language Selection
            CreateWindowExW(0, L"STATIC", I18n::Get("SET_LANG_LABEL"),
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                30, yOff + 2, 130, 22, hwnd, NULL, hInst, NULL);

            HWND hComboLang = CreateWindowExW(0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                160, yOff, 190, 120, hwnd, (HMENU)(UINT_PTR)IDC_COMBO_LANG, hInst, NULL);

            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)I18n::Get("LANG_AUTO"));
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)I18n::Get("LANG_TR"));
            SendMessageW(hComboLang, CB_ADDSTRING, 0, (LPARAM)I18n::Get("LANG_EN"));
            SendMessageW(hComboLang, CB_SETCURSEL, settings.language, 0);

            yOff += 32;

            HWND hAutostart = CreateWindowExW(0, L"BUTTON", I18n::Get("SET_AUTOSTART"),
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, yOff, 330, 22, hwnd, (HMENU)(UINT_PTR)IDC_CHK_AUTOSTART, hInst, NULL);
            if (settings.autostart) SendMessageW(hAutostart, BM_SETCHECK, BST_CHECKED, 0);

            yOff += 28;

            HWND hDarkMode = CreateWindowExW(0, L"BUTTON", L"Koyu Tema (Dark Mode)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                30, yOff, 330, 22, hwnd, (HMENU)(UINT_PTR)IDC_CHK_DARKMODE, hInst, NULL);
            if (settings.isDarkMode) SendMessageW(hDarkMode, BM_SETCHECK, BST_CHECKED, 0);

            yOff += 45;

            CreateWindowExW(0, L"BUTTON", I18n::Get("SET_SAVE_BTN"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                firstRun ? 145 : 65, yOff, 110, 32, hwnd, (HMENU)(UINT_PTR)IDC_BTN_SAVE_DLG, hInst, NULL);

            if (!firstRun) {
                CreateWindowExW(0, L"BUTTON", I18n::Get("SET_CANCEL_BTN"),
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                    215, yOff, 110, 32, hwnd, (HMENU)(UINT_PTR)IDC_BTN_CANCEL_DLG, hInst, NULL);
            }

            yOff += 45;

            CreateWindowExW(0, L"BUTTON", I18n::Get("MENU_EXIT"),
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                65, yOff, 260, 30, hwnd, (HMENU)(UINT_PTR)IDC_BTN_QUIT_APP, hInst, NULL);

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            App* app = &App::GetInstance();
            HDC hdc = (HDC)wParam;
            bool isDark = app->GetSettings().isDarkMode;
            SetTextColor(hdc, isDark ? RGB(255, 255, 255) : RGB(20, 20, 20));
            SetBkColor(hdc, isDark ? RGB(26, 26, 46) : RGB(240, 242, 245));
            return (LRESULT)g_hbrBg;
        }

        case WM_CTLCOLOREDIT: {
            App* app = &App::GetInstance();
            HDC hdc = (HDC)wParam;
            bool isDark = app->GetSettings().isDarkMode;
            SetTextColor(hdc, isDark ? RGB(0, 255, 255) : RGB(0, 102, 204));
            SetBkColor(hdc, isDark ? RGB(40, 40, 60) : RGB(255, 255, 255));
            return (LRESULT)g_hbrEditBg;
        }

        case WM_COMMAND: {
            WORD id = LOWORD(wParam);
            WORD code = HIWORD(wParam);
            if (code == BN_CLICKED || code == 0) {
                if (id == IDC_BTN_SAVE_DLG) {
                    App* app = &App::GetInstance();
                    Settings& settings = app->GetSettings();

                    if (g_tempVkey == 0) {
                        MessageBoxW(hwnd, L"Lütfen geçerli bir kısayol tuşu basın!", L"Uyarı", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    settings.ctrl  = g_tempCtrl;
                    settings.alt   = g_tempAlt;
                    settings.shift = g_tempShift;
                    settings.vkey  = g_tempVkey;

                    settings.autostart  = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
                    settings.isDarkMode = (SendMessageW(GetDlgItem(hwnd, IDC_CHK_DARKMODE),  BM_GETCHECK, 0, 0) == BST_CHECKED);

                    int selectedLang = (int)SendMessageW(GetDlgItem(hwnd, IDC_COMBO_LANG), CB_GETCURSEL, 0, 0);
                    if (selectedLang != CB_ERR) {
                        settings.language = selectedLang;
                    }

                    if (!app->RegisterAppHotkey()) {
                        MessageBoxW(hwnd, 
                            L"Bu kısayol Windows veya başka bir uygulama tarafından kullanıldığı için kaydedilemedi!\r\nLütfen farklı bir kısayol deneyin.", 
                            L"Kısayol Kayıt Hatası", MB_OK | MB_ICONWARNING);
                        return 0;
                    }

                    settings.Save();
                    MessageBoxW(hwnd, (settings.GetEffectiveLanguage() == 1) ? L"Ayarlar başarıyla kaydedildi!" : L"Settings saved successfully!", L"ETDSelect", MB_OK | MB_ICONINFORMATION);
                    DestroyWindow(hwnd);
                } else if (id == IDC_BTN_CANCEL_DLG) {
                    DestroyWindow(hwnd);
                } else if (id == IDC_BTN_QUIT_APP) {
                    int res = MessageBoxW(hwnd, 
                        L"ETDSelect arka plandan tamamen kapatılacak.\r\nOnaylıyor musunuz?", 
                        L"Uygulamayı Kapat", MB_YESNO | MB_ICONQUESTION);
                    if (res == IDYES) {
                        App::GetInstance().Cleanup();
                        PostQuitMessage(0);
                        ExitProcess(0);
                    }
                }
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_hbrBg) { DeleteObject(g_hbrBg); g_hbrBg = NULL; }
            if (g_hbrEditBg) { DeleteObject(g_hbrEditBg); g_hbrEditBg = NULL; }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ---- About Dialog ----

LRESULT CALLBACK App::AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            App* app = &App::GetInstance();
            bool isDark = app->GetSettings().isDarkMode;
            int lang = app->GetSettings().GetEffectiveLanguage();
            COLORREF bgCol = isDark ? RGB(26, 26, 46) : RGB(240, 242, 245);
            g_hbrBg = CreateSolidBrush(bgCol);

            HINSTANCE hInst = app->GetHInstance();

            CreateWindowExW(0, L"STATIC", L"ETDSelect v1.0",
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 15, 350, 24, hwnd, NULL, hInst, NULL);

            wchar_t devBuf[128];
            wsprintfW(devBuf, (lang == 1) ? L"Yapımcı: Emir Tuğra Dağ" : L"Developer: Emir Tuğra Dağ");
            CreateWindowExW(0, L"STATIC", devBuf,
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 42, 350, 20, hwnd, NULL, hInst, NULL);

            const wchar_t* aboutText = (lang == 1) ?
                L"ETDSelect - Açık Kaynaklı Ekran Alıntısı Aracı\r\n"
                L"Lisans: MIT Open Source License\r\n\r\n"
                L"Sorumluluk Reddi:\r\n"
                L"Projeye destek vermem zorunlu değildir, geliştirmeyi bırakabilirim. "
                L"Sadece yaptım ve bıraktım. Hatalar olabilir, olası sorunlardan sorumlu değilim. "
                L"Yerelde çalışan açık kaynaklı bir projedir."
                :
                L"ETDSelect - Open Source Screenshot Tool\r\n"
                L"License: MIT Open Source License\r\n\r\n"
                L"Disclaimer:\r\n"
                L"This is a personal open-source screenshot project. "
                L"The developer is under no obligation to provide ongoing updates or support. "
                L"The developer is not liable for any potential issues or bugs.\r\n\r\n"
                L"ETDSelect runs locally on your system and respects your privacy.";

            HWND hText = CreateWindowExW(0, L"EDIT", aboutText,
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_CENTER,
                20, 75, 350, 140, hwnd, NULL, hInst, NULL);

            CreateWindowExW(0, L"BUTTON", (lang == 1) ? L"Tamam" : L"OK",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                145, 230, 100, 30, hwnd, (HMENU)(UINT_PTR)IDC_BTN_CANCEL_DLG, hInst, NULL);

            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT: {
            App* app = &App::GetInstance();
            HDC hdc = (HDC)wParam;
            bool isDark = app->GetSettings().isDarkMode;
            SetTextColor(hdc, isDark ? RGB(255, 255, 255) : RGB(20, 20, 20));
            SetBkColor(hdc, isDark ? RGB(26, 26, 46) : RGB(240, 242, 245));
            return (LRESULT)g_hbrBg;
        }

        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_BTN_CANCEL_DLG || LOWORD(wParam) == IDOK) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (g_hbrBg) {
                DeleteObject(g_hbrBg);
                g_hbrBg = NULL;
            }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::ShowSettings(bool firstRun) {
    if (m_hwnd) EnableWindow(m_hwnd, FALSE);

    static bool dlgClassReg = false;
    if (!dlgClassReg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = SettingsDlgProc;
        wc.hInstance = m_hInst;
        wc.lpszClassName = L"ETDSelectSettingsClass";
        wc.hbrBackground = CreateSolidBrush(m_settings.isDarkMode ? RGB(26, 26, 46) : RGB(240, 242, 245));
        RegisterClassW(&wc);
        dlgClassReg = true;
    }

    int dlgW = 390, dlgH = firstRun ? 390 : 370;
    int dlgX = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int dlgY = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"ETDSelectSettingsClass",
        firstRun ? I18n::Get("SET_FIRST_TITLE") : I18n::Get("SET_TITLE"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        m_hwnd, NULL, m_hInst, (LPVOID)(firstRun ? (void*)1 : NULL)
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_hwnd) EnableWindow(m_hwnd, TRUE);
}

void App::ShowAbout() {
    if (m_hwnd) EnableWindow(m_hwnd, FALSE);

    static bool aboutClassReg = false;
    if (!aboutClassReg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AboutDlgProc;
        wc.hInstance = m_hInst;
        wc.lpszClassName = L"ETDSelectAboutClass";
        wc.hbrBackground = CreateSolidBrush(m_settings.isDarkMode ? RGB(26, 26, 46) : RGB(240, 242, 245));
        RegisterClassW(&wc);
        aboutClassReg = true;
    }

    int dlgW = 390, dlgH = 310;
    int dlgX = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int dlgY = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"ETDSelectAboutClass",
        I18n::Get("ABOUT_TITLE"),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        m_hwnd, NULL, m_hInst, NULL
    );

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_hwnd) EnableWindow(m_hwnd, TRUE);
}

LRESULT CALLBACK App::HelpDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HBRUSH hbrBg = NULL;
    static HBRUSH hbrEditBg = NULL;

    switch (msg) {
        case WM_CREATE: {
            bool isDark = App::GetInstance().GetSettings().isDarkMode;
            int lang = App::GetInstance().GetSettings().GetEffectiveLanguage();
            hbrBg = CreateSolidBrush(isDark ? RGB(26, 26, 46) : RGB(240, 242, 245));
            hbrEditBg = CreateSolidBrush(isDark ? RGB(35, 35, 55) : RGB(255, 255, 255));

            HINSTANCE hInst = GetModuleHandle(NULL);

            CreateWindowExW(0, L"STATIC", I18n::Get("HELP_TITLE"),
                WS_CHILD | WS_VISIBLE | SS_CENTER,
                20, 15, 460, 24, hwnd, NULL, hInst, NULL);

            const wchar_t* helpText = (lang == 1) ?
                L"✨ ETDSelect'e Hoş Geldiniz!\r\n\r\n"
                L"1. ✂️ Ekran Alıntısı Alma:\r\n"
                L"   • Kısayol tuşunuza (Varsayılan: Ctrl + Shift + S) basarak ekran alıntısını başlatabilirsiniz.\r\n\r\n"
                L"2. 🖐️ Seçim Alanını Taşıma & Boyutlandırma:\r\n"
                L"   • Seçimi Taşı (🖐️) aracına tıklayarak,\r\n"
                L"   • Klavyeden Spacebar (Uzay Çubuğu) basılı tutarak,\r\n"
                L"   • Seçim çerçevesinin mavi kenar çizgisine yaklaşıp sürükleyerek,\r\n"
                L"   • Farenin orta tuşuna (Tekerlek) basılı tutarak alanı kolayca taşıyabilirsiniz.\r\n"
                L"   • Kutunun 8 kenar tutamacından tutarak boyutu değiştirebilirsiniz.\r\n\r\n"
                L"3. ✏️ Çizim & Düzenleme Araçları:\r\n"
                L"   • Kalem, Ok, Boş/Dolu Dikdörtgen, Yazı (T), Mozaik, Vurgulayıcı ve Pürüzsüz Silgi.\r\n\r\n"
                L"4. ⌨️ Klavye Kısayolları:\r\n"
                L"   • Ctrl + Z : Çizilen son çizgiyi/işlemi geri alır.\r\n"
                L"   • Ctrl + S : Görseli arka planda sessizce kaydedip kapatır.\r\n"
                L"   • Ctrl + C : Görseli panoya kopyalayıp kapatır.\r\n"
                L"   • ESC      : Seçimi iptal eder.\r\n\r\n"
                L"5. 🖱️ Sağ Tık Menüsü:\r\n"
                L"   • Seçim alanında sağ tıklayarak 'Nasıl Kullanılır?', 'Hakkında' veya 'Arka Plandan Kapat' seçeneklerine ulaşabilirsiniz.\r\n"
                :
                L"✨ Welcome to ETDSelect!\r\n\r\n"
                L"1. ✂️ Taking Screenshots:\r\n"
                L"   • Press your shortcut key (Default: Ctrl + Shift + S) to start screenshot selection.\r\n\r\n"
                L"2. 🖐️ Moving & Resizing Selection:\r\n"
                L"   • Click the Move Tool (🖐️),\r\n"
                L"   • Hold Spacebar on your keyboard,\r\n"
                L"   • Drag near the blue border line,\r\n"
                L"   • Hold Middle Mouse Button to move the selection area.\r\n"
                L"   • Drag any of the 8 handles to resize.\r\n\r\n"
                L"3. ✏️ Drawing Tools:\r\n"
                L"   • Pen, Arrow, Hollow/Filled Rectangle, Text (T), Mosaic (Blur), Highlighter, and Eraser.\r\n\r\n"
                L"4. ⌨️ Keyboard Shortcuts:\r\n"
                L"   • Ctrl + Z : Undo last drawing action.\r\n"
                L"   • Ctrl + S : Save image to Pictures folder and close.\r\n"
                L"   • Ctrl + C : Copy image to clipboard as PNG and close.\r\n"
                L"   • ESC      : Cancel selection.\r\n\r\n"
                L"5. 🖱️ Right-Click Context Menu:\r\n"
                L"   • Right-click anywhere during selection to access 'How to Use', 'About', or 'Exit Application'.\r\n";

            HWND hEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", helpText,
                WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
                20, 45, 465, 340, hwnd, NULL, hInst, NULL
            );

            CreateWindowExW(0, L"BUTTON", (lang == 1) ? L"Anladım" : L"Got it",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                190, 398, 120, 32, hwnd, (HMENU)IDOK, hInst, NULL);

            return 0;
        }

        case WM_CTLCOLORSTATIC: {
            HDC hdc = (HDC)wParam;
            bool isDark = App::GetInstance().GetSettings().isDarkMode;
            SetTextColor(hdc, isDark ? RGB(255, 255, 255) : RGB(20, 20, 20));
            SetBkColor(hdc, isDark ? RGB(26, 26, 46) : RGB(240, 242, 245));
            return (LRESULT)hbrBg;
        }

        case WM_CTLCOLOREDIT: {
            HDC hdc = (HDC)wParam;
            bool isDark = App::GetInstance().GetSettings().isDarkMode;
            SetTextColor(hdc, isDark ? RGB(230, 230, 230) : RGB(20, 20, 20));
            SetBkColor(hdc, isDark ? RGB(35, 35, 55) : RGB(255, 255, 255));
            return (LRESULT)hbrEditBg;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (hbrBg) { DeleteObject(hbrBg); hbrBg = NULL; }
            if (hbrEditBg) { DeleteObject(hbrEditBg); hbrEditBg = NULL; }
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void App::ShowHelp() {
    if (m_hwnd) EnableWindow(m_hwnd, FALSE);

    static bool helpClassReg = false;
    if (!helpClassReg) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HelpDlgProc;
        wc.hInstance = m_hInst;
        wc.hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APPICON));
        wc.lpszClassName = L"ETDSelectHelpClass";
        wc.hbrBackground = CreateSolidBrush(m_settings.isDarkMode ? RGB(26, 26, 46) : RGB(240, 242, 245));
        RegisterClassW(&wc);
        helpClassReg = true;
    }

    int dlgW = 520, dlgH = 480;
    int dlgX = (GetSystemMetrics(SM_CXSCREEN) - dlgW) / 2;
    int dlgY = (GetSystemMetrics(SM_CYSCREEN) - dlgH) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        L"ETDSelectHelpClass",
        L"ETDSelect - Nasıl Kullanılır?",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        m_hwnd, NULL, m_hInst, NULL
    );

    HICON hIconBig = LoadIconW(m_hInst, MAKEINTRESOURCEW(IDI_APPICON));
    HICON hIconSmall = (HICON)LoadImageW(m_hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    SendMessageW(hDlg, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(hDlg, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

    MSG msg;
    while (IsWindow(hDlg) && GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (m_hwnd) EnableWindow(m_hwnd, TRUE);
}
