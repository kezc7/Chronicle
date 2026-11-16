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

// THIRD_PARTY_NOTICES.md names the CommonLibSSE-NG commit both builds link and
// its licence text as it stands at that commit. vcpkg builds 3.6.0, commit
// d51fa09, from the colorglass registry baseline pinned in
// vcpkg-configuration.json; that commit is MIT (upstream became GPL-3.0-or-later
// with exceptions at 12f8518). A new baseline can bring a newer commit under
// other terms, so the pin is checked here too: change it, and this test sends
// you back to the notice.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const COMMIT = 'd51fa0938bf53f829ff83a144079136f0d4a2f18';
const BASELINE = 'eb2f2bd143b8f7c52b57ef5a822163b56f7be7d6';
// CommonLibSSE-NG's LICENSE at d51fa09, LF line endings (the same text vcpkg
// installs as share/commonlibsse-ng/copyright).
const LICENSE_SHA256 = 'f8f983d0d1e2bb361f117487465f21f5f4047acca3610f7aba3482d524c83ce4';
const read = name => fs.readFileSync(path.join(ROOT, name), 'utf8').replace(/\r\n/g, '\n');

function commonLibSection() {
  const notices = read('THIRD_PARTY_NOTICES.md');
  const start = notices.indexOf('\n## CommonLibSSE-NG\n');
  assert.ok(start >= 0, 'THIRD_PARTY_NOTICES.md has a CommonLibSSE-NG section');
  const end = notices.indexOf('\n## ', start + 1);
  return notices.slice(start, end < 0 ? undefined : end);
}

test('the notice names the CommonLibSSE-NG commit and its MIT text, word for word', () => {
  const section = commonLibSection();
  assert.ok(section.includes(COMMIT), 'names the commit');
  const block = /```text\n([\s\S]*?)\n```/.exec(section);
  assert.ok(block, 'the licence text is quoted');
  assert.equal(crypto.createHash('sha256').update(block[1] + '\n', 'utf8').digest('hex'), LICENSE_SHA256,
    'the quoted licence differs from CommonLibSSE-NG\'s LICENSE at that commit');
  assert.doesNotMatch(section.replace(/relicensing to GPL-3\.0-or-later with its modding and linking\s+exceptions/, ''),
    /GPL|Modding Exception|Linking Exception/, 'an MIT copy is never described as GPL or as granting exceptions');
});

test('the vcpkg baseline the notice was checked against is still the one pinned', () => {
  const config = JSON.parse(read('vcpkg-configuration.json'));
  const colorglass = (config.registries || []).find(registry => /colorglass/.test(registry.repository || ''));
  assert.ok(colorglass && colorglass.packages.includes('commonlibsse-ng'), 'commonlibsse-ng comes from the colorglass registry');
  assert.equal(colorglass.baseline, BASELINE,
    'the baseline moved: find the CommonLibSSE-NG commit it builds, check its licence, and update THIRD_PARTY_NOTICES.md and this test');
  assert.ok(commonLibSection().includes(BASELINE));
});
