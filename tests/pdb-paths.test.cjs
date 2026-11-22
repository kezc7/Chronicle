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

// CHRON-40: a release's DLL and PDB name no folder on the build machine. The
// release is built through a drive made for it (build.ps1 -Neutral), and
// package.ps1 stops when tools/pdb-paths.py finds any drive-letter path outside
// the toolchain's folders and the build drive's <drive>:\src\Chronicle. The
// paths below are made up; none is a real machine's.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const TOOL = path.join(ROOT, 'tools', 'pdb-paths.py');
const read = name => fs.readFileSync(path.join(ROOT, name), 'utf8');

// A binary-looking file with these strings in it, ASCII and UTF-16 by turns.
function blob(strings) {
  const parts = [Buffer.from([0, 1, 2, 0x7f, 0])];
  strings.forEach((text, i) => {
    parts.push(i % 2 ? Buffer.from(text, 'utf16le') : Buffer.from(text, 'latin1'), Buffer.from([0, 0, 0xff, 0]));
  });
  return Buffer.concat(parts);
}

function run(files) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-paths-'));
  const names = Object.entries(files).map(([name, data]) => {
    fs.writeFileSync(path.join(dir, name), data);
    return path.join(dir, name);
  });
  const result = spawnSync('python', ['-I', TOOL, ...names], {encoding: 'utf8'});
  fs.rmSync(dir, {recursive: true, force: true});
  return result;
}

const NEUTRAL = [
  'W:\\src\\Chronicle\\src\\Chronicle.cpp',
  'R:\\src\\Chronicle\\out\\build\\x64-Release\\vcpkg_installed\\x64-windows-static\\include\\spdlog\\spdlog.h',
  'C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\include\\vector',
  'C:\\Program Files (x86)\\Windows Kits\\10\\Include\\10.0.26100.0\\ucrt\\stdio.h',
  'C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\vcpkg\\scripts\\cmake\\x.cmake',
  'C:\\PROGRA~2\\MICROS~2\\2022\\BuildTools\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\cl.exe',
  'D:\\a\\_work\\1\\s\\src\\vctools\\crt\\vcruntime\\src\\eh\\throw.cpp',
  'd:\\os\\obj\\amd64fre\\onecore\\x.obj',
  '/PATHMAP:W:\\src\\Chronicle=\\src\\Chronicle "C:\\Program Files (x86)\\Microsoft',
];

test('a build through the drive, and the toolchain\'s own folders, pass', () => {
  const result = run({'Chronicle.dll': blob(NEUTRAL.slice(0, 3)), 'Chronicle.pdb': blob(NEUTRAL)});
  assert.equal(result.status, 0, result.stdout + result.stderr);
  assert.match(result.stdout, /paths outside the allowed roots: 0 \(PASS\)/);
});

for (const [what, local] of [
  ['a development folder', 'Q:\\Development\\SomeMod\\src\\main.cpp'],
  ['a user folder', 'C:\\Users\\builder\\AppData\\Local\\Temp\\cl.tmp'],
  ['another drive\'s checkout', 'E:\\mods\\Chronicle\\out\\build\\Chronicle.pdb'],
  ['a layout without the drive\'s src folder', 'W:\\Chronicle\\src\\Chronicle.cpp'],
]) {
  test(`${what} fails, in the DLL or the PDB, plain or UTF-16`, () => {
    for (const [dll, pdb] of [[[local], NEUTRAL], [NEUTRAL.slice(0, 2), NEUTRAL.concat([local])]]) {
      const result = run({'Chronicle.dll': blob(dll), 'Chronicle.pdb': blob(pdb)});
      assert.equal(result.status, 1, result.stdout);
      assert.match(result.stdout, /<- not allowed/);
      assert.match(result.stdout, /\(FAIL\)/);
    }
  });
}

