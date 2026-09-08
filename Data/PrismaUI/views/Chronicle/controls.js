'use strict';

const runtime = { connected: false, loaded: false, visible: false, profile: '', character: '' };
let interactionOpen = false;
let historySaveTimer = null;
let bootstrapTimer = null;
let searchText = '';
let speakerFilter = '';
let focusPending = false;
let settingsSaveInFlight = null;
const PENDING_SETTINGS_KEY = 'chronicle_pending_settings';

function interactionHint() {
  return config.general.interact_hotkey + ': interact · Shift+' + config.general.config_hotkey.toUpperCase() + ': settings';
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
  statusText('Requesting mouse…');
  if (!sendToGame('focusAcquired', '')) {
    focusResult(false, 'Game connection unavailable');
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
  if (release) sendToGame('focusReleased', '');
}

function focusResult(success, reason) {
  if (success && (!interactionOpen || !focusPending)) return false;
  focusPending = false;
  if (!success) closeInteraction(false);
  document.body.classList.toggle('mouse-active', success);
  statusText(reason);
  if (success && inputOpen) document.getElementById('chat-input').focus();
  return true;
}

function setVisibility(visible) {
  runtime.visible = visible;
  document.body.classList.toggle('runtime-hidden', !visible);
  if (!visible) closeInteraction(false);
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
  }
  runtime.loaded = data.loaded;
  runtime.character = data.character;
}

function historyKey() { return 'chronicle_history_' + runtime.profile; }

function flushHistory() {
  if (historySaveTimer) clearTimeout(historySaveTimer);
  historySaveTimer = null;
  if (!runtime.profile || !config.general.persist_history) return;
  try {
    localStorage.setItem(historyKey(), JSON.stringify(messages.slice(-config.general.max_messages)
      .map(({channel, speaker, message, timestamp}) => ({channel, speaker, message, timestamp}))));
  } catch (_) { statusText('History could not be saved; export it before closing'); }
}

function scheduleHistorySave() {
  if (!historySaveTimer) historySaveTimer = setTimeout(flushHistory, 500);
}

function nativeBootstrap(data) {
  clearInterval(bootstrapTimer);
  runtime.connected = true;
  runtime.nativeHotkeys = data.nativeHotkeys === true;
  if (data.settings) config = deepMerge(deepMerge({}, DEFAULT_CONFIG), data.settings);
  normalizeConfig();
  applyConfig();
  statusText(data.warning || ('Chronicle ' + data.version + ' · ' + interactionHint()));
  // Migrate existing browser preferences once if no native settings file exists.
  if (!retryPendingSettings() && !data.settings && !data.warning) persistConfig();
}

function nativeHotkey(action) {
  if (!runtime.connected || !runtime.loaded || !runtime.visible) return false;
  if (action === 'settings') openSettings();
  else if (action === 'input') openInput();
  else if (action === 'interact') beginInteraction();
  return true;
}

function startHandshake() {
  const handshake = () => {
    if (!runtime.connected) sendToGame('uiReady', '');
  };
  bootstrapTimer = setInterval(handshake, 500);
  handshake();
}

function saveResult(ok, detail) {
  statusText(detail);
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
  const pending = readPendingSettings();
  if (!pending) return false;

  config = deepMerge(deepMerge({}, DEFAULT_CONFIG), pending.settings);
  normalizeConfig();
  applyConfig();
  settingsSaveInFlight = JSON.stringify(config, null, 2);
  statusText('Pending settings found — retrying native save…');
  if (!sendToGame('settingsChanged', settingsSaveInFlight)) {
    settingsSaveInFlight = null;
    statusText('Settings remain pending; native connection unavailable');
  }
  return true;
}

function settingsSaveResult(ok, detail) {
  const attempted = settingsSaveInFlight;
  settingsSaveInFlight = null;
  if (attempted) {
    if (ok) {
      clearPendingSettings();
    } else if (!savePendingSettings(attempted)) {
      detail = detail + ' Current session only: pending settings could not be cached.';
    }
  }
  saveResult(ok, detail);
}

function diagnosticsResult(data) {
  document.getElementById('diagnostics-output').textContent =
    'Chronicle ' + data.version + ' / PrismaUI API ' + data.api + '\n' +
    'Game connection: ' + (data.bridge ? 'connected' : 'missing') + '\n' +
    'Game loaded: ' + data.gameLoaded + ' · Window visible: ' + data.visible + '\n' +
    'Mouse focus: ' + data.focus + ' · Subtitle polling: ' + data.poller + '\n' +
    'HUD subtitle path: ' + (data.hudPath || '(none resolved)') + '\n' +
    'Messages captured this load: ' + data.messages + '\n' +
    'Last subtitle: ' + (data.lastSubtitle || '(none captured yet)') + '\n' +
    'Settings: ' + data.settingsFile;
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
  all.value = ''; all.textContent = 'All speakers'; select.appendChild(all);
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
  statusText(copied ? 'Filtered history copied' : 'Press Ctrl+C to copy the selected history');
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
