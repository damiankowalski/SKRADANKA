@echo off
rem Configures and builds Penumbra with the MSVC toolchain.
rem Usage: Tools\Build.bat [debug|release|dist]   (default: debug)
setlocal
set PRESET=%~1
if "%PRESET%"=="" set PRESET=debug

call "%~dp0VsDevEnv.bat" || exit /b 1
pushd "%~dp0.."
cmake --preset %PRESET% || (popd & exit /b 1)
cmake --build --preset %PRESET% || (popd & exit /b 1)
popd
