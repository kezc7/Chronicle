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

// CHRON-30: a page the host loads again comes back whole. Magelight releases
// hidden views when the game runs short of video memory and loads each page
// again when it is shown; the page has then lost everything native sent it.
// Native answers by sending that one page its role and the handshake again
// (Chronicle::ReplayPage). These tests run the real index.html as Magelight's
// two copies, which share one storage, load one again mid-play, hand it what
// ReplayPage sends, and check its lines, settings and visibility are back.

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

// The player's settings file, as the DLL reads it: not the page's defaults.
const FILE = {
  general: {hide_in_menus: true, hide_in_dialogue: false, max_messages: 100, persist_history: true, language: 'en'},
  window: {theme: 'high-contrast', anchor: 'custom', opacity: 0.65},
  channels: {},
};
const RUNTIME = {loaded: true, profile: '17940456036136542', character: 'Dragonborn', lineage: null};

// A freshly loaded page in its role ('panel', 'hud', or PrismaUI's 'full'),
// with nothing from native yet. `stored` is the mod's storage, shared by the
// copies and kept across a reload.
// options.fetch: the page's fetch (config.json); by default it fails, as a
// Magelight page's does. Timers are kept so a test can see what still runs.
function load(role, stored, options = {}) {
  const saves = [];
  const timers = new Map();
  let nextTimer = 1;
  const window = Object.assign(new Window(), {
    focusAcquired() {}, focusReleased() {}, uiReady() {}, diagnostics() {},
    settingsChanged: value => saves.push(JSON.parse(value)),
    playerInput() {}, consoleCommand() {}, exportHistory() {}, historyImport() {}, historyCleared() {},
  });
  const document = new Document(html, window);
  const context = vm.createContext({
    window, document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver,
    console: {error() {}, warn() {}, log() {}},
    navigator: {getGamepads: () => []},
    localStorage: {
      getItem: key => stored.has(key) ? stored.get(key) : null,
      setItem: (key, value) => stored.set(key, String(value)),
      removeItem: key => stored.delete(key),
    },
    fetch: options.fetch || (() => Promise.reject(new Error('no config.json in tests'))),
    getComputedStyle: () => ({getPropertyValue: () => ''}),
    requestAnimationFrame() {}, cancelAnimationFrame() {}, ResizeObserver: class { observe() {} },
    performance: {now: () => 0},
    setTimeout() { return 0; }, clearTimeout() {},
    setInterval: (fn, ms) => { const id = nextTimer++; timers.set(id, ms); return id; },
    clearInterval: id => timers.delete(id),
  });
  for (const [name, source] of sources) vm.runInContext(source, context, {filename: name});
  if (role !== 'full') context.setHostRole(role);
  // Copied into this realm, so deepEqual compares values, not Array prototypes.
  const lines = () => Array.from(vm.runInContext('messages', context), entry => entry.message);
  const config = () => vm.runInContext('config', context);
  const connected = () => vm.runInContext('runtime.connected', context);
  // The panel's save timer, run now.
  const flush = () => vm.runInContext('flushHistory()', context);
  // The handshake's uiReady timer (500 ms) while it still runs.
  const handshaking = () => [...timers.values()].includes(500);
  return {context, document, saves, lines, config, connected, flush, handshaking};
}

// What native sends a page: the first handshake (Bootstrap) and, for a page
// loaded again, ReplayPage. The same three calls, in this order.
function handshake(page, visible = true) {
  page.context.nativeBootstrap({settings: FILE, defaults: null, warning: '', version: 'test', nativeHotkeys: true,
    gameLanguage: 'ENGLISH', host: 'Magelight'});
  page.context.setRuntime(RUNTIME);
  page.context.setVisibility(visible);
}

let clock = 1700000000000;
const line = message => JSON.stringify({channel: 'dialogue', speaker: 'Lucien', message, timestamp: ++clock});
// Native sends every line to every copy.
const send = (pages, message) => { const text = line(message); for (const page of pages) page.context.addMessage(text); };

