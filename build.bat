@echo off
setlocal

rem Usage: build.bat [release|test]   (debug by default; test builds and runs the tests)

rem Find MSVC via vswhere unless cl is already on PATH (e.g. a Developer Command Prompt)
where cl >nul 2>nul
if not errorlevel 1 goto :have_cl
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do set "VCVARS=%%i"
if not defined VCVARS (
    echo MSVC not found. Install Visual Studio Build Tools with the C++ workload.
    exit /b 1
)
call "%VCVARS%" >nul 2>nul
:have_cl

set BIN=bin
set OBJ=obj
if not exist %BIN% mkdir %BIN%
rem Every build recompiles everything, so start obj\ clean. Linking obj\*.obj would
rem otherwise pick up stale objects from renamed sources or the other configuration.
if exist %OBJ% rmdir /s /q %OBJ%
mkdir %OBJ%

set IMGUI=third-party\imgui
set TOML=third-party\tomlc17\src
set INCLUDES=/I src /I %IMGUI% /I %IMGUI%\backends /I %TOML%
set DEFINES=/DUNICODE /D_UNICODE

if "%1"=="release" (
    set CFG=/O2 /MD /DNDEBUG
    set LINKCFG=
) else (
    set CFG=/Od /Zi /RTC1 /MDd /D_DEBUG /DSURUME_DEBUG
    set LINKCFG=/DEBUG
)

set COMMON=/nologo /c /MP /EHsc /utf-8 %CFG% %DEFINES% %INCLUDES% /Fo:%OBJ%\ /Fd:%OBJ%\
if "%1"=="test" goto :test

rem Third-party code: no warnings, we don't own it
cl %COMMON% /std:c++20 /W0 ^
    %IMGUI%\imgui.cpp %IMGUI%\imgui_draw.cpp %IMGUI%\imgui_tables.cpp %IMGUI%\imgui_widgets.cpp ^
    %IMGUI%\backends\imgui_impl_win32.cpp %IMGUI%\backends\imgui_impl_dx11.cpp
if errorlevel 1 exit /b 1

cl %COMMON% /W0 /D_CRT_SECURE_NO_WARNINGS %TOML%\tomlc17.c
if errorlevel 1 exit /b 1

rem App code. C4062 as an error: every switch on an enum must handle all members.
cl %COMMON% /std:c++20 /W4 /WX /we4062 src\*.cpp
if errorlevel 1 exit /b 1

rem Resources (app icon)
rc /nologo /fo %OBJ%\surume.res src\surume.rc
if errorlevel 1 exit /b 1

link /nologo %LINKCFG% /SUBSYSTEM:WINDOWS /OUT:%BIN%\surume.exe ^
    /PDB:%OBJ%\surume.pdb /ILK:%OBJ%\surume.ilk %OBJ%\*.obj %OBJ%\surume.res ^
    user32.lib shell32.lib ole32.lib uuid.lib d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1

echo Built %BIN%\surume.exe
endlocal
exit /b 0

rem Tests: the non-GUI modules plus src\tests, run against throwaway repositories
:test
cl %COMMON% /W0 /D_CRT_SECURE_NO_WARNINGS %TOML%\tomlc17.c
if errorlevel 1 exit /b 1

cl %COMMON% /std:c++20 /W4 /WX src\git.cpp src\log.cpp src\persistence.cpp src\tests\*.cpp
if errorlevel 1 exit /b 1

link /nologo /DEBUG /SUBSYSTEM:CONSOLE /OUT:%BIN%\surume_tests.exe ^
    /PDB:%OBJ%\surume_tests.pdb /ILK:%OBJ%\surume_tests.ilk %OBJ%\*.obj shell32.lib
if errorlevel 1 exit /b 1

%BIN%\surume_tests.exe
exit /b %errorlevel%
