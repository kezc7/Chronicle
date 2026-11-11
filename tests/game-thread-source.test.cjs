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

// The game-thread rule, checked in the source: neither of SKSE's task queues
// can be trusted to reach the game thread, so game work is posted only through
// GameThread::Post, which hands it to the suite's game-thread pump
// (DovahGameThread.h), and every game event sink checks the thread it runs on.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const SRC = path.join(__dirname, '../src');
// Other mods' headers, copied in unchanged. DovahGameThread.h is the pump
// itself; it falls back to SKSE's UI tasks only when no hook can be installed.
const VENDORED = new Set(['MagelightUI_API.h', 'PrismaUI_API.h', 'DovahHub_API.h', 'DovahMenu_API.h', 'DovahGameThread.h']);

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

test('no SKSE task queue for game work', () => {
  const found = sources().filter(file => /\b(AddTask|AddUITask)\s*\(/.test(file.code)).map(file => file.name);
  assert.deepEqual(found, [], 'SKSE\'s task queues can run off the game thread; use GameThread::Post');
});

test('GameThread::Post hands work to the pump, installed at kPostLoad', () => {
  const files = sources();
  const helper = files.find(file => file.name === 'GameThread.h').code;
  assert.match(helper, /#include "DovahGameThread\.h"/);
  assert.match(helper, /bool Post\(F task, [^{]*\)\s*\{\s*DovahGameThread::Post\(/);
  assert.match(helper, /if \(DovahGameThread::Pumping\(\)\) return DovahGameThread::Id\(\);/);
  const plugin = files.find(file => file.name === 'Plugin.cpp').code;
  assert.match(plugin, /case SKSE::MessagingInterface::kPostLoad:\s*DovahGameThread::Install\("Chronicle"\);/);
  assert.doesNotMatch(plugin.slice(0, plugin.indexOf('RegisterListener')), /DovahGameThread::Install/,
    'Install waits for kPostLoad: DovahHub loads after Chronicle');
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
test('the self-test runs only when started, and only on the pump', () => {
  const files = sources();
  const starters = files.filter(file => /ChronicleSelfTestRun::Start(FromChat|FromHub)\s*\(/.test(file.code)).map(file => file.name).sort();
  // /selftest typed in the chat box, and DovahHub's Diagnostics > Run self-test: never a load or a game event.
  assert.deepEqual(starters, ['Api.cpp', 'DovahHubModule.cpp']);
  const selfTest = files.find(file => file.name === 'SelfTest.cpp').code;
  assert.match(selfTest, /GameThread::Post\(\[\] \{ Tick\(\); \}\)/);
  assert.doesNotMatch(selfTest, /std::thread|CreateThread|AddTask|AddUITask/);
  // DovahHub's runner is used only behind HasSelfTest.
  const hub = files.find(file => file.name === 'DovahHubModule.cpp').code;
  for (const call of ['RegisterSelfTest', 'ReportSelfTestStep(g_module', 'FinishSelfTest(g_module']) {
    const at = hub.indexOf('g_hub->' + call);
    assert.ok(at > 0, call);
    assert.match(hub.slice(Math.max(0, at - 200), at), /HasSelfTest\(g_hub\)/, call + ' is behind HasSelfTest');
  }
});
