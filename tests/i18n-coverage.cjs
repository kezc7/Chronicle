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

// Translation coverage. Both UI hosts load the same page, so one pass covers
// PrismaUI and Magelight. It checks:
//   - the page catalogue (i18n.js): missing or extra keys, values identical to
//     English, English words left inside a translation, broken placeholders;
//   - the native phrase table (RuntimePolicy.h PhraseText) the same way;
//   - user-facing text that bypasses both: page text and title, placeholder
//     and aria-label attributes without a data-i18n key, string literals the
//     scripts show directly, and C++ literals sent to the chat, the page or
//     DovahMenu.
// Chat content (dialogue, notifications, quest names) comes from the game in
// its own language and is not part of this.
//
// node tests/i18n-coverage.cjs prints the counts per language;
// tests/i18n-coverage.test.cjs fails while any count is above zero.

'use strict';

const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');

// I18N_ROOT points the report at another checkout (an older tree, for before/after counts).
const root = process.env.I18N_ROOT || path.join(__dirname, '..');
const view = path.join(root, 'Data/PrismaUI/views/Chronicle');
const hub = path.join(root, 'Data/PrismaUI/views/DovahHub/modules/chronicle');
const read = file => fs.readFileSync(file, 'utf8');

// Names, key names, units and file formats that stay as they are in every
// language. Compared case-insensitively, word by word.
const KEEP = new Set([
  'chronicle', 'skyrim', 'skyrimnet', 'dovahmenu', 'prismaui', 'magelight', 'scaleform', 'gfx', 'hud',
  'npc', 'npcs', 'mmo', 'api', 'kb', 'px', 'json', 'skse', 'data', 'plugins', 'mod', 'mods',
  'shift', 'ctrl', 'alt', 'tab', 'enter', 'escape', 'esc', 'backspace', 'f1', 'f8', 'f12', 'x', 'y', 's',
  'dwemer', 'ok', 'keycode',
]);

// Words that are correct in that language even though English spells them
// the same way. Each one was checked against the language, not just let through.
const SAME_WORD = {
  fr: ['message', 'messages', 'notifications', 'session', 'active', 'interaction', 'menus', 'mode', 'photo', 'visible',
       'corrections', 'max', 'position', 'configuration', 'exact', 'palette', 'distance', 'console', 'code', 'combat'],
  de: ['export', 'native', 'quest', 'tabs', 'max', 'position', 'optional', 'palette', 'code', 'info'],
  it: ['mouse', 'native', 'file', 'max', 'per', 'console', 'chat'],
  es: ['visible', 'cursor', 'real', 'chat'],
  pl: [],
  tr: [],
};
// Why the less obvious ones are kept: German Skyrim calls quests "Quests"
// (the journal tab, "Quest begonnen"); "max." is the usual abbreviation in
// French, German and Italian interfaces; Italian says "mouse" and "file" and
// Italian and Spanish say "chat". The rest are ordinary words of the language
// (French "position", "session"; German "Export", "optional"; Spanish "visible").

// Keys where shared letters are expected and the word check would only add noise.
const NO_WORD_CHECK = {
  months: 'month abbreviations share their first letters across languages',
  date: 'only placeholders',
};

// Latin words allowed inside Chinese, Japanese and Korean text: the names and
// key names above, plus nothing else.
const CJK = new Set(['zh-CN', 'ja', 'ko']);

// Keys whose value may equal English in a given language, with the reason.
const SAME_VALUE = {
  'channel.skyrimnet': 'mod name',
  'theme.skyrim': 'game name',
  'theme.dwemer': 'lore name used untranslated in every official Skyrim translation',
  'status.version': 'only a name and placeholders',
};
const SAME_VALUE_IN = {
  'fr:channel.notification': 'French "notifications"', 'fr:channel.combat': 'French "combat"',
  'fr:channel.console': 'French "console"', 'fr:theme.minimal': 'French "minimal"',
  'fr:theme.transparent': 'French "transparent"', 'fr:sp.positionX': 'French "position"',
  'fr:sp.positionY': 'French "position"', 'fr:sp.combat': 'French "combat"', 'fr:sp.position': 'French "position"',
  'fr:Combat': 'French "combat"',
  'de:channel.system': 'German "System"', 'de:theme.modern': 'German "modern"', 'de:theme.minimal': 'German "minimal"',
  'de:theme.transparent': 'German "transparent"', 'de:sp.positionX': 'German "Position"',
  'de:sp.positionY': 'German "Position"', 'de:sp.position': 'German "Position"',
  'it:channel.console': 'Italian "console"', 'it:diag.no': 'Italian "no"',
  'es:sp.tabGeneral': 'Spanish "general"', 'es:diag.no': 'Spanish "no"',
  'pl:channel.system': 'Polish "system"', 'pl:channel.narrator': 'Polish "narrator"', 'pl:Narrator': 'Polish "narrator"',
  'tr:theme.modern': 'Turkish "modern"', 'tr:theme.minimal': 'Turkish "minimal"',
};

