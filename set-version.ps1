# Chronicle — Skyrim SE/AE chat and activity log
# Copyright (C) 2026 kezc7
# This file is part of Chronicle and is licensed under the GNU GPL, version 3
# or any later version. See LICENSE.

# Sets Chronicle's version. CMakeLists.txt's project VERSION is the one version
# (the DLL, its FileVersion and the archive name are built from it); this writes
# it there and into the places that can't read CMake: package.json, vcpkg.json
# and the README's title. The release notes for the version are written by hand;
# tests/version.test.cjs fails until every one of them agrees.
#
#   .\set-version.ps1 -Version 1.4.23
param(
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
    [string]$Root = $PSScriptRoot
)
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
# Each field: the file, and a pattern whose first group is everything before the
# version and whose second is everything after it. Line endings are left alone.
$fields = @(
    @{ File = 'CMakeLists.txt'; Pattern = '(\bproject\s*\(\s*Chronicle\s+VERSION\s+)\d+\.\d+\.\d+(\b)' },
    @{ File = 'package.json';   Pattern = '("version"\s*:\s*")[^"]*(")' },
    @{ File = 'vcpkg.json';     Pattern = '("version-string"\s*:\s*")[^"]*(")' },
    @{ File = 'README.md';      Pattern = '(\A# Chronicle )\S+([ \t]*\r?\n)' }
)
# Read and check everything first, so a missing field changes no file.
$writes = foreach ($field in $fields) {
    $path = Join-Path $Root $field.File
    $text = [System.IO.File]::ReadAllText($path, $utf8)
    $pattern = New-Object System.Text.RegularExpressions.Regex($field.Pattern)
    if ($pattern.Matches($text).Count -ne 1) { throw "$($field.File): expected exactly one version field" }
    @{ Path = $path; File = $field.File; Old = $text; New = $pattern.Replace($text, ('${1}' + $Version + '${2}')) }
}
foreach ($write in $writes) {
    if ($write.New -ceq $write.Old) { Write-Host "[Chronicle] $($write.File) already says $Version"; continue }
    [System.IO.File]::WriteAllText($write.Path, $write.New, $utf8)
    Write-Host "[Chronicle] $($write.File) -> $Version"
}
Write-Host "[Chronicle] Now add the '## $Version' release notes to README.md."
