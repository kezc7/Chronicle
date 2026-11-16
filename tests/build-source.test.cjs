// Chronicle — Skyrim SE/AE chat and activity log
// Copyright (C) 2026 kezc7
//
// This file is part of Chronicle.
// Chronicle is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// Chronicle is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. See LICENSE.

// A path the compiler, the linker or vcpkg worked in can end up inside a binary, so a
// build must not work in a user folder. build.ps1 points the temporary folder and
// vcpkg's downloads, registries and binary cache at out\work before CMake runs, and
// after the MSVC environment is loaded (which would otherwise put the inherited values
// back). build.bat only calls build.ps1, so there is one build to keep right. The
// release script then reads every entry of the archive it made.
//
// The whole suite is built with one compiler, Visual Studio 2022's (MSVC 14.4x). With a
// newer Visual Studio installed beside it, a fresh configure picks the newer one without
// a word, so build.ps1 loads 2022's by version, checks what it got, and refuses a build
// folder that remembers another compiler. vcpkg builds the dependencies with the same
// toolset, and the release script reads the linker version out of the DLL.
'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const FOLDERS = ['TMP', 'TEMP', 'VCPKG_DOWNLOADS', 'VCPKG_DEFAULT_BINARY_CACHE', 'X_VCPKG_REGISTRIES_CACHE'];
const read = name => fs.readFileSync(path.join(ROOT, name), 'utf8');

// What's wrong with a script's folder setup: [] when every folder is set between the
// MSVC environment and the first CMake run, and none points into a user folder.
function folderProblems(script, {setter, environment, configure}) {
  const problems = [];
  const loaded = script.indexOf(environment);
  const run = script.indexOf(configure);
  if (loaded < 0) problems.push('the MSVC environment step was not found');
  if (run < 0) problems.push('the CMake configure step was not found');
  for (const name of FOLDERS) {
    const match = setter(name).exec(script);
    if (!match) {
      problems.push(`${name} is not set`);
      continue;
    }
    if (match.index < loaded) problems.push(`${name} is set before the MSVC environment is loaded`);
    if (match.index > run) problems.push(`${name} is set after CMake has run`);
    if (/\b(USERPROFILE|LOCALAPPDATA|APPDATA|HOMEPATH)\b|[\\/]Users[\\/]/i.test(match[0])) problems.push(`${name} points into a user folder`);
  }
  return problems;
}

const POWERSHELL = {
  // A row of the $folders table: NAME = "<path>"
  setter: name => new RegExp(`^[ \\t]*${name}[ \\t]*=[ \\t]*"[^"\\r\\n]+"`, 'm'),
  environment: 'cmd /c',
  configure: '& $cmake',
};

test('build.ps1 keeps temporary files and the vcpkg folders out of the user folder', () => {
  const script = read('build.ps1');
  assert.deepEqual(folderProblems(script, POWERSHELL), []);
  // The table is applied to the process, and the folders exist before vcpkg needs them.
  assert.match(script, /foreach \(\$name in \$folders\.Keys\)[\s\S]{0,200}New-Item -ItemType Directory[\s\S]{0,200}SetEnvironmentVariable\(\$name, \$folders\[\$name\], 'Process'\)/);
  assert.ok(script.indexOf("SetEnvironmentVariable($name, $folders[$name], 'Process')") < script.indexOf('& $cmake'));
});

test('build.bat runs build.ps1 and has no build of its own', () => {
  const script = read('build.bat');
  assert.match(script, /^powershell -NoProfile -File "%~dp0build\.ps1" -Config Debug %\*\r?$/m);
  assert.doesNotMatch(script, /cmake|ninja|VsDevCmd|vcpkg/i);
});

test('a missing, late or user-folder setting is named', () => {
  const good = [
    'cmd /c "VsDevCmd.bat && set"',
    '$folders = [ordered]@{',
    ...FOLDERS.map(name => `    ${name} = "$work\\${name.toLowerCase()}"`),
    '}',
    '& $cmake -S $src',
  ].join('\r\n');
  assert.deepEqual(folderProblems(good, POWERSHELL), []);
  assert.deepEqual(folderProblems(good.replace(/^ {4}TEMP = .*\r\n/m, ''), POWERSHELL), ['TEMP is not set']);
  assert.deepEqual(folderProblems(good.replace('"$work\\tmp"', '"$env:LOCALAPPDATA\\Temp"'), POWERSHELL), ['TMP points into a user folder']);
  assert.deepEqual(folderProblems(good.replace('"$work\\vcpkg_downloads"', '"C:\\Users\\someone\\vcpkg"'), POWERSHELL),
    ['VCPKG_DOWNLOADS points into a user folder']);
  const late = good.replace('& $cmake -S $src', '').replace('$folders', '& $cmake -S $src\r\n$folders');
  assert.equal(folderProblems(late, POWERSHELL).length, FOLDERS.length);
  const early = good.replace('cmd /c "VsDevCmd.bat && set"\r\n', '').replace('& $cmake', 'cmd /c "VsDevCmd.bat && set"\r\n& $cmake');
  assert.ok(folderProblems(early, POWERSHELL).every(line => line.endsWith('is set before the MSVC environment is loaded')));
  assert.equal(folderProblems(early, POWERSHELL).length, FOLDERS.length);
});

