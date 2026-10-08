const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

const view = path.join(__dirname, '../Data/PrismaUI/views/Chronicle');
const controls = fs.readFileSync(path.join(view, 'controls.js'), 'utf8');
const css = fs.readFileSync(path.join(view, 'styles.css'), 'utf8');
const html = fs.readFileSync(path.join(view, 'index.html'), 'utf8');

function loadControls() {
  const context = vm.createContext({console: {error() {}}});
  vm.runInContext(controls, context);
  return context;
}

test('valid themes are kept and anything else falls back to Skyrim', () => {
  const context = loadControls();
  const normalize = theme => vm.runInContext(`normalizeTheme(${JSON.stringify(theme)})`, context);
  for (const theme of vm.runInContext('THEMES', context)) assert.equal(normalize(theme), theme);
  assert.equal(normalize('not-a-theme'), 'skyrim');
  assert.equal(normalize(undefined), 'skyrim');
  assert.equal(normalize(''), 'skyrim');
});

test('every theme has a settings option, and every non-default theme a style block', () => {
  const themes = vm.runInContext('THEMES', loadControls());
  for (const theme of themes) {
    assert.match(html, new RegExp(`<option value="${theme}">`), `missing <option> for ${theme}`);
    if (theme !== 'skyrim') {
      assert.match(css, new RegExp(`body\\[data-theme="${theme}"\\]\\s*\\{`), `missing style block for ${theme}`);
    }
  }
});

test('every theme colour used by the stylesheet has a default value', () => {
  const used = new Set([...css.matchAll(/var\((--theme-[a-z-]+)/g)].map(m => m[1]));
  // The default (Skyrim) theme is the body block that defines --theme-font.
  const defaults = css.match(/\nbody \{\s*--theme-font[\s\S]*?\n\}/)[0];
  for (const name of used) assert.match(defaults, new RegExp(`${name}\\s*:`), `${name} has no default`);
});
