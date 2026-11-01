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

const runtime = { connected: false, loaded: false, visible: false, profile: '', character: '', gameLanguage: '', host: '' };
// Chat window themes (styles.css body[data-theme] blocks); anything else,
// including a missing value, falls back to the default Skyrim theme.
const THEMES = ['skyrim', 'daedric', 'dwemer', 'classic-mmo', 'modern', 'minimal', 'transparent', 'high-contrast'];
function normalizeTheme(theme) { return THEMES.includes(theme) ? theme : 'skyrim'; }
let interactionOpen = false;
let historySaveTimer = null;
let bootstrapTimer = null;
let searchText = '';
let speakerFilter = '';
let focusPending = false;
let settingsSaveInFlight = null;
let settingsSaveQueued = null;
const PENDING_SETTINGS_KEY = 'chronicle_pending_settings';
// Magelight cannot switch a view between click-through and interactive, so it
// runs two copies of this page: 'hud' only shows the log, 'panel' takes input
// and is the only one that saves. Every other host runs one 'full' page.
let hostRole = 'full';

function setHostRole(role) { hostRole = role === 'hud' || role === 'panel' ? role : 'full'; }
function ownsStorage() { return hostRole !== 'hud'; }

// Magelight hands the panel's state to the hud copy when an interaction ends:
// settings, a cleared history, the window position and the viewed tab.
// Lines with a later timestamp than `cutoff` reached the receiving copy after
// this snapshot was taken (the native side sends every line to both copies).
function exportViewState() {
  const cutoff = messages.reduce((latest, entry) => Math.max(latest, Number(entry.timestamp) || 0), historyClearedAt);
  return JSON.stringify({
    config, activeChannel, search: searchText, speaker: speakerFilter, cutoff,
    messages: messages.map(({channel, speaker, message, timestamp}) => ({channel, speaker, message, timestamp})),
  });
}

function importViewState(state) {
  if (!state || typeof state !== 'object' || !state.config || typeof state.config !== 'object') return false;
  const incoming = Array.isArray(state.messages) ? state.messages : [];
  // Keep this copy's lines that arrived after the snapshot instead of losing
  // them until the next handover; anything older follows the snapshot, so a
  // history cleared in the panel stays cleared.
  const lineKey = entry => [entry.timestamp, entry.channel, entry.speaker, entry.message].join('');
  const known = new Set(incoming.map(lineKey));
  const cutoff = Number(state.cutoff);
  const later = Number.isFinite(cutoff) ? messages
    .filter(entry => (Number(entry.timestamp) || 0) > cutoff && !known.has(lineKey(entry)))
    .map(({channel, speaker, message, timestamp}) => ({channel, speaker, message, timestamp})) : [];
  config = deepMerge(deepMerge({}, DEFAULT_CONFIG), state.config);
  messages.length = 0;
  document.getElementById('message-list').replaceChildren();
  unreadCount = 0;
  userScrolledUp = false;
  if (DEFAULT_CONFIG.channels[state.activeChannel]) activeChannel = state.activeChannel;
  normalizeConfig();
  applyConfig();
  incoming.concat(later).forEach(entry => addMessage(entry, false));
  refreshSpeakerOptions();
  document.getElementById('history-search').value = typeof state.search === 'string' ? state.search : '';
  document.getElementById('speaker-filter').value = typeof state.speaker === 'string' ? state.speaker : '';
  switchChannel(activeChannel);
  wakeWindow();
  return true;
}

function interactionHint() {
  return t('status.hint', {interact: config.general.interact_hotkey, settings: config.general.config_hotkey.toUpperCase()});
}

function statusText(text) {
  document.getElementById('status-text').textContent = text;
  document.getElementById('sp-status').textContent = text;
}

