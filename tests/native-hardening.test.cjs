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

// Native fixes the test programs can't run, because they need the game's
// library (CommonLibSSE-NG) or a host: read from source, so a later change
// can't drop them quietly. The behaviour tests sit beside the code they cover
// where the harness allows (RuntimeTests, MessageBuilderTests, the page tests).

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const src = name => fs.readFileSync(path.join(__dirname, '../src', name), 'utf8');
const body = (text, signature) => {
  const at = text.indexOf(signature);
  assert.ok(at >= 0, `${signature} exists`);
  let depth = 0;
  for (let i = text.indexOf('{', at); i < text.length; ++i) {
    if (text[i] === '{') ++depth;
    else if (text[i] === '}' && --depth === 0) return text.slice(at, i + 1);
  }
  throw new Error(`${signature} has no end`);
};

test('CHRON-35: a history read returns a save the writer is still writing, not the file it replaces', () => {
  const writer = src('HistoryWriter.cpp');
  assert.match(writer, /std::map<std::filesystem::path, std::shared_ptr<const Job>> g_inFlight;/);
  const load = body(writer, 'std::vector<ChronicleHistory::Line> Load(const std::filesystem::path& path)');
  const queued = load.indexOf('g_jobs.find(path)');
  const writing = load.indexOf('g_inFlight.find(path)');
  const disk = load.indexOf('std::filesystem::exists(path)');
  assert.ok(queued > 0 && queued < writing && writing < disk, 'queued, then being written, then the disk');
  const run = body(writer, 'void Run()');
  assert.match(run, /g_inFlight\[path\] = taken;/, 'the writer keeps the job while it writes');
  assert.match(run, /it->second == job\) g_inFlight\.erase\(it\);/, 'and lets go of it after');
});

test('CHRON-36: the subtitle poller copies the game\'s list under its lock and works on the copy', () => {
  const events = src('Events.cpp');
  const copy = events.indexOf('RE::BSSpinLockGuard locker(mgr->lock);');
  assert.ok(copy > 0, 'the manager\'s own lock');
  const block = events.slice(copy, events.indexOf('m_activeSubtitles = std::move(current);', copy));
  assert.doesNotMatch(block.slice(block.indexOf('}')), /mgr->subtitles/, 'nothing reads the list after the lock');
  assert.match(block, /for \(const auto& \[speaker, text\] : subtitles\)/);
});

test('CHRON-36: an interaction ending forgets the controller combo\'s held buttons', () => {
  const close = body(src('Chronicle.cpp'), 'void Chronicle::ForceCloseInput()');
  assert.match(close, /std::lock_guard lock\(m_padMutex\);\s*m_padGate\.Reset\(\);/);
  assert.match(close, /SetEscapeLayer\(false\);/, 'and the page\'s list (CHRON-32)');
});

test('CHRON-37: Magelight\'s events and the handover answer are handled on the game-thread pump', () => {
  const host = src('UiHostMagelight.cpp');
  const onEvent = body(host, 'static void OnEvent(const EventData* ev, void* user)');
  assert.match(onEvent, /GameThread::Post\(\[self, type = ev->type, view = ev->view, detail = std::string\(/);
  assert.doesNotMatch(onEvent, /switch|m_panelActive|ReturnToHud/, 'no state changes on the callback thread');
  const onExported = body(host, 'static void OnExported(ViewId, const char* result, const char* exception, void* user)');
  const posted = onExported.indexOf('GameThread::Post(');
  assert.ok(posted > 0 && onExported.indexOf('m_panelActive') > posted, 'the swap back happens on the pump');
  assert.match(onExported, /result = std::string\(result \? result : ""\)/, 'the strings are copied before the call ends');
});

test('CHRON-32: native leaves Escape to the page while it has a list open, and Magelight lets the panel take it', () => {
  const chronicle = src('Chronicle.cpp');
  assert.match(chronicle, /RegisterListener\("escapeLayer"/);
  assert.match(chronicle, /state\.pageLayer = m_escapeLayer;/);
  assert.match(body(chronicle, 'void Chronicle::SetEscapeLayer(bool open)'), /UiHost\(\)\.CaptureEscape\(open\);/);
  assert.match(body(src('UiHostMagelight.cpp'), 'void CaptureEscape(bool capture) override'), /m_api->SetEscapeCapture\(m_panel, capture\)/);
  assert.match(fs.readFileSync(path.join(__dirname, '../Data/PrismaUI/views/Chronicle/bridge.js'), 'utf8'), /'escapeLayer'[,\]]/);
});

test('CHRON-39: a SkyrimNet name answer from before a load is dropped, and byte cuts keep whole characters', () => {
  const net = src('SkyrimNet.cpp');
  assert.match(net, /new NameReply\(text, name, crosshair, Chronicle::GetSingleton\(\)->WorldRevision\(\)\)/);
  assert.match(net, /chronicle->WorldRevision\(\) != world/);
  assert.match(src('Api.cpp'), /ChronicleUtf8::Cut\(ChronicleUtf8::ReplaceInvalid\(desc->help\)\.value, 200\)/);
  for (const name of ['Api.cpp', 'SelfTest.cpp', 'Events.cpp']) {
    assert.doesNotMatch(src(name), /ReplaceInvalid\([^;]*\)\.value\.substr\(/, `${name}: no plain cut after cleaning`);
  }
  assert.doesNotMatch(src('Chronicle.h'), /m_hideInMenus|m_hideInPhotoMode/, 'the unused copies are gone');
});
