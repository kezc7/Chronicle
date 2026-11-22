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

// The real index.html with its scripts (page-dom.cjs), handshaken as native
// would, and every call the page makes to native recorded.

'use strict';

const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const {Document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver, Window} = require('./page-dom.cjs');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const html = fs.readFileSync(path.join(view, 'index.html'), 'utf8');
const sources = ['dropdowns.js', 'i18n.js', 'bridge.js', 'controls.js', 'pad.js', 'script.js']
  .map(name => [name, fs.readFileSync(path.join(view, name), 'utf8')]);
const CALLS = ['focusAcquired', 'focusReleased', 'uiReady', 'diagnostics', 'settingsChanged', 'playerInput', 'consoleCommand',
  'exportHistory', 'historyImport', 'historyCleared', 'selfTestReply', 'escapeLayer', 'padStyle', 'commandsQuery', 'addresseeQuery'];

function page({settings = {general: {language: 'en'}, window: {}, channels: {}}, lines = [], host = 'PrismaUI'} = {}) {
  const sent = [];
  const window = new Window();
  for (const name of CALLS) window[name] = value => sent.push([name, value]);
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
  context.nativeBootstrap({settings, defaults: null, warning: '', version: 'test', nativeHotkeys: true, gameLanguage: 'ENGLISH', host});
  context.setRuntime({loaded: true, profile: '42', character: 'Dragonborn', lineage: null, lines});
  context.setVisibility(true);
  const run = code => vm.runInContext(code, context);
  const key = (key, extra = {}) => (document.activeElement || document.body).dispatchEvent(
    new KeyboardEvent('keydown', {key, bubbles: true, cancelable: true, ...extra}));
  const open = action => { context.nativeHotkey(action); context.focusResult(true, 'focus.active'); };
  const calls = name => sent.filter(([call]) => call === name).map(([, value]) => value);
  const type = text => {
    const input = document.getElementById('chat-input');
    input.value = text;
    input.dispatchEvent(new Event('input', {bubbles: true}));
  };
  return {context, document, sent, run, key, open, calls, type, Event};
}

module.exports = {page};
