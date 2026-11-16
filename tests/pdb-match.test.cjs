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

// The zip ships Chronicle.pdb beside Chronicle.dll, so crash logs can name
// Chronicle's functions. A crash logger loads a PDB only when its GUID and age
// are the ones the DLL's CodeView record names, so a PDB from any other link
// is worse than none: package.ps1 checks them with tools/pdb-match.py and
// stops when they differ or the PDB is missing. The release build writes the
// PDB with the release optimisations kept.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const TOOL = path.join(ROOT, 'tools', 'pdb-match.py');
const PACKAGE = fs.readFileSync(path.join(ROOT, 'package.ps1'), 'utf8');
const windows = {skip: process.platform !== 'win32' && 'needs Windows PowerShell'};

// A 64-bit DLL-shaped file whose debug directory holds one record: CodeView
// (RSDS: GUID, age, PDB name) or, with guid null, a POGO record instead.
function dll(guid, age) {
  const file = Buffer.alloc(0x400);
  file.write('MZ', 0, 'latin1');
  const pe = 0x40, optional = pe + 24, table = optional + 240;
  file.writeUInt32LE(pe, 0x3c);
  file.write('PE\0\0', pe, 'latin1');
  file.writeUInt16LE(0x8664, pe + 4);
  file.writeUInt16LE(1, pe + 6);  // one section
  file.writeUInt16LE(240, pe + 20);  // optional header size
  file.writeUInt16LE(0x20b, optional);  // PE32+
  file.writeUInt32LE(0x1000, optional + 112 + 6 * 8);  // debug directory RVA
  file.writeUInt32LE(28, optional + 112 + 6 * 8 + 4);
  file.write('.rdata', table, 'latin1');
  file.writeUInt32LE(0x200, table + 8);  // virtual size
  file.writeUInt32LE(0x1000, table + 12);  // virtual address
  file.writeUInt32LE(0x200, table + 16);  // raw size
  file.writeUInt32LE(0x200, table + 20);  // raw offset
  const entry = 0x200, record = entry + 28;
  file.writeUInt32LE(guid ? 2 : 13, entry + 12);
  file.writeUInt32LE(28, entry + 16);
  file.writeUInt32LE(0x1000 + 28, entry + 20);
  file.writeUInt32LE(record, entry + 24);
  if (guid) {
    file.write('RSDS', record, 'latin1');
    guid.copy(file, record + 4);
    file.writeUInt32LE(age, record + 20);
    file.write('Chronicle.pdb\0', record + 24, 'latin1');
  }
  return file;
}

// An MSF 7.0 PDB of 512-byte blocks: the superblock, the block map (block 3),
// the stream directory (block 4), the info stream 1 (GUID, block 5) and the DBI
// stream 3 (age, block 6).
function pdb(guid, age) {
  const block = 512;
  const file = Buffer.alloc(7 * block);
  file.write('Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0', 0, 'latin1');
  [block, 1, 7, 4 + 4 * 4 + 2 * 4, 0, 3].forEach((value, i) => file.writeUInt32LE(value, 32 + i * 4));
  file.writeUInt32LE(4, 3 * block);
  [4, 0, 28, 0, 64, 5, 6].forEach((value, i) => file.writeUInt32LE(value, 4 * block + i * 4));
  file.writeUInt32LE(20000404, 5 * block);
  file.writeUInt32LE(0x5f000000, 5 * block + 4);
  file.writeUInt32LE(age, 5 * block + 8);
  guid.copy(file, 5 * block + 12);
  file.writeInt32LE(-1, 6 * block);
  file.writeUInt32LE(19990903, 6 * block + 4);
  file.writeUInt32LE(age, 6 * block + 8);
  return file;
}

function folder(t) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-pdb-'));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  return dir;
}

function match(t, dllBytes, pdbBytes) {
  const dir = folder(t);
  fs.writeFileSync(path.join(dir, 'Chronicle.dll'), dllBytes);
  if (pdbBytes) fs.writeFileSync(path.join(dir, 'Chronicle.pdb'), pdbBytes);
  const run = spawnSync('python', ['-I', TOOL, path.join(dir, 'Chronicle.dll'), path.join(dir, 'Chronicle.pdb')], {encoding: 'utf8'});
  assert.equal(run.error, undefined, 'Python must be on PATH');
  return {status: run.status, out: run.stdout};
}

const guid = crypto.randomBytes(16);
const otherGuid = Buffer.from(guid.map(byte => byte ^ 0xff));

test('a PDB from the same link matches', t => {
  const result = match(t, dll(guid, 3), pdb(guid, 3));
  assert.equal(result.status, 0, result.out);
  assert.match(result.out, /the PDB belongs to the DLL/);
});

