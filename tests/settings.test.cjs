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

const i18nSource = fs.readFileSync(path.join(__dirname, '../Data/PrismaUI/views/Chronicle/i18n.js'), 'utf8');
const source = fs.readFileSync(
  path.join(__dirname, '../Data/PrismaUI/views/Chronicle/controls.js'), 'utf8');

function deepMerge(target, source) {
  for (const key of Object.keys(source || {})) {
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      target[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
}

function setup() {
  const saved = new Map();
  const nodes = new Map();
  const node = () => ({
    textContent: '', value: '', hidden: false,
    classList: {add() {}, remove() {}, toggle() {}},
    replaceChildren() {}, blur() {}, focus() {}, select() {},
  });
  const defaults = {
    general: {interact_hotkey: 'F8', config_hotkey: 'N', persist_history: true, max_messages: 100},
    window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
  };
  const context = vm.createContext({
    config: deepMerge({}, defaults),
    DEFAULT_CONFIG: defaults,
    messages: [], inputHistory: [], inputHistoryIndex: -1,
    unreadCount: 0, userScrolledUp: false,
    document: {
      getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
      },
    },
    localStorage: {
      getItem: key => saved.has(key) ? saved.get(key) : null,
      setItem: (key, value) => saved.set(key, value),
      removeItem: key => saved.delete(key),
    },
    clearInterval() {}, clearTimeout() {}, setTimeout() {},
    console: {error() {}},
    deepMerge, normalizeConfig() {}, applyConfig() {}, refreshSpeakerOptions() {},
    restoreHistory() {}, showUnread() {}, sendToGame() { return false; },
    inputOpen: false, settingsOpen: false,
  });
  vm.runInContext(i18nSource, context);
  vm.runInContext(source, context);
  return {context, saved};
}

test('pending settings override stale native settings and survive a failed retry', () => {
  const {context, saved} = setup();
  const pending = {
    general: {hide_in_menus: false},
    window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
  };
  saved.set('chronicle_pending_settings', JSON.stringify(pending));
  const calls = [];
  context.sendToGame = (event, value) => {
    calls.push([event, value]);
    return true;
  };

  context.nativeBootstrap({
    settings: {
      general: {hide_in_menus: true},
      window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
    },
    warning: '',
    version: '1.2.0',
  });

  assert.equal(vm.runInContext('config.general.hide_in_menus', context), false);
  assert.equal(calls.length, 1);
  assert.equal(JSON.parse(calls[0][1]).general.hide_in_menus, false);

  context.settingsSaveResult(false, 'native save failed');
  assert.equal(saved.has('chronicle_pending_settings'), true);
  assert.match(context.document.getElementById('status-text').textContent, /native save failed/);

  context.nativeBootstrap({
    settings: {
      general: {hide_in_menus: true},
      window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
    },
    warning: '',
    version: '1.2.0',
  });
  context.settingsSaveResult(true, 'saved');
  assert.equal(saved.has('chronicle_pending_settings'), false);
});

test('rapid settings saves are serialized and retain the newest failed payload', () => {
  const {context, saved} = setup();
  const calls = [];
  context.sendToGame = (event, value) => {
    calls.push([event, value]);
    return true;
  };

  context.config = deepMerge({}, context.DEFAULT_CONFIG || {
    general: {}, window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
  });
  context.config.marker = 'A';
  context.persistConfig();
  context.config.marker = 'B';
  context.persistConfig();

  assert.equal(calls.length, 1);
  assert.equal(JSON.parse(calls[0][1]).marker, 'A');

  context.settingsSaveResult(true, 'A saved');
  assert.equal(calls.length, 2);
  assert.equal(JSON.parse(calls[1][1]).marker, 'B');
  assert.equal(JSON.parse(saved.get('chronicle_pending_settings')).marker, 'B');

  context.settingsSaveResult(false, 'B failed');
  assert.equal(JSON.parse(saved.get('chronicle_pending_settings')).marker, 'B');
  assert.match(context.document.getElementById('status-text').textContent, /B failed/);
});

test('a failed first save still dispatches the queued newest save', () => {
  const {context, saved} = setup();
  const calls = [];
  context.sendToGame = (event, value) => {
    calls.push([event, value]);
    return true;
  };

  context.config.marker = 'A';
  context.persistConfig();
  context.config.marker = 'B';
  context.persistConfig();
  context.settingsSaveResult(false, 'A failed');

  assert.equal(calls.length, 2);
  assert.equal(JSON.parse(calls[1][1]).marker, 'B');
  assert.equal(JSON.parse(saved.get('chronicle_pending_settings')).marker, 'B');

  context.settingsSaveResult(true, 'B saved');
  assert.equal(saved.has('chronicle_pending_settings'), false);
});

test('host-supplied first-run defaults apply without a settings file and lose to it', () => {
  const {context} = setup();
  const calls = [];
  context.sendToGame = (event, value) => { calls.push([event, value]); return true; };

  // Magelight: the page cannot fetch config.json, so the DLL sends it.
  context.nativeBootstrap({
    settings: null, defaults: {general: {hide_in_menus: false, max_messages: 250}},
    warning: '', version: '1.2.37', host: 'Magelight',
  });
  assert.equal(vm.runInContext('config.general.hide_in_menus', context), false);
  assert.equal(vm.runInContext('config.general.max_messages', context), 250);
  assert.equal(vm.runInContext('runtime.host', context), 'Magelight');
  // The first-run migration persists the merged defaults as the settings file.
  assert.equal(calls.length, 1);
  assert.equal(JSON.parse(calls[0][1]).general.max_messages, 250);

  // A saved settings file is authoritative over the bundled defaults.
  context.nativeBootstrap({
    settings: {general: {hide_in_menus: true}, window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: []},
    defaults: {general: {hide_in_menus: false, max_messages: 250}},
    warning: '', version: '1.2.37', host: 'Magelight',
  });
  assert.equal(vm.runInContext('config.general.hide_in_menus', context), true);
  assert.equal(vm.runInContext('config.general.max_messages', context), 100);

  // PrismaUI sends no defaults and no host name.
  context.nativeBootstrap({settings: null, defaults: null, warning: '', version: '1.2.37'});
  assert.equal(vm.runInContext('runtime.host', context), 'PrismaUI');
});
