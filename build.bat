@echo off
REM Build CombatSpeedPlus.dll!

set MEWGENICS_DIR=D:\SteamLibrary\steamapps\common\Mewgenics

setlocal

REM Locate Visual Studio via vswhere...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo ERROR: vswhere.exe not found. Is Visual Studio installed?
    pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
    echo ERROR: Could not find a Visual Studio installation.
    pause
    exit /b 1
)

if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo ERROR: vcvarsall.bat not found at "%VSDIR%\VC\Auxiliary\Build\"
    pause
    exit /b 1
)

echo Setting up x64 MSVC environment...
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1

REM Build...
echo.
echo Building CombatSpeedPlus.dll...
cl /LD /O2 /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /TP src\CombatSpeedPlus.c /Fe:CombatSpeedPlus.dll

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

echo.
echo Build succeeded!

REM Clean intermediate files...
del /Q CombatSpeedPlus.obj CombatSpeedPlus.lib CombatSpeedPlus.exp 2>nul

REM Deploy...
if not defined MEWGENICS_DIR (
    echo.
    echo WARNING: MEWGENICS_DIR not set. Cannot deploy.
    echo Set it to your Mewgenics install directory, e.g.:
    echo   set MEWGENICS_DIR=D:\SteamLibrary\steamapps\common\Mewgenics
    pause
    exit /b 1
)

if not exist "%MEWGENICS_DIR%\mods" (
    mkdir "%MEWGENICS_DIR%\mods"
)

copy /Y CombatSpeedPlus.dll "%MEWGENICS_DIR%\mods\CombatSpeedPlus.dll"
echo Deployed to %MEWGENICS_DIR%\mods\CombatSpeedPlus.dll