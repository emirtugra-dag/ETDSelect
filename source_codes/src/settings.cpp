#include "settings.h"
#include <shlobj.h>

Settings::Settings() : ctrl(true), alt(false), shift(true), vkey('S'), autostart(true), isDarkMode(true), language(0) {
    m_iniPath = GetAppDataDir() + L"\\settings.ini";
    Load();
}

std::wstring Settings::GetAppDataDir() const {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        std::wstring dir = std::wstring(path) + L"\\ETDSelect";
        CreateDirectoryW(dir.c_str(), NULL);
        return dir;
    }
    return L".";
}

bool Settings::IsFirstRun() const {
    DWORD attrib = GetFileAttributesW(m_iniPath.c_str());
    return (attrib == INVALID_FILE_ATTRIBUTES);
}

void Settings::SetFirstRun(bool) {
}

int Settings::GetEffectiveLanguage() const {
    if (language == 1) return 1; // Turkish
    if (language == 2) return 2; // English
    // 0 = AUTO: detect OS language
    LANGID langId = GetUserDefaultUILanguage();
    if (PRIMARYLANGID(langId) == LANG_TURKISH) {
        return 1; // TR
    }
    return 2; // EN
}

void Settings::Load() {
    if (IsFirstRun()) {
        wchar_t docs[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docs))) {
            savePath = std::wstring(docs) + L"\\ETDSelect Screenshots";
        } else {
            savePath = L"C:\\ETDSelect Screenshots";
        }
        autostart = true;
        isDarkMode = true;
        language = 0;
        return;
    }

    const wchar_t* ini = m_iniPath.c_str();
    ctrl  = GetPrivateProfileIntW(L"Hotkey", L"Ctrl",  1,   ini) != 0;
    alt   = GetPrivateProfileIntW(L"Hotkey", L"Alt",   0,   ini) != 0;
    shift = GetPrivateProfileIntW(L"Hotkey", L"Shift", 1,   ini) != 0;
    vkey  = (UINT)GetPrivateProfileIntW(L"Hotkey", L"Key", 'S', ini);
    if (vkey == 0) {
        ctrl = true;
        alt = false;
        shift = true;
        vkey = 'S';
    }

    autostart  = GetPrivateProfileIntW(L"General", L"Autostart",  1, ini) != 0;
    isDarkMode = GetPrivateProfileIntW(L"General", L"DarkMode",   1, ini) != 0;
    language   = GetPrivateProfileIntW(L"General", L"Language",   0, ini);

    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(L"General", L"SavePath", L"", buf, MAX_PATH, ini);
    savePath = buf;
    if (savePath.empty()) {
        wchar_t docs[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, docs))) {
            savePath = std::wstring(docs) + L"\\ETDSelect Screenshots";
        }
    }
}

void Settings::Save() {
    const wchar_t* ini = m_iniPath.c_str();
    WritePrivateProfileStringW(L"Hotkey", L"Ctrl",  ctrl  ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"Hotkey", L"Alt",   alt   ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"Hotkey", L"Shift", shift ? L"1" : L"0", ini);

    wchar_t keyStr[16];
    wsprintfW(keyStr, L"%u", vkey);
    WritePrivateProfileStringW(L"Hotkey", L"Key", keyStr, ini);
    WritePrivateProfileStringW(L"General", L"Autostart",  autostart  ? L"1" : L"0", ini);
    WritePrivateProfileStringW(L"General", L"DarkMode",   isDarkMode ? L"1" : L"0", ini);

    wchar_t langStr[16];
    wsprintfW(langStr, L"%d", language);
    WritePrivateProfileStringW(L"General", L"Language", langStr, ini);

    WritePrivateProfileStringW(L"General", L"SavePath", savePath.c_str(), ini);

    ApplyAutostartRegistry();
}

void Settings::ApplyAutostartRegistry() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (autostart) {
            wchar_t exePath[MAX_PATH];
            GetModuleFileNameW(NULL, exePath, MAX_PATH);
            std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
            RegSetValueExW(hKey, L"ETDSelect", 0, REG_SZ, (BYTE*)quoted.c_str(), (DWORD)(quoted.length() + 1) * sizeof(wchar_t));
        } else {
            RegDeleteValueW(hKey, L"ETDSelect");
        }
        RegCloseKey(hKey);
    }
}

UINT Settings::GetModifiers() const {
    UINT mod = 0;
    if (ctrl)  mod |= MOD_CONTROL;
    if (alt)   mod |= MOD_ALT;
    if (shift) mod |= MOD_SHIFT;
    return mod;
}

void Settings::SetModifiers(UINT mods) {
    ctrl  = (mods & MOD_CONTROL) != 0;
    alt   = (mods & MOD_ALT) != 0;
    shift = (mods & MOD_SHIFT) != 0;
}

UINT Settings::GetVirtualKey() const {
    return vkey;
}

std::wstring Settings::GetSavePath() const {
    return savePath;
}