// The two Magelight copies mid-play: three lines in, the panel's saved them.
function playing() {
  const stored = new Map();
  const panel = load('panel', stored);
  const hud = load('hud', stored);
  handshake(panel);
  handshake(hud);
  for (const message of ['one', 'two', 'three']) send([panel, hud], message);
  panel.flush();
  return {stored, panel, hud};
}

test('Magelight: the HUD copy loaded again for video memory gets its lines, settings and visibility back', () => {
  const {stored, panel, hud} = playing();
  assert.deepEqual(hud.lines(), ['one', 'two', 'three']);

  // Released while hidden, shown again: a new page in the same storage.
  const again = load('hud', stored);
  // Without the handshake the page is what the player saw: empty, defaults.
  assert.deepEqual(again.lines(), []);
  assert.equal(again.connected(), false);
  assert.equal(again.config().window.theme, 'skyrim');

  // A line Magelight queued for the page lands before the handshake does.
  send([panel, again], 'four');
  panel.flush();
  handshake(again);
  assert.deepEqual(again.lines(), ['one', 'two', 'three', 'four'], 'every line once, in order');
  assert.equal(again.connected(), true);
  assert.equal(again.config().window.theme, 'high-contrast', "the player's settings, not the defaults");
  assert.equal(again.config().general.hide_in_dialogue, false);
  assert.equal(again.document.body.classList.contains('runtime-hidden'), false, 'shown');
  assert.equal(vm.runInContext('hostRole', again.context), 'hud', 'still the display-only copy');

  // Lines keep coming afterwards.
  send([panel, again], 'five');
  assert.deepEqual(again.lines(), ['one', 'two', 'three', 'four', 'five']);
  assert.equal(again.saves.length, 0, 'the HUD copy never saves');
});

test('a line queued while the page was gone, and not yet in storage, still shows once', () => {
  const {stored, panel} = playing();
  const again = load('hud', stored);
  send([panel, again], 'four');   // the panel's save timer hasn't run yet
  handshake(again);
  assert.deepEqual(again.lines(), ['one', 'two', 'three', 'four']);
});

test('the visibility comes back as native has it: hidden stays hidden', () => {
  const {stored} = playing();
  const again = load('hud', stored);
  handshake(again, false);
  assert.equal(again.document.body.classList.contains('runtime-hidden'), true);
  assert.deepEqual(again.lines(), ['one', 'two', 'three'], 'the lines are there when it shows');
});

test("Magelight: the panel copy loaded again shows and saves the player's settings, not the defaults", () => {
  const {stored, hud} = playing();
  const again = load('panel', stored);
  handshake(again);
  assert.deepEqual(again.lines(), ['one', 'two', 'three']);
  again.context.nativeHotkey('settings');
  again.context.focusResult(true, 'focus.active');
  assert.equal(vm.runInContext('settingsOpen', again.context), true);
  assert.equal(again.document.getElementById('sp-hide-dialogue').checked, false);
  assert.equal(again.document.getElementById('sp-theme').value, 'high-contrast');
  again.document.getElementById('sp-apply').click();
  assert.equal(again.saves.length, 1);
  assert.equal(again.saves[0].general.hide_in_dialogue, false);
  assert.equal(again.saves[0].window.theme, 'high-contrast');
  // The HUD copy, not loaded again, kept its lines.
  assert.deepEqual(hud.lines(), ['one', 'two', 'three']);
});

test('PrismaUI: its one page loaded again gets its lines and settings back the same way', () => {
  const stored = new Map();
  const page = load('full', stored);
  handshake(page);
  send([page], 'one');
  page.flush();
  const again = load('full', stored);
  send([again], 'two');
  handshake(again);
  assert.deepEqual(again.lines(), ['one', 'two']);
  assert.equal(again.config().window.theme, 'high-contrast');
});

test('another character after a reload starts with its own log, not the old one', () => {
  const {stored} = playing();
  const again = load('hud', stored);
  handshake(again);
  again.context.setRuntime({loaded: false, profile: '', character: '', lineage: null});
  again.context.setRuntime({loaded: true, profile: '42', character: 'Other', lineage: null});
  assert.deepEqual(again.lines(), [], "the first character's lines don't follow");
});