test('the release script reads every entry of the archive it made', () => {
  const script = read('package.ps1');
  const made = script.indexOf('ZipFile]::Open(');
  const scan = script.indexOf('zip-user-scan.py');
  assert.ok(made >= 0 && scan > made, 'the scan runs on the finished archive');
  assert.match(script.slice(scan), /\$LASTEXITCODE -ne 0[\s\S]{0,120}Remove-Item -LiteralPath \$zip[\s\S]{0,120}throw/);
  assert.ok(fs.existsSync(path.join(ROOT, 'tools', 'zip-user-scan.py')));
});

test("build.ps1 loads Visual Studio 2022's compiler and refuses another", () => {
  const script = read('build.ps1');
  // The compiler's environment comes from a 17.x install that vswhere finds.
  assert.match(script, /^\$msvcBase = & \$vswhere [^\r\n]*-version '\[17\.0,18\.0\)'[^\r\n]*-property installationPath\r?$/m);
  assert.match(script, /^\$devCmd = "\$msvcBase\\Common7\\Tools\\VsDevCmd\.bat"\r?$/m);
  assert.ok(script.includes("$compilerPattern = '^14\\.4\\d\\.'"));
  const loaded = script.indexOf('cmd /c');
  const checked = script.indexOf('if ($env:VCToolsVersion -notmatch $compilerPattern)');
  const configure = script.indexOf('& $cmake');
  const compile = script.indexOf('--build $build');
  assert.ok(loaded >= 0 && checked > loaded && checked < configure, 'the loaded compiler is checked before CMake runs');
  assert.match(script.slice(checked, checked + 200), /throw/);
  // A build folder keeps the compiler it was first configured with: checked before the
  // configure (an old folder) and after it (a new one).
  const guards = [...script.matchAll(/^Assert-BuildFolderCompiler\r?$/gm)].map(match => match.index);
  assert.equal(guards.length, 2);
  assert.ok(guards[0] < configure && guards[1] > configure && guards[1] < compile);
  assert.ok(script.includes("'[\\\\/]MSVC[\\\\/]14\\.4\\d\\.'"), 'the folder check reads the toolset from the compiler path');
});

test('vcpkg builds the dependencies with the same toolset', () => {
  const triplet = read(path.join('triplets', 'x64-windows-static.cmake'));
  for (const line of ['set(VCPKG_TARGET_ARCHITECTURE x64)', 'set(VCPKG_CRT_LINKAGE static)', 'set(VCPKG_LIBRARY_LINKAGE static)',
    'set(VCPKG_PLATFORM_TOOLSET v143)']) {
    assert.ok(triplet.split(/\r?\n/).includes(line), line);
  }
  const cmake = read('CMakeLists.txt');
  const overlay = cmake.indexOf('set(VCPKG_OVERLAY_TRIPLETS "${CMAKE_CURRENT_LIST_DIR}/triplets"');
  assert.ok(overlay >= 0 && overlay < cmake.indexOf('project('), 'the triplet folder is named before project()');
  assert.ok(cmake.split(/\r?\n/).includes('set(VCPKG_TARGET_TRIPLET x64-windows-static)'));
  const script = read('build.ps1');
  assert.ok(script.includes('-DVCPKG_TARGET_TRIPLET=x64-windows-static'));
  // v143 alone can match an older compiler inside a newer install, so vcpkg is given the
  // install, and the compiler it reports for every triplet is checked before compiling.
  const configure = script.indexOf('& $cmake');
  const install = script.indexOf('$env:VCPKG_VISUAL_STUDIO_PATH = $msvcBase');
  const checked = script.search(/^Assert-DependencyCompiler\r?$/m);
  assert.ok(install >= 0 && install < configure, 'vcpkg is told which Visual Studio before CMake runs');
  assert.ok(checked > configure && checked < script.indexOf('--build $build'), "vcpkg's compiler is checked after the configure");
  assert.ok(script.includes("-Pattern '^Compiler found: (.*)$'"));
  assert.match(script, /if \(!\$found\) \{ throw/);
});

test('the release script refuses a DLL from another linker', () => {
  const script = read('package.ps1');
  // The linker's major and minor version are bytes 2 and 3 of the optional header,
  // which starts 24 bytes after the PE signature that offset 0x3C points to.
  assert.ok(script.includes('$peHeader = [System.BitConverter]::ToInt32($dllBytes, 0x3C)'));
  assert.ok(script.includes("$linker = '{0}.{1:00}' -f $dllBytes[$peHeader + 26], $dllBytes[$peHeader + 27]"));
  const check = script.indexOf("if ($linker -notmatch '^14\\.4\\d$') { throw");
  assert.ok(check > script.indexOf('build.ps1') && check < script.indexOf('ZipFile]::Open('), 'checked after the build, before the archive is written');
});
