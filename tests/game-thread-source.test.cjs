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

// Events reach Chronicle's sinks on whatever thread fires them: in one load
// order input and menu events came on Magelight's present thread, hits and
// deaths on workers, mod events on Papyrus's threads (CHRON-20). So a sink does
// no game work where it runs: it copies what it needs and posts the work to the
// pump, whose handler checks the thread. Three sinks only read their event and
// atomics, and stay where they are.
const ANY_THREAD = new Set(['ChronicleInputSink', 'TopicInfoEventSink', 'LevelUpEventSink']);
// Game calls a sink may not make where it runs (they go in the posted work).
const GAME_CALLS = /PlayerDisplayName\(|LookupByID|->As<|GetDisplayFullName|IsMenuOpen|GetMenu\(|OnMenuOpenClose|SubtitlePoller|IsDead\(|GetName\(|RE::UI::|IsPlayerTeammate|GetCommandingActor|m_state\./;

// The text from an opening brace or parenthesis at `open` to its partner.
function block(code, open) {
  const pairs = {'{': '}', '(': ')'};
  const close = pairs[code[open]];
  let depth = 0;
  for (let i = open; i < code.length; ++i) {
    if (code[i] === code[open]) ++depth;
    else if (code[i] === close && --depth === 0) return code.slice(open, i + 1);
  }
  return code.slice(open);
}

// The code with every call of `name(...)` taken out: what runs where it is written.
function without(code, name) {
  let out = '';
  let at = 0;
  for (let i = code.indexOf(name + '('); i >= 0; i = code.indexOf(name + '(', at)) {
    out += code.slice(at, i);
    at = i + block(code, i + name.length).length + name.length;
  }
  return out + code.slice(at);
}

// Every problem with the event sinks in these sources, as "file Sink: what".
function sinkProblems(files) {
  const problems = [];
  for (const file of files) {
    const re = /BSEventNotifyControl\s+(?:(\w+)::)?ProcessEvent\s*\([^)]*\)\s*(?:override\s*)?\{/g;
    let match;
    while ((match = re.exec(file.code))) {
      const classes = [...file.code.slice(0, match.index).matchAll(/class\s+(\w+)/g)];
      const sink = match[1] || (classes.length ? classes[classes.length - 1][1] : 'ProcessEvent');
      const body = block(file.code, match.index + match[0].length - 1);
      const here = without(body, 'GameThread::Post');
      if (/GameThread::Check\(\)/.test(here)) problems.push(`${file.name} ${sink}: checks the thread where the event arrives; check in the posted handler`);
      if (!ANY_THREAD.has(sink) && !/GameThread::Post\(/.test(body)) problems.push(`${file.name} ${sink}: doesn't post its work to the pump`);
      const call = here.match(GAME_CALLS);
      if (call) problems.push(`${file.name} ${sink}: ${call[0]} runs where the event arrives`);
    }
  }
  return problems;
}

test('no event sink does game work on the thread its event arrives on', () => {
  assert.deepEqual(sinkProblems(sources()), []);
});

test('a sink doing game work where it runs is caught', () => {
  const planted = code => sinkProblems([{name: 'Planted.cpp', code}]);
  // The 1.4.17 menu sink: the work and the check where the event arrives.
  assert.deepEqual(planted(`RE::BSEventNotifyControl MenuSink::ProcessEvent(const RE::MenuOpenCloseEvent* e, X*) {
    GameThread::Check(); Chronicle::GetSingleton()->OnMenuOpenClose(e->menuName.c_str(), e->opening); return kContinue; }`), [
    "Planted.cpp MenuSink: checks the thread where the event arrives; check in the posted handler",
    "Planted.cpp MenuSink: doesn't post its work to the pump",
    'Planted.cpp MenuSink: OnMenuOpenClose runs where the event arrives',
  ]);
  // Posting part of it isn't enough: the player is still read where the event arrives.
  assert.deepEqual(planted(`RE::BSEventNotifyControl ModSink::ProcessEvent(const E* e, X*) {
    auto name = PlayerDisplayName(); GameThread::Post([name] { Use(name); }); return kContinue; }`),
  ['Planted.cpp ModSink: PlayerDisplayName( runs where the event arrives']);
  // Game work inside the posted lambda is where it belongs.
  assert.deepEqual(planted(`RE::BSEventNotifyControl HitSink::ProcessEvent(const E* e, X*) {
    GameThread::Post([t = e->target] { GameThread::Check(); auto* a = t->As<RE::Actor>(); }); return kContinue; }`), []);
});

test('the work sinks post checks the thread when it runs', () => {
  const code = Object.fromEntries(sources().map(file => [file.name, file.code]));
  const starts = (file, signature) => {
    const at = code[file].indexOf(signature);
    assert.ok(at >= 0, signature);
    return /^\{\s*GameThread::Check\(\);/.test(block(code[file], code[file].indexOf('{', at)));
  };
  for (const [file, signature] of [['Events.cpp', 'void HandleModEvent('], ['Events.cpp', 'void HandleHit('],
    ['Events.cpp', 'void HandleDeath('], ['SkyrimNet.cpp', 'void Look('], ['Chronicle.cpp', 'void Chronicle::OnMenuOpenClose(']]) {
    assert.ok(starts(file, signature), `${signature} starts with GameThread::Check()`);
  }
});

test('the input path reads only atomics; its work and page updates go to the pump', () => {
  const chronicle = sources().find(file => file.name === 'Chronicle.cpp').code;
  for (const signature of ['bool Chronicle::OnHotkey(', 'bool Chronicle::OnPadButton(']) {
    const body = block(chronicle, chronicle.indexOf('{', chronicle.indexOf(signature)));
    const here = without(body, 'MainTask');
    assert.doesNotMatch(here, /m_state\.|RE::UI|IsMenuOpen|InvokeUI\(|RequestUnfocus\(|RequestFocus\(/, signature);
  }
  // The game thread publishes what the input path may know.
  assert.match(chronicle, /void Chronicle::PublishInputGate\(\)\s*\{\s*GameThread::Check\(\);/);
  // A page update asked for on another thread is posted, not run there.
  const invoke = block(chronicle, chronicle.indexOf('{', chronicle.indexOf('void Chronicle::InvokeUI(')));
  assert.match(invoke, /if \(!GameThread::IsCurrent\(\)\) \{[\s\S]*GameThread::Post\([\s\S]*return;\s*\}/);
  // The host's focus listeners hand their work to the pump like every other listener.
  assert.match(chronicle, /RegisterListener\("focusAcquired", \[\]\(const char\*\) \{ MainTask\(/);
  assert.match(chronicle, /RegisterListener\("focusReleased", \[\]\(const char\*\) \{ MainTask\(/);
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