test('it reads a zip\'s DLL and PDB, and a file it can\'t read is an error', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-paths-'));
  const zip = path.join(dir, 'pack.zip');
  const py = 'import sys, zipfile\nwith zipfile.ZipFile(sys.argv[1], "w") as z:\n' +
    '    z.writestr("SKSE/Plugins/Chronicle.dll", bytes.fromhex(sys.argv[2]))\n    z.writestr("README.md", "Q:\\\\Development\\\\x")\n';
  fs.writeFileSync(path.join(dir, 'make.py'), py);
  const made = spawnSync('python', ['-I', path.join(dir, 'make.py'), zip, blob(['Q:\\Development\\SomeMod\\a.cpp']).toString('hex')], {encoding: 'utf8'});
  assert.equal(made.status, 0, made.stderr);
  const result = spawnSync('python', ['-I', TOOL, zip], {encoding: 'utf8'});
  assert.equal(result.status, 1, result.stdout);
  assert.match(result.stdout, /SKSE\/Plugins\/Chronicle\.dll/);
  assert.doesNotMatch(result.stdout, /README/, 'only DLLs and PDBs are read');
  const missing = spawnSync('python', ['-I', TOOL, path.join(dir, 'gone.pdb')], {encoding: 'utf8'});
  assert.equal(missing.status, 2);
  fs.rmSync(dir, {recursive: true, force: true});
});

test('every release is built through the drive and stops on a local path', () => {
  const pack = read('package.ps1');
  assert.match(pack, /\$neutral = \$Config -eq 'Release'/);
  assert.match(pack, /build\.ps1" -Config \$Config -UiHost \$UiHost -Clean:\$Clean -Neutral:\$neutral/);
  const match = pack.indexOf('tools\\pdb-match.py');
  const gate = pack.indexOf('& python -I "$projectRoot\\tools\\pdb-paths.py" "$stage\\SKSE\\Plugins\\Chronicle.dll" "$stage\\SKSE\\Plugins\\Chronicle.pdb"');
  assert.ok(match > 0 && gate > match, 'the path check reads the staged DLL and PDB, after the PDB check');
  assert.ok(gate < pack.indexOf('ZipFile]::Open($zip'), 'before any archive is made');
  assert.match(pack.slice(gate, gate + 400), /throw 'Chronicle\.dll or Chronicle\.pdb names a folder on this machine; package not created'/);
});

test('the neutral build maps a drive to the repository and always removes it', () => {
  const build = read('build.ps1');
  assert.match(build, /\[switch\]\$Neutral/);
  assert.match(build, /if \(\$Neutral\) \{ \$Clean = \$true \}/, 'always a clean build');
  assert.ok(build.indexOf('$Clean = $true') < build.indexOf('Remove-Item -LiteralPath $build -Recurse'), 'decided before the clean');
  assert.match(build, /'neutral-roots\\Chronicle'/);
  assert.match(build, /New-Item -ItemType Junction -Path \$link -Target \$src/);
  assert.match(build, /& subst\.exe "\$\{letter\}:" \$holder/);
  assert.match(build, /\$src = "\$neutralDrive\\src\\Chronicle"/);
  // A stopped run's junction is replaced; a real folder in its place is never deleted.
  assert.match(build, /ReparsePoint\)\) \{\s*throw/);
  assert.match(build, /\[System\.IO\.Directory\]::Delete\(\$link, \$false\)/, 'a link is removed, never what it points at');
  const tryAt = build.indexOf('\ntry {');
  const finallyAt = build.indexOf('} finally {');
  assert.ok(tryAt > build.indexOf('$src = "$neutralDrive') && finallyAt > build.indexOf('node --test $uiTests'));
  assert.match(build.slice(finallyAt), /Remove-NeutralRoot/);
  assert.match(build, /& subst\.exe \$script:neutralDrive \/D/);
  assert.match(build, /\$archives = if \(\$Neutral\) \{ 'archives-neutral' \} else \{ 'archives' \}/, 'its own vcpkg cache');
  assert.match(build, /VCPKG_DEFAULT_BINARY_CACHE = "\$work\\vcpkg\\\$archives"/);
  assert.match(build, /-DCHRONICLE_NEUTRAL_PATHS=\$\(if \(\$Neutral\) \{ 'ON' \} else \{ 'OFF' \}\)/);
  const cmake = read('CMakeLists.txt');
  assert.match(cmake, /if\(CHRONICLE_NEUTRAL_PATHS\)\s*target_link_options\(\$\{PROJECT_NAME\} PRIVATE "\/PDBALTPATH:%_PDB%"\)/);
});
