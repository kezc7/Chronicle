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

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const sources = ['i18n.js', 'bridge.js', 'controls.js', 'pad.js', 'script.js']
  .map(name => fs.readFileSync(path.join(view, name), 'utf8'));
const CHANNELS = ['all', 'dialogue', 'system', 'notification', 'skyrimnet', 'narrator', 'player', 'combat', 'console'];

function setup({host = 'PrismaUI'} = {}) {
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
      replaceChildren(...items) { this.children = items; },
      appendChild(item) { this.children.push(item); return item; },
      setAttribute() {}, querySelector: () => null, querySelectorAll: () => [],
      getBoundingClientRect: () => ({left: 0, top: 0, right: 0, bottom: 0}),
      blur() {}, focus() {}, select() {}, remove() {}, closest: () => null,
    };
  };
  const tabs = CHANNELS.map(channel => { const tab = node(); tab.dataset.channel = channel; return tab; });
  const window = {
    innerWidth: 1920, innerHeight: 1080, addEventListener() {},
    focusAcquired: () => calls.push('focusAcquired'),
    focusReleased: () => calls.push('focusReleased'),
    uiReady: () => calls.push('uiReady'),
    settingsChanged() {},
    playerInput: value => calls.push(['playerInput', JSON.parse(value)]),
    consoleCommand: value => calls.push(['consoleCommand', value]),
  };
  const pads = [];
  const context = vm.createContext({
    window, console: {error() {}},
    navigator: {getGamepads: () => pads},
    document: {
      body: node(), documentElement: node(), head: node(), activeElement: null,
      getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
      },
      createElement: node,
      querySelector: () => null,
      querySelectorAll: selector => selector.startsWith('.tab') ? tabs : [],
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
  vm.runInContext(`nativeBootstrap({settings: null, warning: '', version: 'test', nativeHotkeys: true, host: '${host}'});
    setRuntime({loaded: true, profile: 'p', character: 'Dragonborn'}); setVisibility(true);`, context);
  // A player's choice of channels: Narrator and Combat off, the rest on.
  vm.runInContext(`CHANNELS.forEach(ch => { config.channels[ch].enabled = ch !== 'narrator' && ch !== 'combat'; });
    applyConfig();`, context);
  calls.length = 0;
  // A standard-mapping pad: buttons by index, axes [lx, ly, rx, ry].
  const pad = {id: 'Skyrim Controller (Standard Mapping)', connected: true,
    buttons: Array.from({length: 17}, () => ({pressed: false, value: 0})), axes: [0, 0, 0, 0]};
  const press = (...indices) => pad.buttons.forEach((button, index) => {
    button.pressed = indices.includes(index); button.value = button.pressed ? 1 : 0;
  });
  const plug = () => { pads.length = 0; pads.push(pad); };
  let now = 1000;
  const tick = (ms = 16) => { now += ms; context.padTick(now); };
  const open = () => { context.nativeHotkey('interact', 'pad'); context.focusResult(true, 'focus.active'); };
  return {context, calls, keydown, nodes, pad, press, plug, tick, open, tabs};
}

const A = 0, B = 1, X = 2, Y = 3, LB = 4, RB = 5, UP = 12, DOWN = 13, LEFT = 14, RIGHT = 15;
const active = context => vm.runInContext('activeChannel', context);

test('the combo opens the window with the pad as the input, and its held buttons press nothing', () => {
  const {context, press, plug, tick, open} = setup();
  plug();
  press(LB, Y);                       // the native combo, still held as the window opens
  open();
  assert.equal(vm.runInContext('interactionOpen', context), true);
  assert.equal(context.document.documentElement.dataset.meridianInput, 'gamepad');
  tick(); tick();
  press(Y, A);                        // Y still held from the combo; A is new
  tick();
  assert.equal(vm.runInContext('inputOpen', context), true, 'A opened the chat bar');
  assert.equal(vm.runInContext("document.getElementById('speaker-filter').value", context), '', 'held Y did nothing');
});

test('LB / RB and the d-pad step through the channels shown, round and round', () => {
  const {context, press, plug, tick, open, tabs} = setup();
  plug(); open(); tick();
  assert.equal(active(context), 'all');
  press(RB); tick(); press(); tick();
  assert.equal(active(context), 'dialogue');
  press(LB); tick(); press(); tick();
  press(LB); tick(); press(); tick();
  assert.equal(active(context), 'console', 'wraps from All to the last channel shown');
  assert.ok(tabs.find(tab => tab.dataset.channel === 'combat').classList.contains('hidden-channel'));
  press(LB); tick(); press(); tick();
  assert.equal(active(context), 'player', 'Combat and Narrator are off, so they are skipped');
  press(RB); tick(); press(); tick();
  press(RIGHT); tick(); press(); tick();
  assert.equal(active(context), 'all');
  assert.equal(vm.runInContext('inputTarget', context), 'all', 'the chat bar follows the tab, as a click does');
});

test('the d-pad scrolls the log and repeats while held; the right stick scrolls too', () => {
  const {nodes, pad, press, plug, tick, open} = setup();
  plug(); open(); tick();
  const list = nodes.get('message-list');
  list.scrollTop = 1000;
  press(UP); tick();
  assert.equal(list.scrollTop, 952, 'one step');
  tick(300);
  assert.equal(list.scrollTop, 952, 'no repeat before the delay');
  tick(100);
  assert.equal(list.scrollTop, 904, 'repeats after 380 ms');
  tick(90);
  assert.equal(list.scrollTop, 856, 'then every 90 ms');
  press(); tick();
  pad.axes[3] = -1;                   // right stick pushed up
  tick();
  assert.equal(list.scrollTop, 834);
  pad.axes[3] = 0.1;                  // inside the dead zone
  tick();
  assert.equal(list.scrollTop, 834);
});

test('A, then up, picks the latest command, which goes back on its own channel', () => {
  const {context, calls, nodes, press, plug, tick, open} = setup();
  // Earlier, with the keyboard: a console command, then a chat line.
  context.nativeHotkey('input'); context.focusResult(true, 'focus.active');
  vm.runInContext("setInputTarget('console')", context);
  nodes.get('chat-input').value = 'coc whiterun';
  vm.runInContext('submitInput()', context);
  context.nativeHotkey('input'); context.focusResult(true, 'focus.active');
  vm.runInContext("setInputTarget('all')", context);
  nodes.get('chat-input').value = '/selftest';
  vm.runInContext('submitInput()', context);
  calls.length = 0;

  plug(); open(); tick();
  press(A); tick(); press(); tick();
  assert.equal(vm.runInContext('inputOpen', context), true);
  press(UP); tick(); press(); tick();
  assert.equal(nodes.get('chat-input').value, '/selftest');
  press(UP); tick(); press(); tick();
  assert.equal(nodes.get('chat-input').value, 'coc whiterun');
  assert.equal(vm.runInContext('inputTarget', context), 'console', 'the recalled command takes its channel back');
  press(A); tick();
  assert.deepEqual(calls.filter(call => Array.isArray(call)), [['consoleCommand', 'coc whiterun']]);
});

test('B closes the chat bar first, then the window', () => {
  const {context, calls, press, plug, tick, open} = setup();
  plug(); open(); tick();
  press(A); tick(); press(); tick();
  press(B); tick(); press(); tick();
  assert.equal(vm.runInContext('inputOpen', context), false);
  assert.equal(vm.runInContext('interactionOpen', context), true);
  press(B); tick();
  assert.equal(vm.runInContext('interactionOpen', context), false);
  assert.ok(calls.includes('focusReleased'));
});

test('Y walks the speaker filter and X jumps to the newest line', () => {
  const {context, nodes, press, plug, tick, open} = setup();
  vm.runInContext('refreshSpeakerOptions = () => {}', context);
  const select = nodes.get('speaker-filter');
  select.options = [{value: ''}, {value: 'Lydia'}, {value: 'Serana'}];
  plug(); open(); tick();
  press(Y); tick(); press(); tick();
  assert.equal(select.value, 'Lydia');
  press(Y); tick(); press(); tick();
  press(Y); tick(); press(); tick();
  assert.equal(select.value, '', 'round to all speakers');
  const list = nodes.get('message-list');
  list.scrollTop = 0; list.scrollHeight = 900;
  press(X); tick();
  assert.equal(list.scrollTop, 900);
});

test('the pad does nothing while the window is closed or the page is the display copy', () => {
  const {context, nodes, press, plug, tick} = setup();
  plug();
  press(A); tick(); press(); tick();
  assert.equal(vm.runInContext('inputOpen', context), false);
  context.nativeHotkey('interact', 'pad'); context.focusResult(true, 'focus.active');
  vm.runInContext("hostRole = 'hud'", context);
  nodes.get('message-list').scrollTop = 500;
  tick(); press(DOWN); tick();
  assert.equal(nodes.get('message-list').scrollTop, 500);
});

test('arrow keys drive the log the same way (Magelight types the pad as keys)', () => {
  const {context, nodes, keydown, open} = setup({host: 'Magelight'});
  open();
  nodes.get('message-list').scrollTop = 200;
  assert.equal(keydown({key: 'ArrowDown', keyCode: 40}), true);
  assert.equal(nodes.get('message-list').scrollTop, 248);
  keydown({key: 'ArrowRight', keyCode: 39});
  assert.equal(active(context), 'dialogue');
  assert.equal(context.document.documentElement.dataset.meridianInput, 'gamepad', 'a typed d-pad keeps the pad prompts');
  keydown({key: 'Enter', keyCode: 13});
  assert.equal(vm.runInContext('inputOpen', context), true, 'A (Enter) opens the chat bar');
  keydown({key: 'a', keyCode: 65, target: {tagName: 'INPUT'}});
  assert.equal(context.document.documentElement.dataset.meridianInput, 'keyboardMouse', 'a real key brings the keyboard back');
});

test('arrow keys in the search box stay in the search box', () => {
  const {context, nodes, keydown, open} = setup();
  open();
  nodes.get('message-list').scrollTop = 200;
  assert.equal(keydown({key: 'ArrowDown', keyCode: 40, target: {tagName: 'INPUT'}}), false);
  assert.equal(nodes.get('message-list').scrollTop, 200);
  assert.equal(active(context), 'all');
});

test('the prompts name the buttons each host gives the page, most needed first', () => {
  const labels = bar => bar.children.map(prompt => prompt.children.map(child => child.textContent).join(' '));
  const prisma = setup();
  prisma.open();
  assert.deepEqual(labels(prisma.nodes.get('pad-prompts')),
    ['A Recent commands', 'B Close', 'LB RB Channel', '↑ ↓ Scroll', 'Y Speaker', 'X Newest']);
  vm.runInContext('openInput()', prisma.context);
  assert.deepEqual(labels(prisma.nodes.get('pad-prompts')), ['↑ ↓ Recent commands', 'A Send', 'B Cancel', 'LB RB Channel']);
  const magelight = setup({host: 'Magelight'});
  magelight.open();
  assert.deepEqual(labels(magelight.nodes.get('pad-prompts')),
    ['A Recent commands', 'B Close', '← → Channel', '↑ ↓ Scroll']);
  vm.runInContext("config.general.language = 'de'; applyConfig();", magelight.context);
  assert.deepEqual(labels(magelight.nodes.get('pad-prompts')),
    ['A Letzte Befehle', 'B Schließen', '← → Kanal', '↑ ↓ Blättern']);
});
