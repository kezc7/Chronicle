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

// The chat bar from the approved mockups: CHRON-45 ("/" offers the commands;
// the Console target looks different) and CHRON-46 (who a line goes to, and
// picking someone else).

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {page} = require('./page-harness.cjs');

const rows = p => Array.from(p.document.getElementById('input-complete').querySelectorAll('.input-complete__row'),
  row => row.querySelector('b').textContent);
const listShown = p => !p.document.getElementById('input-complete').hidden;

test('CHRON-45: "/" lists the commands with their help, and asks native for the mods\' ones once', () => {
  const p = page();
  p.open('input');
  p.type('/');
  assert.deepEqual(rows(p), ['/help', '/selftest']);
  assert.equal(p.calls('commandsQuery').length, 1);
  p.context.setCommands([{prefix: 'dworks', help: 'dump | resort | status'}]);
  assert.deepEqual(rows(p), ['/help', '/selftest', '/dworks']);
  p.type('/d');
  assert.deepEqual(rows(p), ['/dworks']);
  assert.match(p.document.getElementById('input-complete').textContent, /dump \| resort \| status/);
  assert.equal(p.calls('commandsQuery').length, 1, 'asked once per opening');
  p.type('hello');
  assert.equal(listShown(p), false, 'only a line starting with / gets the list');
});

test('CHRON-45: up and down move in the list, Tab completes, Escape closes only the list', () => {
  const p = page();
  p.open('input');
  p.type('/');
  p.key('ArrowDown');
  assert.equal(p.document.querySelector('.input-complete__row.is-selected b').textContent, '/selftest');
  p.key('Tab');
  assert.equal(p.document.getElementById('chat-input').value, '/selftest ');
  assert.equal(listShown(p), false);
  p.type('/');
  p.key('Escape');
  assert.equal(listShown(p), false, 'the list closed');
  assert.equal(p.run('inputOpen'), true, 'the chat bar stays');
});

test('CHRON-45: on the pad, up and down move in the list and A completes', () => {
  const p = page();
  p.open('input');
  p.type('/');
  p.run("padFire('down'); padFire('accept')");
  assert.equal(p.document.getElementById('chat-input').value, '/selftest ');
  assert.equal(p.run('inputOpen'), true, 'A completed, it did not send');
});

test('CHRON-45: the Console target has its own look and no command list', () => {
  const p = page({settings: {general: {language: 'en'}, window: {}, channels: {console: {enabled: true}}}});
  p.open('input');
  p.run("setInputTarget('console')");
  const bar = p.document.getElementById('input-bar');
  assert.equal(bar.classList.contains('console-target'), true);
  assert.equal(p.document.getElementById('input-console-badge').hidden, false);
  p.type('/');
  assert.equal(listShown(p), false, 'a console command is not one of Chronicle\'s');
  p.run("setInputTarget('all')");
  assert.equal(bar.classList.contains('console-target'), false);
  assert.equal(p.document.getElementById('input-console-badge').hidden, true);
});

const NEARBY = {loaded: true, auto: {id: 1, name: 'Lydia', why: 'looking'},
  nearby: [{id: 1, name: 'Lydia'}, {id: 2, name: 'Faendal'}, {id: 3, name: 'Sigrid'}]};
const pill = p => p.document.getElementById('input-addressee');

test('CHRON-46: the bar shows who the line goes to, as native chose', () => {
  const p = page();
  p.open('input');
  assert.equal(p.calls('addresseeQuery').length, 1, 'asked when the bar opens');
  assert.equal(pill(p).hidden, true, 'nothing before native answers');
  p.context.setAddressee(NEARBY);
  assert.equal(pill(p).hidden, false);
  assert.match(pill(p).textContent, /→ Lydia\s*looking at/);
  p.context.setAddressee({...NEARBY, auto: {id: 2, name: 'Faendal', why: 'conversation'}});
  assert.match(pill(p).textContent, /Faendal\s*in this conversation/);
  p.context.setAddressee({loaded: true, auto: {why: 'nearby'}, nearby: []});
  assert.match(pill(p).textContent, /Nearby\s*everyone in earshot/);
  p.context.setAddressee({loaded: false, nearby: []});
  assert.equal(pill(p).hidden, true, 'no pill without SkyrimNet');
});

test('CHRON-46: Ctrl+arrows and Y pick someone else, and the line is sent to them', () => {
  const p = page();
  p.open('input');
  p.context.setAddressee(NEARBY);
  p.key('ArrowRight', {ctrlKey: true});
  assert.match(pill(p).textContent, /Faendal\s*picked/, 'Lydia is the automatic choice, so the list goes on to Faendal');
  p.run("padFire('tertiary')");
  assert.match(pill(p).textContent, /Sigrid\s*picked/);
  p.type('Is the road safe?');
  p.key('Enter');
  assert.deepEqual(JSON.parse(p.calls('playerInput')[0]), {text: 'Is the road safe?', channel: 'all', to: 3});

  p.open('input');
  p.context.setAddressee(NEARBY);
  p.key('ArrowLeft', {ctrlKey: true});
  assert.match(pill(p).textContent, /Nearby/, 'back from the automatic choice is everyone nearby');
  p.type('Hello all');
  p.key('Enter');
  assert.deepEqual(JSON.parse(p.calls('playerInput')[1]), {text: 'Hello all', channel: 'all', to: 'nearby'});

  p.open('input');
  p.context.setAddressee(NEARBY);
  p.type('Hello');
  p.key('Enter');
  assert.deepEqual(JSON.parse(p.calls('playerInput')[2]), {text: 'Hello', channel: 'all'}, 'no pick: Chronicle chooses');
});

test('CHRON-46: the Console target has no pill', () => {
  const p = page({settings: {general: {language: 'en'}, window: {}, channels: {console: {enabled: true}}}});
  p.open('input');
  p.context.setAddressee(NEARBY);
  p.run("setInputTarget('console')");
  assert.equal(pill(p).hidden, true);
});

test('CHRON-45 and CHRON-46 natively: the answers come from native data, and the pick goes first', () => {
  const src = name => fs.readFileSync(path.join(__dirname, '../src', name), 'utf8');
  const chronicle = src('Chronicle.cpp');
  assert.match(chronicle, /RegisterListener\("commandsQuery"[\s\S]{0,200}setCommands\(" \+ ChronicleApi::CommandsJson\(\)/);
  assert.match(chronicle, /RegisterListener\("addresseeQuery"[\s\S]{0,200}setAddressee\(" \+ ChronicleSkyrimNet::PreviewJson\(\)/);
  assert.match(chronicle, /it->get<std::string>\(\) == "nearby"\) pick\.nearby = true;/);
  const net = src('SkyrimNet.cpp');
  const send = net.slice(net.indexOf('void Send('));
  assert.ok(send.indexOf('a_pick.nearby') < send.indexOf('ConversationPartner()'), 'the pick comes before Chronicle\'s choice');
  const preview = net.slice(net.indexOf('std::string PreviewJson()'), net.indexOf('bool Loaded()'));
  assert.doesNotMatch(preview, /DispatchStaticCall/, 'the preview never waits on Papyrus');
  assert.match(net, /ForEachHighActor/);
  const bridge = fs.readFileSync(path.join(__dirname, '../Data/PrismaUI/views/Chronicle/bridge.js'), 'utf8');
  assert.match(bridge, /'commandsQuery', 'addresseeQuery'/);
});
