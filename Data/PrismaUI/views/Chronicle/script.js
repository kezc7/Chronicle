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

'use strict';

// Config defaults

const DEFAULT_CONFIG = {
  version: 2,
  general: {
    enabled: true,
    input_hotkey: 'Enter',
    interact_hotkey: 'F8',
    config_hotkey: 'N',       // Shift + this key opens the settings panel
    max_messages: 100,
    message_fade_time: 0,
    persist_history: true,
    hide_in_menus: true,
    hide_in_photo_mode: true,
    combat_hits: true,
    dialogue_radius: 0,       // Skyrim world units; 0 = unlimited
    hide_game_notifications: false,
    language: 'auto',          // 'auto' follows Skyrim's sLanguage
    toggle_hotkey: '',         // show/hide hotkey; '' = none
    remember_hidden: true,     // keep the show/hide state across loads
    user_hidden: false,        // that state, saved when remember_hidden is on
    auto_hide: 0,              // seconds without new messages before the window fades; 0 = never
  },
  window: {
    position_x: 20,
    position_y: null,   // null = use CSS default (60vh)
    width: 520,
    height: 240,
    opacity: 0.65,
    font_size: 13,
    theme: 'skyrim',
    anchor: 'custom',
  },
  channels: {
    all:       { enabled: true,  colour: null,       prefix: '',         show_timestamp: false },
    dialogue:  { enabled: true,  colour: '#ffffff',  prefix: '',         show_timestamp: false },
    system:    { enabled: true,  colour: '#f5c518',  prefix: '[SYSTEM]', show_timestamp: false },
    notification: { enabled: true, colour: '#fde68a', prefix: '[INFO]', show_timestamp: false },
    skyrimnet: { enabled: false,  colour: '#7ec8f5',  prefix: '',         show_timestamp: false },
    narrator:  { enabled: false,  colour: '#c084fc',  prefix: '[NARR]',   show_timestamp: false },
    player:    { enabled: false,  colour: '#86efac',  prefix: '',         show_timestamp: false },
    combat:    { enabled: false,  colour: '#fb923c',  prefix: '[COMBAT]', show_timestamp: false },
    console:   { enabled: false, colour: '#94a3b8',  prefix: '[CON]',    show_timestamp: false },
  },
  speaker_colour_mode: 'white', // 'white' keeps the default; 'automatic' colours unconfigured speakers
  // Exact display-name overrides for speaker labels.
  speaker_colours: {},
  // Extra Scaleform paths for HUD subtitle replacers, one per line in settings.
  hud_subtitle_paths: [],
};

let config = deepMerge({}, DEFAULT_CONFIG);
let activeChannel = 'all';
// Where submitted input goes. Independent of the viewed tab so picking a
// target (menu or Tab) does not change the history view.
let inputTarget = 'all';
const messages = [];
let updateResizeHandlePosition = () => {};
let userScrolledUp = false;
let unreadCount = 0;
const inputHistory = [];
let inputHistoryIndex = -1;


// Utility

