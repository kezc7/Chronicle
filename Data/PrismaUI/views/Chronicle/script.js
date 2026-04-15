/* ============================================================
   Chronicle — Chat Window Script
   ============================================================ */

'use strict';

// ---------------------------------------------------------------------------
// Config defaults
// ---------------------------------------------------------------------------

const DEFAULT_CONFIG = {
  general: {
    enabled: true,
    input_hotkey: 'Enter',
    config_hotkey: 'N',       // Shift + this key opens the settings panel
    max_messages: 100,
    message_fade_time: 0,
    hide_in_menus: true,
    hide_in_photo_mode: true,
  },
  window: {
    position_x: 20,
    position_y: null,   // null = use CSS default (60vh)
    width: 520,
    height: 240,
    opacity: 0.65,
    font_size: 13,
  },
  channels: {
    all:       { enabled: true,  colour: null,       prefix: '',         show_timestamp: false },
    dialogue:  { enabled: true,  colour: '#ffffff',  prefix: '',         show_timestamp: false },
    system:    { enabled: true,  colour: '#f5c518',  prefix: '[SYSTEM]', show_timestamp: false },
    skyrimnet: { enabled: true,  colour: '#7ec8f5',  prefix: '',         show_timestamp: false },
    narrator:  { enabled: true,  colour: '#c084fc',  prefix: '[NARR]',   show_timestamp: false },
    player:    { enabled: true,  colour: '#86efac',  prefix: '',         show_timestamp: false },
    combat:    { enabled: true,  colour: '#fb923c',  prefix: '[COMBAT]', show_timestamp: false },
    console:   { enabled: false, colour: '#94a3b8',  prefix: '[CON]',    show_timestamp: false },
  },
};

let config = deepMerge({}, DEFAULT_CONFIG);
let activeChannel = 'all';
const messages = [];

// ---------------------------------------------------------------------------
// Utility
// ---------------------------------------------------------------------------