test('a PDB with another GUID or another age does not', t => {
  for (const [label, file] of [['GUID', pdb(otherGuid, 3)], ['age', pdb(guid, 4)]]) {
    const result = match(t, dll(guid, 3), file);
    assert.equal(result.status, 1, `${label}: ${result.out}`);
    assert.match(result.out, /the PDB is from another link than the DLL/);
  }
});

test('a missing PDB, a DLL without CodeView or a file that is no PDB is never a pass', t => {
  assert.equal(match(t, dll(guid, 3), null).status, 2);
  assert.match(match(t, dll(null), pdb(guid, 3)).out, /no CodeView record/);
  assert.equal(match(t, dll(null), pdb(guid, 3)).status, 2);
  assert.equal(match(t, dll(guid, 3), dll(guid, 3)).status, 2);
});

// The PDB lines of package.ps1 itself, from the missing-PDB check to the stop
// on a mismatch, run against a build folder and a staging folder of our own.
function stagePdb(t, pdbBytes) {
  const start = PACKAGE.indexOf('if (!(Test-Path -LiteralPath "$buildDir\\Chronicle.pdb"))');
  const stop = PACKAGE.indexOf("throw 'Chronicle.pdb is not from the link that made Chronicle.dll; package not created' }");
  assert.ok(start >= 0 && stop > start, 'package.ps1 has its PDB step');
  const step = PACKAGE.slice(start, PACKAGE.indexOf('}', stop) + 1);
  const dir = folder(t);
  const build = path.join(dir, 'build'), stage = path.join(dir, 'stage');
  fs.mkdirSync(build);
  fs.mkdirSync(path.join(stage, 'SKSE', 'Plugins'), {recursive: true});
  fs.writeFileSync(path.join(build, 'Chronicle.dll'), dll(guid, 3));
  fs.copyFileSync(path.join(build, 'Chronicle.dll'), path.join(stage, 'SKSE', 'Plugins', 'Chronicle.dll'));
  if (pdbBytes) fs.writeFileSync(path.join(build, 'Chronicle.pdb'), pdbBytes);
  fs.writeFileSync(path.join(dir, 'step.ps1'), [
    "$ErrorActionPreference = 'Stop'",
    `$projectRoot = '${ROOT}'`, `$buildDir = '${build}'`, `$stage = '${stage}'`,
    'try {', step, "'staged' } catch { $_.Exception.Message; exit 1 }",
  ].join('\r\n'));
  const run = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-File', path.join(dir, 'step.ps1')], {encoding: 'utf8'});
  return {status: run.status, out: run.stdout, staged: fs.existsSync(path.join(stage, 'SKSE', 'Plugins', 'Chronicle.pdb'))};
}

test('a package without a matching PDB fails', windows, t => {
  const missing = stagePdb(t, null);
  assert.equal(missing.status, 1, missing.out);
  assert.match(missing.out, /Chronicle\.pdb is missing from .*; package not created/);

  for (const other of [pdb(otherGuid, 3), pdb(guid, 2)]) {
    const wrong = stagePdb(t, other);
    assert.equal(wrong.status, 1, wrong.out);
    assert.match(wrong.out, /Chronicle\.pdb is not from the link that made Chronicle\.dll; package not created/);
  }

  const right = stagePdb(t, pdb(guid, 3));
  assert.equal(right.status, 0, right.out);
  assert.match(right.out, /staged/);
  assert.ok(right.staged);
});

test('the PDB is staged beside the DLL, checked before the archive, and scanned in it', () => {
  const dll = PACKAGE.indexOf('Copy-Item -LiteralPath "$buildDir\\Chronicle.dll" -Destination "$stage\\SKSE\\Plugins\\Chronicle.dll"');
  const copy = PACKAGE.indexOf('Copy-Item -LiteralPath "$buildDir\\Chronicle.pdb" -Destination "$stage\\SKSE\\Plugins\\Chronicle.pdb"');
  const check = PACKAGE.indexOf('& python -I "$projectRoot\\tools\\pdb-match.py" "$stage\\SKSE\\Plugins\\Chronicle.dll" "$stage\\SKSE\\Plugins\\Chronicle.pdb"');
  const zip = PACKAGE.indexOf('ZipFile]::Open(');
  const scan = PACKAGE.indexOf('zip-user-scan.py');
  assert.ok(dll >= 0 && copy > dll && check > copy && check < zip, 'staged after the DLL and checked before the archive');
  // Every staged file goes into the archive, and the scan reads every entry of it.
  assert.match(PACKAGE, /foreach \(\$file in Get-ChildItem -LiteralPath \$stage -File -Recurse\)[\s\S]{0,200}CreateEntryFromFile/);
  assert.ok(scan > zip);
});

test('the release build writes the PDB and keeps its optimisations', () => {
  const cmake = fs.readFileSync(path.join(ROOT, 'CMakeLists.txt'), 'utf8');
  assert.ok(cmake.includes('target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/Z7>)'));
  assert.ok(cmake.includes('target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)'));
});
