#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>

int main() {
    std::wstring fullPath = L"C:\\Users\\vboxuser\\AppData\\Local\\Temp\\ETDSelect\\resim_20261027_234500.png";

    if (!OpenClipboard(NULL)) return 1;
    EmptyClipboard();

    size_t pathChars = fullPath.length() + 2;
    size_t totalBytes = sizeof(DROPFILES) + pathChars * sizeof(wchar_t);
    HGLOBAL hDrop = GlobalAlloc(GHND | GMEM_SHARE, totalBytes);
    DROPFILES* pDrop = (DROPFILES*)GlobalLock(hDrop);
    pDrop->pFiles = sizeof(DROPFILES);
    pDrop->fWide = TRUE;
    wchar_t* pFileList = (wchar_t*)((BYTE*)pDrop + sizeof(DROPFILES));
    memcpy(pFileList, fullPath.c_str(), (fullPath.length() + 1) * sizeof(wchar_t));
    pFileList[fullPath.length() + 1] = L'\0';
    GlobalUnlock(hDrop);

    SetClipboardData(CF_HDROP, hDrop);
    CloseClipboard();

    return 0;
}