function beginInteraction() {
  if (!runtime.connected || !runtime.loaded || !runtime.visible) return false;
  if (interactionOpen) return true;
  interactionOpen = true;
  focusPending = true;
  document.body.classList.add('interacting');
  refreshSpeakerOptions();
  statusText(t('status.requestingMouse'));
  if (!sendToGame('focusAcquired', '')) {
    focusResult(false, 'status.noConnection');
    return false;
  }
  return true;
}

function closeInteraction(release = true) {
  if (settingsOpen && typeof discardSettingsPreview === 'function') discardSettingsPreview();
  focusPending = false;
  interactionOpen = false;
  inputOpen = false;
  settingsOpen = false;
  document.body.classList.remove('interacting', 'mouse-active');
  document.getElementById('input-bar').classList.add('hidden');
  document.getElementById('settings-overlay').classList.add('hidden');
  document.getElementById('copy-output').hidden = true;
  if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
  statusText(interactionHint());
  if (typeof wakeWindow === 'function') wakeWindow(); // auto-hide counts from now
  if (release) sendToGame('focusReleased', '');
}

function focusResult(success, reason) {
  if (hostRole === 'hud') return false;
  if (success && (!interactionOpen || !focusPending)) {
    // A grant that arrives after the interaction closed would leave Chronicle
    // holding the keyboard with nothing open; hand it straight back.
    if (!interactionOpen) sendToGame('focusReleased', '');
    return false;
  }
  focusPending = false;
  if (!success) closeInteraction(false);
  document.body.classList.toggle('mouse-active', success);
  statusText(t(reason));
  if (success && inputOpen) document.getElementById('chat-input').focus();
  return true;
}

function setVisibility(visible) {
  runtime.visible = visible;
  document.body.classList.toggle('runtime-hidden', !visible);
  if (!visible) closeInteraction(false);
}

// Called by the game side after the show/hide hotkey; the native side owns the
// visibility. Saved only when "Remember hidden state" is on.
function setUserHidden(hidden) {
  if (!config.general.remember_hidden || config.general.user_hidden === hidden) return;
  config.general.user_hidden = hidden;
  persistConfig();
}

function setRuntime(data) {
  if (runtime.profile !== data.profile) {
    flushHistory();
    messages.length = 0;
    document.getElementById('message-list').replaceChildren();
    runtime.profile = data.profile;
    inputHistory.length = 0;
    inputHistoryIndex = -1;
    unreadCount = 0;
    userScrolledUp = false;
    showUnread(0);
    restoreHistory();
    refreshSpeakerOptions();
    // History kept only here so far: native takes it once, for this character.
    if (data.historyEmpty === true && runtime.profile && messages.length) {
      sendToGame('historyImport', {profile: runtime.profile,
        lines: messages.map(({channel, speaker, message, timestamp}) => ({channel, speaker, message, timestamp}))});
    }
  }
  runtime.loaded = data.loaded;
  runtime.character = data.character;
}

function historyKey() { return 'chronicle_history_' + runtime.profile; }

function flushHistory() {
  if (historySaveTimer) clearTimeout(historySaveTimer);
  historySaveTimer = null;
  if (!ownsStorage() || !runtime.profile || !config.general.persist_history) return;
  try {
    localStorage.setItem(historyKey(), JSON.stringify(messages.slice(-config.general.max_messages)
      .map(({channel, speaker, message, timestamp}) => ({channel, speaker, message, timestamp}))));
  } catch (_) { statusText(t('status.historyNotSaved')); }
}

function scheduleHistorySave() {
  if (!historySaveTimer) historySaveTimer = setTimeout(flushHistory, 500);
}

