# Chronicle — Skyrim SE/AE chat and activity log
# Copyright (C) 2026 kezc7
# This file is part of Chronicle and is licensed under the GNU GPL, version 3
# or any later version. See LICENSE.

# Chronicle build script
param(
    [string]$Config = "Debug",
    # PrismaUI is the release build; Magelight is the beta build. Each host
    # gets its own build directory so the two DLLs never overwrite each other.
    [ValidateSet('PrismaUI','Magelight')][string]$UiHost = 'PrismaUI',
    # Empty this host's build folder first, so everything in it is built again (a folder
    # configured with another compiler has to go). vcpkg's cache under out\work stays.
    [switch]$Clean,
    # CHRON-40: build through a drive made for the build, so no folder on this machine is
    # named in the DLL or its PDB (package.ps1 uses it for every Release). Implies -Clean.
    [switch]$Neutral
)

# CMake, Ninja and vcpkg are tools, taken from the Visual Studio that has them.
$toolsBase = "C:\Program Files\Microsoft Visual Studio\18\Community"
$cmake     = "$toolsBase\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja     = "$toolsBase\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
$vcpkgRoot = "$toolsBase\VC\vcpkg"
$toolchain = "$vcpkgRoot\scripts\buildsystems\vcpkg.cmake"
$src       = Split-Path -Parent $MyInvocation.MyCommand.Path
$build     = "$src\out\build\x64-$Config"
if ($UiHost -ne 'PrismaUI') { $build += "-$UiHost" }

# The compiler is Visual Studio 2022's (MSVC 14.4x), whatever newer Visual Studio is also
# installed: the whole suite is built with one compiler. Its Build Tools are enough.
# vcpkg picks its own compiler for the dependencies, from the newest Visual Studio that
# has the toolset, so it is told which install (VCPKG_VISUAL_STUDIO_PATH below) and
# which toolset (triplets\x64-windows-static.cmake), and what it used is checked.
$compilerPattern = '^14\.4\d\.'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvcBase = & $vswhere -products * -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath
if (!$msvcBase) { throw '[Chronicle] Visual Studio 2022 (or its Build Tools) with the C++ tools was not found' }
$devCmd = "$msvcBase\Common7\Tools\VsDevCmd.bat"

# A build folder remembers the compiler it was first configured with and keeps using it,
# whatever environment is loaded later. One made with another compiler has to go.
function Assert-BuildFolderCompiler {
    if (!(Test-Path -LiteralPath "$build\CMakeCache.txt")) { return }
    $line = Select-String -LiteralPath "$build\CMakeCache.txt" -Pattern '^CMAKE_CXX_COMPILER:[A-Z]+=(.*)$' | Select-Object -First 1
    if (!$line) { return }
    $compiler = $line.Matches[0].Groups[1].Value
    if ($compiler -notmatch '[\\/]MSVC[\\/]14\.4\d\.') {
        throw "[Chronicle] $build was configured with another compiler ($compiler). Build again with -Clean."
    }
}
# vcpkg names the compiler it found for each triplet every time it runs.
function Assert-DependencyCompiler {
    $found = @(Select-String -LiteralPath "$build\vcpkg-manifest-install.log" -Pattern '^Compiler found: (.*)$')
    if (!$found) { throw "[Chronicle] vcpkg didn't say which compiler it built the dependencies with" }
    foreach ($line in $found) {
        $compiler = $line.Matches[0].Groups[1].Value
        if ($compiler -notmatch '[\\/]MSVC[\\/]14\.4\d\.') {
            throw "[Chronicle] vcpkg built the dependencies with another compiler ($compiler). Delete $build and build again."
        }
    }
}

# A clean never deletes a git worktree: one inside the build folder stops it.
. "$src\tools\worktree-guard.ps1"
# A neutral build always starts empty: the drive's letter can differ from one run to the next,
# and a build folder remembers the paths it was configured with.
if ($Neutral) { $Clean = $true }
if ($Clean -and (Test-Path -LiteralPath $build)) {
    Assert-NoWorktreeInside -Folder $build -Repo $src
    Write-Host "[Chronicle] Emptying $build..."
    Remove-Item -LiteralPath $build -Recurse -Force -ErrorAction Stop
}
Assert-BuildFolderCompiler

