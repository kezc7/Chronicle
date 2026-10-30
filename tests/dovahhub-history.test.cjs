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

// The History tab Chronicle adds to DovahHub, against a fake
// window.dovahhubModule('chronicle') and a minimal DOM.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const {mount} = require(path.join(__dirname, '../Data/PrismaUI/views/DovahHub/modules/chronicle/history.js'));

const ROW_HEIGHT = 20;

function element(tag) {
  const classes = new Set();
  const listeners = {};
  const node = {
    tagName: tag.toUpperCase(), children: [], parent: null, dataset: {}, style: {}, attributes: {},
    hidden: false, disabled: false, type: '', value: '', placeholder: '', _text: '', clientHeight: 100, scrollTop: 0,
    get className() { return [...classes].join(' '); },
    set className(value) { classes.clear(); String(value).split(/\s+/).filter(Boolean).forEach(name => classes.add(name)); },
    classList: {
      add: name => classes.add(name), remove: name => classes.delete(name), contains: name => classes.has(name),
      toggle: (name, on) => ((on ?? !classes.has(name)) ? classes.add(name) : classes.delete(name)),
    },
    get textContent() { return this.children.length ? this.children.map(child => child.textContent).join('') : this._text; },
    set textContent(value) { this.children = []; this._text = String(value); },
    get scrollHeight() { return this.children.length * ROW_HEIGHT; },
    setAttribute(name, value) { this.attributes[name] = String(value); },
    appendChild(child) { child.remove(); child.parent = this; this.children.push(child); return child; },
    insertBefore(child, before) {
      child.remove(); child.parent = this;
      const at = this.children.indexOf(before);
      this.children.splice(at < 0 ? this.children.length : at, 0, child);
      return child;
    },
    replaceChildren(...items) { this.children.forEach(child => { child.parent = null; }); this.children = []; items.forEach(item => this.appendChild(item)); },
    remove() { if (this.parent) { this.parent.children.splice(this.parent.children.indexOf(this), 1); this.parent = null; } },
    addEventListener(type, fn) { (listeners[type] ||= []).push(fn); },
    dispatch(type) { (listeners[type] || []).forEach(fn => fn({})); },
    querySelector(selector) {
      const name = selector.replace(/^\./, '');
      for (const child of this.children) {
        if (child.classList.contains(name)) return child;
        const found = child.querySelector(selector);
        if (found) return found;
      }
      return null;
    },
  };
  return node;
}

const doc = {createElement: element};

function line(timestamp, channel, speaker, message) {
  return {timestamp, channel, speaker, message};
}

// A fake module: calls are answered from `store` (oldest first), as Chronicle's DLL pages it.
function setup(store, options = {}) {
  const root = element('div');
  const container = element('div');
  container.className = 'chronicle-history';
  root.appendChild(container);
  const calls = [];
  const pushes = [];
  const events = [];
  let hold = null;
  const mod = {
    root,
    call(request, payload) {
      calls.push({request, payload});
      const answer = () => {
        const end = payload.before === undefined ? store.length : store.filter(entry => entry.timestamp < payload.before).length;
        const start = Math.max(0, end - (payload.limit || 200));
        return {profile: options.profile || '42', lines: store.slice(start, end), more: start > 0, colours: options.colours || {}};
      };
      if (options.holdNext) {
        options.holdNext = false;
        return new Promise(resolve => { hold = () => resolve(answer()); });
      }
      return Promise.resolve(answer());
    },
    onPush: fn => pushes.push(fn),
    onEvent: fn => events.push(fn),
  };
  const page = mount(mod, doc, () => Date.UTC(2026, 9, 30, 12));
  return {
    page, calls, list: page.list,
    push: json => pushes.forEach(fn => fn(json)),
    event: name => events.forEach(fn => fn(name)),
    release: () => hold && hold(),
    texts: () => page.list.children.filter(row => !row.hidden).map(row => row.children[2].textContent),
  };
}

const settle = () => new Promise(resolve => setImmediate(resolve));

test('showing the tab loads and renders the history, newest at the bottom', async () => {
  const store = [line(1, 'dialogue', 'Lydia', 'I am sworn to carry your burdens.'), line(2, 'system', '', 'Game saved'),
    line(3, 'dialogue', 'Hadvar', '<b>Not markup</b>')];
  const tab = setup(store, {colours: {dialogue: '#123456'}});
  tab.event('tabShown');
  await settle();
  assert.deepEqual(tab.calls[0], {request: 'history', payload: {limit: 200}});
  assert.deepEqual(tab.texts(), ['I am sworn to carry your burdens.', 'Game saved', '<b>Not markup</b>']);
  const lydia = tab.list.children[0];
  assert.equal(lydia.children[1].textContent, 'Lydia:');
  assert.equal(lydia.children[1].style.color, '#123456');
  assert.equal(tab.list.children[1].children[2].style.color, '#f5c518'); // system lines are coloured whole
  assert.equal(tab.list.children[0].classList.contains('wf-loot-row'), true);
  assert.equal(tab.list.scrollTop, tab.list.scrollHeight);
  assert.equal(tab.page.older.hidden, true);
});

