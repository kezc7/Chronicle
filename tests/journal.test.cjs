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

// CHRON-48 from the approved mockups: journal dividers (off unless turned on),
// the speaker click, and the Markdown story written beside the export.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {page} = require('./page-harness.cjs');

let clock = 1763800000000;
let nextId = 0;
const line = (channel, speaker, message, gameDays, gameHour, place, gap = 9000) =>
  ({channel, speaker, message, timestamp: (clock += gap), id: ++nextId, gameDays, gameHour, place});
const LINES = [
  line('dialogue', 'Lydia', 'I am sworn to carry your burdens.', 13.4, 9.5, 'Whiterun'),
  line('notification', '', 'Quest started: Dragon Rising', 13.4, 9.6, 'Whiterun'),
  line('dialogue', 'Alvor', 'Hello there.', 13.8, 18.5, 'Riverwood'),
  line('dialogue', 'Lydia', 'We should rest.', 14.1, 6.2, 'Riverwood'),
];
const ON = {general: {language: 'en', journal_dividers: true}, window: {}, channels: {}};
const dividers = p => Array.from(p.document.getElementById('message-list').querySelectorAll('.journal-divider'), d => d.textContent);

test('journal dividers are off unless turned on', () => {
  const p = page({lines: LINES});
  assert.deepEqual(dividers(p), []);
  assert.equal(p.run('config.general.journal_dividers'), false);
});

test('a divider at each new place or game day, with its time', () => {
  const p = page({settings: ON, lines: LINES});
  assert.deepEqual(dividers(p), ['Whiterun · Day 14, 09:30', 'Riverwood · Day 14, 18:30', 'Riverwood · Day 15, 06:12']);
  // A live line in a new place gets its own; one in the same place doesn't.
  p.context.addMessage(JSON.stringify(line('dialogue', 'Delphine', 'Close the door.', 14.2, 9.0, 'Sleeping Giant Inn')));
  p.context.addMessage(JSON.stringify(line('dialogue', 'Orgnar', 'Ale?', 14.2, 9.1, 'Sleeping Giant Inn')));
  assert.equal(dividers(p).length, 4);
  assert.equal(dividers(p)[3], 'Sleeping Giant Inn · Day 15, 09:00');
  // Turned off in Settings and applied: they go.
  p.open('settings');
  p.document.getElementById('sp-journal').click();
  p.document.getElementById('sp-apply').click();
  assert.deepEqual(dividers(p), []);
});

test('a burst never runs across a divider', () => {
  const p = page({settings: ON, lines: []});
  p.context.addMessage(JSON.stringify(line('notification', '', 'Iron Ingot added', 13.8, 18.5, 'Riverwood', 100)));
  p.context.addMessage(JSON.stringify(line('notification', '', 'Iron Ingot added', 13.8, 18.5, 'Riverwood', 100)));
  p.context.addMessage(JSON.stringify(line('notification', '', 'Iron Ingot added', 13.8, 18.6, 'Riverwood Trader', 100)));
  const shown = Array.from(p.run('messages'), entry => entry.count);
  assert.deepEqual(shown, [2, 1]);
});

const speaker = (p, name) => Array.from(p.document.getElementById('message-list').querySelectorAll('.speaker'))
  .filter(el => el.textContent.startsWith(name)).pop();

test('clicking a speaker shows only them, and again shows everyone', () => {
  const p = page({lines: LINES});
  p.open('interact');
  speaker(p, 'Lydia').dispatchEvent(new p.Event('click', {bubbles: true}));
  assert.equal(p.run('speakerFilter'), 'Lydia');
  speaker(p, 'Lydia').dispatchEvent(new p.Event('click', {bubbles: true}));
  assert.equal(p.run('speakerFilter'), '');
});

test('Shift+click on someone who speaks opens the chat bar addressed to them', () => {
  const p = page({lines: LINES});
  p.open('interact');
  const click = new p.Event('click', {bubbles: true});
  click.shiftKey = true;
  speaker(p, 'Alvor').dispatchEvent(click);
  assert.equal(p.run('inputOpen'), true);
  assert.equal(p.document.getElementById('chat-input').value, 'Alvor, ');
});

test('hovering a speaker shows what a click and a Shift+click do', () => {
  const p = page({lines: LINES});
  p.open('interact');
  speaker(p, 'Lydia').dispatchEvent(new p.Event('mouseover', {bubbles: true}));
  const tip = p.document.getElementById('speaker-tip');
  assert.equal(tip.hidden, false);
  assert.match(tip.textContent, /Show only Lydia/);
  assert.match(tip.textContent, /Talk to Lydia/);
  p.document.getElementById('message-list').dispatchEvent(new p.Event('mouseout', {bubbles: true}));
  assert.equal(tip.hidden, true);
});

test('natively: live lines carry their place and game time, and Export writes the story beside the text', () => {
  const chronicle = fs.readFileSync(path.join(__dirname, '../src/Chronicle.cpp'), 'utf8');
  assert.match(chronicle, /withId\["place"\] = line\.place;/);
  assert.match(chronicle, /withId\["gameDays"\] = line\.gameDays;/);
  const exportBody = chronicle.slice(chronicle.indexOf('void Chronicle::ExportHistory'));
  assert.match(exportBody, /story\.replace_extension\("\.md"\);[\s\S]{0,300}ChronicleHistory::MarkdownStory\(lines, PlayerDisplayName\(\)\)/);
});
