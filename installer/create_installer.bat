@echo off
echo Creating ETDSelect Installer Package...
mkdir ETDSelect_Install 2>nul
copy "..\build\ETDSelect.exe" "ETDSelect_Install\"
copy "install.bat" "ETDSelect_Install\"
copy "uninstall.bat" "ETDSelect_Install\"
echo You can now ZIP the ETDSelect_Install directory.
pause
