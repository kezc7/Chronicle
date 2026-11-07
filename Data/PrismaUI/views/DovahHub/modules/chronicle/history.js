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
  const CHANNELS = ['all', 'dialogue', 'skyrimnet', 'narrator', 'player', 'system', 'notification', 'combat', 'console'];
  // The page's own text, in the languages of Chronicle's chat window (i18n.js),
  // which use the same channel names. The DLL says which one Chronicle shows.
  const STRINGS = {
    "en": {
      "channel.all": "All",
      "channel.dialogue": "Dialogue",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Narrator",
      "channel.player": "Player",
      "channel.system": "System",
      "channel.notification": "Info",
      "channel.combat": "Combat",
      "channel.console": "Console",
      "channels": "Channels",
      "search": "Search history",
      "older": "Load older",
      "empty": "Nothing yet for this character.",
      "unavailable": "Chronicle’s history is not available.",
      "months": "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec",
      "date": "{day} {month} {time}",
    },
    "fr": {
      "channel.all": "Tout",
      "channel.dialogue": "Dialogues",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Narrateur",
      "channel.player": "Joueur",
      "channel.system": "Système",
      "channel.notification": "Infos",
      "channel.combat": "Combat",
      "channel.console": "Console",
      "channels": "Canaux",
      "search": "Rechercher dans l'historique",
      "older": "Charger les plus anciens",
      "empty": "Rien pour l'instant pour ce personnage.",
      "unavailable": "L'historique de Chronicle n'est pas disponible.",
      "months": "janv. févr. mars avr. mai juin juil. août sept. oct. nov. déc.",
      "date": "{day} {month} {time}",
    },
    "de": {
      "channel.all": "Alle",
      "channel.dialogue": "Dialoge",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Erzähler",
      "channel.player": "Spieler",
      "channel.system": "System",
      "channel.notification": "Meldungen",
      "channel.combat": "Kampf",
      "channel.console": "Konsole",
      "channels": "Kanäle",
      "search": "Verlauf durchsuchen",
      "older": "Ältere laden",
      "empty": "Für diesen Charakter noch nichts.",
      "unavailable": "Der Verlauf von Chronicle ist nicht verfügbar.",
      "months": "Jan. Feb. März Apr. Mai Juni Juli Aug. Sept. Okt. Nov. Dez.",
      "date": "{day}. {month} {time}",
    },
    "it": {
      "channel.all": "Tutti",
      "channel.dialogue": "Dialoghi",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Narratore",
      "channel.player": "Giocatore",
      "channel.system": "Sistema",
      "channel.notification": "Notifiche",
      "channel.combat": "Combattimento",
      "channel.console": "Console",
      "channels": "Canali",
      "search": "Cerca nella cronologia",
      "older": "Carica precedenti",
      "empty": "Ancora niente per questo personaggio.",
      "unavailable": "La cronologia di Chronicle non è disponibile.",
      "months": "gen feb mar apr mag giu lug ago set ott nov dic",
      "date": "{day} {month} {time}",
    },
    "es": {
      "channel.all": "Todo",
      "channel.dialogue": "Diálogos",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Narrador",
      "channel.player": "Jugador",
      "channel.system": "Sistema",
      "channel.notification": "Avisos",
      "channel.combat": "Combate",
      "channel.console": "Consola",
      "channels": "Canales",
      "search": "Buscar en el historial",
      "older": "Cargar anteriores",
      "empty": "Todavía no hay nada para este personaje.",
      "unavailable": "El historial de Chronicle no está disponible.",
      "months": "ene feb mar abr may jun jul ago sept oct nov dic",
      "date": "{day} {month} {time}",
    },
    "pl": {
      "channel.all": "Wszystko",
      "channel.dialogue": "Dialogi",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Narrator",
      "channel.player": "Gracz",
      "channel.system": "System",
      "channel.notification": "Powiadomienia",
      "channel.combat": "Walka",
      "channel.console": "Konsola",
      "channels": "Kanały",
      "search": "Szukaj w historii",
      "older": "Wczytaj starsze",
      "empty": "Na razie nic dla tej postaci.",
      "unavailable": "Historia Chronicle jest niedostępna.",
      "months": "sty lut mar kwi maj cze lip sie wrz paź lis gru",
      "date": "{day} {month} {time}",
    },
    "tr": {
      "channel.all": "Tümü",
      "channel.dialogue": "Diyaloglar",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "Anlatıcı",
      "channel.player": "Oyuncu",
      "channel.system": "Sistem",
      "channel.notification": "Bildirimler",
      "channel.combat": "Savaş",
      "channel.console": "Konsol",
      "channels": "Kanallar",
      "search": "Geçmişte ara",
      "older": "Daha eskileri yükle",
      "empty": "Bu karakter için henüz bir şey yok.",
      "unavailable": "Chronicle geçmişi kullanılamıyor.",
      "months": "Oca Şub Mar Nis May Haz Tem Ağu Eyl Eki Kas Ara",
      "date": "{day} {month} {time}",
    },
    "zh-CN": {
      "channel.all": "全部",
      "channel.dialogue": "对话",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "旁白",
      "channel.player": "玩家",
      "channel.system": "系统",
      "channel.notification": "通知",
      "channel.combat": "战斗",
      "channel.console": "控制台",
      "channels": "频道",
      "search": "搜索记录",
      "older": "加载更早的记录",
      "empty": "此角色暂无记录。",
      "unavailable": "无法获取 Chronicle 的记录。",
      "months": "1月 2月 3月 4月 5月 6月 7月 8月 9月 10月 11月 12月",
      "date": "{month}{day}日 {time}",
    },
    "ja": {
      "channel.all": "すべて",
      "channel.dialogue": "会話",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "ナレーター",
      "channel.player": "プレイヤー",
      "channel.system": "システム",
      "channel.notification": "通知",
      "channel.combat": "戦闘",
      "channel.console": "コンソール",
      "channels": "チャンネル",
      "search": "履歴を検索",
      "older": "さらに古い履歴を読み込む",
      "empty": "このキャラクターの履歴はまだありません。",
      "unavailable": "Chronicle の履歴を取得できません。",
      "months": "1月 2月 3月 4月 5月 6月 7月 8月 9月 10月 11月 12月",
      "date": "{month}{day}日 {time}",
    },
    "ko": {
      "channel.all": "전체",
      "channel.dialogue": "대화",
      "channel.skyrimnet": "SkyrimNet",
      "channel.narrator": "내레이터",
      "channel.player": "플레이어",
      "channel.system": "시스템",
      "channel.notification": "알림",
      "channel.combat": "전투",
      "channel.console": "콘솔",
      "channels": "채널",
      "search": "기록 검색",
      "older": "이전 기록 불러오기",
      "empty": "이 캐릭터의 기록이 아직 없습니다.",
      "unavailable": "Chronicle 기록을 불러올 수 없습니다.",
      "months": "1월 2월 3월 4월 5월 6월 7월 8월 9월 10월 11월 12월",
      "date": "{month} {day}일 {time}",
    },
  };

  function language(code) {
    return Object.prototype.hasOwnProperty.call(STRINGS, code) ? code : 'en';
  }

  function text(lang, key) {
    const table = STRINGS[language(lang)];
    return Object.prototype.hasOwnProperty.call(table, key) ? table[key] : STRINGS.en[key];
  }
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

  function formatTime(timestamp, now, lang) {
    const date = new Date(Number(timestamp) || 0);
    const pad = value => String(value).padStart(2, '0');
    const time = pad(date.getHours()) + ':' + pad(date.getMinutes());
    if (date.toDateString() === new Date(now).toDateString()) return time;
    const month = text(lang, 'months').split(' ')[date.getMonth()];
    return text(lang, 'date').replace('{day}', date.getDate()).replace('{month}', month).replace('{time}', time);
  }

  function mount(mod, doc, clock) {
    const now = clock || (() => Date.now());
    // Until the first reply names Chronicle's language, follow the hub page's.
    let lang = language(doc.documentElement && doc.documentElement.lang);
    const root = mod.root.querySelector('.chronicle-history') || mod.root;
    root.replaceChildren();

    const tools = el(doc, 'div', 'chronicle-history__tools');
    const chips = el(doc, 'div', 'chronicle-history__chips');
    chips.setAttribute('role', 'group');
    const chipFor = {};
    CHANNELS.forEach(channel => {
      const chip = el(doc, 'button', 'hearth-filter-chip' + (channel === 'all' ? ' is-selected' : ''));
      chip.type = 'button';
      chip.dataset.channel = channel;
      chip.addEventListener('click', () => setChannel(channel));
      chipFor[channel] = chip;
      chips.appendChild(chip);
    });
    const search = el(doc, 'label', 'hearth-search chronicle-history__search');
    const query = el(doc, 'input', 'chronicle-history__query');
    query.type = 'search';
    query.addEventListener('input', () => { state.search = String(query.value || '').trim().toLowerCase(); applyFilters(); });
    search.appendChild(query);
    tools.appendChild(chips);
    tools.appendChild(search);

    const older = el(doc, 'button', 'hearth-proof-button chronicle-history__older');
    older.type = 'button';
    older.hidden = true;
    older.addEventListener('click', () => loadOlder());
    const list = el(doc, 'div', 'chronicle-history__list');
    list.setAttribute('role', 'log');
    const empty = el(doc, 'p', 'hearth-detail-note chronicle-history__empty');
    empty.hidden = true;
    let available = true;

    function applyLanguage() {
      chips.setAttribute('aria-label', text(lang, 'channels'));
      CHANNELS.forEach(channel => { chipFor[channel].textContent = text(lang, 'channel.' + channel); });
      query.placeholder = text(lang, 'search');
      query.setAttribute('aria-label', text(lang, 'search'));
      older.textContent = text(lang, 'older');
      empty.textContent = text(lang, available ? 'empty' : 'unavailable');
    }
    applyLanguage();
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
      row.appendChild(el(doc, 'span', 'chronicle-history__time', formatTime(line.timestamp, now(), lang)));
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
          available = false;
          applyLanguage();
          state.lines.forEach(entry => entry.row.remove());
          state.lines = [];
          state.more = false;
          updateChrome();
          return;
        }
        available = true;
        if (typeof reply.language === 'string') lang = language(reply.language);
        applyLanguage();
        const profile = String(reply.profile || '');
        if (profile !== state.profile) {
          // Another character: the last one's search does not carry over.
          state.search = '';
          query.value = '';
        }
        state.profile = profile;
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
    module.exports = { mount, formatTime, STRINGS };
  } else if (typeof window !== 'undefined' && typeof window.dovahhubModule === 'function') {
    const mod = window.dovahhubModule('chronicle');
    if (mod) mount(mod, document);
  }
})();