function deepMerge(target, source) {
  for (const key of Object.keys(source)) {
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      target[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
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

// ---------------------------------------------------------------------------
// Config loading / saving
// ---------------------------------------------------------------------------

async function loadConfig() {
  try {
    const res = await fetch('config.json?_=' + Date.now());
    if (res.ok) {
      const saved = await res.json();
      config = deepMerge(config, saved);
    }
  } catch (_) {
    // First run or file missing — defaults apply
  }
  applyConfig();
}

function applyConfig() {
  const { window: win } = config;
  const root = document.documentElement;

  if (win.position_x != null) root.style.setProperty('--win-x', win.position_x + 'px');
  if (win.position_y != null) root.style.setProperty('--win-y', win.position_y + 'px');
  root.style.setProperty('--win-width',  win.width  + 'px');
  root.style.setProperty('--win-height', win.height + 'px');
  root.style.setProperty('--opacity',    win.opacity);
  root.style.setProperty('--font-size',  win.font_size + 'px');

  // Channel tab visibility
  document.querySelectorAll('.tab').forEach(tab => {
    const ch = tab.dataset.channel;
    if (!ch) return;
    const chCfg = config.channels[ch];
    tab.classList.toggle('hidden-channel', chCfg ? !chCfg.enabled : false);
  });

  // Dynamic channel colours injected as a <style> block
  const styleId = 'chronicle-channel-colours';
  let styleEl = document.getElementById(styleId);
  if (!styleEl) {
    styleEl = document.createElement('style');
    styleEl.id = styleId;
    document.head.appendChild(styleEl);
  }
  const rules = Object.entries(config.channels)
    .filter(([, v]) => v.colour)
    .map(([ch, v]) => `.message[data-channel="${ch}"] .speaker { color: ${v.colour}; }`);
  styleEl.textContent = rules.join('\n');
}

function persistConfig() {
  if (window.skse && typeof window.skse.writeFile === 'function') {
    window.skse.writeFile('config.json', JSON.stringify(config, null, 2));
  } else {
    localStorage.setItem('chronicle_config', JSON.stringify(config));
  }
}

// ---------------------------------------------------------------------------
// Message rendering
// ---------------------------------------------------------------------------

/**
 * Called by the C++ plugin via PrismaUI Invoke.
 * @param {object} data - { channel, speaker, message, timestamp }
 */
function addMessage(data) {
  if (!config.general.enabled) return;
  if (typeof data === 'string') {
    try { data = JSON.parse(data); } catch (_) { return; }
  }

  const { channel, speaker, message, timestamp } = data;
  const chCfg = config.channels[channel];

  if (chCfg && !chCfg.enabled) return;

  const entry = { channel, speaker, message, timestamp: timestamp || Date.now(), el: null };
  messages.push(entry);

  while (messages.length > config.general.max_messages) {
    const old = messages.shift();
    old.el?.remove();
  }

  const el = buildMessageEl(entry, chCfg);
  entry.el = el;
  el.classList.toggle('hidden', !shouldShowMessage(channel));

  document.getElementById('message-list').appendChild(el);
  scrollToBottom();

  if (config.general.message_fade_time > 0) {
    setTimeout(() => el.classList.add('fading'), config.general.message_fade_time * 1000);
  }
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
  return div;
}

function shouldShowMessage(channel) {
  if (activeChannel === 'all') return channel !== 'console';
  return channel === activeChannel;
}

function scrollToBottom() {
  const list = document.getElementById('message-list');
  list.scrollTop = list.scrollHeight;
}

// ---------------------------------------------------------------------------
// Channel tabs
// ---------------------------------------------------------------------------

function switchChannel(channel) {
  activeChannel = channel;

  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.toggle('active', tab.dataset.channel === channel);
  });

  document.getElementById('input-channel-label').textContent =
    '[' + channel.charAt(0).toUpperCase() + channel.slice(1) + ']';

  document.querySelectorAll('.message').forEach(el => {
    el.classList.toggle('hidden', !shouldShowMessage(el.dataset.channel));
  });

  scrollToBottom();
}

// ---------------------------------------------------------------------------
// Input bar
// ---------------------------------------------------------------------------

let inputOpen = false;

function openInput() {
  if (inputOpen || settingsOpen) return;
  inputOpen = true;

  const bar = document.getElementById('input-bar');
  const input = document.getElementById('chat-input');
  bar.classList.remove('hidden');
  input.value = '';
  input.focus();

  sendToGame('focusAcquired', '');
}

function closeInput() {
  if (!inputOpen) return;
  inputOpen = false;

  document.getElementById('input-bar').classList.add('hidden');
  document.getElementById('chat-input').blur();

  sendToGame('focusReleased', '');
}

function submitInput() {
  const input = document.getElementById('chat-input');
  const text = input.value.trim();
  if (!text) {
    closeInput();
    return;
  }

  if (activeChannel === 'console') {
    sendToGame('consoleCommand', text);
  } else {
    sendToGame('playerInput', text);
  }

  closeInput();
}

// ---------------------------------------------------------------------------
// Settings panel
// ---------------------------------------------------------------------------

let settingsOpen = false;

const CHANNEL_LABELS = {
  all: 'All', dialogue: 'Dialogue', system: 'System',
  skyrimnet: 'SkyrimNet', narrator: 'Narrator',
  player: 'Player', combat: 'Combat', console: 'Console',
};

function openSettings() {
  if (settingsOpen) return;
  // Close input bar first if open
  closeInput();
  settingsOpen = true;

  populateSettingsForm();
  document.getElementById('settings-overlay').classList.remove('hidden');
  sendToGame('focusAcquired', '');
}

function closeSettings() {
  if (!settingsOpen) return;
  settingsOpen = false;

  document.getElementById('settings-overlay').classList.add('hidden');
  sendToGame('focusReleased', '');
}

function toggleSettings() {
  if (settingsOpen) closeSettings();
  else openSettings();
}

function populateSettingsForm() {
  const g = config.general;
  const w = config.window;

  document.getElementById('sp-enabled').checked     = g.enabled;
  document.getElementById('sp-hide-menus').checked  = g.hide_in_menus;
  document.getElementById('sp-hide-photo').checked  = g.hide_in_photo_mode;
  document.getElementById('sp-config-hotkey').value = g.config_hotkey || 'N';
  document.getElementById('sp-input-hotkey').value  = g.input_hotkey  || 'Enter';
  document.getElementById('sp-max-messages').value  = g.max_messages;

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

  buildChannelRows();
}

function buildChannelRows() {
  const container = document.getElementById('sp-channel-list');
  container.innerHTML = '';

  for (const [ch, label] of Object.entries(CHANNEL_LABELS)) {
    const cfg = config.channels[ch] || DEFAULT_CONFIG.channels[ch];
    const row = document.createElement('div');
    row.className = 'sp-ch-row';
    row.dataset.channel = ch;

    row.innerHTML = `
      <span class="sp-ch-name">${label}</span>
      <label class="sp-toggle">
        <input type="checkbox" class="ch-enabled" ${cfg.enabled ? 'checked' : ''} />
        <span class="sp-track"></span>
      </label>
      <input type="text" class="ch-prefix" value="${escapeHtml(cfg.prefix || '')}"
             placeholder="Prefix" maxlength="16" />
      <input type="color" class="ch-colour" value="${cfg.colour || '#ffffff'}"
             ${ch === 'all' ? 'disabled' : ''} />
    `;

    container.appendChild(row);
  }
}

function collectSettingsFromForm() {
  const channels = {};
  document.querySelectorAll('.sp-ch-row').forEach(row => {
    const ch = row.dataset.channel;
    channels[ch] = {
      enabled:        row.querySelector('.ch-enabled').checked,
      colour:         ch === 'all' ? null : row.querySelector('.ch-colour').value,
      prefix:         row.querySelector('.ch-prefix').value,
      show_timestamp: (config.channels[ch] || {}).show_timestamp || false,
    };
  });

  return {
    general: {
      enabled:            document.getElementById('sp-enabled').checked,
      config_hotkey:      document.getElementById('sp-config-hotkey').value || 'N',
      input_hotkey:       document.getElementById('sp-input-hotkey').value  || 'Enter',
      max_messages:       parseInt(document.getElementById('sp-max-messages').value, 10) || 100,
      message_fade_time:  parseInt(document.getElementById('sp-fade-time').value, 10) || 0,
      hide_in_menus:      document.getElementById('sp-hide-menus').checked,
      hide_in_photo_mode: document.getElementById('sp-hide-photo').checked,
    },
    window: {
      width:     parseInt(document.getElementById('sp-width').value, 10)      || 520,
      height:    parseInt(document.getElementById('sp-height').value, 10)     || 240,
      opacity:   parseFloat(document.getElementById('sp-opacity').value)      || 0.65,
      font_size: parseInt(document.getElementById('sp-font-size').value, 10)  || 13,
      position_x: config.window.position_x,
      position_y: config.window.position_y,
    },
    channels,
  };
}

function applyAndSaveSettings() {
  const newCfg = collectSettingsFromForm();
  config = deepMerge(config, newCfg);
  applyConfig();
  persistConfig();
  sendToGame('settingsChanged', JSON.stringify(config));
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

// Settings close button
document.getElementById('sp-close').addEventListener('click', closeSettings);

// Settings Apply / Reset
document.getElementById('sp-apply').addEventListener('click', () => {
  applyAndSaveSettings();
  closeSettings();
});

document.getElementById('sp-reset').addEventListener('click', () => {
  config = deepMerge({}, DEFAULT_CONFIG);
  populateSettingsForm();
  applyConfig();
});

// Gear icon button
document.getElementById('settings-btn').addEventListener('click', e => {
  e.stopPropagation();
  toggleSettings();
});

// ---------------------------------------------------------------------------
// PrismaUI bridge
// ---------------------------------------------------------------------------

function sendToGame(eventName, data) {
  if (window.skse && typeof window.skse.sendEvent === 'function') {
    window.skse.sendEvent(eventName, typeof data === 'string' ? data : JSON.stringify(data));
  }
}

// ---------------------------------------------------------------------------
// Window visibility (called by C++ plugin)
// ---------------------------------------------------------------------------

function hideWindow() {
  document.getElementById('chronicle-window').classList.add('hidden');
}

function showWindow() {
  const win = document.getElementById('chronicle-window');
  win.classList.remove('hidden');
  win.classList.add('fading-in');
  setTimeout(() => win.classList.remove('fading-in'), 300);
}

// ---------------------------------------------------------------------------
// Save window position/size
// ---------------------------------------------------------------------------

function saveWindowState(x, y, w, h) {
  config.window.position_x = x;
  config.window.position_y = y;
  config.window.width = w;
  config.window.height = h;
  persistConfig();
}

// ---------------------------------------------------------------------------
// Drag to move
// ---------------------------------------------------------------------------

(function initDrag() {
  const win = document.getElementById('chronicle-window');
  let dragging = false;
  let startX, startY, origX, origY;

  const handle = document.getElementById('channel-tabs');

  handle.addEventListener('mousedown', e => {
    if (e.target.classList.contains('tab') || e.target.id === 'settings-btn') return;
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
    win.style.left = (origX + e.clientX - startX) + 'px';
    win.style.top  = (origY + e.clientY - startY) + 'px';
  });

  document.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    const style = getComputedStyle(win);
    saveWindowState(
      parseInt(style.left, 10), parseInt(style.top, 10),
      parseInt(style.width, 10), parseInt(style.height, 10),
    );
  });
})();

