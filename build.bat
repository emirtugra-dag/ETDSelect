@echo off
setlocal

:: Check if MinGW is in PATH, fallback if needed
where g++ >nul 2>nul
if errorlevel 1 (
    if exist C:\Users\vboxuser\Desktop\mingw64\bin\g++.exe (
        set PATH=C:\Users\vboxuser\Desktop\mingw64\bin;%PATH%
    )
)

:: Compiler settings
set CXX=g++
set WINDRES=windres
set CXXFLAGS=-std=c++17 -O2 -finput-charset=UTF-8 -fexec-charset=UTF-8 -DUNICODE -D_UNICODE -mwindows -municode -static -static-libgcc -static-libstdc++
set LDFLAGS=-lgdiplus -lcomctl32 -lmsimg32 -lcomdlg32 -lshlwapi -luser32 -lgdi32 -lshell32 -lole32

:: Create build directory
if not exist build mkdir build

echo [1/4] Compiling application resources...
%WINDRES% src/resource.rc -o build/resource.o
if errorlevel 1 goto :compile_error

echo [2/4] Compiling source files...
%CXX% %CXXFLAGS% -c src/main.cpp -o build/main.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% -c src/app.cpp -o build/app.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% -c src/settings.cpp -o build/settings.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% -c src/i18n.cpp -o build/i18n.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% -c src/overlay.cpp -o build/overlay.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% -c src/editor.cpp -o build/editor.o
if errorlevel 1 goto :compile_error

echo [3/4] Linking ETDSelect.exe...
%CXX% %CXXFLAGS% build/main.o build/app.o build/settings.o build/i18n.o build/overlay.o build/editor.o build/resource.o -o ETDSelect.exe %LDFLAGS%
if errorlevel 1 goto :compile_error

echo [4/4] Building Installer (ETDSelect_Setup.exe)...
%WINDRES% installer/setup_resource.rc -o build/setup_resource.o
if errorlevel 1 goto :compile_error
%CXX% %CXXFLAGS% installer/setup.cpp build/setup_resource.o -o ETDSelect_Setup.exe -lshlwapi -lole32 -lshell32 -luser32 -lgdi32 -luuid
if errorlevel 1 goto :compile_error

echo.
echo ========================================================
echo   BUILD SUCCESSFUL!
echo   Application: ETDSelect.exe
echo   Installer:   ETDSelect_Setup.exe
echo ========================================================
echo.
exit /b 0

:compile_error
echo.
echo ========================================================
echo   ERROR: Build process failed!
echo ========================================================
echo.
pause
exit /b 1
