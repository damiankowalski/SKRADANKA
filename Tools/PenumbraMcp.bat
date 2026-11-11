@echo off
rem Starts the Penumbra MCP server on stdin/stdout. Registered for Claude Code in .mcp.json.
rem Uses build\release if it exists, otherwise build\debug; set PENUMBRA_MCP_BUILD=debug|release to choose.
rem Extra arguments are passed on, e.g. --backend headless, --port 7778, --project DemoProject.
rem Nothing but the server may write to stdout here: it carries the protocol.
setlocal
set PN_BUILD=%PENUMBRA_MCP_BUILD%
if "%PN_BUILD%"=="" if exist "%~dp0..\build\release\bin\PenumbraEditor.exe" set PN_BUILD=release
if "%PN_BUILD%"=="" set PN_BUILD=debug
set PN_EDITOR=%~dp0..\build\%PN_BUILD%\bin\PenumbraEditor.exe
if not exist "%PN_EDITOR%" (
    echo [Penumbra] %PN_EDITOR% not found: run Tools\Build.bat %PN_BUILD% first. 1>&2
    exit /b 1
)
"%PN_EDITOR%" --mcp %*