function deepMerge(target, source) {
  for (const key of Object.keys(source)) {
    if (['__proto__', 'constructor', 'prototype'].includes(key)) continue;
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      target[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function normalizeConfig() {
  config.version = 2;
  for (const section of ['general', 'window', 'channels']) {
    if (!config[section] || typeof config[section] !== 'object' || Array.isArray(config[section])) {
      config[section] = deepMerge({}, DEFAULT_CONFIG[section]);
    }
  }
  for (const [channel, defaults] of Object.entries(DEFAULT_CONFIG.channels)) {
    if (!config.channels[channel] || typeof config.channels[channel] !== 'object') {
      config.channels[channel] = { ...defaults };
    }
  }
  normalizeSpeakerColours();
  config.speaker_colour_mode = config.speaker_colour_mode === 'automatic' ? 'automatic' : 'white';
  config.hud_subtitle_paths = Array.isArray(config.hud_subtitle_paths)
    ? [...new Set(config.hud_subtitle_paths
      .filter(path => typeof path === 'string')
      .map(path => path.trim())
      .filter(path => path.length > 0 && path.length <= 256))].slice(0, 32)
    : [];
  const g = config.general;
  const w = config.window;

  g.enabled = g.enabled !== false;
  g.max_messages = clamp(Number(g.max_messages) || 100, 1, 500);
  g.message_fade_time = clamp(Number(g.message_fade_time) || 0, 0, 60);
  g.persist_history = g.persist_history !== false;
  g.combat_hits = g.combat_hits !== false;
  g.dialogue_radius = clamp(Number(g.dialogue_radius) || 0, 0, 5000);
  g.hide_game_notifications = g.hide_game_notifications === true;
  g.language = normalizeLanguage(g.language);
  if (typeof g.toggle_hotkey !== 'string' || !/^(Enter|[a-z0-9]|F([1-9]|1[0-2]))?$/i.test(g.toggle_hotkey)) g.toggle_hotkey = '';
  g.remember_hidden = g.remember_hidden !== false;
  g.user_hidden = g.remember_hidden && g.user_hidden === true;
  g.auto_hide = clamp(Math.round(Number(g.auto_hide) || 0), 0, 300);
  for (const key of ['input_hotkey', 'config_hotkey', 'interact_hotkey']) {
    if (typeof g[key] !== 'string' || !g[key].trim()) g[key] = DEFAULT_CONFIG.general[key];
    if (!/^(Enter|[a-z0-9]|F([1-9]|1[0-2]))$/i.test(g[key])) g[key] = DEFAULT_CONFIG.general[key];
  }
  config.channels.all.enabled = true;

  w.width = clamp(Number(w.width) || 520, 280, 1920);
  w.height = clamp(Number(w.height) || 240, 120, 1080);
  w.font_size = clamp(Number(w.font_size) || 13, 10, 24);
  const opacity = Number(w.opacity);
  w.opacity = Number.isFinite(opacity) ? clamp(opacity, 0, 1) : 0.65;
  w.theme = normalizeTheme(w.theme);
  if (!['custom', 'bottom-left', 'bottom-right', 'top-left', 'top-right'].includes(w.anchor)) {
    w.anchor = 'custom';
  }
  w.width = Math.min(w.width, window.innerWidth - 20);
  w.height = Math.min(w.height, window.innerHeight - 20);
  if (w.position_x != null) w.position_x = clamp(Number(w.position_x) || 0, 0, window.innerWidth - w.width);
  if (w.position_y != null) w.position_y = clamp(Number(w.position_y) || 0, 0, window.innerHeight - w.height);
}

function formatTimestamp(ts) {
  const d = new Date(ts);
  const h = String(d.getHours()).padStart(2, '0');
  const m = String(d.getMinutes()).padStart(2, '0');
  return `${h}:${m}`;
}

function escapeHtml(str) {
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function normalizeSpeakerColours() {
  if (!config.speaker_colours || typeof config.speaker_colours !== 'object' ||
      Array.isArray(config.speaker_colours)) {
    config.speaker_colours = {};
    return;
  }

  const clean = {};
  const names = new Map();
  for (const [rawName, rawColour] of Object.entries(config.speaker_colours)) {
    const name = String(rawName).trim();
    const colour = String(rawColour || '');
    if (!name || !/^#[0-9a-f]{6}$/i.test(colour)) continue;
    const folded = name.toLowerCase();
    if (names.has(folded)) delete clean[names.get(folded)];
    clean[name] = colour.toLowerCase();
    names.set(folded, name);
  }
  config.speaker_colours = clean;
}

const AUTOMATIC_SPEAKER_COLOURS = [
  '#fca5a5', '#fdba74', '#fde68a', '#bef264', '#86efac', '#5eead4',
  '#67e8f9', '#93c5fd', '#a5b4fc', '#c4b5fd', '#f0abfc', '#f9a8d4',
  '#fb7185', '#fb923c', '#facc15', '#a3e635', '#4ade80', '#2dd4bf',
  '#22d3ee', '#60a5fa', '#818cf8', '#8b5cf6', '#d946ef', '#ec4899',
];

function automaticSpeakerColour(name) {
  let hash = 2166136261;
  for (const character of name.toLowerCase()) {
    hash = Math.imul(hash ^ character.charCodeAt(0), 16777619);
  }
  return AUTOMATIC_SPEAKER_COLOURS[(hash >>> 0) % AUTOMATIC_SPEAKER_COLOURS.length];
}

function speakerColour(speaker) {
  const name = String(speaker || '').trim();
  if (!name) return '';
  if (config.speaker_colours[name]) return config.speaker_colours[name];
  const folded = name.toLowerCase();
  const match = Object.entries(config.speaker_colours)
    .find(([savedName]) => savedName.toLowerCase() === folded);
  if (match) return match[1];
  return config.speaker_colour_mode === 'automatic' ? automaticSpeakerColour(name) : '';
}

// Config loading / saving

async function loadConfig() {
  // Magelight's Content-Security-Policy blocks a page from fetching its own
  // files; its native side sends config.json with nativeBootstrap instead.
  const hostFetchesDefaults = typeof window !== 'undefined' && !!window.__MAGELIGHT__;
  try {
    if (!hostFetchesDefaults) {
      const res = await fetch('config.json?_=' + Date.now());
      if (res.ok) {
        const saved = await res.json();
        config = deepMerge(config, saved);
      }
    }
  } catch (_) {
    // First run or file missing — defaults apply
  }
  normalizeConfig();
  applyConfig();
}

function applyConfig() {
  const { window: win } = config;
  const root = document.documentElement;
  uiLanguage = resolveLanguage(config.general.language, runtime.gameLanguage);
  applyTranslations();
  if (!runtime.connected) statusText(t('ui.connecting'));
  showUnread(unreadCount);
  document.body.dataset.theme = win.theme || 'skyrim';
  document.body.dataset.anchor = win.anchor || 'custom';
  document.getElementById('settings-btn').title = t('ui.settingsTitle', {key: config.general.config_hotkey.toUpperCase()});
  document.getElementById('chronicle-window')
    .classList.toggle('hidden', !config.general.enabled);

  if (win.position_x != null) root.style.setProperty('--win-x', win.position_x + 'px');
  if (win.position_y != null) root.style.setProperty('--win-y', win.position_y + 'px');
  else root.style.removeProperty('--win-y');
  root.style.setProperty('--win-width',  win.width  + 'px');
  root.style.setProperty('--win-height', win.height + 'px');
  root.style.setProperty('--opacity',    win.opacity);
  root.style.setProperty('--font-size',  win.font_size + 'px');

  while (messages.length > config.general.max_messages) {
    const old = messages.shift();
    old.el?.remove();
  }

  // Channel tab visibility
  document.querySelectorAll('.tab').forEach(tab => {
    const ch = tab.dataset.channel;
    if (!ch) return;
    const chCfg = config.channels[ch];
    tab.classList.toggle('hidden-channel', chCfg ? !chCfg.enabled : false);
  });

  if (!config.channels[activeChannel]?.enabled) activeChannel = 'all';
  switchChannel(activeChannel);
  setInputTarget(config.channels[inputTarget]?.enabled ? inputTarget : 'all');
  wakeWindow(); // restart the auto-hide timer with the current setting

  // Dynamic channel colours injected as a <style> block
  const styleId = 'chronicle-channel-colours';
  let styleEl = document.getElementById(styleId);
  if (!styleEl) {
    styleEl = document.createElement('style');
    styleEl.id = styleId;
    document.head.appendChild(styleEl);
  }
  const rules = Object.entries(config.channels)
    .filter(([ch, v]) => DEFAULT_CONFIG.channels[ch] && v && /^#[0-9a-f]{6}$/i.test(v.colour))
    .map(([ch, v]) => {
      const speakerRule = `.message[data-channel="${ch}"] .speaker { color: ${v.colour}; }`;
      if (ch !== 'system' && ch !== 'notification') return speakerRule;
      return `${speakerRule}
.message[data-channel="${ch}"] .prefix,
.message[data-channel="${ch}"] .text { color: ${v.colour}; }`;
    });
  styleEl.textContent = rules.join('\n');
  applySpeakerColours();

  // The resize handle is a fixed-position sibling. Reposition it after
  // applying saved window coordinates, not only after a resize.
  requestAnimationFrame(updateResizeHandlePosition);
}

// Message rendering

/**
 * Called by the C++ plugin via PrismaUI Invoke.
 * @param {object} data - { channel, speaker, message, timestamp }
 */
function addMessage(data) {
  const persist = arguments.length < 2 || arguments[1] !== false;
  if (typeof data === 'string') {
    try { data = JSON.parse(data); } catch (_) { return; }
  }

  if (!data || typeof data !== 'object') return;
  const { channel, speaker, message, timestamp } = data;
  if (!DEFAULT_CONFIG.channels[channel] || typeof message !== 'string') return;
  const chCfg = config.channels[channel];


  const entry = { channel, speaker, message, timestamp: timestamp || Date.now(), el: null };
  const list = document.getElementById('message-list');
  const shouldStick = !userScrolledUp;
  messages.push(entry);

  while (messages.length > config.general.max_messages) {
    const old = messages.shift();
    old.el?.remove();
  }

  const el = buildMessageEl(entry, chCfg);
  entry.el = el;
  el.classList.toggle('hidden', !shouldShowMessage(channel) || !matchesFilters(entry));

  list.appendChild(el);
  if (shouldShowMessage(channel) && matchesFilters(entry)) {
    if (shouldStick) scrollToBottom();
    else showUnread(++unreadCount);
  }



  if (persist && config.general.persist_history) persistHistory();
  if (interactionOpen) refreshSpeakerOptions();
  if (persist) wakeWindow(); // restored history does not count as new activity
}

// Auto-hide: after general.auto_hide seconds without new messages the whole
// window fades out; new messages and any interaction bring it back.
let autoHideTimer = null;
function wakeWindow() {
  const win = document.getElementById('chronicle-window');
  win.classList.remove('idle-hidden');
  clearTimeout(autoHideTimer);
  autoHideTimer = null;
  const seconds = Number(config.general.auto_hide) || 0;
  if (seconds <= 0) return;
  autoHideTimer = setTimeout(function hide() {
    if (interactionOpen || inputOpen || settingsOpen) {
      autoHideTimer = setTimeout(hide, seconds * 1000);
      return;
    }
    win.classList.add('idle-hidden');
  }, seconds * 1000);
}

function buildMessageEl(entry, chCfg) {
  const { channel, speaker, message, timestamp } = entry;
  const div = document.createElement('div');
  div.className = 'message';
  div.dataset.channel = channel;

  let html = '';
  if (chCfg?.show_timestamp) {
    html += `<span class="timestamp">${escapeHtml(formatTimestamp(timestamp))}</span>`;
  }
  if (chCfg?.prefix) {
    html += `<span class="prefix">${escapeHtml(chCfg.prefix)}</span>`;
  }
  if (speaker) {
    html += `<span class="speaker">${escapeHtml(speaker)}: </span>`;
  }
  html += `<span class="text">${escapeHtml(message)}</span>`;

  div.innerHTML = html;
  const speakerEl = div.querySelector('.speaker');
  const colour = speakerColour(speaker);
  if (speakerEl && colour) speakerEl.style.color = colour;
  return div;
}

function applySpeakerColours() {
  document.querySelectorAll('.message').forEach(messageEl => {
    const speakerEl = messageEl.querySelector('.speaker');
    if (!speakerEl) return;
    const entry = messages.find(item => item.el === messageEl);
    const colour = speakerColour(entry?.speaker);
    if (colour) speakerEl.style.color = colour;
    else speakerEl.style.removeProperty('color');
  });
}

function shouldShowMessage(channel) {
  // The player's own typed lines always show in All, even with the Player channel off.
  if (channel === 'player' && activeChannel === 'all') return true;
  if (!config.channels[channel]?.enabled) return false;
  // Console is off by default, so users who enable it see its lines in All too.
  if (activeChannel === 'all') return true;
  return channel === activeChannel;
}

function scrollToBottom() {
  const list = document.getElementById('message-list');
  list.scrollTop = list.scrollHeight;
  userScrolledUp = false;
  unreadCount = 0;
  showUnread(0);
}

function showUnread(count) {
  const button = document.getElementById('new-messages');
  button.textContent = count > 0 ? t(count === 1 ? 'ui.newMessage' : 'ui.newMessagesCount', {count}) : t('ui.newMessages');
  button.classList.toggle('hidden', count === 0);
}

function persistHistory() { scheduleHistorySave(); }

function restoreHistory() {
  if (!config.general.persist_history || !runtime.profile) return;
  try {
    const saved = JSON.parse(localStorage.getItem(historyKey()) || '[]');
    if (Array.isArray(saved)) {
      saved.slice(-config.general.max_messages).forEach(entry => addMessage(entry, false));
    }
  } catch (_) {}
}

function clearHistory() {
  clearTimeout(historySaveTimer);
  historySaveTimer = null;
  messages.length = 0;
  document.getElementById('message-list').replaceChildren();
  try { localStorage.removeItem(historyKey()); } catch (_) {}
  unreadCount = 0;
  userScrolledUp = false;
  showUnread(0);
  refreshSpeakerOptions();
  // The native history behind Chronicle's DovahHub tab is cleared with it.
  if (runtime.profile) sendToGame('historyCleared', runtime.profile);
}

function runDiagnostics() {
  document.getElementById('diagnostics-output').textContent = t('diag.checking');
  if (!sendToGame('diagnostics', '')) {
    document.getElementById('diagnostics-output').textContent = t('diag.unavailable');
  }
}

// Channel tabs

function switchChannel(channel) {
  activeChannel = channel;

  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.toggle('active', tab.dataset.channel === channel);
  });

  refreshFilters();

  scrollToBottom();
}

function setInputTarget(channel) {
  inputTarget = channel;
  document.getElementById('input-channel-label').textContent =
    '[' + channelLabel(channel) + ']';
  closeChannelMenu();
}

// Enabled channels in tab order; shared by Tab cycling and the channel menu.
function enabledChannels() {
  return [...document.querySelectorAll('.tab[data-channel]')]
    .map(tab => tab.dataset.channel)
    .filter(channel => config.channels[channel]?.enabled);
}

function closeChannelMenu() {
  document.getElementById('input-channel-menu').classList.add('hidden');
  document.getElementById('input-channel-label').setAttribute('aria-expanded', 'false');
}

function toggleChannelMenu() {
  const menu = document.getElementById('input-channel-menu');
  if (!menu.classList.contains('hidden')) { closeChannelMenu(); return; }
  menu.replaceChildren(...enabledChannels().map(channel => {
    const item = document.createElement('button');
    item.type = 'button';
    item.setAttribute('role', 'menuitem');
    item.dataset.channel = channel;
    item.textContent = channelLabel(channel);
    item.classList.toggle('active', channel === inputTarget);
    return item;
  }));
  const label = document.getElementById('input-channel-label');
  menu.classList.remove('hidden');
  // Open above the label, or below it when the window sits near the top.
  const anchor = label.getBoundingClientRect();
  const above = anchor.top - 4 >= menu.offsetHeight;
  menu.style.left = anchor.left + 'px';
  menu.style.top = above ? '' : (anchor.bottom + 4) + 'px';
  menu.style.bottom = above ? (window.innerHeight - anchor.top + 4) + 'px' : '';
  label.setAttribute('aria-expanded', 'true');
}

// Input bar

let inputOpen = false;

function openInput() {
  if (inputOpen || settingsOpen || !config.general.enabled) return false;
  if (!beginInteraction()) return false;
  inputOpen = true;

  const bar = document.getElementById('input-bar');
  const input = document.getElementById('chat-input');
  bar.classList.remove('hidden');
  input.value = '';
  input.focus();

  return true;
}

function closeInput() {
  if (!inputOpen) return;
  inputOpen = false;
  closeChannelMenu();

  document.getElementById('input-bar').classList.add('hidden');
  document.getElementById('chat-input').blur();


}

function submitInput() {
  const input = document.getElementById('chat-input');
  const text = input.value.trim();
  if (!text) {
    closeInput();
    closeInteraction();
    return;
  }

  if (inputHistory[inputHistory.length - 1] !== text) {
    inputHistory.push(text);
    if (inputHistory.length > 50) inputHistory.shift();
  }
  inputHistoryIndex = -1;

  if (inputTarget === 'console') {
    sendToGame('consoleCommand', text);
  } else {
    sendToGame('playerInput', text);
  }

  closeInput();
  closeInteraction();
}

// Settings panel

let settingsOpen = false;
let settingsSnapshot = null;

// Channels in settings order; names come from the translations.
const CHANNELS = ['all', 'dialogue', 'system', 'notification', 'skyrimnet', 'narrator', 'player', 'combat', 'console'];
function channelLabel(channel) { return t('channel.' + channel); }

function openSettings() {
  if (settingsOpen) return false;
  if (!beginInteraction()) return false;
  // Close input bar first if open
  closeInput();
  settingsSnapshot = deepMerge({}, config);
  settingsOpen = true;

  populateSettingsForm();
  document.getElementById('settings-overlay').classList.remove('hidden');

  return true;
}

function closeSettings() {
  if (!settingsOpen) return;
  discardSettingsPreview();
  settingsOpen = false;

  document.getElementById('settings-overlay').classList.add('hidden');
  closeInteraction();

}

function discardSettingsPreview() {
  if (!settingsSnapshot) return;
  const snapshot = settingsSnapshot;
  settingsSnapshot = null;
  config = deepMerge({}, snapshot);
  normalizeConfig();
  applyConfig();
}

function toggleSettings() {
  if (settingsOpen) closeSettings();
  else openSettings();
}

function populateSettingsForm() {
  document.getElementById('sp-interact-hotkey').value = config.general.interact_hotkey;
  const g = config.general;
  const w = config.window;

  document.getElementById('sp-enabled').checked     = g.enabled;
  document.getElementById('sp-hide-menus').checked  = g.hide_in_menus;
  document.getElementById('sp-hide-photo').checked  = g.hide_in_photo_mode;
  document.getElementById('sp-config-hotkey').value = g.config_hotkey || 'N';
  document.getElementById('sp-input-hotkey').value  = g.input_hotkey  || 'Enter';
  hotkeyStatus('');
  document.getElementById('sp-max-messages').value  = g.max_messages;
  document.getElementById('sp-persist-history').checked = g.persist_history !== false;
  document.getElementById('sp-combat-hits').checked = g.combat_hits !== false;
  document.getElementById('sp-dialogue-radius').value = g.dialogue_radius;
  document.getElementById('sp-dialogue-radius-val').value = g.dialogue_radius;
  document.getElementById('sp-hide-game-notifications').checked = g.hide_game_notifications === true;

  const fadeEl = document.getElementById('sp-fade-time');
  fadeEl.value = g.message_fade_time;
  document.getElementById('sp-fade-time-val').textContent = g.message_fade_time;

  const opEl = document.getElementById('sp-opacity');
  opEl.value = w.opacity;
  document.getElementById('sp-opacity-val').textContent = parseFloat(w.opacity).toFixed(2);

  const fsEl = document.getElementById('sp-font-size');
  fsEl.value = w.font_size;
  document.getElementById('sp-font-size-val').textContent = w.font_size;

  document.getElementById('sp-width').value  = w.width;
  document.getElementById('sp-height').value = w.height;
  document.getElementById('sp-theme').value = w.theme || 'skyrim';
  document.getElementById('sp-language').value = g.language || 'auto';
  document.getElementById('sp-toggle-hotkey').value = g.toggle_hotkey || '';
  document.getElementById('sp-remember-hidden').checked = g.remember_hidden !== false;
  document.getElementById('sp-auto-hide').value = g.auto_hide;
  document.getElementById('sp-auto-hide-val').value = g.auto_hide;
  document.getElementById('sp-anchor').value = w.anchor || 'custom';
  document.getElementById('sp-speaker-colour-mode').value = config.speaker_colour_mode;
  document.getElementById('sp-hud-subtitle-paths').value = config.hud_subtitle_paths.join('\n');
  syncPositionControls();

  buildChannelRows();
  buildSpeakerColourRows();
}

function buildChannelRows() {
  const container = document.getElementById('sp-channel-list');
  container.innerHTML = '';

  for (const ch of CHANNELS) {
    const label = escapeHtml(channelLabel(ch));
    const cfg = config.channels[ch] || DEFAULT_CONFIG.channels[ch];
    const row = document.createElement('div');
    row.className = 'sp-ch-row';
    row.dataset.channel = ch;

    // data-i18n keys let a language change re-translate rows built here.
    row.innerHTML = `
      <span class="sp-ch-name" data-i18n="channel.${ch}">${label}</span>
      <label class="sp-toggle">
        <input type="checkbox" class="ch-enabled" ${cfg.enabled ? 'checked' : ''} />
        <span class="sp-track"></span>
      </label>
      <input type="text" class="ch-prefix" value="${escapeHtml(cfg.prefix || '')}"
             placeholder="${escapeHtml(t('sp.prefix'))}" data-i18n-placeholder="sp.prefix" maxlength="16" />
      <label title="${escapeHtml(t('sp.showTimestamps'))}" data-i18n-title="sp.showTimestamps">
        <input type="checkbox" class="ch-timestamp" ${cfg.show_timestamp ? 'checked' : ''} />
      </label>
      <input type="color" class="ch-colour" value="${cfg.colour || '#ffffff'}"
             ${ch === 'all' ? 'disabled' : ''} />
    `;

    container.appendChild(row);
  }
}

function buildSpeakerColourRows() {
  const container = document.getElementById('sp-speaker-colour-list');
  const datalist = document.getElementById('sp-speaker-options');
  if (!container || !datalist) return;

  const knownNames = new Set(Object.keys(config.speaker_colours));
  messages.forEach(entry => { if (entry.speaker) knownNames.add(entry.speaker); });
  datalist.replaceChildren();
  [...knownNames].sort((a, b) => a.localeCompare(b)).forEach(name => {
    const option = document.createElement('option');
    option.value = name;
    datalist.appendChild(option);
  });

  container.replaceChildren();
  const rules = Object.entries(config.speaker_colours)
    .sort(([a], [b]) => a.localeCompare(b));
  if (rules.length === 0) {
    const empty = document.createElement('div');
    empty.className = 'sp-empty';
    empty.dataset.i18n = 'sp.noSpeakerColours';
    empty.textContent = t('sp.noSpeakerColours');
    container.appendChild(empty);
    return;
  }

  rules.forEach(([name, colour]) => {
    const row = document.createElement('div');
    row.className = 'sp-speaker-row';
    row.dataset.speaker = name;
    row.innerHTML = '<span class="sp-speaker-name"></span>' +
      '<input type="color" class="sp-speaker-colour" />' +
      '<button type="button" class="sp-remove-speaker"></button>';
    row.querySelector('.sp-speaker-name').textContent = name;
    row.querySelector('.sp-remove-speaker').dataset.i18n = 'sp.remove';
    row.querySelector('.sp-remove-speaker').textContent = t('sp.remove');
    row.querySelector('.sp-speaker-colour').value = colour;
    container.appendChild(row);
  });
}

function collectSpeakerColours() {
  const result = {};
  document.querySelectorAll('.sp-speaker-row').forEach(row => {
    const name = (row.dataset.speaker || '').trim();
    const colour = row.querySelector('.sp-speaker-colour')?.value || '';
    if (name && /^#[0-9a-f]{6}$/i.test(colour)) result[name] = colour.toLowerCase();
  });
  return result;
}

function addSpeakerColour() {
  const nameInput = document.getElementById('sp-speaker-name');
  const colourInput = document.getElementById('sp-speaker-new-colour');
  const name = nameInput.value.trim();
  const colour = colourInput.value;
  if (!name) {
    statusText(t('status.enterNpcName'));
    nameInput.focus();
    return;
  }

  for (const savedName of Object.keys(config.speaker_colours)) {
    if (savedName.toLowerCase() === name.toLowerCase()) delete config.speaker_colours[savedName];
  }
  config.speaker_colours[name] = colour;
  normalizeSpeakerColours();
  applyConfig();
  buildSpeakerColourRows();
  nameInput.value = '';
  statusText(t('status.speakerPreview'));
}

function removeSpeakerColour(row) {
  const name = row.dataset.speaker || '';
  delete config.speaker_colours[name];
  normalizeSpeakerColours();
  applyConfig();
  buildSpeakerColourRows();
  statusText(t('status.speakerRemoved'));
}

function collectSettingsFromForm() {
  const channels = {};
  document.querySelectorAll('.sp-ch-row').forEach(row => {
    const ch = row.dataset.channel;
    channels[ch] = {
      enabled:        row.querySelector('.ch-enabled').checked,
      colour:         ch === 'all' ? null : row.querySelector('.ch-colour').value,
      prefix:         row.querySelector('.ch-prefix').value,
      show_timestamp: row.querySelector('.ch-timestamp').checked,
    };
  });

  return {
    general: {
      enabled:            document.getElementById('sp-enabled').checked,
      config_hotkey:      document.getElementById('sp-config-hotkey').value || 'N',
      interact_hotkey:    document.getElementById('sp-interact-hotkey').value || 'F8',
      input_hotkey:       document.getElementById('sp-input-hotkey').value  || 'Enter',
      max_messages:       Math.min(500, Math.max(1,
        parseInt(document.getElementById('sp-max-messages').value, 10) || 100)),
      message_fade_time:  parseInt(document.getElementById('sp-fade-time').value, 10) || 0,
      persist_history:    document.getElementById('sp-persist-history').checked,
      hide_in_menus:      document.getElementById('sp-hide-menus').checked,
      hide_in_photo_mode: document.getElementById('sp-hide-photo').checked,
      combat_hits:        document.getElementById('sp-combat-hits').checked,
      dialogue_radius:    clamp(Number(document.getElementById('sp-dialogue-radius-val').value) || 0, 0, 5000),
      hide_game_notifications: document.getElementById('sp-hide-game-notifications').checked,
      language:           document.getElementById('sp-language').value || 'auto',
      toggle_hotkey:      document.getElementById('sp-toggle-hotkey').value || '',
      remember_hidden:    document.getElementById('sp-remember-hidden').checked,
      user_hidden:        document.getElementById('sp-remember-hidden').checked && config.general.user_hidden === true,
      auto_hide:          parseInt(document.getElementById('sp-auto-hide').value, 10) || 0,
    },
    window: {
      width:     Math.min(1920, Math.max(280,
        parseInt(document.getElementById('sp-width').value, 10) || 520)),
      height:    Math.min(1080, Math.max(120,
        parseInt(document.getElementById('sp-height').value, 10) || 240)),
      opacity:   Math.min(1, Math.max(0,
        Number.isFinite(parseFloat(document.getElementById('sp-opacity').value))
          ? parseFloat(document.getElementById('sp-opacity').value) : 0.65)),
      font_size: Math.min(24, Math.max(10,
        parseInt(document.getElementById('sp-font-size').value, 10) || 13)),
      theme:     document.getElementById('sp-theme').value || 'skyrim',
      anchor:    document.getElementById('sp-anchor').value || 'custom',
      position_x: Number(document.getElementById('sp-position-x').value),
      position_y: Number(document.getElementById('sp-position-y').value),
    },
    speaker_colour_mode: document.getElementById('sp-speaker-colour-mode').value === 'automatic'
      ? 'automatic' : 'white',
    channels,
    speaker_colours: collectSpeakerColours(),
    hud_subtitle_paths: document.getElementById('sp-hud-subtitle-paths').value
      .split(/\r?\n/).map(path => path.trim())
      .filter(path => path.length > 0 && path.length <= 256)
      .filter((path, index, paths) => paths.indexOf(path) === index)
      .slice(0, 32),
  };
}

function applyAndSaveSettings() {
  const newCfg = collectSettingsFromForm();
  config = deepMerge(config, newCfg);
  normalizeConfig();
  applyConfig();
  settingsSnapshot = deepMerge({}, config);
  persistConfig();
  if (config.general.persist_history) {
    persistHistory();
  } else {
    try { localStorage.removeItem(historyKey()); } catch (_) {}
  }

}

// Settings panel tab switching
document.getElementById('sp-tabs').addEventListener('click', e => {
  const tab = e.target.closest('.sp-tab');
  if (!tab) return;
  const section = tab.dataset.sp;
  document.querySelectorAll('.sp-tab').forEach(t => t.classList.toggle('active', t === tab));
  document.querySelectorAll('.sp-section').forEach(s => {
    s.classList.toggle('active', s.id === 'sp-' + section);
  });
});

// Slider live labels
function bindSlider(id, valId, fmt) {
  const slider = document.getElementById(id);
  const label  = document.getElementById(valId);
  slider.addEventListener('input', () => {
    label.textContent = fmt ? fmt(slider.value) : slider.value;
  });
}
bindSlider('sp-opacity',   'sp-opacity-val',   v => parseFloat(v).toFixed(2));
bindSlider('sp-font-size', 'sp-font-size-val', v => v);
bindSlider('sp-fade-time', 'sp-fade-time-val', v => v);
// Auto-hide: slider for quick changes, number box for exact seconds (0-300).
(() => {
  const slider = document.getElementById('sp-auto-hide');
  const box = document.getElementById('sp-auto-hide-val');
  slider.addEventListener('input', () => { box.value = slider.value; });
  box.addEventListener('change', () => {
    const seconds = clamp(Math.round(Number(box.value) || 0), 0, 300);
    box.value = seconds;
    slider.value = seconds;
  });
})();

document.getElementById('sp-close').addEventListener('click', closeSettings);

// Settings Apply / Reset
document.getElementById('sp-apply').addEventListener('click', () => {
  applyAndSaveSettings();
});

document.getElementById('sp-reset').addEventListener('click', () => {
  config = deepMerge({}, DEFAULT_CONFIG);
  populateSettingsForm();
  normalizeConfig();
  applyConfig();
  statusText(t('status.defaultsRestored'));
});

document.getElementById('sp-test').addEventListener('click', runDiagnostics);
document.getElementById('sp-speaker-add').addEventListener('click', addSpeakerColour);
document.getElementById('sp-speaker-colour-mode').addEventListener('change', event => {
  config.speaker_colour_mode = event.target.value === 'automatic' ? 'automatic' : 'white';
  applySpeakerColours();
  statusText(t('status.speakerPreview'));
});
document.getElementById('sp-speaker-name').addEventListener('keydown', event => {
  if (event.key === 'Enter') {
    addSpeakerColour();
    event.preventDefault();
  }
});
document.getElementById('sp-speaker-colour-list').addEventListener('input', event => {
  const row = event.target.closest('.sp-speaker-row');
  if (!row || !event.target.classList.contains('sp-speaker-colour')) return;
  config.speaker_colours[row.dataset.speaker] = event.target.value;
  normalizeSpeakerColours();
  applySpeakerColours();
  statusText(t('status.speakerPreview'));
});
document.getElementById('sp-speaker-colour-list').addEventListener('click', event => {
  const button = event.target.closest('.sp-remove-speaker');
  if (!button) return;
  removeSpeakerColour(button.closest('.sp-speaker-row'));
});

document.getElementById('settings-btn').addEventListener('click', e => {
  e.stopPropagation();
  toggleSettings();
});

document.getElementById('clear-history').addEventListener('click', e => {
  e.stopPropagation();
  // Only a deliberate click while interacting clears; a key press reaching a
  // focused button outside an interaction must not erase the history.
  if (!interactionOpen) return;
  clearHistory();
});

document.getElementById('new-messages').addEventListener('click', e => {
  e.stopPropagation();
  scrollToBottom();
});

document.getElementById('message-list').addEventListener('scroll', e => {
  const list = e.currentTarget;
  const distanceFromBottom = list.scrollHeight - list.scrollTop - list.clientHeight;
  userScrolledUp = distanceFromBottom > 12;
  if (!userScrolledUp) {
    unreadCount = 0;
    showUnread(0);
  }
});

// PrismaUI bridge

// sendToGame is provided by bridge.js.

// Window visibility (called by C++ plugin)

function hideWindow() {
  document.getElementById('chronicle-window').classList.add('hidden');
}

function showWindow() {
  const win = document.getElementById('chronicle-window');
  win.classList.remove('hidden');
  win.classList.add('fading-in');
  setTimeout(() => win.classList.remove('fading-in'), 300);
}

// Save window position/size

function saveWindowState(x, y, w, h) {
  config.window.position_x = x;
  config.window.position_y = y;
  config.window.width = w;
  config.window.height = h;
  syncPositionControls();
  if (settingsOpen) {
    statusText(t('status.windowPreview'));
  } else {
    persistConfig();
  }
}

function syncPositionControls() {
  const xSlider = document.getElementById('sp-position-x');
  const ySlider = document.getElementById('sp-position-y');
  if (!xSlider || !ySlider) return;

  const win = document.getElementById('chronicle-window');
  const rect = win.getBoundingClientRect();
  const width = Number(config.window.width) || 520;
  const height = Number(config.window.height) || 240;
  const maxX = Math.max(0, window.innerWidth - width);
  const maxY = Math.max(0, window.innerHeight - height);
  const useSavedPosition = config.window.anchor === 'custom';
  const xValue = !useSavedPosition || config.window.position_x == null ? rect.left : Number(config.window.position_x);
  const yValue = !useSavedPosition || config.window.position_y == null ? rect.top : Number(config.window.position_y);
  const x = clamp(Number.isFinite(xValue) ? xValue : 20, 0, maxX);
  const y = clamp(Number.isFinite(yValue) ? yValue : Math.max(0, window.innerHeight - height - 40), 0, maxY);

  xSlider.max = maxX;
  ySlider.max = maxY;
  xSlider.value = Math.round(x);
  ySlider.value = Math.round(y);
  const xBox = document.getElementById('sp-position-x-val');
  const yBox = document.getElementById('sp-position-y-val');
  xBox.max = maxX;
  yBox.max = maxY;
  xBox.value = Math.round(x);
  yBox.value = Math.round(y);
}

// Slider, nudge buttons and exact-value box all route here and stay in sync.
function previewPosition(axis, value) {
  const slider = document.getElementById(axis === 'position_x' ? 'sp-position-x' : 'sp-position-y');
  const raw = Number(value);
  if (!Number.isFinite(raw)) return;
  const numeric = clamp(Math.round(raw), 0, Number(slider.max) || 0);
  config.window.anchor = 'custom';
  config.window[axis] = numeric;
  document.body.dataset.anchor = 'custom';
  document.documentElement.style.setProperty(axis === 'position_x' ? '--win-x' : '--win-y', numeric + 'px');
  slider.value = numeric;
  document.getElementById(axis === 'position_x' ? 'sp-position-x-val' : 'sp-position-y-val').value = numeric;
}

// Drag to move

(function initDrag() {
  const win = document.getElementById('chronicle-window');
  let dragging = false;
  let startX, startY, origX, origY;

  const handle = document.getElementById('channel-tabs');

  handle.addEventListener('mousedown', e => {
    if (!interactionOpen || e.target.closest('button')) return;
    const rect = win.getBoundingClientRect();
    config.window.anchor = 'custom';
    document.body.dataset.anchor = 'custom';
    win.style.left = rect.left + 'px'; win.style.top = rect.top + 'px';
    dragging = true;
    startX = e.clientX;
    startY = e.clientY;
    const style = getComputedStyle(win);
    origX = parseInt(style.left, 10);
    origY = parseInt(style.top, 10);
    e.preventDefault();
  });

  document.addEventListener('mousemove', e => {
    if (!dragging) return;
    win.style.left = clamp(origX + e.clientX - startX, 0, window.innerWidth - win.offsetWidth) + 'px';
    win.style.top  = clamp(origY + e.clientY - startY, 0, window.innerHeight - win.offsetHeight) + 'px';
  });

  document.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    config.window.anchor = 'custom';
    document.body.dataset.anchor = 'custom';
    const style = getComputedStyle(win);
    saveWindowState(
      parseInt(style.left, 10), parseInt(style.top, 10),
      parseInt(style.width, 10), parseInt(style.height, 10),
    );
  });
})();

// Resize handle

(function initResize() {
  const win = document.getElementById('chronicle-window');
  const handle = document.getElementById('resize-handle');
  let resizing = false;
  let startX, startY, origW, origH;

  function positionHandle() {
    const r = win.getBoundingClientRect();
    handle.style.left = (r.right - 14) + 'px';
    handle.style.top  = (r.bottom - 14) + 'px';
  }
  updateResizeHandlePosition = positionHandle;
  positionHandle();
  new ResizeObserver(positionHandle).observe(win);

  handle.addEventListener('mousedown', e => {
    if (!interactionOpen) return;
    resizing = true;
    startX = e.clientX; startY = e.clientY;
    origW = win.offsetWidth; origH = win.offsetHeight;
    e.preventDefault();
  });

  document.addEventListener('mousemove', e => {
    if (!resizing) return;
    const rect = win.getBoundingClientRect();
    win.style.width = Math.min(window.innerWidth - rect.left, Math.max(280, origW + e.clientX - startX)) + 'px';
    win.style.height = Math.min(window.innerHeight - rect.top, Math.max(120, origH + e.clientY - startY)) + 'px';
    positionHandle();
  });

  document.addEventListener('mouseup', () => {
    if (!resizing) return;
    resizing = false;
    const style = getComputedStyle(win);
    saveWindowState(
      parseInt(style.left, 10), parseInt(style.top, 10),
      parseInt(style.width, 10), parseInt(style.height, 10),
    );
  });
})();

// Keyboard handling

document.addEventListener('keydown', e => {
  if (runtime.nativeHotkeys && !interactionOpen) {
    // Keys reach the page only while it holds focus, so with nothing open
    // that focus is stale. Keep game keys (Tab, Space, Enter) away from the
    // window's buttons and give the keyboard back to Skyrim.
    e.preventDefault();
    sendToGame('focusReleased', '');
    return;
  }
  if (!runtime.visible || !runtime.loaded || e.repeat) return;
  if (e.key === 'Escape' && interactionOpen) {
    closeInteraction();
    document.getElementById('copy-output').hidden = true;
    e.preventDefault(); return;
  }
  const typing = /^(INPUT|TEXTAREA|SELECT)$/.test(e.target.tagName);
  if (!typing && e.key.toUpperCase() === config.general.interact_hotkey.toUpperCase()) {
    if (interactionOpen) closeInteraction();
    else { beginInteraction(); refreshSpeakerOptions(); }
    e.preventDefault(); return;
  }
  if (typing && !inputOpen) return;
  // Settings panel takes priority when open
  if (settingsOpen) {
    if (e.key === 'Escape') {
      closeSettings();
      e.preventDefault();
    }
    return;
  }

  // Input bar
  if (inputOpen) {
    if (e.key === 'Escape') {
      closeInput();
      e.preventDefault();
    } else if (e.key === 'Enter') {
      submitInput();
      e.preventDefault();
    } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      if (inputHistory.length === 0) return;
      if (e.key === 'ArrowUp') {
        inputHistoryIndex = Math.min(inputHistoryIndex + 1, inputHistory.length - 1);
      } else {
        inputHistoryIndex = Math.max(inputHistoryIndex - 1, -1);
      }
      e.target.value = inputHistoryIndex < 0
        ? '' : inputHistory[inputHistory.length - 1 - inputHistoryIndex];
      e.preventDefault();
    } else if (e.key === 'Tab') {
      const channels = enabledChannels();
      setInputTarget(channels[(channels.indexOf(inputTarget) + 1) % channels.length]);
      e.preventDefault();
    }
    return;
  }

  // Shift + config_hotkey → open settings
  const cfgKey = (config.general.config_hotkey || 'N').toUpperCase();
  if (e.shiftKey && e.key.toUpperCase() === cfgKey) {
    openSettings();
    e.preventDefault();
    return;
  }

  // Input hotkey → open input bar
  const inputKey = config.general.input_hotkey || 'Enter';
  if (e.key.toUpperCase() === inputKey.toUpperCase()) {
    openInput();
    e.preventDefault();
  }
});

// Channel tab clicks

document.getElementById('channel-tabs').addEventListener('click', e => {
  const tab = e.target.closest('.tab[data-channel]');
  // Clicking a tab still sets the target too, so typing on Console runs commands.
  if (tab) { switchChannel(tab.dataset.channel); setInputTarget(tab.dataset.channel); }
});

// Input-bar channel menu. Mousedown is cancelled so the chat input keeps
// keyboard focus and typing continues straight after a pick.
const channelPicker = document.getElementById('input-channel-picker');
channelPicker.addEventListener('mousedown', e => e.preventDefault());
channelPicker.addEventListener('click', e => {
  const item = e.target.closest('#input-channel-menu [data-channel]');
  if (item) setInputTarget(item.dataset.channel);
  else if (e.target.closest('#input-channel-label')) toggleChannelMenu();
  if (inputOpen) document.getElementById('chat-input').focus();
});

// Init

loadConfig().then(() => {
  let saved = null;
  try { saved = localStorage.getItem('chronicle_config'); } catch (_) {}
  if (saved) {
    try { config = deepMerge(config, JSON.parse(saved)); } catch (_) {}
  }

  normalizeConfig();
  applyConfig();
  startHandshake();

  // The native handshake supplies the authoritative settings file and profile.

});
document.getElementById('history-search').addEventListener('input', refreshFilters);
document.getElementById('speaker-filter').addEventListener('change', refreshFilters);
document.getElementById('copy-history').addEventListener('click', copyHistory);
document.getElementById('export-history').addEventListener('click', () => {
  if (!sendToGame('exportHistory', visibleHistoryText())) statusText(t('status.exportUnavailable'));
});
document.getElementById('sp-position').addEventListener('click', resetWindowPosition);
document.getElementById('sp-position-x').addEventListener('input', event => previewPosition('position_x', event.target.value));
document.getElementById('sp-position-y').addEventListener('input', event => previewPosition('position_y', event.target.value));
document.getElementById('sp-position-x-val').addEventListener('change', event => previewPosition('position_x', event.target.value));
document.getElementById('sp-position-y-val').addEventListener('change', event => previewPosition('position_y', event.target.value));
const dialogueRadiusSlider = document.getElementById('sp-dialogue-radius');
const dialogueRadiusValue = document.getElementById('sp-dialogue-radius-val');
function syncDialogueRadius(value) {
  const radius = clamp(Number(value) || 0, 0, 5000);
  dialogueRadiusSlider.value = radius;
  dialogueRadiusValue.value = radius;
}
dialogueRadiusSlider.addEventListener('input', event => syncDialogueRadius(event.target.value));
dialogueRadiusValue.addEventListener('input', event => syncDialogueRadius(event.target.value));
// Hotkey boxes: click one, then press the key. Typing a key name was easy to
// get wrong, and an invalid value silently fell back to the default. Only keys
// the native side understands are accepted: letters, digits, Enter, F1–F12.
// e.keyCode is the Windows virtual-key code, exactly what the native side
// compares against, and PrismaUI's engine reports it reliably; its e.key and
// e.code did not match modern browsers (in-game, 0 and 9 were rejected).
// Number-pad keys (96-105) are different keys natively, so they are refused.
function isNumpadKey(e) {
  return (e.keyCode >= 96 && e.keyCode <= 105) || (e.code || '').startsWith('Numpad');
}

function hotkeyName(e) {
  const vk = e.keyCode;
  if (vk === 13) return 'Enter';
  if (vk >= 112 && vk <= 123) return 'F' + (vk - 111);
  if ((vk >= 48 && vk <= 57) || (vk >= 65 && vk <= 90)) return String.fromCharCode(vk);
  if (vk) return null;
  // No key code at all: fall back to the modern properties.
  if (isNumpadKey(e)) return null;
  if (e.key === 'Enter') return 'Enter';
  if (/^F([1-9]|1[0-2])$/.test(e.key)) return e.key;
  if (/^(Key[A-Z]|Digit[0-9])$/.test(e.code || '')) return e.code.slice(-1);
  if (/^[a-z0-9]$/i.test(e.key)) return e.key.toUpperCase();
  return null;
}

// Escape (27) still closes settings, Tab (9) moves between fields, and lone
// modifiers (16 Shift, 17 Ctrl, 18 Alt, 91/92 Windows) wait for the real key.
function isPassThroughKey(e) {
  return [27, 9, 16, 17, 18, 91, 92].includes(e.keyCode) ||
    ['Escape', 'Tab', 'Shift', 'Control', 'Alt', 'Meta'].includes(e.key);
}

function hotkeyStatus(text, error = false) {
  const status = document.getElementById('sp-hotkey-status');
  status.textContent = text;
  status.classList.toggle('error', error);
}

// Interact, Input and Show/hide are plain keys and must differ (Settings is
// Shift + key, so it may share a letter with them).
function checkHotkeyClash() {
  const keys = ['sp-interact-hotkey', 'sp-input-hotkey', 'sp-toggle-hotkey']
    .map(id => document.getElementById(id).value.toUpperCase()).filter(Boolean);
  const clash = keys.find((key, i) => keys.indexOf(key) !== i);
  if (clash) hotkeyStatus(t('hotkey.clashAny', {key: clash}), true);
  else hotkeyStatus(t('hotkey.applyToSave'));
}

document.querySelectorAll('.sp-hotkey').forEach(box => {
  const optional = box.dataset.optional === 'true';
  box.addEventListener('focus', () => {
    box.dataset.previous = box.value;
    box.dataset.cleared = '';
    box.value = '';
    box.placeholder = t('hotkey.press');
  });
  box.addEventListener('blur', () => {
    if (!box.value && !box.dataset.cleared) box.value = box.dataset.previous || '';
    box.placeholder = optional ? t('hotkey.none') : '';
  });
  box.addEventListener('keydown', e => {
    if (isPassThroughKey(e)) return;
    e.preventDefault();
    e.stopPropagation();
    // Backspace (8) or Delete (46) removes an optional key (Show/hide).
    if (optional && (e.keyCode === 8 || e.keyCode === 46)) {
      box.value = '';
      box.dataset.cleared = 'true';
      box.blur();
      checkHotkeyClash();
      return;
    }
    const name = hotkeyName(e);
    if (!name) {
      if (isNumpadKey(e)) {
        hotkeyStatus(t('hotkey.numpad'), true);
        return;
      }
      // Raw details let a screenshot of this message diagnose engine quirks.
      hotkeyStatus(t('hotkey.unsupported') + ' ' +
        '(key "' + e.key + '", code "' + e.code + '", keyCode ' + e.keyCode + ')', true);
      return;
    }
    box.value = name;
    box.blur();
    checkHotkeyClash();
  });
});

document.querySelectorAll('.sp-nudge').forEach(button => button.addEventListener('click', event => {
  const axis = button.dataset.axis;
  const slider = document.getElementById(axis === 'position_x' ? 'sp-position-x' : 'sp-position-y');
  previewPosition(axis, Number(slider.value) + Number(button.dataset.step) * (event.shiftKey ? 10 : 1));
}));
document.getElementById('sp-width').addEventListener('input', syncPositionControls);
document.getElementById('sp-height').addEventListener('input', syncPositionControls);
window.addEventListener('pagehide', flushHistory);
window.addEventListener('resize', () => { normalizeConfig(); applyConfig(); });
setInterval(() => {
  const age = config.general.message_fade_time * 1000;
  messages.forEach(entry => entry.el.classList.toggle('fading',
    !interactionOpen && age > 0 && Date.now() - entry.timestamp > age));
}, 1000);
