#pragma once
#include <windows.h>
#include <string>

class Settings {
public:
    Settings();
    ~Settings() {}

    bool IsFirstRun() const;
    void SetFirstRun(bool val);
    void Load();
    void Save();

    UINT GetModifiers() const;
    void SetModifiers(UINT mods);
    UINT GetVirtualKey() const;
    UINT GetKeyCode() const { return vkey; }
    void SetKeyCode(UINT key) { vkey = key; }
    std::wstring GetSavePath() const;
    void ApplyAutostartRegistry();

    int GetEffectiveLanguage() const;

    bool ctrl;
    bool alt;
    bool shift;
    UINT vkey;
    bool autostart;
    bool isDarkMode;
    int language; // 0 = AUTO, 1 = TR, 2 = EN
    std::wstring savePath;

private:
    std::wstring m_iniPath;
    std::wstring GetAppDataDir() const;
};