test('channel chips and the search box filter the rows', async () => {
  const tab = setup([line(1, 'dialogue', 'Lydia', 'Hello'), line(2, 'combat', 'Combat', 'Bandit hit'),
    line(3, 'dialogue', 'Serana', 'Farewell')]);
  tab.event('tabShown');
  await settle();
  tab.page.chips.dialogue.dispatch('click');
  assert.deepEqual(tab.texts(), ['Hello', 'Farewell']);
  assert.equal(tab.page.chips.dialogue.classList.contains('is-selected'), true);
  assert.equal(tab.page.chips.all.classList.contains('is-selected'), false);
  tab.page.query.value = 'SERANA';
  tab.page.query.dispatch('input');
  assert.deepEqual(tab.texts(), ['Farewell']);
  tab.page.chips.all.dispatch('click');
  tab.page.query.value = 'bandit';
  tab.page.query.dispatch('input');
  assert.deepEqual(tab.texts(), ['Bandit hit']);
});

test('Load older pages back with "before" and keeps the view in place', async () => {
  const store = Array.from({length: 450}, (_, i) => line(i + 1, 'dialogue', 'Lydia', 'Line ' + (i + 1)));
  const tab = setup(store);
  tab.event('tabShown');
  await settle();
  assert.equal(tab.list.children.length, 200);
  assert.equal(tab.page.older.hidden, false);
  tab.list.scrollTop = 0; // reading the oldest loaded line
  tab.page.older.dispatch('click');
  await settle();
  assert.deepEqual(tab.calls[1], {request: 'history', payload: {before: 251, limit: 200}});
  assert.equal(tab.list.children.length, 400);
  assert.equal(tab.list.children[0].children[2].textContent, 'Line 51');
  assert.equal(tab.list.scrollTop, 200 * ROW_HEIGHT); // the same row is still at the top
  tab.page.older.dispatch('click');
  await settle();
  assert.equal(tab.list.children.length, 450);
  assert.equal(tab.page.older.hidden, true);
});

test('pushed lines are added, and follow the bottom only when it was in view', async () => {
  const store = Array.from({length: 10}, (_, i) => line(i + 1, 'dialogue', 'Lydia', 'Line ' + (i + 1)));
  const tab = setup(store);
  tab.event('tabShown');
  await settle();
  tab.push({line: line(11, 'dialogue', 'Lydia', 'Line 11')});
  assert.equal(tab.list.children.length, 11);
  assert.equal(tab.list.scrollTop, tab.list.scrollHeight);
  tab.list.scrollTop = 0;
  tab.push(JSON.stringify({line: line(12, 'skyrimnet', 'Serana', 'Line 12')}));
  assert.equal(tab.list.children.length, 12);
  assert.equal(tab.list.scrollTop, 0);
  tab.push({line: {channel: 'nonsense', message: 'dropped'}});
  assert.equal(tab.list.children.length, 12);
});

test('lines pushed during a load are kept once', async () => {
  const store = [line(1, 'dialogue', 'Lydia', 'One'), line(2, 'dialogue', 'Lydia', 'Two')];
  const tab = setup(store, {holdNext: true});
  tab.event('tabShown');
  tab.push({line: line(2, 'dialogue', 'Lydia', 'Two')});  // already in the reply
  tab.push({line: line(3, 'dialogue', 'Lydia', 'Three')}); // newer than the reply
  tab.release();
  await settle();
  assert.deepEqual(tab.texts(), ['One', 'Two', 'Three']);
});

test('a reset reloads the list for the new character', async () => {
  const store = [line(1, 'dialogue', 'Lydia', 'First character')];
  const tab = setup(store);
  tab.event('tabShown');
  await settle();
  store.splice(0, store.length, line(5, 'dialogue', 'Serana', 'Second character'));
  tab.push({reset: true});
  await settle();
  assert.deepEqual(tab.texts(), ['Second character']);
  store.length = 0;
  tab.push({reset: true});
  await settle();
  assert.equal(tab.list.children.length, 0);
  assert.equal(tab.page.state.lines.length, 0);
  tab.event('tabHidden');
  tab.push({reset: true});
  await settle();
  assert.equal(tab.calls.length, 3); // no reload while hidden; the next tabShown loads
});
