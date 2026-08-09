@echo off
echo Are you sure you want to uninstall ETDSelect?
pause

echo Killing ETDSelect process...
taskkill /f /im ETDSelect.exe 2>nul

echo Removing from startup...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "ETDSelect" /f

echo Removing Start Menu shortcut...
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\ETDSelect.lnk" 2>nul

echo Removing install directory...
set "INSTALL_DIR=%LOCALAPPDATA%\ETDSelect"
rmdir /s /q "%INSTALL_DIR%"

echo Uninstallation complete.
pause
