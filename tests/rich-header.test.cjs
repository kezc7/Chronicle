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

// Every object in the DLL comes from Visual Studio 2022's tools. The linker
// version alone can't show that: a dependency vcpkg built with a newer
// compiler links into a DLL whose linker is still 2022's. The Rich header the
// linker writes names the build of every tool that made an object, and
// package.ps1 stops before zipping when one is 35700 or higher (VS 2026 is
// 357xx and 36xxx). tools/rich-header.py is DovahCamp ebd7d9b's, unchanged.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const TOOL = path.join(ROOT, 'tools', 'rich-header.py');
const TOOL_SHA256 = '6c6ac8ba528b759644f978a0866203fef983ec87486cda7fcd8a94401249642e';  // DovahCamp ebd7d9b, LF
const LIMIT = '35700';

// A DLL-shaped file: a DOS header, a Rich header of the given [product, build,
// count] entries (none: no header at all) and the PE signature after it.
function dll(entries) {
  const key = 0x1d2c3b4a;
  const words = entries ? [0x536e6144, 0, 0, 0, ...entries.flatMap(([product, build, count]) => [(product << 16 | build) >>> 0, count])] : [];
  const rich = Buffer.alloc(entries ? words.length * 4 + 8 : 0);
  words.forEach((word, i) => rich.writeUInt32LE((word ^ key) >>> 0, i * 4));
  if (entries) {
    rich.write('Rich', words.length * 4, 'latin1');
    rich.writeUInt32LE(key, words.length * 4 + 4);
  }
  const pe = 0x80 + rich.length;
  const file = Buffer.alloc(pe + 0x18);
  file.write('MZ', 0, 'latin1');
  file.writeUInt32LE(pe, 0x3c);
  rich.copy(file, 0x80);
  file.write('PE\0\0', pe, 'latin1');
  return file;
}

function check(t, entries) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-rich-'));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  const file = path.join(dir, 'Chronicle.dll');
  fs.writeFileSync(file, dll(entries));
  const run = spawnSync('python', ['-I', TOOL, file, '--below', LIMIT], {encoding: 'utf8'});
  assert.equal(run.error, undefined, 'Python must be on PATH');
  return {status: run.status, out: run.stdout};
}

test('rich-header.py is the copy from DovahCamp ebd7d9b', () => {
  const lf = fs.readFileSync(TOOL, 'utf8').replace(/\r\n/g, '\n');
  assert.equal(crypto.createHash('sha256').update(lf, 'utf8').digest('hex'), TOOL_SHA256,
    'tools/rich-header.py drifted: copy it again from DovahCamp with git show');
});

test('an object from build 36263 fails the check', t => {
  const result = check(t, [[0x0102, 35230, 1], [0x0105, 35230, 40], [0x0104, 36263, 4], [0x0093, 30729, 6]]);
  assert.equal(result.status, 1, result.out);
  assert.match(result.out, /product 0x0104 build 36263 x4 {2}<- too new/);
  assert.match(result.out, /1 of 4 entries have a build number of 35700 or higher/);
});

test('the first VS 2026 build fails too', t => {
  assert.equal(check(t, [[0x0105, 35700, 1]]).status, 1);
});

test("Visual Studio 2022's builds pass", t => {
  const result = check(t, [[0x0102, 35230, 1], [0x0105, 35207, 40], [0x0093, 33145, 2], [0x0093, 30729, 6]]);
  assert.equal(result.status, 0, result.out);
  assert.match(result.out, /4 entries, highest build 35230, all below 35700/);
});

test('no Rich header is a failure, not a pass', t => {
  const result = check(t, null);
  assert.equal(result.status, 2, result.out);
  assert.match(result.out, /none found/);
});

test('package.ps1 checks the DLL it packs, after the build and before the archive', () => {
  const script = fs.readFileSync(path.join(ROOT, 'package.ps1'), 'utf8');
  const run = script.indexOf('& python -I "$projectRoot\\tools\\rich-header.py" "$buildDir\\Chronicle.dll" --below 35700');
  assert.ok(run >= 0, 'package.ps1 runs the check on $buildDir\\Chronicle.dll with --below 35700');
  assert.ok(run > script.indexOf('build.ps1" -Config'), 'after the build');
  assert.ok(run < script.indexOf('Copy-Item -LiteralPath "$buildDir\\Chronicle.dll"'), 'before the DLL is staged');
  assert.ok(run < script.indexOf('ZipFile]::Open('), 'before the archive is written');
  assert.match(script.slice(run, run + 300), /\r?\nif \(\$LASTEXITCODE -ne 0\) \{ throw /, 'any exit but 0 stops the package');
});
