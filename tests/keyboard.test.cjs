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

// Loads the real view scripts against a minimal DOM stub to check how keys
// and button presses behave when the page holds focus with nothing open.
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
    localStorage: {getItem: () => null, setItem() {}, removeItem() {}},
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
  return {context, calls, keydown, nodes};
}

test('keys reaching the page with nothing open release the stale focus', () => {
  const {calls, keydown} = setup();
  // Tab (Journal) and Space (jump) must not move focus or press a button.
  assert.equal(keydown({key: 'Tab', keyCode: 9}), true);
  assert.equal(keydown({key: ' ', keyCode: 32, repeat: true}), true);
  assert.deepEqual(calls, ['focusReleased', 'focusReleased']);
});

test('keys during an interaction are not treated as stale focus', () => {
  const {context, calls, keydown} = setup();
  assert.equal(context.nativeHotkey('settings'), true);
  assert.equal(vm.runInContext('settingsOpen', context), true);
  context.focusResult(true, 'focus.active');
  calls.length = 0;
  keydown({key: 'N', keyCode: 78, shiftKey: true}); // a second Shift+N leaves settings open
  assert.equal(vm.runInContext('settingsOpen', context), true);
  assert.deepEqual(calls, []);
});

test('a focus grant after the interaction closed is handed back', () => {
  const {context, calls} = setup();
  context.nativeHotkey('settings');
  context.closeInteraction(false); // e.g. a menu opened before the grant
  calls.length = 0;
  assert.equal(context.focusResult(true, 'focus.active'), false);
  assert.deepEqual(calls, ['focusReleased']);
});

test('the Clear button only clears during an interaction', () => {
  const {context, nodes} = setup();
  context.addMessage({channel: 'dialogue', speaker: 'Cicero', message: 'Hello'});
  nodes.get('clear-history').dispatch('click');
  assert.equal(vm.runInContext('messages.length', context), 1);
  context.nativeHotkey('interact');
  context.focusResult(true, 'focus.active');
  nodes.get('clear-history').dispatch('click');
  assert.equal(vm.runInContext('messages.length', context), 0);
});
