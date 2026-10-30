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

// Chronicle's History tab inside DovahHub: the lines Chronicle's chat window
// has shown for the current character. Talks to Chronicle's DLL only through
// window.dovahhubModule('chronicle'); game text goes in with textContent.
(function () {
  'use strict';

  const PAGE = 200;
  const MAX_LINES = 1000;
  const CHANNELS = [
    ['all', 'All'], ['dialogue', 'Dialogue'], ['skyrimnet', 'SkyrimNet'], ['narrator', 'Narrator'],
    ['player', 'Player'], ['system', 'System'], ['notification', 'Info'], ['combat', 'Combat'], ['console', 'Console'],
  ];
  // The chat window's default channel colours; the DLL sends the player's own.
  const COLOURS = {
    dialogue: '#ffffff', system: '#f5c518', notification: '#fde68a', skyrimnet: '#7ec8f5',
    narrator: '#c084fc', player: '#86efac', combat: '#fb923c', console: '#94a3b8',
  };
  // As in the chat window, these colour their whole line, the rest only the speaker.
  const WHOLE_LINE = ['system', 'notification'];

  function el(doc, tag, className, text) {
    const node = doc.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function sameLine(a, b) {
    return a.timestamp === b.timestamp && a.channel === b.channel && a.speaker === b.speaker && a.message === b.message;
  }

  function validLine(line) {
    return line && typeof line === 'object' && typeof line.message === 'string' && typeof line.channel === 'string' &&
      Object.prototype.hasOwnProperty.call(COLOURS, line.channel);
  }

  function formatTime(timestamp, now) {
    const date = new Date(Number(timestamp) || 0);
    const pad = value => String(value).padStart(2, '0');
    const time = pad(date.getHours()) + ':' + pad(date.getMinutes());
    if (date.toDateString() === new Date(now).toDateString()) return time;
    const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
    return date.getDate() + ' ' + months[date.getMonth()] + ' ' + time;
  }

  function mount(mod, doc, clock) {
    const now = clock || (() => Date.now());
    const root = mod.root.querySelector('.chronicle-history') || mod.root;
    root.replaceChildren();

    const tools = el(doc, 'div', 'chronicle-history__tools');
    const chips = el(doc, 'div', 'chronicle-history__chips');
    chips.setAttribute('role', 'group');
    chips.setAttribute('aria-label', 'Channels');
    const chipFor = {};
    CHANNELS.forEach(([channel, label]) => {
      const chip = el(doc, 'button', 'hearth-filter-chip' + (channel === 'all' ? ' is-selected' : ''), label);
      chip.type = 'button';
      chip.dataset.channel = channel;
      chip.addEventListener('click', () => setChannel(channel));
      chipFor[channel] = chip;
      chips.appendChild(chip);
    });
    const search = el(doc, 'label', 'hearth-search chronicle-history__search');
    const query = el(doc, 'input', 'chronicle-history__query');
    query.type = 'search';
    query.placeholder = 'Search history';
    query.setAttribute('aria-label', 'Search history');
    query.addEventListener('input', () => { state.search = String(query.value || '').trim().toLowerCase(); applyFilters(); });
    search.appendChild(query);
    tools.appendChild(chips);
    tools.appendChild(search);

    const older = el(doc, 'button', 'hearth-proof-button chronicle-history__older', 'Load older');
    older.type = 'button';
    older.hidden = true;
    older.addEventListener('click', () => loadOlder());
    const list = el(doc, 'div', 'chronicle-history__list');
    list.setAttribute('role', 'log');
    const empty = el(doc, 'p', 'hearth-detail-note chronicle-history__empty', 'Nothing yet for this character.');
    empty.hidden = true;
    root.appendChild(tools);
    root.appendChild(older);
    root.appendChild(list);
    root.appendChild(empty);

    const state = {
      lines: [],        // {line, row}, oldest first
      more: false,
      profile: '',
      channel: 'all',
      search: '',
      colours: Object.assign({}, COLOURS),
      shown: false,
      loading: 0,       // the load in flight; pushes wait for it
      buffered: [],
      olderPending: false,
    };

    function atBottom() {
      return list.scrollTop + list.clientHeight >= list.scrollHeight - 4;
    }

    function matches(line) {
      if (state.channel !== 'all' && line.channel !== state.channel) return false;
      if (!state.search) return true;
      return ((line.speaker || '') + ' ' + line.message).toLowerCase().includes(state.search);
    }

    function buildRow(line) {
      const row = el(doc, 'button', 'wf-loot-row chronicle-history__row');
      row.type = 'button';
      row.dataset.channel = line.channel;
      const colour = state.colours[line.channel] || COLOURS[line.channel];
      row.appendChild(el(doc, 'span', 'chronicle-history__time', formatTime(line.timestamp, now())));
      const speaker = el(doc, 'span', 'chronicle-history__speaker', line.speaker ? line.speaker + ':' : '');
      const message = el(doc, 'span', 'chronicle-history__message', line.message);
      if (colour) {
        speaker.style.color = colour;
        if (WHOLE_LINE.includes(line.channel)) message.style.color = colour;
      }
      row.appendChild(speaker);
      row.appendChild(message);
      row.hidden = !matches(line);
      return row;
    }

    function applyFilters() {
      const stick = atBottom();
      state.lines.forEach(entry => { entry.row.hidden = !matches(entry.line); });
      Object.keys(chipFor).forEach(channel => chipFor[channel].classList.toggle('is-selected', channel === state.channel));
      updateChrome();
      if (stick) list.scrollTop = list.scrollHeight;
    }

    function setChannel(channel) {
      state.channel = channel;
      applyFilters();
    }

    function updateChrome() {
      older.hidden = !state.more;
      older.disabled = state.olderPending;
      empty.hidden = state.lines.length > 0;
    }

    function trim() {
      while (state.lines.length > MAX_LINES) {
        state.lines.shift().row.remove();
        state.more = true;
      }
    }

    function append(line) {
      if (!validLine(line)) return;
      const stick = atBottom();
      const row = buildRow(line);
      state.lines.push({ line, row });
      list.appendChild(row);
      trim();
      updateChrome();
      if (stick) list.scrollTop = list.scrollHeight;
    }

    function applyColours(reply) {
      if (!reply.colours || typeof reply.colours !== 'object') return;
      Object.keys(COLOURS).forEach(channel => {
        const colour = reply.colours[channel];
        state.colours[channel] = typeof colour === 'string' && /^#[0-9a-f]{6}$/i.test(colour) ? colour : COLOURS[channel];
      });
    }

    function load() {
      const ticket = ++state.loading;
      state.buffered = [];
      return mod.call('history', { limit: PAGE }).then(reply => {
        if (ticket !== state.loading) return;  // a newer load or a reset since
        state.loading = 0;
        if (!reply || reply.error || !Array.isArray(reply.lines)) {
          empty.textContent = 'Chronicle’s history is not available.';
          state.lines.forEach(entry => entry.row.remove());
          state.lines = [];
          state.more = false;
          updateChrome();
          return;
        }
        empty.textContent = 'Nothing yet for this character.';
        state.profile = String(reply.profile || '');
        applyColours(reply);
        state.lines.forEach(entry => entry.row.remove());
        state.lines = [];
        const lines = reply.lines.filter(validLine);
        lines.forEach(line => {
          const row = buildRow(line);
          state.lines.push({ line, row });
          list.appendChild(row);
        });
        state.more = reply.more === true;
        // Lines pushed while the reply was on its way, unless the reply had them.
        const tail = lines.slice(-50);
        state.buffered.forEach(line => { if (!tail.some(seen => sameLine(seen, line))) append(line); });
        state.buffered = [];
        trim();
        updateChrome();
        list.scrollTop = list.scrollHeight;
      });
    }

    function loadOlder() {
      if (!state.more || state.olderPending || !state.lines.length) return Promise.resolve();
      state.olderPending = true;
      updateChrome();
      const profile = state.profile;
      const before = state.lines[0].line.timestamp;
      return mod.call('history', { before, limit: PAGE }).then(reply => {
        state.olderPending = false;
        if (!reply || reply.error || !Array.isArray(reply.lines) || String(reply.profile || '') !== profile ||
          !state.lines.length || state.lines[0].line.timestamp !== before) {
          updateChrome();
          return;
        }
        // Keep the rows on screen where they are while older ones go in above.
        const fromBottom = list.scrollHeight - list.scrollTop;
        const first = state.lines[0].row;
        const added = reply.lines.filter(validLine).map(line => ({ line, row: buildRow(line) }));
        added.forEach(entry => list.insertBefore(entry.row, first));
        state.lines = added.concat(state.lines).slice(-MAX_LINES);
        state.more = reply.more === true;
        updateChrome();
        list.scrollTop = list.scrollHeight - fromBottom;
      });
    }

    mod.onPush(json => {
      let data = json;
      if (typeof data === 'string') {
        try { data = JSON.parse(data); } catch (_) { return; }
      }
      if (!data || typeof data !== 'object') return;
      if (data.reset === true) {
        if (state.shown) load();
        else state.loading = 0;
        return;
      }
      if (!data.line) return;
      if (state.loading) state.buffered.push(data.line);
      else append(data.line);
    });

    mod.onEvent(name => {
      if (name === 'tabShown') {
        state.shown = true;
        load();
      } else if (name === 'tabHidden' || name === 'closed') {
        state.shown = false;
      }
    });

    return { state, load, loadOlder, setChannel, list, older, query, chips: chipFor };
  }

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = { mount, formatTime };
  } else if (typeof window !== 'undefined' && typeof window.dovahhubModule === 'function') {
    const mod = window.dovahhubModule('chronicle');
    if (mod) mount(mod, document);
  }
})();
