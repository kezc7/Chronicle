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

// The settings panel on a controller, run on the real index.html (the suite's
// pad and settings rule of 17 Nov): the pad walks past every control of every
// tab without getting stuck, and "Hide in conversations" (CHRON-28) can be set
// with the pad, is saved, and reads back after a reload, on both hosts. The
// lists are DovahHub's list control (dropdowns.js, CHRON-29): A opens one, the
// d-pad moves in it, A picks and B closes it.

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const {Document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver, Window} = require('./page-dom.cjs');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const html = fs.readFileSync(path.join(view, 'index.html'), 'utf8');
const sources = ['dropdowns.js', 'i18n.js', 'bridge.js', 'controls.js', 'pad.js', 'script.js']
  .map(name => [name, fs.readFileSync(path.join(view, name), 'utf8')]);

const A = 0, B = 1, LB = 4, RB = 5, UP = 12, DOWN = 13, LEFT = 14, RIGHT = 15;

// One page on one host. `settings` is the saved settings file as the DLL hands
// it over at load (null: none yet).
function setup({host = 'PrismaUI', settings = null} = {}) {
  const saves = [];
  const replies = [];
  const window = Object.assign(new Window(), {
    focusAcquired() {}, focusReleased() {}, uiReady() {}, diagnostics() {},
    // The DLL writes the file and answers each save after the call returns.
    settingsChanged: value => { saves.push(JSON.parse(value)); replies.push(true); },
    playerInput() {}, consoleCommand() {}, exportHistory() {}, historyImport() {}, historyCleared() {},
  });
  const document = new Document(html, window);
  const pads = [];
  const stored = new Map();
  const context = vm.createContext({
    window, document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver,
    console: {error() {}, warn() {}, log() {}},
    navigator: {getGamepads: () => pads},
    localStorage: {
      getItem: key => stored.has(key) ? stored.get(key) : null,
      setItem: (key, value) => stored.set(key, String(value)),
      removeItem: key => stored.delete(key),
    },
    fetch: () => Promise.reject(new Error('no config.json in tests')),
    getComputedStyle: () => ({getPropertyValue: () => ''}),
    requestAnimationFrame() {}, cancelAnimationFrame() {}, ResizeObserver: class { observe() {} },
    performance: {now: () => 0},
    setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  for (const [name, source] of sources) vm.runInContext(source, context, {filename: name});
  context.nativeBootstrap({settings, warning: '', version: 'test', nativeHotkeys: true, host});
  context.setRuntime({loaded: true, profile: 'p', character: 'Dragonborn'});
  context.setVisibility(true);
  const answer = () => { while (replies.length) context.settingsSaveResult(replies.shift(), 'settings.saved'); };
  answer();
  saves.length = 0;  // a first run saves the defaults once; only the player's saves count below

  const pad = {id: 'Skyrim Controller (Standard Mapping)', connected: true,
    buttons: Array.from({length: 17}, () => ({pressed: false, value: 0})), axes: [0, 0, 0, 0]};
  pads.push(pad);
  let now = 1000;
  const tick = () => { now += 16; context.padTick(now); answer(); };
  const press = index => {
    pad.buttons[index].pressed = true; pad.buttons[index].value = 1; tick();
    pad.buttons[index].pressed = false; pad.buttons[index].value = 0; tick();
  };
  // Magelight's pad layer types the d-pad as arrows and A as Enter.
  const KEYS = {[UP]: ['ArrowUp', 38], [DOWN]: ['ArrowDown', 40], [LEFT]: ['ArrowLeft', 37],
    [RIGHT]: ['ArrowRight', 39], [A]: ['Enter', 13], [B]: ['Escape', 27]};
  const type = index => {
    const [key, keyCode] = KEYS[index];
    const event = new KeyboardEvent('keydown', {key, keyCode, code: key, bubbles: true, cancelable: true});
    (document.activeElement || document.body).dispatchEvent(event);
    answer();
  };
  const input = host === 'Magelight' ? type : press;

  const openSettings = () => {
    context.nativeHotkey('settings', 'pad');
    context.focusResult(true, 'focus.active');
    assert.equal(vm.runInContext('settingsOpen', context), true, 'the settings panel opened');
    tick(); tick();   // the page's first pad frame only notes what is already held
  };
  const at = () => vm.runInContext('padState.settingsAt', context);
  // A control's name for messages: its id, its tab, or its class on a channel row.
  const name = el => {
    if (!el) return 'nothing';
    if (el.id) return el.id;
    if (el.dataset.sp) return 'tab:' + el.dataset.sp;
    const row = el.closest('.sp-ch-row');
    const role = [...el.classList].find(c => /^(ch-|sp-)/.test(c) && c !== 'sp-field') || el.tagName;
    return role + (row ? '@' + row.dataset.channel : '');
  };
  const activeTab = () => document.querySelector('.sp-tab.is-active').dataset.sp;
  const walkTo = id => {
    for (let i = 0; i < 200 && (at() || {}).id !== id; ++i) input(DOWN);
    assert.equal(name(at()), id, `the pad reached ${id}`);
  };
  const config = () => vm.runInContext('config', context);
  const listOpen = () => window.DovahHubChoices.isOpen();
  // A list: A opens it, `steps` downs move in it, A picks.
  const choose = steps => {
    input(A);
    assert.equal(listOpen(), true, `the list on ${name(at())} opened`);
    for (let i = 0; i < steps; ++i) input(DOWN);
    input(A);
    assert.equal(listOpen(), false);
  };
  return {context, document, saves, config, press, type, input, openSettings, at, name, activeTab, walkTo, listOpen, choose};
}

// What a player can reach on the open tab, read from the page itself: every
// field, list and button, in page order, the tabs first and the footer last.
function reachable(document) {
  const panel = document.getElementById('settings-panel');
  const section = panel.querySelector('.sp-section.active');
  const footer = document.getElementById('sp-footer');
  const isControl = el => ['INPUT', 'BUTTON', 'TEXTAREA'].includes(el.tagName);
  const usable = el => !el.disabled && !el.hidden && el.type !== 'hidden' && !el.closest('.hidden');
  return [...document.querySelectorAll('.sp-tab'), ...section.descendants(), ...footer.descendants()]
    .filter(el => isControl(el) && usable(el));
}

for (const host of ['PrismaUI', 'Magelight']) {
  test(`${host}: the pad walks past every control on every settings tab and comes round again`, () => {
    const page = setup({host});
    page.openSettings();
    for (const tab of ['general', 'window', 'channels']) {
      assert.equal(page.activeTab(), tab);
      const expected = reachable(page.document);
      assert.ok(expected.length > 8, `${tab}: the tab has controls`);
      const seen = [];
      page.input(DOWN);
      const first = page.at();
      do { seen.push(page.at()); page.input(DOWN); } while (page.at() !== first && seen.length < 400);
      // The walk starts beside the tab button; turned to start at the top, it is the page's own order.
      const top = seen.indexOf(expected[0]);
      assert.ok(top >= 0, `${tab}: the walk reached the first tab`);
      const walked = [...seen.slice(top), ...seen.slice(0, top)];
      assert.deepEqual(walked.map(page.name), expected.map(page.name), `${tab}: every control once, in page order`);
      // Up goes back the same way.
      page.input(UP);
      assert.equal(page.at(), seen[seen.length - 1]);
      // Next tab: RB on PrismaUI; right on the tab itself on Magelight, which has no shoulder buttons.
      if (host === 'Magelight') {
        const tabButton = page.document.querySelector('.sp-tab.is-active');
        for (let i = 0; i < 400 && page.at() !== tabButton; ++i) page.input(DOWN);
        assert.equal(page.at(), tabButton, 'the pad reached the open tab');
        page.input(RIGHT);
      } else {
        page.press(RB);
      }
    }
    assert.equal(page.activeTab(), 'general', 'the tabs come round again');
    if (host === 'PrismaUI') { page.press(LB); assert.equal(page.activeTab(), 'channels'); }
  });

  test(`${host}: a list opens on A, the d-pad moves in it, A picks and B closes it; closed, the d-pad moves past`, () => {
    const page = setup({host});
    page.openSettings();
    page.walkTo('sp-controller-combo');
    const combo = page.document.getElementById('sp-controller-combo');
    assert.equal(combo.tagName, 'BUTTON');
    assert.equal(combo.value, 'lb+y');
    const shown = () => combo.querySelector('.wf-choice-button__label').textContent;
    assert.equal(shown(), 'LB + Y', 'the button shows the choice');
    // Left / right never change a closed list.
    page.input(RIGHT); page.input(LEFT);
    assert.equal(combo.value, 'lb+y');
    assert.equal(page.listOpen(), false);
    // A opens it on the current choice; down moves; A picks the highlighted one.
    page.input(A);
    assert.equal(page.listOpen(), true);
    const menu = page.document.querySelector('.wf-select-menu');
    assert.deepEqual(menu.children.map(option => option.textContent), ['LB + Y', 'LB + X', 'RB + Y', 'RB + X', 'Off']);
    assert.equal(page.document.activeElement.textContent, 'LB + Y');
    page.input(DOWN); page.input(DOWN);
    assert.equal(page.document.activeElement.textContent, 'RB + Y');
    assert.equal(combo.value, 'lb+y', 'moving in the list changes nothing');
    page.input(A);
    assert.equal(page.listOpen(), false);
    assert.equal(combo.value, 'rb+y');
    assert.equal(shown(), 'RB + Y');
    assert.equal(page.at(), combo, 'the ring stays on the list');
    // B closes an open list with nothing changed, and leaves the panel open.
    page.input(A); page.input(DOWN); page.input(B);
    assert.equal(page.listOpen(), false);
    assert.equal(combo.value, 'rb+y');
    assert.equal(vm.runInContext('settingsOpen', page.context), true);
    page.input(DOWN);
    assert.notEqual(page.at(), combo, 'down moved on past the closed list');
    // A hotkey box is passed over without starting a key capture.
    page.walkTo('sp-config-hotkey');
    page.input(A);
    page.input(DOWN);
    assert.equal(page.document.getElementById('sp-config-hotkey').value, '0');
    assert.ok(!page.document.activeElement.classList.contains('sp-hotkey'), 'no hotkey box took the keyboard');
    // A slider moves by its own step and stays in its range.
    page.walkTo('sp-auto-hide');
    page.input(RIGHT); page.input(RIGHT);
    assert.equal(page.document.getElementById('sp-auto-hide').value, '2');
    assert.equal(page.document.getElementById('sp-auto-hide-val').value, '2', 'its number box follows');
    page.input(LEFT); page.input(LEFT); page.input(LEFT);
    assert.equal(page.document.getElementById('sp-auto-hide').value, '0');
    // B closes the panel and puts back what wasn't applied.
    page.input(B);
    assert.equal(page.config().general.controller_combo, 'lb+y');
  });

  test(`${host}: "Hide in conversations" is off by default, set with the pad, saved, and read back after a reload`, () => {
    // A settings file from 1.4.23 or older has no such key: nothing changes on update.
    const old = setup({host, settings: {general: {hide_in_menus: true}, window: {}, channels: {}}});
    assert.equal(old.config().general.hide_in_dialogue, false);
    old.openSettings();
    const box = old.document.getElementById('sp-hide-dialogue');
    assert.equal(box.checked, false);
    assert.equal(box.closest('.dv-setting-row').querySelector('b').textContent, 'Hide in conversations');

    // The pad ticks it; the ring sits on the switch, not the hidden checkbox.
    old.walkTo('sp-hide-dialogue');
    assert.ok(box.closest('.sp-toggle').classList.contains('dv-focused'));
    old.input(A);
    assert.equal(box.checked, true);
    assert.ok(box.closest('.sp-toggle').classList.contains('is-on'), 'the switch shows it');
    assert.equal(old.saves.length, 0, 'nothing is saved before Apply');
    old.walkTo('sp-apply');
    old.input(A);
    assert.equal(old.saves.length, 1, 'Apply saves once');
    const saved = old.saves[0];
    assert.equal(saved.general.hide_in_dialogue, true);
    assert.equal(saved.general.hide_in_menus, true, 'the other settings are kept');

    // The DLL hands the saved file back at the next load.
    const reloaded = setup({host, settings: saved});
    assert.equal(reloaded.config().general.hide_in_dialogue, true);
    reloaded.openSettings();
    assert.equal(reloaded.document.getElementById('sp-hide-dialogue').checked, true);

    // And off again.
    reloaded.walkTo('sp-hide-dialogue');
    reloaded.input(A);
    reloaded.walkTo('sp-apply');
    reloaded.input(A);
    assert.equal(reloaded.saves.at(-1).general.hide_in_dialogue, false);
    const again = setup({host, settings: reloaded.saves.at(-1)});
    again.openSettings();
    assert.equal(again.document.getElementById('sp-hide-dialogue').checked, false);
  });
}

test('every setting the pad can change, on every tab, survives a save and a reload', () => {
  const page = setup();
  page.openSettings();
  const doc = page.document;
  // Every field's value, by name, whichever tab it is on.
  const values = p => {
    const panel = p.document.getElementById('settings-panel');
    return Object.fromEntries([...panel.querySelectorAll('input'), ...panel.querySelectorAll('button[data-choice]')]
      .map(el => [p.name(el), el.type === 'checkbox' ? el.checked : el.value]));
  };
  const before = values(page);
  for (const tab of ['general', 'window', 'channels']) {
    assert.equal(page.activeTab(), tab);
    const tabs = doc.querySelectorAll('.sp-tab');
    for (const el of reachable(doc).filter(el => !tabs.includes(el) && !doc.getElementById('sp-footer').contains(el))) {
      const list = el.matches('[data-choice]');
      const adjustable = list || ['range', 'number', 'checkbox'].includes(el.type);
      // With a corner anchor the page reads the position from the window's layout,
      // which this test has none of: the anchor stays custom here and is checked below.
      if (!adjustable || el.id === 'sp-anchor') continue;
      for (let i = 0; i < 400 && page.at() !== el; ++i) page.input(DOWN);
      assert.equal(page.at(), el, `the pad reached ${page.name(el)}`);
      if (list) page.choose(1);
      else page.input(el.type === 'checkbox' ? A : RIGHT);
    }
    page.press(RB);
  }
  const changed = values(page);
  const different = Object.keys(changed).filter(key => changed[key] !== before[key]);
  assert.ok(different.length > 30, `the pad changed ${different.length} settings`);
  page.walkTo('sp-apply');
  page.input(A);
  const reloaded = setup({settings: page.saves.at(-1)});
  reloaded.openSettings();
  assert.deepEqual(values(reloaded), changed);
  // "All" can't be turned off (the log always has it): its switch is disabled, not one that resets.
  assert.equal(reloaded.document.querySelector('.sp-ch-row[data-channel=all] .ch-enabled').disabled, true);

  reloaded.press(RB);
  reloaded.walkTo('sp-anchor');
  reloaded.choose(1);
  const anchor = reloaded.document.getElementById('sp-anchor').value;
  assert.notEqual(anchor, 'custom');
  reloaded.walkTo('sp-apply');
  reloaded.input(A);
  const third = setup({settings: reloaded.saves.at(-1)});
  third.openSettings();
  assert.equal(third.document.getElementById('sp-anchor').value, anchor);
});
