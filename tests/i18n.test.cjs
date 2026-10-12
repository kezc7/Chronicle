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
const i18n = fs.readFileSync(path.join(view, 'i18n.js'), 'utf8');
const html = fs.readFileSync(path.join(view, 'index.html'), 'utf8');
const scripts = ['controls.js', 'script.js'].map(file => fs.readFileSync(path.join(view, file), 'utf8')).join('\n');

function load() {
  const context = vm.createContext({});
  vm.runInContext(i18n, context);
  return context;
}

const run = (context, code) => vm.runInContext(code, context);
const placeholders = text => [...text.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort().join(',');

test('every language has exactly the English keys, with the same placeholders', () => {
  const context = load();
  const strings = run(context, 'STRINGS');
  const english = strings.en;
  for (const language of run(context, 'LANGUAGES')) {
    const table = strings[language];
    assert.ok(table, `missing table for ${language}`);
    assert.deepEqual(Object.keys(table).sort(), Object.keys(english).sort(), `${language} keys differ from English`);
    for (const key of Object.keys(english)) {
      assert.ok(table[key].trim(), `${language} ${key} is empty`);
      assert.equal(placeholders(table[key]), placeholders(english[key]), `${language} ${key} placeholders differ`);
    }
  }
});

test('every key used by the page and scripts exists in English', () => {
  const english = run(load(), 'STRINGS.en');
  const used = new Set([
    ...[...html.matchAll(/data-i18n(?:-[a-z-]+)?="([^"]+)"/g)].map(m => m[1]),
    // Whole literal keys only; built keys like t('channel.' + id) are covered below.
    ...[...scripts.matchAll(/\bt\('([a-zA-Z0-9.-]+)'\s*[,)]/g)].map(m => m[1]),
    ...[...scripts.matchAll(/'(status\.[a-zA-Z]+)'/g)].map(m => m[1]),
  ]);
  for (const channel of ['all', 'dialogue', 'system', 'notification', 'skyrimnet', 'narrator', 'player', 'combat', 'console']) {
    used.add('channel.' + channel);
  }
  for (const key of used) assert.ok(key in english, `missing English text for ${key}`);
});

test('translation lookup falls back to English, then to the key itself', () => {
  const context = load();
  run(context, "uiLanguage = 'fr'");
  assert.equal(run(context, "t('sp.apply')"), 'Appliquer');
  assert.equal(run(context, "t('status.version', {version: '1.2.18', hint: 'x'})"), 'Chronicle 1.2.18 · x');
  run(context, "uiLanguage = 'xx'");
  assert.equal(run(context, "t('sp.apply')"), 'Apply');
  assert.equal(run(context, "t('Some finished sentence')"), 'Some finished sentence');
});

test('Auto follows the game language and unknown values fall back sensibly', () => {
  const context = load();
  assert.equal(run(context, "resolveLanguage('auto', 'FRENCH')"), 'fr');
  assert.equal(run(context, "resolveLanguage('auto', 'german')"), 'de');
  assert.equal(run(context, "resolveLanguage('auto', 'RUSSIAN')"), 'en');
  assert.equal(run(context, "resolveLanguage('auto', '')"), 'en');
  assert.equal(run(context, "resolveLanguage('de', 'FRENCH')"), 'de');
  assert.equal(run(context, "normalizeLanguage('fr')"), 'fr');
  assert.equal(run(context, "normalizeLanguage('klingon')"), 'auto');
  assert.equal(run(context, 'normalizeLanguage(undefined)'), 'auto');
});

test('every language except Auto is offered in settings', () => {
  for (const language of run(load(), 'LANGUAGES')) {
    assert.match(html, new RegExp(`<option value="${language}"[ >]`), `no settings option for ${language}`);
  }
});
