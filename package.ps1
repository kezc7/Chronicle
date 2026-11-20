# Chronicle — Skyrim SE/AE chat and activity log
# Copyright (C) 2026 kezc7
# This file is part of Chronicle and is licensed under the GNU GPL, version 3
# or any later version. See LICENSE.

param(
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Config = 'Debug',
    # PrismaUI packages as Chronicle-<version>; Magelight (beta) as Chronicle-Magelight-<version>
    # with the page under Magelight/Chronicle/views instead of PrismaUI/views/Chronicle.
    [ValidateSet('PrismaUI','Magelight')][string]$UiHost = 'PrismaUI',
    # A release label ('beta', 'rc1') names the zip Chronicle-<version>-<label>-<host>
    # instead of the timestamped build name; a rerun replaces it.
    [ValidatePattern('^[a-z0-9][a-z0-9.-]{0,31}$')][string]$Label = '',
    # Passed on to the build: empty this host's build folder and build everything again.
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
# One version: the project VERSION in CMakeLists.txt. Every other place that states it
# (package.json, vcpkg.json, the README's title and newest notes) must agree before
# anything is built; set-version.ps1 writes them.
& node --test "$projectRoot\tests\version.test.cjs"
if ($LASTEXITCODE -ne 0) { throw 'A version field disagrees with CMakeLists.txt; package not created' }
& "$projectRoot\build.ps1" -Config $Config -UiHost $UiHost -Clean:$Clean
if ($LASTEXITCODE -ne 0) { throw 'Build failed; package not created' }
$cmakeText = Get-Content -LiteralPath "$projectRoot\CMakeLists.txt" -Raw
if ($cmakeText -notmatch '\bproject\s*\(\s*Chronicle\s+VERSION\s+(\d+\.\d+\.\d+)\b') { throw 'Cannot find project version' }
$version = $Matches[1]
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$buildDir = "$projectRoot\out\build\x64-$Config"
$viewDir = 'PrismaUI\views\Chronicle'
$name = "Chronicle-$version-$Config-$stamp"
if ($UiHost -eq 'Magelight') {
    $buildDir += '-Magelight'
    $viewDir = 'Magelight\Chronicle\views'
    $name = "Chronicle-Magelight-$version-$Config-$stamp"
}
if ($Label) { $name = "Chronicle-$version-$Label-$UiHost" }
# The DLL about to be packed must be this version's build.
$dllVersion = (Get-Item -LiteralPath "$buildDir\Chronicle.dll").VersionInfo.FileVersion
if ($dllVersion -ne "$version.0") { throw "Chronicle.dll says $dllVersion, not $version.0; package not created" }
# One compiler across the suite: the DLL must come from Visual Studio 2022's linker (14.4x).
# The linker's version is in the DLL's own header.
$dllBytes = [System.IO.File]::ReadAllBytes("$buildDir\Chronicle.dll")
$peHeader = [System.BitConverter]::ToInt32($dllBytes, 0x3C)
$linker = '{0}.{1:00}' -f $dllBytes[$peHeader + 26], $dllBytes[$peHeader + 27]
if ($linker -notmatch '^14\.4\d$') { throw "Chronicle.dll was linked by $linker, not Visual Studio 2022's 14.4x; package not created" }
# The linker can be 2022's while objects inside the DLL (a dependency vcpkg built) come from a
# newer compiler. The Rich header names the build of every tool that made an object: VS 2022
# is below 35700. No header, or one that can't be read, fails too (tools\rich-header.py,
# copied unchanged from DovahCamp ebd7d9b).
& python -I "$projectRoot\tools\rich-header.py" "$buildDir\Chronicle.dll" --below 35700
if ($LASTEXITCODE -ne 0) { throw "Chronicle.dll holds an object from a compiler newer than Visual Studio 2022's, or has no readable Rich header; package not created" }
$stage = Join-Path $projectRoot "out\staging\$name"
# A labelled name repeats between runs: never pack an earlier run's files. A clean never
# deletes a git worktree.
. "$projectRoot\tools\worktree-guard.ps1"
if (Test-Path -LiteralPath $stage) {
    Assert-NoWorktreeInside -Folder $stage -Repo $projectRoot
    Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath "$projectRoot\out\$name.zip") { Remove-Item -LiteralPath "$projectRoot\out\$name.zip" -Force }
New-Item -ItemType Directory -Path "$stage\SKSE\Plugins", "$stage\$viewDir" -Force | Out-Null
Copy-Item -LiteralPath "$buildDir\Chronicle.dll" -Destination "$stage\SKSE\Plugins\Chronicle.dll"
# The PDB ships beside the DLL, so crash logs can name Chronicle's functions. It must come from
# the same link: tools\pdb-match.py compares the CodeView GUID and age the DLL names with the
# PDB's own. It holds build paths, so the user-folder scan below reads it like any other entry.
if (!(Test-Path -LiteralPath "$buildDir\Chronicle.pdb")) { throw "Chronicle.pdb is missing from $buildDir; package not created" }
Copy-Item -LiteralPath "$buildDir\Chronicle.pdb" -Destination "$stage\SKSE\Plugins\Chronicle.pdb"
& python -I "$projectRoot\tools\pdb-match.py" "$stage\SKSE\Plugins\Chronicle.dll" "$stage\SKSE\Plugins\Chronicle.pdb"
if ($LASTEXITCODE -ne 0) { throw 'Chronicle.pdb is not from the link that made Chronicle.dll; package not created' }
# Explicit file list: the page's own files, never preview or test assets. dropdowns.js and
# dropdowns.css are DovahHub's list control, copied unchanged (CHRON-29).
foreach ($file in 'index.html','script.js','controls.js','pad.js','bridge.js','i18n.js','styles.css','config.json','dropdowns.js','dropdowns.css') {
    Copy-Item -LiteralPath "$projectRoot\Data\PrismaUI\views\Chronicle\$file" -Destination "$stage\$viewDir\$file"
}
# The suite's theme kit (from DovahHub, unchanged) and its fonts with their licence.
New-Item -ItemType Directory -Path "$stage\$viewDir\theme\fonts" -Force | Out-Null
Copy-Item -LiteralPath "$projectRoot\Data\PrismaUI\views\Chronicle\theme\dovah-theme.css" -Destination "$stage\$viewDir\theme\dovah-theme.css"
foreach ($file in 'BarlowCondensed-Regular.ttf','BarlowCondensed-Medium.ttf','BarlowCondensed-SemiBold.ttf','OFL-BarlowCondensed.txt') {
    Copy-Item -LiteralPath "$projectRoot\Data\PrismaUI\views\Chronicle\theme\fonts\$file" -Destination "$stage\$viewDir\theme\fonts\$file"
}
# Chronicle's DovahHub tab: module pages live inside DovahHub's own view folder, on either UI host.
$moduleDir = 'PrismaUI\views\DovahHub\modules\chronicle'
New-Item -ItemType Directory -Path "$stage\$moduleDir" -Force | Out-Null
foreach ($file in 'index.html','history.css','history.js') {
    Copy-Item -LiteralPath "$projectRoot\Data\$moduleDir\$file" -Destination "$stage\$moduleDir\$file"
}
Copy-Item -LiteralPath "$projectRoot\README.md", "$projectRoot\LICENSE", "$projectRoot\THIRD_PARTY_NOTICES.md" -Destination $stage
$zip = Join-Path $projectRoot "out\$name.zip"
# Build the archive directly so entry names always use '/' (Windows PowerShell 5.1 Compress-Archive writes '\').
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
        [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally { $archive.Dispose() }
# Read the actual archive and verify the native and UI payload against this build.
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $relative = $file.FullName.Substring($stage.Length + 1).Replace('\','/')
        $entry = $archive.GetEntry($relative)
        if (!$entry) { throw "Missing archive entry: $relative" }
        $stream = $entry.Open()
        $sha = [System.Security.Cryptography.SHA256]::Create()
        try { $actual = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($actual -ne (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash) { throw "Archive content mismatch: $relative" }
    }
} finally { $archive.Dispose() }
# Only Chronicle's own files ship: every entry name of the finished archive must be on
# tools\zip-allowlist.txt for this host.
& python -I "$projectRoot\tools\zip-allowlist.py" $zip --host $UiHost
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $zip -Force
    throw 'The archive holds an entry that is not on the allowlist; package removed'
}
# Nothing shipped may hold a user-folder path or the name of the user who built it:
# every entry is read, the DLL included.
& python -I "$projectRoot\tools\zip-user-scan.py" $zip
if ($LASTEXITCODE -ne 0) {
    Remove-Item -LiteralPath $zip -Force
    throw 'The archive names a user folder or the building user; package removed'
}
Write-Host "[Chronicle] Verified package: $zip"
Get-FileHash -LiteralPath $zip -Algorithm SHA256