const words = text => (text.match(/[\p{L}][\p{L}\p{N}'’-]*/gu) || []).map(w => w.toLowerCase().replace(/['’-]+$/, ''));
const placeholders = text => [...text.matchAll(/\{(\w+)\}/g)].map(m => m[1]).sort().join(',');
// Placeholders and file names (Chronicle.log) stay as they are in every language.
const unnamed = text => text.replace(/\{\w+\}|\b[\w-]+\.(?:log|json|txt)\b/g, ' ');
const meaningful = text => words(unnamed(text)).filter(w => !KEEP.has(w));

function checkTable(english, table, language, sameValue) {
  const found = { missing: [], extra: [], identical: [], englishLeft: [], placeholders: [] };
  const allowed = new Set([...(SAME_WORD[language] || [])]);
  for (const key of Object.keys(english)) {
    if (!Object.prototype.hasOwnProperty.call(table, key) || !String(table[key]).trim()) {
      found.missing.push(key);
      continue;
    }
    const source = english[key];
    const value = table[key];
    if (placeholders(value) !== placeholders(source)) found.placeholders.push(key);
    if (value === source) {
      if (meaningful(source).length && !sameValue(key)) found.identical.push(key);
      continue;
    }
    if (NO_WORD_CHECK[key]) continue;
    let left;
    if (CJK.has(language)) {
      left = words(unnamed(value)).filter(w => /^[a-z]{2,}$/.test(w) && !KEEP.has(w));
    } else {
      const sourceWords = new Set(meaningful(source).filter(w => w.length >= 3));
      left = meaningful(value).filter(w => sourceWords.has(w) && !allowed.has(w));
    }
    if (left.length) found.englishLeft.push(key + ' (' + [...new Set(left)].join(', ') + ')');
  }
  for (const key of Object.keys(table)) {
    if (!Object.prototype.hasOwnProperty.call(english, key)) found.extra.push(key);
  }
  return found;
}

function pageCatalogue() {
  const context = vm.createContext({});
  vm.runInContext(read(path.join(view, 'i18n.js')), context);
  return {
    languages: vm.runInContext('LANGUAGES', context).filter(l => l !== 'en'),
    strings: vm.runInContext('STRINGS', context),
  };
}

// The History tab Chronicle adds to DovahHub keeps its own small table.
function hubCatalogue() {
  const file = path.join(hub, 'history.js');
  if (!fs.existsSync(file)) return { strings: { en: {} } };
  const context = vm.createContext({ module: { exports: {} } });
  vm.runInContext(read(file), context);
  // An older history.js exported only mount and formatTime.
  return { strings: context.module.exports.STRINGS || { en: {} } };
}

