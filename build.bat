rem Chronicle — Skyrim SE/AE chat and activity log
rem Copyright (C) 2026 kezc7
rem This file is part of Chronicle and is licensed under the GNU GPL, version 3
rem or any later version. See LICENSE.

@echo off
rem The build lives in build.ps1 (the compiler, the folders it may work in, the tests).
rem This runs its Debug build; anything after the name is passed on, e.g. -UiHost Magelight.
powershell -NoProfile -File "%~dp0build.ps1" -Config Debug %*
exit /b %ERRORLEVEL%
