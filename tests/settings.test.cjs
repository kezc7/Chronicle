const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(
  path.join(__dirname, '../Data/PrismaUI/views/Chronicle/controls.js'), 'utf8');

function deepMerge(target, source) {
  for (const key of Object.keys(source || {})) {
    if (source[key] && typeof source[key] === 'object' && !Array.isArray(source[key])) {
      target[key] = deepMerge(target[key] || {}, source[key]);
    } else {
      target[key] = source[key];
    }
  }
  return target;
}

function setup() {
  const saved = new Map();
  const nodes = new Map();
  const node = () => ({
    textContent: '', value: '', hidden: false,
    classList: {add() {}, remove() {}, toggle() {}},
    replaceChildren() {}, blur() {}, focus() {}, select() {},
  });
  const defaults = {
    general: {interact_hotkey: 'F8', config_hotkey: 'N', persist_history: true, max_messages: 100},
    window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
  };
  const context = vm.createContext({
    config: deepMerge({}, defaults),
    DEFAULT_CONFIG: defaults,
    messages: [], inputHistory: [], inputHistoryIndex: -1,
    unreadCount: 0, userScrolledUp: false,
    document: {
      getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
      },
    },
    localStorage: {
      getItem: key => saved.has(key) ? saved.get(key) : null,
      setItem: (key, value) => saved.set(key, value),
      removeItem: key => saved.delete(key),
    },
    clearInterval() {}, clearTimeout() {}, setTimeout() {},
    console: {error() {}},
    deepMerge, normalizeConfig() {}, applyConfig() {}, refreshSpeakerOptions() {},
    restoreHistory() {}, showUnread() {}, sendToGame() { return false; },
    inputOpen: false, settingsOpen: false,
  });
  vm.runInContext(source, context);
  return {context, saved};
}

test('pending settings override stale native settings and survive a failed retry', () => {
  const {context, saved} = setup();
  const pending = {
    general: {hide_in_menus: false},
    window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
  };
  saved.set('chronicle_pending_settings', JSON.stringify(pending));
  const calls = [];
  context.sendToGame = (event, value) => {
    calls.push([event, value]);
    return true;
  };

  context.nativeBootstrap({
    settings: {
      general: {hide_in_menus: true},
      window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
    },
    warning: '',
    version: '1.2.0',
  });

  assert.equal(vm.runInContext('config.general.hide_in_menus', context), false);
  assert.equal(calls.length, 1);
  assert.equal(JSON.parse(calls[0][1]).general.hide_in_menus, false);

  context.settingsSaveResult(false, 'native save failed');
  assert.equal(saved.has('chronicle_pending_settings'), true);
  assert.match(context.document.getElementById('status-text').textContent, /native save failed/);

  context.nativeBootstrap({
    settings: {
      general: {hide_in_menus: true},
      window: {}, channels: {}, speaker_colours: {}, hud_subtitle_paths: [],
    },
    warning: '',
    version: '1.2.0',
  });
  context.settingsSaveResult(true, 'saved');
  assert.equal(saved.has('chronicle_pending_settings'), false);
});
