@echo off
set "INSTALL_DIR=%LOCALAPPDATA%\ETDSelect"
echo Installing ETDSelect to %INSTALL_DIR%...

mkdir "%INSTALL_DIR%" 2>nul
copy /Y "ETDSelect.exe" "%INSTALL_DIR%\"
copy /Y "uninstall.bat" "%INSTALL_DIR%\"

echo Adding to startup...
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "ETDSelect" /t REG_SZ /d "\"%INSTALL_DIR%\ETDSelect.exe\"" /f

echo Creating Start Menu shortcut...
set "START_MENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\ETDSelect.lnk"
powershell -Command "$wshell = New-Object -ComObject WScript.Shell; $s = $wshell.CreateShortcut('%START_MENU%'); $s.TargetPath = '%INSTALL_DIR%\ETDSelect.exe'; $s.WorkingDirectory = '%INSTALL_DIR%'; $s.Save()"

echo Starting ETDSelect...
start "" "%INSTALL_DIR%\ETDSelect.exe"

echo Installation complete!
pause
