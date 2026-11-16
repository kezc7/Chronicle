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

// Only Chronicle's own files ship. package.ps1 checks every entry name of the
// finished zip against tools/zip-allowlist.txt and removes the zip when one
// isn't on it.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const TOOL = path.join(ROOT, 'tools', 'zip-allowlist.py');

const VIEW = ['bridge.js', 'config.json', 'controls.js', 'i18n.js', 'index.html', 'pad.js', 'script.js', 'styles.css',
  'theme/dovah-theme.css', 'theme/fonts/BarlowCondensed-Medium.ttf', 'theme/fonts/BarlowCondensed-Regular.ttf',
  'theme/fonts/BarlowCondensed-SemiBold.ttf', 'theme/fonts/OFL-BarlowCondensed.txt'];
const COMMON = ['LICENSE', 'README.md', 'THIRD_PARTY_NOTICES.md', 'SKSE/Plugins/Chronicle.dll', 'SKSE/Plugins/Chronicle.pdb',
  ...['history.css', 'history.js', 'index.html'].map(file => 'PrismaUI/views/DovahHub/modules/chronicle/' + file)];
const PACKAGE = {
  PrismaUI: [...COMMON, ...VIEW.map(file => 'PrismaUI/views/Chronicle/' + file)],
  Magelight: [...COMMON, ...VIEW.map(file => 'Magelight/Chronicle/views/' + file)],
};

// Writes a zip with these entry names (Python's zipfile keeps them as given) and checks it.
function check(t, names, host) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-allow-'));
  t.after(() => fs.rmSync(dir, {recursive: true, force: true}));
  const zip = path.join(dir, 'test.zip');
  const made = spawnSync('python', ['-I', '-c',
    'import json,sys,zipfile\nwith zipfile.ZipFile(sys.argv[1],"w") as z:\n    for n in json.loads(sys.argv[2]): z.writestr(n, b"x")',
    zip, JSON.stringify(names)], {encoding: 'utf8'});
  assert.equal(made.status, 0, made.stderr);
  const run = spawnSync('python', ['-I', TOOL, zip, '--host', host], {encoding: 'utf8'});
  return {status: run.status, out: run.stdout};
}

test("each host's package is on its allowlist", t => {
  for (const host of ['PrismaUI', 'Magelight']) {
    const result = check(t, PACKAGE[host], host);
    assert.equal(result.status, 0, `${host}: ${result.out}`);
  }
});

test('a zip with one stray entry is refused, naming it', t => {
  const result = check(t, [...PACKAGE.PrismaUI, 'docs/CONCERNS.md'], 'PrismaUI');
  assert.equal(result.status, 1, result.out);
  assert.match(result.out, /not on the allowlist: docs\/CONCERNS\.md/);
  assert.match(result.out, new RegExp(`1 of ${PACKAGE.PrismaUI.length + 1} entries`));
});

test('other strays are refused too', t => {
  for (const [host, stray] of [
    ['PrismaUI', 'Magelight/Chronicle/views/index.html'],
    ['Magelight', 'PrismaUI/views/Chronicle/index.html'],
    ['PrismaUI', 'PrismaUI/views/Chronicle/notes.md'],
    ['PrismaUI', 'PrismaUI/views/Chronicle/history.test.cjs'],
    ['PrismaUI', 'SKSE/Plugins/Chronicle.ini'],
    ['PrismaUI', 'PrismaUI/views/Chronicle/../../../x.js'],
  ]) {
    const result = check(t, [...PACKAGE[host], stray], host);
    assert.equal(result.status, 1, `${stray}: ${result.out}`);
  }
});

test('package.ps1 checks the finished archive and removes it on a stray entry', () => {
  const script = fs.readFileSync(path.join(ROOT, 'package.ps1'), 'utf8');
  const run = script.indexOf('& python -I "$projectRoot\\tools\\zip-allowlist.py" $zip --host $UiHost');
  assert.ok(run > script.indexOf('ZipFile]::Open('), 'after the archive is written');
  assert.match(script.slice(run, run + 300), /\$LASTEXITCODE -ne 0[\s\S]{0,60}Remove-Item -LiteralPath \$zip -Force[\s\S]{0,40}throw/);
});
