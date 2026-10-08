'use strict';

const runtime = { connected: false, loaded: false, visible: false, profile: '', character: '', gameLanguage: '' };
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
  if (success && (!interactionOpen || !focusPending)) return false;
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
  if (data.settings) config = deepMerge(deepMerge({}, DEFAULT_CONFIG), data.settings);
  normalizeConfig();
  applyConfig();
  statusText(data.warning || t('status.version', {version: data.version, hint: interactionHint()}));
  // Migrate existing browser preferences once if no native settings file exists.
  if (!retryPendingSettings() && !data.settings && !data.warning) persistConfig();
}

function nativeHotkey(action) {
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
    'Chronicle ' + data.version + ' / PrismaUI API ' + data.api,
    t('diag.connection', {value: t(data.bridge ? 'diag.connected' : 'diag.missing')}),
    t('diag.state', {loaded: data.gameLoaded, visible: data.visible}),
    t('diag.focus', {focus: data.focus, poller: data.poller}),
    t('diag.hudPath', {value: data.hudPath || t('diag.noneResolved')}),
    t('diag.messages', {value: data.messages}),
    t('diag.lastSubtitle', {value: data.lastSubtitle || t('diag.noneYet')}),
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
