/* ============================================================
   Chronicle — Chat Window Script
   ============================================================ */

'use strict';

// ---------------------------------------------------------------------------
// Config — loaded from config.json, then applied
// ---------------------------------------------------------------------------

const DEFAULT_CONFIG = {
  general: {
    enabled: true,
    input_hotkey: 'Enter',
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
    all:       { enabled: true,  colour: null,      prefix: '',        show_timestamp: false },
    dialogue:  { enabled: true,  colour: '#ffffff',  prefix: '',        show_timestamp: false },
    system:    { enabled: true,  colour: '#f5c518',  prefix: '[SYSTEM]', show_timestamp: false },
    skyrimnet: { enabled: true,  colour: '#7ec8f5',  prefix: '',        show_timestamp: false },
    narrator:  { enabled: true,  colour: '#c084fc',  prefix: '[NARR]',  show_timestamp: false },
    player:    { enabled: true,  colour: '#86efac',  prefix: '',        show_timestamp: false },
    combat:    { enabled: true,  colour: '#fb923c',  prefix: '[COMBAT]', show_timestamp: false },
    console:   { enabled: false, colour: '#94a3b8',  prefix: '[CON]',   show_timestamp: false },
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
// Config loading
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
  const { window: win, general } = config;
  const root = document.documentElement;

  if (win.position_x != null) root.style.setProperty('--win-x', win.position_x + 'px');
  if (win.position_y != null) root.style.setProperty('--win-y', win.position_y + 'px');
  root.style.setProperty('--win-width',  win.width  + 'px');
  root.style.setProperty('--win-height', win.height + 'px');
  root.style.setProperty('--opacity',    win.opacity);
  root.style.setProperty('--font-size',  win.font_size + 'px');

  // Update channel tab visibility
  document.querySelectorAll('.tab').forEach(tab => {
    const ch = tab.dataset.channel;
    if (ch === 'all') return; // All is always visible
    const chCfg = config.channels[ch];
    tab.classList.toggle('hidden-channel', chCfg ? !chCfg.enabled : false);
  });

  // Apply dynamic speaker colours from config
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

  // Drop if channel is disabled (console is disabled by default)
  if (chCfg && !chCfg.enabled) return;

  const entry = { channel, speaker, message, timestamp: timestamp || Date.now(), el: null };
  messages.push(entry);

  // Trim history
  while (messages.length > config.general.max_messages) {
    const old = messages.shift();
    old.el?.remove();
  }

  const el = buildMessageEl(entry, chCfg);
  entry.el = el;

  // Only show if current tab matches
  el.classList.toggle('hidden', !shouldShowMessage(channel));

  document.getElementById('message-list').appendChild(el);
  scrollToBottom();

  // Schedule fade-out if configured
  if (config.general.message_fade_time > 0) {
    setTimeout(() => {
      el.classList.add('fading');
    }, config.general.message_fade_time * 1000);
  }
}

function buildMessageEl(entry, chCfg) {
  const { channel, speaker, message, timestamp } = entry;
  const div = document.createElement('div');
  div.className = 'message';
  div.dataset.channel = channel;

  let html = '';

  // Timestamp
  if (chCfg?.show_timestamp) {
    html += `<span class="timestamp">${escapeHtml(formatTimestamp(timestamp))}</span>`;
  }

  // Prefix
  if (chCfg?.prefix) {
    html += `<span class="prefix">${escapeHtml(chCfg.prefix)}</span>`;
  }

  // Speaker
  if (speaker) {
    html += `<span class="speaker">${escapeHtml(speaker)}: </span>`;
  }

  // Message text
  html += `<span class="text">${escapeHtml(message)}</span>`;

  div.innerHTML = html;
  return div;
}

function shouldShowMessage(channel) {
  if (activeChannel === 'all') {
    // All tab shows everything except console
    return channel !== 'console';
  }
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

  // Update tab styles
  document.querySelectorAll('.tab').forEach(tab => {
    tab.classList.toggle('active', tab.dataset.channel === channel);
  });

  // Update input label
  document.getElementById('input-channel-label').textContent =
    '[' + channel.charAt(0).toUpperCase() + channel.slice(1) + ']';

  // Show/hide messages
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
  if (inputOpen) return;
  inputOpen = true;

  const bar = document.getElementById('input-bar');
  const input = document.getElementById('chat-input');
  bar.classList.remove('hidden');
  input.value = '';
  input.focus();

  // Notify C++ to acquire focus (fixes mouse stuck bug)
  sendToGame('focusAcquired', '');
}

function closeInput() {
  if (!inputOpen) return;
  inputOpen = false;

  const bar = document.getElementById('input-bar');
  const input = document.getElementById('chat-input');
  bar.classList.add('hidden');
  input.blur();

  // Notify C++ to release focus
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

function persistConfig() {
  // PrismaUI exposes a file-write API; fall back to localStorage in dev
  if (window.skse && typeof window.skse.writeFile === 'function') {
    window.skse.writeFile('config.json', JSON.stringify(config, null, 2));
  } else {
    localStorage.setItem('chronicle_config', JSON.stringify(config));
  }
}

// ---------------------------------------------------------------------------
// Drag to move
// ---------------------------------------------------------------------------

(function initDrag() {
  const win = document.getElementById('chronicle-window');
  let dragging = false;
  let startX, startY, origX, origY;

  // Drag only from the tabs bar (not message area)
  const handle = document.getElementById('channel-tabs');

  handle.addEventListener('mousedown', e => {
    if (e.target.classList.contains('tab')) return; // clicking a tab, not dragging
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
    const dx = e.clientX - startX;
    const dy = e.clientY - startY;
    const newX = origX + dx;
    const newY = origY + dy;
    win.style.left = newX + 'px';
    win.style.top  = newY + 'px';
  });

  document.addEventListener('mouseup', e => {
    if (!dragging) return;
    dragging = false;
    const style = getComputedStyle(win);
    saveWindowState(
      parseInt(style.left, 10),
      parseInt(style.top, 10),
      parseInt(style.width, 10),
      parseInt(style.height, 10),
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

  // Keep the handle positioned at the window's bottom-right corner
  function positionHandle() {
    const r = win.getBoundingClientRect();
    handle.style.left = (r.right - 14) + 'px';
    handle.style.top  = (r.bottom - 14) + 'px';
  }
  positionHandle();
  new ResizeObserver(positionHandle).observe(win);

  handle.addEventListener('mousedown', e => {
    resizing = true;
    startX = e.clientX;
    startY = e.clientY;
    origW = win.offsetWidth;
    origH = win.offsetHeight;
    e.preventDefault();
  });

  document.addEventListener('mousemove', e => {
    if (!resizing) return;
    const newW = Math.max(280, origW + (e.clientX - startX));
    const newH = Math.max(120, origH + (e.clientY - startY));
    win.style.width  = newW + 'px';
    win.style.height = newH + 'px';
    positionHandle();
  });

  document.addEventListener('mouseup', () => {
    if (!resizing) return;
    resizing = false;
    const style = getComputedStyle(win);
    saveWindowState(
      parseInt(style.left, 10),
      parseInt(style.top, 10),
      parseInt(style.width, 10),
      parseInt(style.height, 10),
    );
  });
})();

// ---------------------------------------------------------------------------
// Keyboard handling
// ---------------------------------------------------------------------------

document.addEventListener('keydown', e => {
  if (inputOpen) {
    if (e.key === 'Escape') {
      closeInput();
      e.preventDefault();
    } else if (e.key === 'Enter') {
      submitInput();
      e.preventDefault();
    } else if (e.key === 'Tab') {
      // Cycle channel tabs
      const tabs = [...document.querySelectorAll('.tab')];
      const current = tabs.findIndex(t => t.dataset.channel === activeChannel);
      const next = tabs[(current + 1) % tabs.length];
      switchChannel(next.dataset.channel);
      e.preventDefault();
    }
    return; // Suppress all other keys while input is open
  }

  // Input hotkey (default Enter) — open the input bar
  const hotkey = config.general.input_hotkey || 'Enter';
  if (e.key === hotkey) {
    openInput();
    e.preventDefault();
  }
});

// ---------------------------------------------------------------------------
// Channel tab clicks
// ---------------------------------------------------------------------------

document.getElementById('channel-tabs').addEventListener('click', e => {
  const tab = e.target.closest('.tab');
  if (tab) switchChannel(tab.dataset.channel);
});

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

loadConfig().then(() => {
  // Restore any config saved in localStorage during dev (no-op in production)
  const saved = localStorage.getItem('chronicle_config');
  if (saved) {
    try { config = deepMerge(config, JSON.parse(saved)); applyConfig(); } catch (_) {}
  }
});
