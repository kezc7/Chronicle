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

// Magelight runs the page twice: a display-only 'hud' copy and the 'panel'
// copy that takes input and saves. Checks the hud copy stays silent and that
// the panel's state reaches it when an interaction ends.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const sources = ['i18n.js', 'bridge.js', 'controls.js', 'script.js']
  .map(name => fs.readFileSync(path.join(view, name), 'utf8'));

function setup() {
  const nodes = new Map();
  const documentListeners = {};
  const calls = [];
  const stored = new Map();
  const node = () => {
    const classes = new Set();
    const listeners = {};
    return {
      textContent: '', value: '', hidden: false, title: '', tagName: 'DIV',
      dataset: {}, style: {setProperty() {}, removeProperty() {}}, options: [], children: [],
      scrollTop: 0, scrollHeight: 0, clientHeight: 0, offsetHeight: 0, offsetWidth: 0,
      classList: {
        add: (...names) => names.forEach(name => classes.add(name)),
        remove: (...names) => names.forEach(name => classes.delete(name)),
        toggle: (name, on) => (on ?? !classes.has(name)) ? classes.add(name) : classes.delete(name),
        contains: name => classes.has(name),
      },
      addEventListener(type, listener) { (listeners[type] ||= []).push(listener); },
      dispatch(type, event = {}) {
        (listeners[type] || []).forEach(listener => listener({stopPropagation() {}, preventDefault() {}, ...event}));
      },
      replaceChildren(...items) { this.children = items; },
      appendChild(item) { this.children.push(item); return item; },
      setAttribute() {}, querySelector: () => null, querySelectorAll: () => [],
      getBoundingClientRect: () => ({left: 0, top: 0, right: 0, bottom: 0}),
      blur() {}, focus() {}, select() {}, remove() {}, closest: () => null,
    };
  };
  const window = {
    innerWidth: 1920, innerHeight: 1080, addEventListener() {},
    focusAcquired: () => calls.push('focusAcquired'),
    focusReleased: () => calls.push('focusReleased'),
    uiReady: () => calls.push('uiReady'),
    settingsChanged: () => calls.push('settingsChanged'),
  };
  const context = vm.createContext({
    window, console: {error() {}},
    document: {
      body: node(), documentElement: node(), head: node(), activeElement: null,
      getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
      },
      createElement: node,
      querySelector: () => null, querySelectorAll: () => [],
      addEventListener(type, listener) { (documentListeners[type] ||= []).push(listener); },
    },
    localStorage: {getItem: key => (stored.has(key) ? stored.get(key) : null), setItem: (key, value) => stored.set(key, value), removeItem: key => stored.delete(key)},
    fetch: () => Promise.reject(new Error('no config.json in tests')),
    getComputedStyle: () => ({}),
    requestAnimationFrame() {}, ResizeObserver: class { observe() {} },
    setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  sources.forEach(source => vm.runInContext(source, context));
  const keydown = event => {
    let prevented = false;
    const full = {key: '', keyCode: 0, repeat: false, shiftKey: false, target: {tagName: 'BODY'},
      preventDefault() { prevented = true; }, ...event};
    documentListeners.keydown.forEach(listener => listener(full));
    return prevented;
  };
  vm.runInContext(`nativeBootstrap({settings: null, warning: '', version: 'test', nativeHotkeys: true});
    setRuntime({loaded: true, profile: 'p', character: 'Dragonborn'}); setVisibility(true);`, context);
  calls.length = 0;
  return {context, calls, keydown, nodes, stored};
}

test('the hud copy never asks for focus, releases it, or saves', () => {
  const {context, calls, stored} = setup();
  context.setHostRole('hud');
  stored.clear();
  assert.equal(context.nativeHotkey('input'), false);
  assert.equal(context.nativeHotkey('settings'), false);
  assert.equal(context.focusResult(true, 'focus.active'), false);
  context.addMessage({channel: 'dialogue', speaker: 'Lydia', message: 'I am sworn to carry your burdens.'});
  context.flushHistory();
  context.persistConfig();
  context.setUserHidden(true);
  assert.deepEqual(calls, []);
  assert.equal(stored.size, 0);
  assert.equal(context.sendToGame('uiReady', ''), true); // the handshake still works
});

test('the panel copy saves like the single page does', () => {
  const {context, calls, stored} = setup();
  context.setHostRole('panel');
  context.addMessage({channel: 'dialogue', speaker: 'Lydia', message: 'Hello'});
  context.flushHistory();
  assert.ok(stored.has('chronicle_history_p'));
  assert.equal(context.nativeHotkey('input'), true);
  assert.deepEqual(calls, ['focusAcquired']);
});

test('an interaction ending hands the panel state to the hud copy', () => {
  const panel = setup().context;
  const hud = setup().context;
  panel.setHostRole('panel');
  hud.setHostRole('hud');
  for (const page of [panel, hud]) {
    page.addMessage({channel: 'dialogue', speaker: 'Lydia', message: 'One'});
    page.addMessage({channel: 'system', speaker: '', message: 'Two'});
  }
  // Made only in the panel: a cleared history, a new message, a moved window, another tab.
  vm.runInContext(`clearHistory(); addMessage({channel: 'dialogue', speaker: 'Serana', message: 'Three'});
    config.window.position_x = 300; switchChannel('dialogue');`, panel);
  assert.equal(hud.importViewState(JSON.parse(panel.exportViewState())), true);
  assert.deepEqual(JSON.parse(vm.runInContext('JSON.stringify(messages.map(entry => entry.message))', hud)), ['Three']);
  assert.equal(vm.runInContext('config.window.position_x', hud), 300);
  assert.equal(vm.runInContext('activeChannel', hud), 'dialogue');
  assert.equal(hud.importViewState(null), false);
  assert.equal(hud.importViewState({messages: []}), false);
});

test('a line that reaches the hud after the panel snapshot is kept', () => {
  const panel = setup().context;
  const hud = setup().context;
  panel.setHostRole('panel');
  hud.setHostRole('hud');
  for (const page of [panel, hud]) page.addMessage({channel: 'dialogue', speaker: 'Lydia', message: 'One', timestamp: 1000});
  const snapshot = JSON.parse(panel.exportViewState());
  // The typed line is sent to both copies; the hud gets it before the snapshot is imported.
  for (const page of [panel, hud]) page.addMessage({channel: 'player', speaker: 'Cerberus', message: 'Hello', timestamp: 2000});
  assert.equal(hud.importViewState(snapshot), true);
  assert.deepEqual(JSON.parse(vm.runInContext('JSON.stringify(messages.map(entry => entry.message))', hud)), ['One', 'Hello']);
  // A second handover, now with the line in the snapshot, does not double it.
  assert.equal(hud.importViewState(JSON.parse(panel.exportViewState())), true);
  assert.deepEqual(JSON.parse(vm.runInContext('JSON.stringify(messages.map(entry => entry.message))', hud)), ['One', 'Hello']);
});
