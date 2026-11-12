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

// One Chronicle.dll runs on SE and AE, built against vcpkg's CommonLibSSE-NG. For six
// types that library has SE's member layout and no AE path: AE moved members of TES,
// InterfaceStrings, UserEvents and BGSSaveLoadManager, and CombatController and ControlMap
// fork on SKYRIM_SUPPORT_AE, which an NG build never defines. Reading any of their
// members, or calling one of their functions (which read members), hits SE's offset on
// AE: the wrong field, or a crash. So Chronicle may only call GetSingleton on them, and
// read the members on the allow-list below, which is empty.
'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const TYPES = {
  TES: {header: 'T/TES.h', allowed: []},
  InterfaceStrings: {header: 'I/InterfaceStrings.h', allowed: []},
  UserEvents: {header: 'U/UserEvents.h', allowed: []},
  BGSSaveLoadManager: {header: 'B/BGSSaveLoadManager.h', allowed: []},
  CombatController: {header: 'C/CombatController.h', allowed: []},
  ControlMap: {header: 'C/ControlMap.h', allowed: []},
};
// Other mods' headers, copied in unchanged: their owners check their own code.
const VENDORED = /(_API\.h|^DovahGameThread\.h)$/;

const stripped = text => text.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/.*$/gm, '').replace(/"(?:[^"\\\n]|\\.)*"/g, '""');

function sources() {
  const files = [];
  (function walk(dir) {
    for (const entry of fs.readdirSync(path.join(ROOT, dir), {withFileTypes: true})) {
      const file = path.posix.join(dir, entry.name);
      if (entry.isDirectory()) walk(file);
      else if (/\.(cpp|h)$/.test(entry.name) && !VENDORED.test(entry.name)) files.push(file);
    }
  })('src');
  return files.map(file => ({file, text: fs.readFileSync(path.join(ROOT, file), 'utf8')}));
}

// Every problem in the given sources, as "file: what". Uses on the allow-list are returned separately.
function layoutProblems(files, types = TYPES) {
  const problems = [];
  const uses = [];
  for (const {file, text: raw} of files) {
    const text = stripped(raw);
    for (const [type, info] of Object.entries(types)) {
      if (!new RegExp(`\\b${type}\\b`).test(text)) continue;
      // Named without RE:: (a using-declaration, an alias): its uses couldn't be followed.
      if (new RegExp(`(?<!RE::)\\b${type}\\b`).test(text)) {
        problems.push(`${file}: ${type} is named without RE::, so its uses can't be followed`);
      }
      // RE::Type::anything but GetSingleton (a static function or member).
      for (const match of text.matchAll(new RegExp(`\\bRE::${type}::(\\w+)`, 'g'))) {
        if (match[1] !== 'GetSingleton') problems.push(`${file}: RE::${type}::${match[1]} (only GetSingleton is safe on SE and AE)`);
      }
      // What holds the singleton: the call itself, and any variable or reference set from it.
      const call = `RE::${type}::GetSingleton\\(\\)`;
      const holders = [call];
      for (const match of text.matchAll(new RegExp(`(\\w+)\\s*=\\s*\\*?\\s*${call}`, 'g'))) holders.push(`\\b${match[1]}\\b`);
      for (const holder of holders) {
        for (const match of text.matchAll(new RegExp(`${holder}\\s*(?:->|\\.)\\s*(\\w+)\\s*(\\()?`, 'g'))) {
          if (match[2]) problems.push(`${file}: ${type}::${match[1]}() is a call; its code reads members that move on AE`);
          else if (!info.allowed.includes(match[1])) problems.push(`${file}: ${type}::${match[1]} isn't on the allow-list of members that stay put on AE`);
          else uses.push(`${type}::${match[1]} (${file})`);
        }
      }
      // A singleton handed on (to a function, into a container) can't be followed either.
      const handedOn = text.match(new RegExp(`[(,{]\\s*\\*?\\s*${call}\\s*[,)}]`, 'g'));
      if (handedOn) problems.push(`${file}: RE::${type}::GetSingleton() is passed on, so its uses can't be followed`);
    }
  }
  return {problems, uses};
}

test("the library's six headers are where the list says", () => {
  // The release build's vcpkg tree; a checkout that hasn't built yet has none.
  const trees = fs.existsSync(path.join(ROOT, 'out', 'build')) ? fs.readdirSync(path.join(ROOT, 'out', 'build'))
    .map(build => path.join(ROOT, 'out', 'build', build, 'vcpkg_installed', 'x64-windows-static', 'include', 'RE'))
    .filter(dir => fs.existsSync(dir)) : [];
  if (!trees.length) return;
  for (const [type, info] of Object.entries(TYPES)) {
    const header = fs.readFileSync(path.join(trees[0], info.header), 'utf8');
    assert.match(header, new RegExp(`\\b(class|struct)\\s+${type}\\b`), `${info.header} declares ${type}`);
  }
});

test("Chronicle's sources call only GetSingleton on the six types, and read none of their members", () => {
  const files = sources();
  assert.ok(files.length > 20, 'the source walk found the plugin');
  const {problems, uses} = layoutProblems(files);
  assert.deepEqual(problems, []);
  assert.deepEqual(uses, [], 'nothing is on the allow-list today; a new use is added to it on purpose');
});

test('a planted TES::ForEachReference is caught, however it is written', () => {
  const plant = code => layoutProblems([{file: 'src/Planted.cpp', text: code}]).problems;
  // Kept in a variable, then called.
  assert.deepEqual(plant(`void F() { auto* tes = RE::TES::GetSingleton();
    tes->ForEachReference([](RE::TESObjectREFR&) { return RE::BSContainer::ForEachResult::kContinue; }); }`),
  ['src/Planted.cpp: TES::ForEachReference() is a call; its code reads members that move on AE']);
  // Called straight on the singleton.
  assert.deepEqual(plant('void F() { RE::TES::GetSingleton()->ForEachReference(nullptr); }'),
    ['src/Planted.cpp: TES::ForEachReference() is a call; its code reads members that move on AE']);
  // A reference to it, and a member read.
  assert.deepEqual(plant('void F() { auto& tes = *RE::TES::GetSingleton(); auto* sky = tes.sky; }'),
    ["src/Planted.cpp: TES::sky isn't on the allow-list of members that stay put on AE"]);
  // A using-declaration hides it; a static function; a hand-off.
  assert.ok(plant('using RE::TES; void F() { TES::GetSingleton(); }').some(p => p.includes('named without RE::')));
  assert.ok(plant('void F() { RE::ControlMap::GetSingleton()->ToggleControls(1, false); }')
    .some(p => p.includes('ControlMap::ToggleControls() is a call')));
  assert.ok(plant('void F() { Keep(RE::UserEvents::GetSingleton()); }').some(p => p.includes('passed on')));
  // What is fine: GetSingleton alone, a null check, and text in comments or strings.
  assert.deepEqual(plant(`// RE::TES::GetSingleton()->ForEachReference in a comment
    void F() { if (!RE::TES::GetSingleton()) return; Log("RE::TES::GetSingleton()->sky"); }`), []);
  // A member on the allow-list passes (none is listed for Chronicle today).
  const listed = {...TYPES, InterfaceStrings: {header: 'I/InterfaceStrings.h', allowed: ['messageBoxData']}};
  const {problems, uses} = layoutProblems([{file: 'src/Planted.cpp',
    text: 'void F() { auto* strings = RE::InterfaceStrings::GetSingleton(); auto x = strings->messageBoxData; }'}], listed);
  assert.deepEqual([problems, uses], [[], ['InterfaceStrings::messageBoxData (src/Planted.cpp)']]);
});