function nativeBootstrap(data) {
  clearInterval(bootstrapTimer);
  runtime.connected = true;
  runtime.nativeHotkeys = data.nativeHotkeys === true;
  runtime.gameLanguage = data.gameLanguage || ''; // Skyrim's sLanguage, for "Auto"
  runtime.host = data.host || 'PrismaUI';
  // A host whose pages cannot fetch config.json (Magelight) sends the bundled
  // first-run defaults; the saved settings file still wins.
  if (data.defaults && typeof data.defaults === 'object' && !Array.isArray(data.defaults)) {
    config = deepMerge(config, data.defaults);
  }
  if (data.settings) config = deepMerge(deepMerge({}, DEFAULT_CONFIG), data.settings);
  normalizeConfig();
  applyConfig();
  statusText(data.warning || t('status.version', {version: data.version, hint: interactionHint()}));
  // Migrate existing browser preferences once if no native settings file exists.
  if (!retryPendingSettings() && !data.settings && !data.warning) persistConfig();
}

function nativeHotkey(action) {
  // The panel copy answers; a release from here would cancel its focus request.
  if (hostRole === 'hud') return false;
  if (!runtime.connected || !runtime.loaded || !runtime.visible) {
    sendToGame('focusReleased', '');
    return false;
  }
  const handled = action === 'settings' ? openSettings() :
    action === 'input' ? openInput() :
    action === 'interact' ? beginInteraction() : false;
  if (!handled) sendToGame('focusReleased', '');
  return handled;
}

function startHandshake() {
  const handshake = () => {
    if (!runtime.connected) sendToGame('uiReady', '');
  };
  bootstrapTimer = setInterval(handshake, 500);
  handshake();
}

// detail is a translation key (or finished text); param fills its {detail}.
function saveResult(ok, detail, param) {
  statusText(t(detail, {detail: param}));
  document.getElementById('sp-status').classList.toggle('error', !ok);
}

function savePendingSettings(serialized) {
  try {
    localStorage.setItem(PENDING_SETTINGS_KEY, serialized);
    return true;
  } catch (_) {
    return false;
  }
}

function readPendingSettings() {
  try {
    const serialized = localStorage.getItem(PENDING_SETTINGS_KEY);
    if (!serialized) return null;
    const parsed = JSON.parse(serialized);
    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
      clearPendingSettings();
      return null;
    }
    return { serialized, settings: parsed };
  } catch (_) {
    clearPendingSettings();
    return null;
  }
}

function clearPendingSettings() {
  try { localStorage.removeItem(PENDING_SETTINGS_KEY); } catch (_) {}
}

function retryPendingSettings() {
  if (!ownsStorage()) return false;
  const pending = readPendingSettings();
  if (!pending) return false;

  config = deepMerge(deepMerge({}, DEFAULT_CONFIG), pending.settings);
  normalizeConfig();
  applyConfig();
  dispatchSettingsSave(JSON.stringify(config, null, 2));
  return true;
}

function dispatchSettingsSave(serialized) {
  settingsSaveInFlight = serialized;
  statusText(t('status.savingSettings'));
  if (!sendToGame('settingsChanged', serialized)) {
    settingsSaveInFlight = null;
    const latest = settingsSaveQueued || serialized;
    settingsSaveQueued = null;
    const cached = savePendingSettings(latest);
    saveResult(false, cached ? 'status.pendingRetry' : 'status.sessionOnly');
  }
}

function persistConfig() {
  if (!ownsStorage()) return;
  const serialized = JSON.stringify(config, null, 2);
  try {
    localStorage.setItem('chronicle_config', serialized);
  } catch (_) {}

  if (settingsSaveInFlight) {
    settingsSaveQueued = serialized;
    statusText(t('status.savingLatest'));
    return;
  }
  dispatchSettingsSave(serialized);
}

function settingsSaveResult(ok, detail, param) {
  if (!settingsSaveInFlight) return;
  const attempted = settingsSaveInFlight;
  const queued = settingsSaveQueued;
  settingsSaveInFlight = null;
  settingsSaveQueued = null;

  if (queued) {
    // Keep the newest payload recoverable while it waits for its own result.
    savePendingSettings(queued);
    saveResult(ok, detail, param);
    dispatchSettingsSave(queued);
  } else if (ok) {
    clearPendingSettings();
    saveResult(ok, detail, param);
  } else if (!savePendingSettings(attempted)) {
    saveResult(ok, t(detail, {detail: param}) + ' ' + t('status.notCached'));
  } else {
    saveResult(ok, detail, param);
  }
}

