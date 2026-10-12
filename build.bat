rem Chronicle — Skyrim SE/AE chat and activity log
rem Copyright (C) 2026 kezc7
rem This file is part of Chronicle and is licensed under the GNU GPL, version 3
rem or any later version. See LICENSE.

@echo off
setlocal

set VS_BASE=C:\Program Files\Microsoft Visual Studio\18\Community
set CMAKE="%VS_BASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set NINJA="%VS_BASE%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
set VCPKG_ROOT=%VS_BASE%\VC\vcpkg
set TOOLCHAIN=%VS_BASE%\VC\vcpkg\scripts\buildsystems\vcpkg.cmake
set SRC_DIR=%~dp0
set BUILD_DIR=%~dp0out\build\x64-Debug

call "%VS_BASE%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64

%CMAKE% -S "%SRC_DIR%" -B "%BUILD_DIR%" ^
    -G Ninja ^
    -DCMAKE_MAKE_PROGRAM=%NINJA% ^
    -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
    -DVCPKG_ROOT="%VCPKG_ROOT%" ^
    -DCMAKE_BUILD_TYPE=Debug

if %ERRORLEVEL% neq 0 (
    echo [Chronicle] CMake configure FAILED
    exit /b %ERRORLEVEL%
)

%CMAKE% --build "%BUILD_DIR%"

if %ERRORLEVEL% neq 0 (
    echo [Chronicle] Build FAILED
    exit /b %ERRORLEVEL%
)

echo [Chronicle] Build SUCCESS — DLL at %BUILD_DIR%\Chronicle.dll