// ---------------------------------------------------------------------------
// Resize handle
// ---------------------------------------------------------------------------

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
  positionHandle();
  new ResizeObserver(positionHandle).observe(win);

  handle.addEventListener('mousedown', e => {
    resizing = true;
    startX = e.clientX; startY = e.clientY;
    origW = win.offsetWidth; origH = win.offsetHeight;
    e.preventDefault();
  });

  document.addEventListener('mousemove', e => {
    if (!resizing) return;
    win.style.width  = Math.max(280, origW + (e.clientX - startX)) + 'px';
    win.style.height = Math.max(120, origH + (e.clientY - startY)) + 'px';
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

// ---------------------------------------------------------------------------
// Keyboard handling
// ---------------------------------------------------------------------------

document.addEventListener('keydown', e => {
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
    } else if (e.key === 'Tab') {
      const tabs = [...document.querySelectorAll('.tab[data-channel]')];
      const current = tabs.findIndex(t => t.dataset.channel === activeChannel);
      switchChannel(tabs[(current + 1) % tabs.length].dataset.channel);
      e.preventDefault();
    }
    return;
  }

  // Shift + config_hotkey → open settings
  const cfgKey = (config.general.config_hotkey || 'N').toUpperCase();
  if (e.shiftKey && e.key === cfgKey) {
    openSettings();
    e.preventDefault();
    return;
  }

  // Input hotkey → open input bar
  const inputKey = config.general.input_hotkey || 'Enter';
  if (e.key === inputKey) {
    openInput();
    e.preventDefault();
  }
});

// ---------------------------------------------------------------------------
// Channel tab clicks
// ---------------------------------------------------------------------------

document.getElementById('channel-tabs').addEventListener('click', e => {
  const tab = e.target.closest('.tab[data-channel]');
  if (tab) switchChannel(tab.dataset.channel);
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

loadConfig().then(() => {
  const saved = localStorage.getItem('chronicle_config');
  if (saved) {
    try { config = deepMerge(config, JSON.parse(saved)); applyConfig(); } catch (_) {}
  }
});
