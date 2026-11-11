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

// The chat window's log follows the loaded save (CHRON-18): lines a save
// never saw are folded away, kept in storage, and come back with the save
// that saw them.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const sources = ['i18n.js', 'bridge.js', 'controls.js', 'pad.js', 'script.js']
  .map(name => fs.readFileSync(path.join(view, name), 'utf8'));

function setup() {
  const nodes = new Map();
  const stored = new Map();
  const node = () => {
    const classes = new Set();
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
      addEventListener() {}, replaceChildren(...items) { this.children = items; },
      appendChild(item) { this.children.push(item); return item; },
      setAttribute() {}, querySelector: () => null, querySelectorAll: () => [],
      getBoundingClientRect: () => ({left: 0, top: 0, right: 0, bottom: 0}),
      blur() {}, focus() {}, select() {}, remove() {}, closest: () => null,
    };
  };
  const context = vm.createContext({
    window: {innerWidth: 1920, innerHeight: 1080, addEventListener() {}, uiReady() {}, focusReleased() {}, historyImport() {}},
    console: {error() {}},
    document: {
      body: node(), documentElement: node(), head: node(), activeElement: null,
      getElementById(id) { if (!nodes.has(id)) nodes.set(id, node()); return nodes.get(id); },
      createElement: node, querySelector: () => null, querySelectorAll: () => [], addEventListener() {},
    },
    localStorage: {getItem: key => stored.has(key) ? stored.get(key) : null, setItem: (key, value) => stored.set(key, value),
      removeItem: key => stored.delete(key)},
    fetch: () => Promise.reject(new Error('no config.json in tests')),
    getComputedStyle: () => ({}), requestAnimationFrame() {}, ResizeObserver: class { observe() {} },
    setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  sources.forEach(source => vm.runInContext(source, context));
  vm.runInContext(`nativeBootstrap({settings: null, warning: '', version: 'test', nativeHotkeys: true}); setVisibility(true);`, context);
  const line = (timestamp, message) => ({channel: 'dialogue', speaker: 'Lydia', message, timestamp});
  const shown = () => [...vm.runInContext('messages.map(entry => entry.message)', context)];
  const saved = () => JSON.parse(stored.get('chronicle_history_42') || '[]').map(entry => entry.message);
  return {context, stored, line, shown, saved};
}

test('loading an older save folds away the lines from later play, and the later save brings them back', () => {
  const {context, stored, line, shown, saved} = setup();
  stored.set('chronicle_history_42', JSON.stringify([line(50, 'before A'), line(150, 'after A, on to B'), line(250, 'after B')]));
  // Load save A (saved at 100), now t=300.
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 100], [300, null]]});
  assert.deepEqual(shown(), ['before A']);
  // Play on from A.
  context.addMessage(line(310, 'new branch'));
  context.flushHistory();
  assert.deepEqual(saved(), ['before A', 'after A, on to B', 'after B', 'new branch'], 'folded lines stay stored');
  // Load save B (saved at 200), now t=400.
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 200], [400, null]]});
  assert.deepEqual(shown(), ['before A', 'after A, on to B'], "B's lines are back; the new branch and play after B are folded");
});

test('a save from before 1.4.17 (no stretches) shows every line, as before', () => {
  const {context, stored, line, shown} = setup();
  stored.set('chronicle_history_42', JSON.stringify([line(50, 'one'), line(150, 'two')]));
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: null});
  assert.deepEqual(shown(), ['one', 'two']);
});

test('the same save published again changes nothing; another save of the same character clears the filters', () => {
  const {context, stored, line, shown} = setup();
  stored.set('chronicle_history_42', JSON.stringify([line(50, 'one')]));
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 100], [300, null]]});
  context.addMessage(line(310, 'two'));
  vm.runInContext("searchText = 'two';", context);
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 100], [300, null]]});
  assert.deepEqual(shown(), ['one', 'two'], 'a repeat of the same runtime keeps the log');
  assert.equal(vm.runInContext('searchText', context), 'two');
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 320], [500, null]]});
  assert.equal(vm.runInContext('searchText', context), '', "a search belongs to the save's log it was made in");
});

test('stored lines keep the shown ones first; folded ones fill the room left', () => {
  const {context, stored, line, saved} = setup();
  vm.runInContext('config.general.max_messages = 3;', context);
  stored.set('chronicle_history_42', JSON.stringify([line(10, 'a'), line(150, 'f1'), line(160, 'f2'), line(170, 'f3')]));
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 100], [300, null]]});
  context.addMessage(line(310, 'b'));
  context.flushHistory();
  assert.deepEqual(saved(), ['a', 'f3', 'b'], 'both shown lines kept, then the newest folded one, in time order');
});

test('Clear history clears the folded lines too', () => {
  const {context, stored, line, saved} = setup();
  stored.set('chronicle_history_42', JSON.stringify([line(10, 'a'), line(150, 'folded')]));
  context.setRuntime({profile: '42', loaded: true, character: 'Dragonborn', lineage: [[0, 100], [300, null]]});
  vm.runInContext('clearHistory(); flushHistory();', context);
  assert.deepEqual(saved(), []);
});