# CHRON-40. The compiler records every source and header it read (the log macros' file names
# are in the DLL itself), and the linker records its own folder, its command line and every
# object and library by full path; neither has an option that rewrites all of those. So a
# neutral build works through a drive made for it, in the suite's layout (DovahHub's DHUB-142,
# DovahHeraldry's DHR-77): the first free letter is mapped to "neutral-roots\Chronicle" at the
# root of this drive, which holds one junction, src\Chronicle (this repository). Every path the
# tools record then reads <drive>:\src\Chronicle\..., whatever the letter, vcpkg's dependency
# builds included (they happen inside the build folder), and /PDBALTPATH:%_PDB% has the DLL name
# its PDB by file name alone. The junction and the drive are made for each run and removed after
# it; the repository is never touched.
$neutralLink = $null
$neutralDrive = $null
function Remove-NeutralRoot {
    if ($script:neutralDrive) {
        & subst.exe $script:neutralDrive /D | Out-Null
        $script:neutralDrive = $null
    }
    if ($script:neutralLink -and (Test-Path -LiteralPath $script:neutralLink)) {
        # A junction is removed as a link: the folder it points at stays as it is.
        [System.IO.Directory]::Delete($script:neutralLink, $false)
        $script:neutralLink = $null
    }
}
if ($Neutral) {
    $holder = Join-Path ([System.IO.Path]::GetPathRoot($src)) 'neutral-roots\Chronicle'
    New-Item -ItemType Directory -Path "$holder\src" -Force | Out-Null
    $link = "$holder\src\Chronicle"
    if (Test-Path -LiteralPath $link) {
        # An earlier run's junction, left by a build that was stopped. Anything else there stops this one.
        if (!((Get-Item -LiteralPath $link -Force).Attributes -band [System.IO.FileAttributes]::ReparsePoint)) {
            throw "[Chronicle] $link is a real folder, not the build's junction; move it away first"
        }
        [System.IO.Directory]::Delete($link, $false)
    }
    New-Item -ItemType Junction -Path $link -Target $src | Out-Null
    $neutralLink = $link
    # Another mod's release can take a letter at the same moment: a refused one is tried again with the next.
    $used = @([System.IO.DriveInfo]::GetDrives() | ForEach-Object { $_.Name.Substring(0, 1).ToUpperInvariant() })
    foreach ($letter in 'W', 'V', 'U', 'T', 'S', 'R', 'O', 'N', 'M', 'I') {
        if ($used -contains $letter) { continue }
        & subst.exe "${letter}:" $holder 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { $neutralDrive = "${letter}:"; break }
    }
    if (!$neutralDrive) {
        Remove-NeutralRoot
        throw '[Chronicle] No free drive letter for the neutral build (W to I are all in use)'
    }
    $src = "$neutralDrive\src\Chronicle"
    $build = "$src\out\build\x64-$Config"
    if ($UiHost -ne 'PrismaUI') { $build += "-$UiHost" }
    Write-Host "[Chronicle] Neutral build: $neutralDrive = $holder (src\Chronicle = the repository)"
}
try {

Write-Host "[Chronicle] Loading MSVC environment..."

# Capture environment from VsDevCmd
$envLines = cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 2>nul && set"
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
    }
}

if ($env:VCToolsVersion -notmatch $compilerPattern) {
    throw "[Chronicle] The loaded compiler is MSVC $($env:VCToolsVersion), not Visual Studio 2022's 14.4x"
}
Write-Host "[Chronicle] Compiler: MSVC $($env:VCToolsVersion)"

$env:VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_VISUAL_STUDIO_PATH = $msvcBase

# Nothing the build reads or writes may sit in a user folder: a path the compiler, the
# linker or vcpkg worked in can end up inside a binary. Temporary files and vcpkg's
# downloads, registries and binary cache go under out\work, beside the build folders
# (vcpkg already builds its packages inside the build folder). CHRONICLE_BUILD_WORK
# names another place. This comes after the MSVC environment, which is inherited.
$work = if ($env:CHRONICLE_BUILD_WORK) { $env:CHRONICLE_BUILD_WORK } else { "$src\out\work" }
# A neutral build keeps its own binary cache: a package built through the real folder names it.
$archives = if ($Neutral) { 'archives-neutral' } else { 'archives' }
$folders = [ordered]@{
    TMP                        = "$work\tmp"
    TEMP                       = "$work\tmp"
    VCPKG_DOWNLOADS            = "$work\vcpkg\downloads"
    VCPKG_DEFAULT_BINARY_CACHE = "$work\vcpkg\$archives"
    X_VCPKG_REGISTRIES_CACHE   = "$work\vcpkg\registries"
}
foreach ($name in $folders.Keys) {
    New-Item -ItemType Directory -Path $folders[$name] -Force | Out-Null
    [System.Environment]::SetEnvironmentVariable($name, $folders[$name], 'Process')
}
if ($work -match '^[A-Za-z]:[\\/]Users[\\/]') {
    Write-Warning "[Chronicle] The build's work folder is inside a user folder; don't cut a release from here."
}

Write-Host "[Chronicle] Configuring..."
& $cmake `
    -S $src `
    -B $build `
    -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    "-DCMAKE_BUILD_TYPE=$Config" `
    "-DCHRONICLE_UI_HOST=$UiHost" `
    "-DCHRONICLE_NEUTRAL_PATHS=$(if ($Neutral) { 'ON' } else { 'OFF' })" `
    -DCHRONICLE_BUILD_TESTS=ON

if ($LASTEXITCODE -ne 0) {
    Write-Error "[Chronicle] Configure FAILED (exit $LASTEXITCODE)"
    exit $LASTEXITCODE
}
Assert-BuildFolderCompiler
Assert-DependencyCompiler

Write-Host "[Chronicle] Building..."
& $cmake --build $build --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Error "[Chronicle] Build FAILED (exit $LASTEXITCODE)"
    exit $LASTEXITCODE
}

$dll = "$build\Chronicle.dll"
& "$toolsBase\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" --test-dir $build --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Chronicle native tests failed" }
$uiTests = Get-ChildItem -LiteralPath "$src\tests" -Filter '*.test.cjs' |
    Select-Object -ExpandProperty FullName
& node --test $uiTests
if ($LASTEXITCODE -ne 0) { throw "Chronicle UI tests failed" }
if (Test-Path $dll) {
    Write-Host ("[Chronicle] SUCCESS -- " + $dll)
} else {
    Write-Warning ("[Chronicle] Build reported success but DLL not found at " + $dll)
}

} finally {
    # The drive and the junction go however the build ended; the build folder keeps its files.
    Remove-NeutralRoot
}
