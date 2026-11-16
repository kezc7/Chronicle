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

// DovahHub's shared headers are copied in unchanged, each from one DovahHub
// commit (git show <commit>:src/api/<file>, never a working tree), and pinned
// here by the SHA-256 of that commit's file with LF line endings. The working
// tree may hold CRLF (core.autocrlf), so the copy is hashed as LF.
//
// Each header has its own pin. The pin is the commit the copy came from, never
// DovahHub's latest, so this test changes only when Chronicle takes a new copy.
// To take one: copy the file from the new commit, then change its row here.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const SRC = path.join(__dirname, '../src');
const HEADERS = [
  // DHUB-123: without a frame hook the work waits for a verified game thread,
  // never runs on another (review FR-PUMP-01).
  {file: 'DovahGameThread.h', commit: '025314d', sha256: '46f138d65685ca4a6ea261d4d7317c0b1418a2a89a16b6259f300b48be6c342d'},
  // Has every member Chronicle and DovahGameThread.h use (the 0.97.4 pump:
  // RunOnGameThread, IsGameThread, GameThreadId, HasPump). The header only
  // ever appends, so this older copy keeps working with newer DovahHubs.
  {file: 'DovahHub_API.h', commit: '429628b', sha256: '05d3191953f413b86128c3be12ade1320a71b19acf293daa5baa04ac072c8900'},
];

const lfSha256 = file => crypto.createHash('sha256')
  .update(fs.readFileSync(file, 'utf8').replace(/\r\n/g, '\n'), 'utf8')
  .digest('hex');

for (const header of HEADERS) {
  test(`${header.file} is DovahHub ${header.commit}'s copy`, () => {
    assert.equal(lfSha256(path.join(SRC, header.file)), header.sha256,
      `${header.file} differs from DovahHub ${header.commit}: copy it again with git show, or pin the commit it came from`);
  });
}

test('the thread header is the one without the off-thread fallback', () => {
  // Older copies (317a522, 911b522, d8e1cc3) ran a job wherever it was after
  // eight tries through SKSE's UI tasks.
  const code = fs.readFileSync(path.join(SRC, 'DovahGameThread.h'), 'utf8');
  assert.match(code, /Game work never runs\s+(\/\/\s+)?on another thread, however long it waits\./);
  assert.doesNotMatch(code, /the work goes to SKSE's UI tasks instead/);
});
