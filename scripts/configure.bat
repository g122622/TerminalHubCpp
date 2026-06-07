@echo off
setlocal

:: ============================================================
:: TerminalHubCpp - Build Environment Setup Script
:: Automatically configures Visual Studio developer environment
:: before running CMake commands.
::
:: Usage:
::   configure.bat                  - Configure with debug preset
::   configure.bat build            - Configure + build
::   configure.bat <cmake args...>  - Run cmake with VS env (any args)
:: ============================================================

:: Call VsDevCmd to set up VS environment
call "%~dp0vsenv.bat" >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to set up Visual Studio developer environment.
    echo Make sure Visual Studio is installed at:
    echo   D:\Program Files\Microsoft Visual Studio\18\Community
    exit /b 1
)

:: Default: configure with the project's standard preset
if "%~1"=="" (
    echo Configuring with preset windows-clang-debug...
    cmake --preset windows-clang-debug
    exit /b %errorlevel%
)

:: "build" shortcut: configure then build
if /i "%~1"=="build" (
    echo Configuring with preset windows-clang-debug...
    cmake --preset windows-clang-debug
    if errorlevel 1 exit /b %errorlevel%
    echo.
    echo Building...
    cmake --build --preset windows-clang-debug
    exit /b %errorlevel%
)

:: Otherwise, pass all args through to cmake
cmake %*
exit /b %errorlevel%
