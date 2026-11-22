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

// CHRON-44: a line that comes again within four seconds, while it is one of the
// last three rows, adds to that row's count instead of taking a new row. The
// chat window (on the real index.html) and the DovahHub tab count the same way.

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

function page() {
  const exported = [];
  const window = Object.assign(new Window(), {
    focusAcquired() {}, focusReleased() {}, uiReady() {}, diagnostics() {}, settingsChanged() {},
    playerInput() {}, consoleCommand() {}, historyImport() {}, historyCleared() {},
    exportHistory: text => exported.push(text),
  });
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
  context.nativeBootstrap({settings: {general: {}, window: {}, channels: {combat: {enabled: true}}}, defaults: null,
    warning: '', version: 'test', nativeHotkeys: true, gameLanguage: 'ENGLISH', host: 'PrismaUI'});
  context.setRuntime({loaded: true, profile: '42', character: 'Dragonborn', lineage: null, lines: []});
  context.setVisibility(true);
  let id = 0;
  // Times from a real clock: a timestamp of 0 reads as "now".
  const T = 1763800000000;
  const send = (at, message, channel = 'notification', speaker = '') =>
    context.addMessage(JSON.stringify({channel, speaker, message, timestamp: T + at, id: ++id}));
  const rows = () => Array.from(vm.runInContext('messages', context), entry =>
    entry.count > 1 ? `${entry.message} x${entry.count}` : entry.message);
  const badges = () => Array.from(document.getElementById('message-list').querySelectorAll('.burst-count'),
    badge => badge.textContent);
  return {context, document, send, rows, badges, exported, T};
}

test('a burst of the same line is one row with its count', () => {
  const {send, rows, badges} = page();
  for (let i = 0; i < 12; ++i) send(1000 + i * 300, 'Iron Arrow added');
  assert.deepEqual(rows(), ['Iron Arrow added x12']);
  assert.deepEqual(badges(), ['×12']);
});

test('the window is four seconds from the last repeat, so a steady stream stays one row', () => {
  const {send, rows} = page();
  send(0, 'Iron Dagger added');
  send(3900, 'Iron Dagger added');
  send(7800, 'Iron Dagger added');   // 7.8 s after the first, 3.9 s after the last
  send(12000, 'Iron Dagger added');  // 4.2 s gap: a new row
  assert.deepEqual(rows(), ['Iron Dagger added x3', 'Iron Dagger added']);
});

test('two lines taking turns (a fight) collapse each on its own row', () => {
  const {send, rows} = page();
  for (let i = 0; i < 5; ++i) {
    send(1000 + i * 500, 'You hit Bandit', 'combat', '');
    send(1250 + i * 500, 'Bandit hits you', 'combat', '');
  }
  assert.deepEqual(rows(), ['You hit Bandit x5', 'Bandit hits you x5']);
});

test('only the last three rows count, and speaker and channel must match too', () => {
  const {send, rows} = page();
  send(0, 'Hello', 'dialogue', 'Lydia');
  send(100, 'Hello', 'dialogue', 'Hadvar');
  send(200, 'Hello', 'notification');
  send(300, 'Quest updated');
  send(400, 'Hello', 'dialogue', 'Lydia');  // Lydia's row is four back: a new row
  send(500, 'Hello', 'notification');       // the notification's row is three back: it counts
  assert.deepEqual(rows(), ['Hello', 'Hello', 'Hello x2', 'Quest updated', 'Hello']);
});

test('the history ids of a burst are all known, so a replayed line shows once', () => {
  const {context, send, rows, T} = page();
  send(0, 'Gold added');
  send(200, 'Gold added');
  context.addMessage(JSON.stringify({channel: 'notification', speaker: '', message: 'Gold added', timestamp: T + 200, id: 2}));
  assert.deepEqual(rows(), ['Gold added x2']);
  // A page handed native's lines in its handshake counts them the same way.
  context.setRuntime({loaded: true, profile: '43', character: 'Other', lineage: null, lines: [
    {channel: 'notification', speaker: '', message: 'Gold added', timestamp: T, id: 1},
    {channel: 'notification', speaker: '', message: 'Gold added', timestamp: T + 200, id: 2},
    {channel: 'notification', speaker: '', message: 'Quest updated', timestamp: T + 300, id: 3},
  ]});
  assert.deepEqual(rows(), ['Gold added x2', 'Quest updated']);
});

