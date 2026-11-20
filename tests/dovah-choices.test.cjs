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

// DovahHub's list control (CHRON-29; the suite's rule of 17 Nov: no native
// <select> on any page). dropdowns.js and dropdowns.css are copied unchanged
// from DovahHub 8210744 (DHUB-135, shipped in DovahHub 0.99.12) and pinned by
// their SHA-256 with LF line endings. A newer copy is taken from a DovahHub
// commit the PO has pushed, and this pin moves with it, never to a working tree.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const VIEW = path.join(ROOT, 'Data/PrismaUI/views/Chronicle');
const PINS = {
  'dropdowns.js': '07aaa473d15fa0b91b8e6c6a5b51a5258ce27e9c066fc2ce78cc08a10a993f9e',
  'dropdowns.css': '2c83f00d93df5edba31973130fb0428cb79cde02482d1ce2d2b5872ccbdae92a',
};
const read = file => fs.readFileSync(file, 'utf8');

test('the list control is the pinned copy from DovahHub 8210744', () => {
  for (const [file, sha] of Object.entries(PINS)) {
    const bytes = fs.readFileSync(path.join(VIEW, file));
    assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), sha, `${file} drifted: take a new copy from a pushed DovahHub commit`);
  }
  const attributes = read(path.join(ROOT, '.gitattributes'));
  for (const file of Object.keys(PINS)) assert.match(attributes, new RegExp(`views/Chronicle/${file.replace('.', '\\.')} -text`));
});

test('the page has no native <select>; its lists are the control\'s buttons, loaded before the page scripts', () => {
  const html = read(path.join(VIEW, 'index.html'));
  assert.doesNotMatch(html, /<select\b/);
  for (const id of ['speaker-filter', 'sp-language', 'sp-controller-combo', 'sp-theme', 'sp-anchor', 'sp-speaker-colour-mode']) {
    assert.match(html, new RegExp(`<button type="button" id="${id}"[^>]* data-choice><span hidden data-choice-options>`), id);
  }
  assert.ok(html.indexOf('src="dropdowns.js"') < html.indexOf('src="script.js"'));
  const kit = html.indexOf('href="theme/dovah-theme.css"');
  const list = html.indexOf('href="dropdowns.css"');
  assert.ok(kit > 0 && kit < list && list < html.indexOf('href="styles.css"'));
  for (const file of ['script.js', 'controls.js', 'pad.js']) {
    assert.doesNotMatch(read(path.join(VIEW, file)), /createElement\(\s*['"]select['"]/, file);
  }
});

test('both packages ship it, and the old settings page is gone', () => {
  const pack = read(path.join(ROOT, 'package.ps1'));
  assert.match(pack, /'dropdowns\.js','dropdowns\.css'/);
  assert.equal(fs.existsSync(path.join(VIEW, 'settings.html')), false);
  assert.doesNotMatch(pack, /settings\.html/);
});

test('its list opens above the settings panel and takes the kit\'s colours', () => {
  const css = read(path.join(VIEW, 'styles.css'));
  const overlay = Number(/#settings-overlay \{[^}]*z-index: (\d+)/.exec(css)[1]);
  const menu = Number(/\.wf-select-menu \{ z-index: (\d+)/.exec(css)[1]);
  assert.ok(menu > overlay, `the list (${menu}) is above the settings overlay (${overlay})`);
  for (const token of ['accent-bright', 'accent-rgb', 'ink', 'ink-soft', 'muted', 'font-body']) {
    assert.ok(read(path.join(VIEW, 'dropdowns.css')).includes(`var(--${token})`), `dropdowns.css reads --${token}`);
  }
  const font = {'font-body': 'font'};
  for (const token of ['accent-bright', 'accent-rgb', 'ink', 'ink-soft', 'muted', 'font-body']) {
    assert.match(css, new RegExp(`--${token}: var\\(--dv-${font[token] || token}\\);`), `--${token} is the kit's`);
  }
});
