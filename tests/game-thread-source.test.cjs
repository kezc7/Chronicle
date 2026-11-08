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

// The game-thread rule, checked in the source: SKSE's AddTask queue can run
// on other threads, so game work is posted only through GameThread::Post
// (AddUITask), and every game event sink checks the thread it runs on.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const SRC = path.join(__dirname, '../src');
// Other mods' headers, copied in unchanged.
const VENDORED = new Set(['MagelightUI_API.h', 'PrismaUI_API.h', 'DovahHub_API.h', 'DovahMenu_API.h']);

function stripComments(text) {
  return text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '');
}

function sources() {
  const files = [];
  const walk = dir => fs.readdirSync(dir, {withFileTypes: true}).forEach(entry => {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full);
    else if (/\.(cpp|h)$/.test(entry.name) && !VENDORED.has(entry.name)) files.push(full);
  });
  walk(SRC);
  return files.map(file => ({name: path.relative(SRC, file).replace(/\\/g, '/'), code: stripComments(fs.readFileSync(file, 'utf8'))}));
}

test('no game work is posted with AddTask', () => {
  const found = sources().filter(file => /\bAddTask\s*\(/.test(file.code)).map(file => file.name);
  assert.deepEqual(found, [], 'AddTask can run off the game thread; use GameThread::Post');
});

test('AddUITask is called only by GameThread::Post', () => {
  const found = sources().filter(file => /\bAddUITask\s*\(/.test(file.code)).map(file => file.name);
  assert.deepEqual(found, ['GameThread.h']);
});

// Topic-info events may arrive on any thread: the sink only copies two form
// IDs and posts them to the game thread.
const ANY_THREAD = new Set(['TopicInfoEventSink']);

test('every game event sink checks the thread it runs on', () => {
  const missing = [];
  for (const file of sources()) {
    const re = /BSEventNotifyControl\s+(?:(\w+)::)?ProcessEvent\s*\([^)]*\)\s*(?:override\s*)?\{/g;
    let match;
    while ((match = re.exec(file.code))) {
      const body = file.code.slice(match.index + match[0].length, match.index + match[0].length + 200);
      if (ANY_THREAD.has(match[1])) continue;
      if (!/^\s*GameThread::Check\(\);/.test(body)) missing.push(file.name + ' ' + (match[1] || 'ProcessEvent'));
    }
  }
  assert.deepEqual(missing, []);
});

test('the poller and HUD read are posted one at a time', () => {
  const events = sources().find(file => file.name === 'Events.cpp').code;
  assert.match(events, /if \(!m_pending\.exchange\(true\)\) \{\s*const bool posted = GameThread::Post/);
  assert.match(events, /if \(m_hudPending\.exchange\(true\)\) \{\s*return;\s*\}/);
});