// PhraseText's switch: one block per Lang, then the English fallback.
const NATIVE_LANG = { Fr: 'fr', De: 'de', It: 'it', Es: 'es', Pl: 'pl', Tr: 'tr', Zh: 'zh-CN', Ja: 'ja', Ko: 'ko' };
function nativeCatalogue() {
  const source = read(path.join(root, 'src/RuntimePolicy.h'));
  const start = source.indexOf('inline std::string_view PhraseText(');
  const end = source.indexOf('return "";', start);
  const body = source.slice(start, end);
  const cases = re => Object.fromEntries([...re].map(m => [m[1], JSON.parse(m[2])]));
  const tables = {};
  const blocks = body.split(/case Lang::(\w+):/).slice(1);
  for (let i = 0; i < blocks.length; i += 2) {
    const language = NATIVE_LANG[blocks[i]];
    if (language) tables[language] = cases(blocks[i + 1].matchAll(/case (\w+): return ("(?:[^"\\]|\\.)*");/g));
  }
  const fallback = body.slice(body.lastIndexOf('case Lang::En:'));
  const english = cases(fallback.slice(fallback.indexOf('switch (phrase)')).matchAll(/case (\w+): return ("(?:[^"\\]|\\.)*");/g));
  return { english, tables };
}

// --- Text that bypasses the catalogue -------------------------------------

// Page text that is the same in every language, with the reason.
const PAGE_KEEP = {
  'Chronicle': 'mod name (page title)',
  'English': 'language names are shown in their own language',
  'Français': 'language name', 'Deutsch': 'language name', 'Italiano': 'language name', 'Español': 'language name',
  'Polski': 'language name', 'Türkçe': 'language name', '简体中文': 'language name', '日本語': 'language name',
  '한국어': 'language name',
  '_root.SomeHud.Subtitle.text': 'an example Scaleform path, typed as code',
  'Esc': 'a keycap: the name printed on the key',
  'F8': 'key name', 'Enter': 'key name', '0': 'key name',
  'Loading Chronicle’s history…': 'DovahHub module page: history.js replaces it as soon as it runs',
};
// Elements whose text or attribute the scripts replace through t() at start-up.
const SET_BY_SCRIPT = {
  'status-text:text': "applyConfig() writes t('ui.connecting') before the page shows",
  'new-messages:text': 'applyConfig() calls showUnread(), which writes it through t()',
  'settings-btn:title': "script.js sets it from t('ui.settingsTitle') with the current key",
  'input-channel-label:text': 'setInputTarget() writes [channelLabel()] at start-up',
};

function pageBypass(file = path.join(view, 'index.html')) {
  const where = path.relative(root, file).replace(/\\/g, '/');
  const html = read(file)
    .replace(/<!--[\s\S]*?-->/g, '')
    .replace(/<(script|style)\b[\s\S]*?<\/\1>/g, '');
  const found = [];
  const stack = [];
  const voids = new Set(['input', 'meta', 'link', 'br', 'img', 'hr', 'source']);
  const tokens = html.matchAll(/<(\/?)([a-zA-Z][\w-]*)([^>]*)>|([^<]+)/g);
  for (const m of tokens) {
    if (m[4] !== undefined) {
      const text = m[4].replace(/&[a-z#0-9]+;/gi, '').replace(/\s+/g, ' ').trim();
      const top = stack[stack.length - 1];
      if (!text || !/\p{L}/u.test(text) || !top || top.i18n || PAGE_KEEP[text]) continue;
      if (SET_BY_SCRIPT[top.id + ':text']) continue;
      found.push(`${where} <${top.tag}${top.id ? ' id=' + top.id : ''}> text "${text}"`);
      continue;
    }
    const [, closing, tag, attrs] = m;
    if (closing) { stack.pop(); continue; }
    const attr = name => (attrs.match(new RegExp('(?:^|\\s)' + name + '="([^"]*)"')) || [])[1];
    const id = attr('id') || '';
    for (const name of ['title', 'placeholder', 'aria-label']) {
      const value = attr(name);
      if (!value || !/\p{L}/u.test(value) || attr('data-i18n-' + name) || PAGE_KEEP[value]) continue;
      if (SET_BY_SCRIPT[id + ':' + name]) continue;
      found.push(`${where} <${tag}${id ? ' id=' + id : ''}> ${name}="${value}"`);
    }
    if (!voids.has(tag.toLowerCase()) && !attrs.trim().endsWith('/')) {
      stack.push({ tag, id, i18n: attr('data-i18n') !== undefined });
    }
  }
  return found;
}

// String literals the scripts put on screen without t(). Two words or more of
// letters, outside comments, console logging, CSS and t('...') keys.
const SCRIPT_KEEP = new Set([
  'use strict',
  // KeyboardEvent.key names compared in code, never shown.
  'Escape', 'Tab', 'Shift', 'Control', 'Alt', 'Meta', 'Enter', 'Backspace',
]);
// "hearth-search chronicle-history__search": CSS class lists.
const classList = text => text.split(' ').every(token => /^[a-z][\w-]*$/.test(token)) && /-|__/.test(text);
function scriptBypass() {
  const found = [];
  const files = ['bridge.js', 'controls.js', 'script.js'].map(file => path.join(view, file))
    .concat(path.join(hub, 'history.js'));
  for (const full of files) {
    const file = path.relative(root, full).replace(/\\/g, '/');
    // The History tab's own table is checked as a catalogue, not as bypassing text.
    let lines = read(full).split(/\r?\n/);
    const table = lines.findIndex(line => /^\s*const STRINGS = \{/.test(line));
    if (table >= 0) {
      const end = lines.findIndex((line, i) => i > table && /^\s*\};/.test(line));
      lines = lines.map((line, i) => (i >= table && i <= end ? '' : line));
    }
    lines.forEach((line, index) => {
      const code = line.replace(/\/\/.*$/, '');
      if (/^\s*\*|console\.|querySelector|classList|getElementById|addEventListener|sendToGame\(|localStorage/.test(code)) return;
      for (const m of code.matchAll(/'((?:[^'\\]|\\.)*)'|"((?:[^"\\]|\\.)*)"/g)) {
        const text = m[1] ?? m[2];
        if (SCRIPT_KEEP.has(text) || classList(text)) continue;
        // Prose: two or more words, or a capitalised word, with no code characters.
        const prose = /\p{L}{2,}[ ,.:;!?…"]+\p{L}{2,}/u.test(text) || /^[A-Z][a-z]{2,}$/.test(text);
        if (!prose || /[<>{}=#$;]|^\.|\.\w+$|--|^[a-z]+(-[a-z]+)+$/.test(text)) continue;
        if (KEEP.has(text.toLowerCase()) || /^(Key|Digit|Numpad|Arrow|Page)/.test(text)) continue;
        found.push(`${file}:${index + 1} "${text}"`);
      }
    });
  }
  return found;
}

// C++ literals that reach the player: chat lines (SendMessage), page status
// text (bootstrap warning) and the DovahMenu entry. Logging is not checked.
function nativeBypass() {
  const found = [];
  for (const file of fs.readdirSync(path.join(root, 'src')).filter(f => f.endsWith('.cpp'))) {
    const lines = read(path.join(root, 'src', file)).split(/\r?\n/);
    lines.forEach((line, index) => {
      const code = line.replace(/\/\/.*$/, '');
      if (/spdlog::|logger::|SKSE::log|throw /.test(code)) return;
      const shown = /SendMessage\(|\bwarning\s*=|\bspeaker\s*=|\.description\s*=|SetText\(|SetDescription\(/.test(code);
      if (!shown) return;
      const literals = [...code.matchAll(/"((?:[^"\\]|\\.)*)"/g)].map(m => m[1]);
      // The first SendMessage literal is the channel id.
      const texts = /SendMessage\(/.test(code) ? literals.filter(l => !/^[a-z]+$/.test(l)) : literals;
      for (const text of texts) {
        if (/\p{L}{2,}/u.test(text) && (/\p{L} +\p{L}/u.test(text) || /^[A-Z][a-z]+$/.test(text))) {
          if (KEEP.has(text.toLowerCase())) continue;
          found.push(`src/${file}:${index + 1} "${text}"`);
        }
      }
    });
  }
  return found;
}

function coverage() {
  const page = pageCatalogue();
  const native = nativeCatalogue();
  const hubTable = hubCatalogue();
  const sameValue = language => key => SAME_VALUE[key] || SAME_VALUE_IN[language + ':' + key];
  const languages = {};
  for (const language of page.languages) {
    const pageFound = checkTable(page.strings.en, page.strings[language] || {}, language, sameValue(language));
    const nativeFound = checkTable(native.english, native.tables[language] || {}, language, sameValue(language));
    const hubFound = checkTable(hubTable.strings.en, hubTable.strings[language] || {}, language, sameValue(language));
    languages[language] = { page: pageFound, native: nativeFound, hub: hubFound };
  }
  return {
    languages,
    bypass: [...pageBypass(), ...(fs.existsSync(path.join(hub, 'index.html')) ? pageBypass(path.join(hub, 'index.html')) : []),
      ...scriptBypass(), ...nativeBypass()],
    englishKeys: Object.keys(page.strings.en).length,
    nativeKeys: Object.keys(native.english).length,
    hubKeys: Object.keys(hubTable.strings.en).length,
  };
}

// Every problem the report lists, as one count.
const total = result => result.bypass.length + Object.values(result.languages).reduce((sum, tables) =>
  sum + Object.values(tables).reduce((s2, found) => s2 + Object.values(found).reduce((s3, list) => s3 + list.length, 0), 0), 0);

function report(result, verbose) {
  const out = [];
  out.push(`Chat window: ${result.englishKeys} keys; DLL phrases: ${result.nativeKeys}; DovahHub History tab: ${result.hubKeys}`);
  out.push('language  missing  same-as-en  english-left  placeholders  extra  (all three tables)');
  for (const [language, tables] of Object.entries(result.languages)) {
    const n = k => Object.values(tables).reduce((sum, found) => sum + found[k].length, 0);
    out.push(`${language.padEnd(8)}  ${String(n('missing')).padStart(7)}  ${String(n('identical')).padStart(10)}  ` +
      `${String(n('englishLeft')).padStart(12)}  ${String(n('placeholders')).padStart(12)}  ${String(n('extra')).padStart(5)}`);
    if (verbose) {
      for (const [where, found] of Object.entries(tables)) {
        for (const [kind, list] of Object.entries(found)) {
          for (const item of list) out.push(`    ${where} ${kind}: ${item}`);
        }
      }
    }
  }
  out.push(`Bypassing the catalogue (all languages): ${result.bypass.length}`);
  if (verbose) for (const item of result.bypass) out.push('    ' + item);
  return out.join('\n');
}

module.exports = { coverage, report, total, checkTable };

if (require.main === module) {
  console.log(report(coverage(), process.argv.includes('--verbose')));
}