function diagnosticsResult(data) {
  document.getElementById('diagnostics-output').textContent = [
    'Chronicle ' + data.version + ' / ' + (data.host || 'PrismaUI') + ' API ' + data.api,
    t('diag.connection', {value: t(data.bridge ? 'diag.connected' : 'diag.missing')}),
    t('diag.state', {loaded: data.gameLoaded, visible: data.visible}),
    t('diag.focus', {focus: data.focus, poller: data.poller}),
    t('diag.hudPath', {value: data.hudPath || t('diag.noneResolved')}),
    t('diag.messages', {value: data.messages}),
    t('diag.lastSubtitle', {value: data.lastSubtitle || t('diag.noneYet')}),
    t('diag.history', {lines: data.historyLines ?? 0, kb: data.historyKb ?? 0, file: data.historyFile || '-'}),
    t('diag.api', {subscribers: data.subscribers ?? 0,
      commands: Array.isArray(data.commands) && data.commands.length ? data.commands.map(c => '/' + c).join(', ') : '-'}),
    t('diag.attribution', {form: data.lastSpeakerFormId || '-', quest: data.lastOwnerQuest || '-'}),
    t('diag.settings', {value: data.settingsFile}),
  ].join('\n');
}

function matchesFilters(entry) {
  return (!speakerFilter || (entry.speaker || '') === speakerFilter) &&
    (!searchText || ((entry.speaker || '') + ' ' + entry.message).toLowerCase().includes(searchText));
}

function refreshFilters() {
  searchText = document.getElementById('history-search').value.toLowerCase().trim();
  speakerFilter = document.getElementById('speaker-filter').value;
  messages.forEach(entry => entry.el.classList.toggle('hidden',
    !shouldShowMessage(entry.channel) || !matchesFilters(entry)));
}

function refreshSpeakerOptions() {
  const select = document.getElementById('speaker-filter');
  const selected = select.value;
  select.replaceChildren();
  const all = document.createElement('option');
  all.value = ''; all.textContent = t('ui.allSpeakers'); select.appendChild(all);
  if (all.dataset) all.dataset.i18n = 'ui.allSpeakers'; // re-translated on language change
  [...new Set(messages.map(entry => entry.speaker).filter(Boolean))].sort().forEach(name => {
    const option = document.createElement('option'); option.value = name; option.textContent = name;
    select.appendChild(option);
  });
  select.value = [...select.options].some(option => option.value === selected) ? selected : '';
  speakerFilter = select.value;
  if (selected !== speakerFilter) refreshFilters();
}

function visibleHistoryText() {
  return messages.filter(entry => shouldShowMessage(entry.channel) && matchesFilters(entry))
    .map(entry => '[' + formatTimestamp(entry.timestamp) + '] ' +
      (entry.speaker ? entry.speaker + ': ' : '') + entry.message).join('\n');
}

function copyHistory() {
  // A selectable fallback works in Ultralight even without Clipboard API access.
  const box = document.getElementById('copy-output');
  box.hidden = false; box.value = visibleHistoryText(); box.focus(); box.select();
  let copied = false;
  try { copied = document.execCommand('copy'); } catch (_) {}
  statusText(t(copied ? 'status.copied' : 'status.pressCtrlC'));
}

function resetWindowPosition() {
  const win = document.getElementById('chronicle-window');
  win.style.left = ''; win.style.top = ''; win.style.width = ''; win.style.height = '';
  config.window.position_x = 20;
  config.window.position_y = Math.max(20, window.innerHeight - config.window.height - 40);
  config.window.anchor = 'custom';
  applyConfig();
  persistConfig();
}
