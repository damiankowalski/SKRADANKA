@echo off
rem Puts the MSVC x64 toolchain (cl, CMake, Ninja) on PATH, once per shell.
if defined VSCMD_VER if "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0
set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo [Penumbra] vswhere.exe not found - install Visual Studio with the C++ workload.
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSINSTALL=%%i
if not defined VSINSTALL (
    echo [Penumbra] No Visual Studio installation with the C++ x64 toolset was found.
    exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
exit /b 0
