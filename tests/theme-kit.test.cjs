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

// The Dovah suite theme kit: copied unchanged from DovahHub (61ddbbd) and
// pinned by its hash, linked before Chronicle's own stylesheet, shipped with
// its fonts, and never overridden: Chronicle's CSS reads the kit's --dv-*
// tokens but sets none, and the DovahHub tab has no colour of its own, so
// DovahHub's theme can recolour it.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const VIEW = path.join(ROOT, 'Data/PrismaUI/views/Chronicle');
const TAB = path.join(ROOT, 'Data/PrismaUI/views/DovahHub/modules/chronicle');
const KIT_SHA256 = 'ad38a0c64e9c32eb7e128f921fc71a327e9db5a96ce2169daf47ba2f37717814';
const FONTS = ['BarlowCondensed-Regular.ttf', 'BarlowCondensed-Medium.ttf', 'BarlowCondensed-SemiBold.ttf', 'OFL-BarlowCondensed.txt'];
const read = file => fs.readFileSync(file, 'utf8');
const stripComments = css => css.replace(/\/\*[\s\S]*?\*\//g, '');

test('the kit is the pinned copy, with its fonts', () => {
  const kit = fs.readFileSync(path.join(VIEW, 'theme/dovah-theme.css'));
  assert.equal(crypto.createHash('sha256').update(kit).digest('hex'), KIT_SHA256, 'dovah-theme.css drifted: take a new copy from DovahHub');
  FONTS.forEach(font => assert.ok(fs.existsSync(path.join(VIEW, 'theme/fonts', font)), font));
});

test('the page links the kit before its own stylesheet, and the package ships it', () => {
  const html = read(path.join(VIEW, 'index.html'));
  const kit = html.indexOf('href="theme/dovah-theme.css"');
  assert.ok(kit > 0 && kit < html.indexOf('href="styles.css"'));
  const pack = read(path.join(ROOT, 'package.ps1'));
  assert.match(pack, /theme\\dovah-theme\.css/);
  FONTS.forEach(font => assert.ok(pack.includes("'" + font + "'"), font));
});

test('Chronicle sets no --dv-* token', () => {
  for (const file of [path.join(VIEW, 'styles.css'), path.join(TAB, 'history.css')]) {
    assert.doesNotMatch(stripComments(read(file)), /--dv-[\w-]+\s*:/, path.basename(file));
  }
});

test('the DovahHub tab has no colour of its own', () => {
  const css = stripComments(read(path.join(TAB, 'history.css')));
  assert.doesNotMatch(css, /#[0-9a-f]{3,8}\b/i);
  assert.doesNotMatch(css, /rgba?\(\s*\d/);
  assert.doesNotMatch(css, /--(accent|ink|dim|muted|line)[\w-]*\b(?<!--dv-[\w-]*)/, 'DovahHub\'s own tokens: use the kit\'s --dv-* ones');
  const js = read(path.join(TAB, 'history.js'));
  assert.doesNotMatch(js, /hearth-|wf-loot/, 'DovahHub\'s own classes: use the kit\'s');
});

test('the settings window uses the kit, not the old controls', () => {
  const html = read(path.join(VIEW, 'index.html'));
  const settings = html.slice(html.indexOf('id="settings-overlay"'));
  assert.match(settings, /id="settings-panel" class="dv-panel dv-page"/);
  assert.doesNotMatch(settings, /class="sp-btn|class="sp-track|class="sp-row"/);
  ['general', 'window', 'channels'].forEach(tab => assert.match(settings, new RegExp('class="dv-tab sp-tab[^"]*"[^>]*data-sp="' + tab + '"')));
  const css = stripComments(read(path.join(VIEW, 'styles.css')));
  assert.doesNotMatch(css, /\.sp-btn|\.sp-track|#4a9eff|Segoe UI', Arial, var\(--font-cjk\)/);
});
