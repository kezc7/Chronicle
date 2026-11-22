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

// CHRON-47: the custom tabs' storage in Chronicle.json. The tab bar and the
// editor come with their mockup; this checks what is kept and what is dropped.

'use strict';

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

function page(settings) {
  const saves = [];
  const window = Object.assign(new Window(), {
    focusAcquired() {}, focusReleased() {}, uiReady() {}, diagnostics() {}, playerInput() {}, consoleCommand() {},
    exportHistory() {}, historyImport() {}, historyCleared() {}, settingsChanged: value => saves.push(JSON.parse(value)),
  });
  const document = new Document(html, window);
  const context = vm.createContext({
    window, document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver,
    console: {error() {}, warn() {}, log() {}}, navigator: {getGamepads: () => []},
    localStorage: {getItem: () => null, setItem() {}, removeItem() {}},
    fetch: () => Promise.reject(new Error('no config.json in tests')),
    getComputedStyle: () => ({getPropertyValue: () => ''}),
    requestAnimationFrame() {}, cancelAnimationFrame() {}, ResizeObserver: class { observe() {} },
    performance: {now: () => 0}, setTimeout() { return 0; }, clearTimeout() {}, setInterval() { return 0; }, clearInterval() {},
  });
  for (const [name, source] of sources) vm.runInContext(source, context, {filename: name});
  context.nativeBootstrap({settings, defaults: null, warning: '', version: 'test', nativeHotkeys: true, host: 'PrismaUI'});
  const tabs = () => JSON.parse(JSON.stringify(vm.runInContext('config.custom_tabs', context)));
  return {context, tabs, saves};
}

test('a file without custom tabs has none', () => {
  assert.deepEqual(page({general: {}, window: {}, channels: {}}).tabs(), []);
  assert.deepEqual(page(null).tabs(), []);
});

test('good tabs are kept, numbered in order, and their channels cleaned', () => {
  const {tabs} = page({general: {}, window: {}, channels: {}, custom_tabs: [
    {id: 'whatever', name: '  Story  ', channels: ['dialogue', 'skyrimnet', 'narrator', 'player', 'dialogue', 'nope', 'all']},
    {name: 'Quests', channels: ['notification']},
  ]});
  assert.deepEqual(tabs(), [
    {id: 'custom-1', name: 'Story', channels: ['dialogue', 'skyrimnet', 'narrator', 'player']},
    {id: 'custom-2', name: 'Quests', channels: ['notification']},
  ]);
});

test('at most two; a tab without a name or a channel is dropped; long names are cut', () => {
  const {tabs} = page({general: {}, window: {}, channels: {}, custom_tabs: [
    {name: '', channels: ['dialogue']},
    {name: 'Nothing', channels: ['all', 'unknown']},
    'not a tab',
    {name: 'A name far longer than twenty-four characters', channels: ['combat']},
    {name: 'Second', channels: ['system']},
    {name: 'Third', channels: ['console']},
  ]});
  assert.deepEqual(tabs().map(tab => tab.name), ['A name far longer than t', 'Second']);
  assert.deepEqual(page({general: {}, window: {}, channels: {}, custom_tabs: 'nope'}).tabs(), []);
});

test('a custom tab shows its own channels, only while each is on', () => {
  const {context} = page({general: {}, window: {}, channels: {combat: {enabled: false}},
    custom_tabs: [{name: 'Story', channels: ['dialogue', 'combat']}]});
  const shows = channel => vm.runInContext(`customTabShows(config.custom_tabs[0], ${JSON.stringify(channel)})`, context);
  assert.equal(shows('dialogue'), true);
  assert.equal(shows('combat'), false, 'combat is off');
  assert.equal(shows('system'), false, 'not in the tab');
});

test('the tabs survive a save and a reload', () => {
  const first = page({general: {}, window: {}, channels: {}, custom_tabs: [{name: 'Story', channels: ['narrator']}]});
  vm.runInContext('persistConfig()', first.context);
  const saved = first.saves.at(-1);
  assert.deepEqual(saved.custom_tabs, [{id: 'custom-1', name: 'Story', channels: ['narrator']}]);
  assert.deepEqual(page(saved).tabs(), saved.custom_tabs);
});

// The tab bar and the editor, from the approved mockup.
const harness = require('./page-harness.cjs');
const STORY = {general: {language: 'en'}, window: {}, channels: {narrator: {enabled: true}},
  custom_tabs: [{name: 'story', channels: ['dialogue', 'narrator']}]};

test('a custom tab sits after All, named in the page language, and shows only its channels', () => {
  const p = harness.page({settings: STORY, lines: [
    {channel: 'dialogue', speaker: 'Lydia', message: 'said', timestamp: 1763800000000, id: 1},
    {channel: 'system', speaker: '', message: 'Game saved', timestamp: 1763800001000, id: 2},
    {channel: 'narrator', speaker: 'Narrator', message: 'The wind rises.', timestamp: 1763800002000, id: 3}]});
  const tabs = Array.from(p.document.querySelectorAll('#channel-tabs .tab'), tab => tab.dataset.channel);
  assert.deepEqual(tabs.slice(0, 3), ['all', 'custom-1', 'dialogue']);
  assert.equal(p.document.querySelector('.tab[data-channel="custom-1"]').textContent, 'Story');
  p.document.querySelector('.tab[data-channel="custom-1"]').click();
  const shown = Array.from(p.run('messages'), entry => entry.el.classList.contains('hidden') ? null : entry.message).filter(Boolean);
  assert.deepEqual(shown, ['said', 'The wind rises.']);
  assert.equal(p.run('inputTarget'), 'dialogue', 'it sends on its first chat channel');
  assert.ok(p.run('padVisibleTabs()').includes('custom-1'), 'LB / RB step through it');
});

test('the editor adds, changes and removes tabs, and Apply saves them', () => {
  const p = harness.page({settings: STORY});
  p.open('settings');
  p.document.querySelector('.sp-tab[data-sp="channels"]').click();
  const rows = () => p.document.querySelectorAll('#sp-custom-tab-list .sp-tab-row');
  assert.equal(rows().length, 1);
  p.document.getElementById('sp-add-tab').click();
  assert.equal(rows().length, 2);
  assert.equal(p.document.getElementById('sp-add-tab').disabled, true, 'two at most');
  const chip = rows()[0].querySelector('.sp-tab-chip[data-channel="system"]');
  chip.click();
  assert.equal(chip.getAttribute('aria-pressed'), 'true');
  rows()[1].querySelector('.sp-tab-remove').click();
  assert.equal(rows().length, 1);
  p.document.getElementById('sp-apply').click();
  const saved = JSON.parse(p.calls('settingsChanged').at(-1));
  assert.deepEqual(saved.custom_tabs, [{id: 'custom-1', name: 'story', channels: ['dialogue', 'narrator', 'system']}]);
});

test('the last channel of a tab stays on', () => {
  const p = harness.page({settings: {...STORY, custom_tabs: [{name: 'quests', channels: ['notification']}]}});
  p.open('settings');
  const chip = p.document.querySelector('.sp-tab-chip[data-channel="notification"]');
  chip.click();
  assert.equal(chip.getAttribute('aria-pressed'), 'true');
});
