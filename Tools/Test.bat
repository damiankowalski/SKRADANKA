@echo off
rem Runs the unit tests and the headless feature-test scene.
rem Usage: Tools\Test.bat [debug|release]   (default: debug)
setlocal
set PRESET=%~1
if "%PRESET%"=="" set PRESET=debug

call "%~dp0VsDevEnv.bat" || exit /b 1
pushd "%~dp0.."
ctest --preset %PRESET% || (popd & exit /b 1)
popd
