#pragma once
#include <windows.h>
#include "settings.h"

class App {
public:
    static App& GetInstance();
    bool Init(HINSTANCE hInstance);
    void Run();
    void Cleanup();
    void ShowSettings(bool firstRun = false);
    void ShowAbout();
    void ShowHelp();
    Settings& GetSettings() { return m_settings; }
    HINSTANCE GetHInstance() const { return m_hInst; }
    HWND GetHWND() const { return m_hwnd; }
    void ShowNotification(const wchar_t* title, const wchar_t* msg);
    bool RegisterAppHotkey();
    void UnregisterAppHotkey();

private:
    App() : m_hwnd(NULL), m_hInst(NULL) {}
    ~App() {}
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AboutDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK HelpDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd;
    HINSTANCE m_hInst;
    Settings m_settings;
};
