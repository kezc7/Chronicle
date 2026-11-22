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

// CHRON-43: Escape and B close one level at a time (an open list, then Settings
// or the chat bar, then the chat window, back to the game), on both hosts, and
// on Magelight every pad button reaches the page once its gamepad helper is
// there (the navigation style). Runs the real index.html.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const {Document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver, Window} = require('./page-dom.cjs');

const ROOT = path.join(__dirname, '..');
const view = path.join(ROOT, 'Data/PrismaUI/views/Chronicle');
const html = fs.readFileSync(path.join(view, 'index.html'), 'utf8');
const sources = ['dropdowns.js', 'i18n.js', 'bridge.js', 'controls.js', 'pad.js', 'script.js']
  .map(name => [name, fs.readFileSync(path.join(view, name), 'utf8')]);
const src = name => fs.readFileSync(path.join(ROOT, 'src', name), 'utf8');

function page({host = 'PrismaUI', role = 'full', helper = null, settings = {general: {}, window: {}, channels: {}}} = {}) {
  const sent = [];
  const record = name => value => sent.push([name, value]);
  const window = Object.assign(new Window(), {
    focusAcquired: record('focusAcquired'), focusReleased: record('focusReleased'), uiReady() {}, diagnostics() {},
    settingsChanged() {}, playerInput() {}, consoleCommand() {}, exportHistory() {}, historyImport() {}, historyCleared() {},
    escapeLayer: record('escapeLayer'), padStyle: record('padStyle'),
  });
  if (helper) window.magelight = {gamepad: helper};
  const document = new Document(html, window);
  const context = vm.createContext({
    window, document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver,
    console: {error() {}, warn() {}, log() {}},
    navigator: {getGamepads: () => []},
    localStorage: {getItem: () => null, setItem() {}, removeItem() {}},
    fetch: () => Promise.reject(new Error('no config.json in tests')),
    getComputedStyle: () => ({getPropertyValue: () => ''}),
    requestAnimationFrame() {}, cancelAnimationFrame() {}, ResizeObserver: class { observe() {} },
    performance: {now: () => 0},
    setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  for (const [name, source] of sources) vm.runInContext(source, context, {filename: name});
  if (role !== 'full') context.setHostRole(role);
  context.nativeBootstrap({settings, defaults: null, warning: '', version: 'test', nativeHotkeys: true,
    gameLanguage: 'ENGLISH', host});
  context.setRuntime({loaded: true, profile: '42', character: 'Dragonborn', lineage: null, lines: []});
  context.setVisibility(true);
  const state = () => ({
    window: vm.runInContext('interactionOpen', context),
    settings: vm.runInContext('settingsOpen', context),
    input: vm.runInContext('inputOpen', context),
  });
  const escape = () => (document.activeElement || document.body).dispatchEvent(
    new KeyboardEvent('keydown', {key: 'Escape', keyCode: 27, code: 'Escape', bubbles: true, cancelable: true}));
  const open = kind => { context.nativeHotkey(kind); context.focusResult(true, 'focus.active'); };
  const layers = () => sent.filter(([name]) => name === 'escapeLayer').map(([, value]) => value);
  return {context, document, sent, state, escape, open, layers};
}

for (const host of ['PrismaUI', 'Magelight']) {
  test(`${host}: Escape steps back from Settings to the chat window, then to the game`, () => {
    const p = page({host, role: host === 'Magelight' ? 'panel' : 'full'});
    p.open('settings');
    assert.deepEqual(p.state(), {window: true, settings: true, input: false});
    assert.deepEqual(p.layers(), ['1'], 'native leaves Escape to the page while Settings is open');
    p.escape();
    assert.deepEqual(p.state(), {window: true, settings: false, input: false}, 'Settings closed, the window stays');
    assert.deepEqual(p.layers(), ['1', '0']);
    p.escape();
    assert.deepEqual(p.state(), {window: false, settings: false, input: false});
    assert.ok(p.sent.some(([name]) => name === 'focusReleased'), 'back to the game');
  });

  test(`${host}: Escape closes the chat bar first, then the window`, () => {
    const p = page({host, role: host === 'Magelight' ? 'panel' : 'full'});
    p.open('input');
    assert.equal(p.state().input, true);
    assert.deepEqual(p.layers(), ['1']);
    p.escape();
    assert.deepEqual(p.state(), {window: true, settings: false, input: false});
    p.escape();
    assert.equal(p.state().window, false);
  });
}

test('an open list in Settings closes first, then Settings, then the window', () => {
  const p = page();
  p.open('settings');
  p.document.getElementById('sp-theme').click();
  assert.equal(p.context.window.DovahHubChoices.isOpen(), true, 'the theme list opened');
  p.escape();
  assert.equal(p.context.window.DovahHubChoices.isOpen(), false, 'the list closed');
  assert.equal(p.state().settings, true, 'Settings stay');
  p.escape();
  assert.deepEqual(p.state(), {window: true, settings: false, input: false});
  p.escape();
  assert.equal(p.state().window, false);
});

test('B on the pad steps back from Settings the same way', () => {
  const p = page();
  p.open('settings');
  vm.runInContext("padFire('cancel')", p.context);
  assert.deepEqual(p.state(), {window: true, settings: false, input: false});
  vm.runInContext("padFire('cancel')", p.context);
  assert.equal(p.state().window, false);
});

test('the Close button still closes everything', () => {
  const p = page();
  p.open('settings');
  p.document.getElementById('sp-close').click();
  assert.equal(p.state().window, false);
});

// A stand-in for window.magelight.gamepad: what the page registers, and a way to press.
function helper() {
  const handlers = [];
  let disposed = 0;
  return {
    onAction(fn) { handlers.push(fn); return () => { disposed++; }; },
    press: (action, phase = 'press') => handlers.map(fn => fn({control: 'x', action, phase, value: 1, x: 0, y: 0, dt: 0})),
    handlers, disposed: () => disposed,
  };
}

test('Magelight with its gamepad helper: the panel asks for the navigation style and takes every button', () => {
  const pad = helper();
  const p = page({host: 'Magelight', role: 'panel', helper: pad});
  assert.deepEqual(p.sent.filter(([name]) => name === 'padStyle').map(([, value]) => value), ['navigation']);
  assert.equal(pad.handlers.length, 1);
  p.open('interact');
  const prompts = p.document.getElementById('pad-prompts').textContent;
  assert.match(prompts, /LB/, 'the shoulder buttons are prompted');
  assert.match(prompts, /Y/);
  const tab = () => vm.runInContext('activeChannel', p.context);
  const first = tab();
  assert.deepEqual(pad.press('nextTab'), [true], 'taken: the helper moves no focus of its own');
  assert.notEqual(tab(), first, 'RB changes the tab');
  pad.press('previousTab');
  assert.equal(tab(), first);
  pad.press('nextTab', 'release');
  assert.equal(tab(), first, 'a release does nothing');
  pad.press('accept');
  assert.equal(p.state().input, true, 'A opens the chat bar');
  pad.press('cancel');
  assert.equal(p.state().input, false, 'B steps back');
  pad.press('cancel');
  assert.equal(p.state().window, false);
});

test('without the helper, on the HUD copy, or with the setting off, the keys style stays', () => {
  const none = page({host: 'Magelight', role: 'panel'});
  assert.deepEqual(none.sent.filter(([name]) => name === 'padStyle').map(([, value]) => value), ['keys']);
  assert.doesNotMatch(none.document.getElementById('pad-prompts').textContent, /LB/);

  const hudPad = helper();
  const hud = page({host: 'Magelight', role: 'hud', helper: hudPad});
  assert.equal(hud.sent.filter(([name]) => name === 'padStyle').length, 0, 'the display copy never asks');
  assert.equal(hudPad.handlers.length, 0);

  const offPad = helper();
  const off = page({host: 'Magelight', role: 'panel', helper: offPad,
    settings: {general: {magelight_pad_navigation: false}, window: {}, channels: {}}});
  assert.deepEqual(off.sent.filter(([name]) => name === 'padStyle').map(([, value]) => value), ['keys']);
  assert.equal(offPad.handlers.length, 0);

  const prisma = page({host: 'PrismaUI', helper: helper()});
  assert.equal(prisma.sent.filter(([name]) => name === 'padStyle').length, 0, 'PrismaUI reads the pad itself');
});

test('native: the pad style goes to the panel on the pump; PrismaUI needs nothing', () => {
  const chronicle = src('Chronicle.cpp');
  assert.match(chronicle, /RegisterListener\("padStyle", \[\]\(const char\* text\) \{\s*const bool navigation = text && std::string_view\(text\) == "navigation";\s*MainTask\(\[navigation\] \{ GetSingleton\(\)->SetPadStyle\(navigation\); \}\);/);
  const magelight = src('UiHostMagelight.cpp');
  assert.match(magelight, /void SetPadStyle\(bool navigation\) override/);
  assert.match(magelight, /PadConfig\(navigation \? kGamepadStyleNavigation : kGamepadStyleKeys\)/);
  assert.match(magelight, /m_api->SetViewGamepad\(m_panel, &pad\)/);
  assert.match(src('UiHostPrisma.cpp'), /void SetPadStyle\(bool\) override \{\}/);
  assert.match(fs.readFileSync(path.join(view, 'bridge.js'), 'utf8'), /'padStyle'/);
});
