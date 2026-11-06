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

// Fails while any language has a missing, untranslated or half-English
// string, or while user-facing text bypasses the catalogues. The details are
// in the failure message; node tests/i18n-coverage.cjs --verbose prints them too.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {coverage, report, total, checkTable} = require('./i18n-coverage.cjs');

test('every user-facing string is translated in all nine languages', () => {
  const result = coverage();
  assert.equal(Object.keys(result.languages).length, 9);
  assert.ok(result.englishKeys > 150 && result.nativeKeys > 20 && result.hubKeys > 10, 'a catalogue failed to load');
  assert.equal(total(result), 0, '\n' + report(result, true));
});

test('the check finds missing keys, English copies, English words and broken placeholders', () => {
  const english = {
    'a': 'Load older', 'b': 'Search history', 'c': 'Saved to {detail}', 'd': 'Reached level {0}', 'e': 'Combat',
  };
  const french = {
    'b': 'Search history', 'c': 'Enregistré dans {chemin}', 'd': 'Niveau {0} atteint vanilla', 'e': 'Combat log',
  };
  const found = checkTable({...english, 'd': 'Reached vanilla level {0}'}, french, 'fr', () => false);
  assert.deepEqual(found.missing, ['a']);
  assert.deepEqual(found.identical, ['b']);
  assert.deepEqual(found.placeholders, ['c']);
  // "combat" is French too, so only "vanilla" counts as English left behind.
  assert.deepEqual(found.englishLeft, ['d (vanilla)']);
  const japanese = checkTable({'a': 'Load older'}, {'a': 'さらに load する'}, 'ja', () => false);
  assert.deepEqual(japanese.englishLeft, ['a (load)']);
});