test('the export writes the count', () => {
  const {context, send} = page();
  send(0, 'Iron Arrow added');
  send(10, 'Iron Arrow added');
  assert.match(vm.runInContext('visibleHistoryText()', context), /Iron Arrow added \(×2\)$/);
});

test('the DovahHub tab counts the same burst the same way', async () => {
  const {mount} = require(path.join(ROOT, 'Data/PrismaUI/views/DovahHub/modules/chronicle/history.js'));
  const element = tag => {
    const classes = new Set();
    const node = {
      tagName: tag.toUpperCase(), children: [], parent: null, dataset: {}, style: {}, hidden: false, type: '', value: '',
      _text: '', clientHeight: 100, scrollTop: 0, scrollHeight: 0, placeholder: '', disabled: false,
      get className() { return [...classes].join(' '); },
      set className(value) { classes.clear(); String(value).split(/\s+/).filter(Boolean).forEach(name => classes.add(name)); },
      classList: {add: name => classes.add(name), remove: name => classes.delete(name), contains: name => classes.has(name),
        toggle: (name, on) => ((on ?? !classes.has(name)) ? classes.add(name) : classes.delete(name))},
      get textContent() { return this.children.length ? this.children.map(child => child.textContent).join('') : this._text; },
      set textContent(value) { this.children = []; this._text = String(value); },
      setAttribute() {}, addEventListener() {},
      replaceChildren(...items) { this.children = []; items.forEach(item => this.appendChild(item)); },
      appendChild(child) { child.parent = this; this.children.push(child); return child; },
      insertBefore(child, before) { child.parent = this; this.children.splice(Math.max(0, this.children.indexOf(before)), 0, child); return child; },
      remove() { if (this.parent) this.parent.children.splice(this.parent.children.indexOf(this), 1); this.parent = null; },
      querySelector(selector) {
        const name = selector.replace(/^\./, '');
        for (const child of this.children) {
          if (child.classList.contains(name)) return child;
          const found = child.querySelector(selector);
          if (found) return found;
        }
        return null;
      },
    };
    return node;
  };
  const root = element('div');
  const holder = element('div');
  holder.className = 'chronicle-history';
  root.appendChild(holder);
  const store = [];
  for (let i = 0; i < 12; ++i) store.push({timestamp: 1000 + i * 300, channel: 'notification', speaker: '', message: 'Iron Arrow added'});
  store.push({timestamp: 9000, channel: 'combat', speaker: '', message: 'You hit Bandit'});
  const pushes = [];
  const events = [];
  const mod = {root, call: () => Promise.resolve({profile: '42', lines: store, more: false}),
    onPush: fn => pushes.push(fn), onEvent: fn => events.push(fn)};
  const tab = mount(mod, {createElement: element}, () => 0);
  events.forEach(fn => fn('tabShown'));
  await new Promise(resolve => setImmediate(resolve));
  const texts = () => tab.list.children.map(row => row.children[2].textContent);
  assert.deepEqual(texts(), ['Iron Arrow added×12', 'You hit Bandit']);
  pushes.forEach(fn => fn({line: {timestamp: 9500, channel: 'combat', speaker: '', message: 'You hit Bandit'}}));
  assert.deepEqual(texts(), ['Iron Arrow added×12', 'You hit Bandit×2']);
});

test('both use the same rule', () => {
  const window = fs.readFileSync(path.join(view, 'script.js'), 'utf8');
  const tab = fs.readFileSync(path.join(ROOT, 'Data/PrismaUI/views/DovahHub/modules/chronicle/history.js'), 'utf8');
  for (const source of [window, tab]) {
    assert.match(source, /const BURST_MS = 4000;/);
    assert.match(source, /const BURST_ROWS = 3;/);
  }
});
