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