// The native side, read from source: a page loaded again is sent these same
// calls, to that page alone, by both hosts.
test('native sends a page loaded again the same handshake, to that page only', () => {
  const source = fs.readFileSync(path.join(ROOT, 'src/Chronicle.cpp'), 'utf8');
  const body = /void Chronicle::ReplayPage\(std::uint64_t page\)\s*\{([\s\S]*?)\n\}/.exec(source);
  assert.ok(body, 'Chronicle::ReplayPage exists');
  const calls = [...body[1].matchAll(/host\.InvokePage\(page, ([^;]+)\);/g)].map(match => match[1]);
  assert.deepEqual(calls, ['BootstrapScript(settings, warning).c_str()', 'RuntimeScript().c_str()',
    'm_hidden ? "setVisibility(false)" : "setVisibility(true)"']);
  assert.doesNotMatch(body[1], /InvokeUI\(/, 'never both copies');
  assert.match(source, /SetPageReloadHandler\(\[\]\(std::uint64_t page\) \{\s*MainTask\(\[page\] \{ Chronicle::GetSingleton\(\)->ReplayPage\(page\); \}\);/);
  // Bootstrap builds the same script.
  assert.match(source, /InvokeUI\(BootstrapScript\(settings, warning\)\);\s*PublishRuntime\(\);\s*UpdateVisibility\(\);/);

  for (const host of ['UiHostMagelight.cpp', 'UiHostPrisma.cpp']) {
    const text = fs.readFileSync(path.join(ROOT, 'src', host), 'utf8');
    assert.match(text, /\.Loaded\((ev->)?view\)\)/, `${host} tells a reload from a first load`);
    assert.match(text, /void InvokePage\(std::uint64_t page, const char\* script\) override/, host);
  }
  const magelight = fs.readFileSync(path.join(ROOT, 'src/UiHostMagelight.cpp'), 'utf8');
  // The role goes first, before the handshake, on every load.
  const ready = magelight.indexOf('case Event::ViewDomReady:');
  assert.ok(magelight.indexOf("setHostRole('hud')", ready) < magelight.indexOf('m_onReload(view)', ready));
  assert.doesNotMatch(magelight, /the native handshake is not repeated/);
});

// CHRON-33: on PrismaUI the page fetches its bundled config.json. On a page
// loaded again the handshake can land first; the fetch answering after it must
// not put the bundled defaults (or an old browser copy) over the player's
// settings, nor start a second handshake.
const settle = () => new Promise(resolve => setImmediate(resolve));
const bundled = {general: {hide_in_dialogue: true}, window: {theme: 'skyrim', opacity: 0.9}, channels: {}};

test('PrismaUI: config.json answering after the handshake leaves the settings and starts no second handshake', async () => {
  const stored = new Map([['chronicle_config', JSON.stringify({window: {theme: 'daedric'}})]]);
  let answer;
  const page = load('full', stored, {fetch: () => new Promise(resolve => { answer = resolve; })});
  handshake(page);
  answer({ok: true, json: async () => bundled});
  await settle();
  assert.equal(page.config().window.theme, 'high-contrast', "the player's theme, not the bundled one or the old copy");
  assert.equal(page.config().window.opacity, 0.65);
  assert.equal(page.config().general.hide_in_dialogue, false);
  assert.equal(page.handshaking(), false, 'no uiReady timer left running');
});

test('a first load still fetches its defaults, asks for the handshake, and stops asking once it comes', async () => {
  const page = load('full', new Map(), {fetch: async () => ({ok: true, json: async () => bundled})});
  await settle();
  assert.equal(page.config().window.opacity, 0.9, 'the bundled defaults before any settings file');
  assert.equal(page.handshaking(), true, 'uiReady is asked for');
  handshake(page);
  assert.equal(page.handshaking(), false);
  assert.equal(page.config().window.theme, 'high-contrast');
});
