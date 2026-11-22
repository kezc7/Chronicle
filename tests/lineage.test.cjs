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
// never saw are folded away and come back with the save that saw them.
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

// CHRON-41: native folds the lines (ChronicleHistory::WindowLines, tested in
// HistoryTests.cpp) and hands the window the loaded save's own; the page shows
// exactly those and keeps no copy. A character's lines from before native kept
// history come from the page's old storage once, folded the same way.
const RUNTIME = lineage => ({profile: '42', loaded: true, character: 'Dragonborn', lineage});

test("each load shows native's lines for that save, and nothing the page stored itself", () => {
  const {context, stored, line, shown} = setup();
  stored.set('chronicle_history_42', JSON.stringify([line(5, 'an old copy')]));
  context.setRuntime({...RUNTIME([[0, 100], [300, null]]), lines: [{...line(50, 'before A'), id: 1}]});
  assert.deepEqual(shown(), ['before A'], 'native has lines: the old copy is not read');
  context.addMessage(JSON.stringify({...line(310, 'new branch'), id: 4}));
  context.setRuntime({...RUNTIME([[0, 200], [400, null]]),
    lines: [{...line(50, 'before A'), id: 1}, {...line(150, 'after A, on to B'), id: 2}]});
  assert.deepEqual(shown(), ['before A', 'after A, on to B'], "B's lines; the branch from A is gone with A");
  assert.deepEqual(JSON.parse(stored.get('chronicle_history_42')).map(entry => entry.message), ['an old copy'],
    'the page writes no history');
});

test('a save from before 1.4.17 (no stretches) shows every line native has, as before', () => {
  const {context, line, shown} = setup();
  context.setRuntime({...RUNTIME(null), lines: [{...line(50, 'one'), id: 1}, {...line(150, 'two'), id: 2}]});
  assert.deepEqual(shown(), ['one', 'two']);
});

test('the same save published again keeps the log and filters; another save of the same character clears the filters', () => {
  const {context, line, shown} = setup();
  context.setRuntime({...RUNTIME([[0, 100], [300, null]]), lines: [{...line(50, 'one'), id: 1}]});
  context.addMessage(JSON.stringify({...line(310, 'two'), id: 2}));
  vm.runInContext("searchText = 'two';", context);
  context.setRuntime({...RUNTIME([[0, 100], [300, null]]), lines: [{...line(50, 'one'), id: 1}, {...line(310, 'two'), id: 2}]});
  assert.deepEqual(shown(), ['one', 'two'], 'a repeat of the same runtime keeps the log, each line once');
  assert.equal(vm.runInContext('searchText', context), 'two');
  context.setRuntime({...RUNTIME([[0, 320], [500, null]]), lines: []});
  assert.equal(vm.runInContext('searchText', context), '', "a search belongs to the save's log it was made in");
});

test("a character's lines from an old version's storage are shown once, folded, and handed to native", () => {
  const {context, stored, line, shown} = setup();
  const imported = [];
  context.window.historyImport = text => imported.push(JSON.parse(text));
  stored.set('chronicle_history_42', JSON.stringify([line(10, 'a'), line(150, 'folded'), line(320, 'b')]));
  context.setRuntime({...RUNTIME([[0, 100], [300, null]]), lines: [], historyEmpty: true});
  assert.deepEqual(shown(), ['a', 'b'], 'the loaded save sees only its own');
  assert.equal(imported.length, 1);
  assert.deepEqual(imported[0].lines.map(entry => entry.message), ['a', 'b']);
});

test('Clear history empties the window and tells native', () => {
  const {context, stored, line, shown} = setup();
  const cleared = [];
  context.window.historyCleared = profile => cleared.push(profile);
  stored.set('chronicle_history_42', JSON.stringify([line(10, 'old')]));
  context.setRuntime({...RUNTIME([[0, 100], [300, null]]), lines: [{...line(10, 'a'), id: 1}]});
  vm.runInContext('clearHistory();', context);
  assert.deepEqual(shown(), []);
  assert.deepEqual(cleared, ['42']);
  assert.equal(stored.has('chronicle_history_42'), false, "an old version's copy goes too");
});
