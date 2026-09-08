const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const source = fs.readFileSync(
  path.join(__dirname, '../Data/PrismaUI/views/Chronicle/controls.js'), 'utf8');

function setup() {
  const nodes = new Map();
  const bodyClasses = new Set();
  const node = () => ({
    textContent: '', value: '', hidden: false, options: [],
    classList: {
      add: (...names) => names.forEach(name => bodyClasses.add(name)),
      remove: (...names) => names.forEach(name => bodyClasses.delete(name)),
      toggle: (name, on) => on ? bodyClasses.add(name) : bodyClasses.delete(name),
    },
    replaceChildren() { this.options = []; }, appendChild(option) { this.options.push(option); },
    blur() {}, focus() {}, select() {},
  });
  const config = {general: {interact_hotkey: 'F8', config_hotkey: 'N', persist_history: true, max_messages: 100}};
  const calls = [];
  const context = vm.createContext({
    config, messages: [], inputHistory: [], inputHistoryIndex: -1,
    unreadCount: 0, userScrolledUp: false,
    document: {
      body: {classList: node().classList},
      activeElement: node(),
      createElement: () => ({value: '', textContent: ''}),
      getElementById(id) {
        if (!nodes.has(id)) nodes.set(id, node());
        return nodes.get(id);
      },
    },
    localStorage: {setItem() {}, removeItem() {}},
    clearInterval() {}, clearTimeout() {}, setTimeout() {},
    console: {error() {}},
    refreshSpeakerOptions() {}, restoreHistory() {}, showUnread() {},
    applyConfig() {}, normalizeConfig() {},
    discardSettingsPreview() { calls.push('discard'); },
    sendToGame: (event) => { calls.push(event); return true; },
    inputOpen: false, settingsOpen: false,
  });
  vm.runInContext(source, context);
  vm.runInContext('runtime.connected = true; runtime.loaded = true; runtime.visible = true;', context);
  return {context, calls, bodyClasses};
}

test('failed focus fully resets interaction state', () => {
  const {context, bodyClasses} = setup();
  context.sendToGame = () => false;
  assert.equal(context.beginInteraction(), false);
  assert.equal(vm.runInContext('interactionOpen', context), false);
  assert.equal(bodyClasses.has('interacting'), false);
  assert.equal(bodyClasses.has('mouse-active'), false);
});

test('stale successful focus result cannot reactivate a closed interaction', () => {
  const {context, bodyClasses} = setup();
  assert.equal(context.beginInteraction(), true);
  context.closeInteraction(false);
  assert.equal(context.focusResult(true, 'late focus'), false);
  assert.equal(bodyClasses.has('mouse-active'), false);
});

test('visibility loss closes settings/input without releasing native focus', () => {
  const {context, calls} = setup();
  context.beginInteraction();
  context.focusResult(true, 'active');
  vm.runInContext('inputOpen = true; settingsOpen = true;', context);
  context.setVisibility(false);
  assert.equal(vm.runInContext('interactionOpen', context), false);
  assert.equal(vm.runInContext('inputOpen', context), false);
  assert.equal(vm.runInContext('settingsOpen', context), false);
  assert.equal(calls.includes('focusReleased'), false);
});

test('native hotkeys are ignored while disconnected, loading, or hidden', () => {
  const {context} = setup();
  vm.runInContext('runtime.connected = false;', context);
  assert.equal(context.nativeHotkey('interact'), false);
  vm.runInContext('runtime.connected = true; runtime.loaded = false;', context);
  assert.equal(context.nativeHotkey('interact'), false);
  vm.runInContext('runtime.loaded = true; runtime.visible = false;', context);
  assert.equal(context.nativeHotkey('interact'), false);
});
