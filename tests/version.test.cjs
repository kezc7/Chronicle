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

// Chronicle has one version: the VERSION of project() in CMakeLists.txt. The DLL, its
// FileVersion and the archive name are built from it. Four other places state a version
// and can't read CMake: package.json, vcpkg.json, the README's title and the README's
// newest release notes. set-version.ps1 writes the first three; the notes are written by
// hand. This test fails when any of them disagrees, on every build, and package.ps1 runs
// it before it builds, so a release can't be cut with a stale one.
'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const FILES = ['CMakeLists.txt', 'package.json', 'vcpkg.json', 'README.md'];

function first(text, pattern) {
  const match = pattern.exec(text);
  return match ? match[1] : null;
}

function jsonField(text, key) {
  try {
    const value = JSON.parse(text)[key];
    return typeof value === 'string' ? value : null;
  } catch {
    return null;
  }
}

// Every place that states the version: [{where, value}], the source first. A value of
// null means the field wasn't found, which is a disagreement too.
function versionFields(files) {
  return [
    {where: 'CMakeLists.txt project VERSION', value: first(files['CMakeLists.txt'], /\bproject\s*\(\s*Chronicle\s+VERSION\s+(\d+\.\d+\.\d+)\b/)},
    {where: 'package.json "version"', value: jsonField(files['package.json'], 'version')},
    {where: 'vcpkg.json "version-string"', value: jsonField(files['vcpkg.json'], 'version-string')},
    {where: 'README.md title', value: first(files['README.md'], /^# Chronicle (\S+)[ \t]*\r?$/m)},
    {where: 'README.md newest release notes', value: first(files['README.md'], /^## (\d+\.\d+\.\d+)\b/m)},
  ];
}

// The fields that don't say what CMake says, as lines for a person to read.
function disagreements(fields) {
  const [source, ...copies] = fields;
  if (!source.value) return [`${source.where} was not found`];
  return copies
    .filter(field => field.value !== source.value)
    .map(field => `${field.where} is ${field.value === null ? 'missing' : field.value}, but ${source.where} is ${source.value}`);
}

function readTree() {
  return Object.fromEntries(FILES.map(name => [name, fs.readFileSync(path.join(ROOT, name), 'utf8')]));
}

test('every version field says what CMakeLists.txt says', () => {
  const fields = versionFields(readTree());
  assert.match(fields[0].value, /^\d+\.\d+\.\d+$/);
  assert.deepEqual(disagreements(fields), [], 'run set-version.ps1, and write the release notes for this version');
});

test('a stale or missing version field is named', () => {
  const good = {
    'CMakeLists.txt': 'cmake_minimum_required(VERSION 3.23)\r\nproject(\r\n    Chronicle\r\n    VERSION 2.3.4\r\n    LANGUAGES CXX\r\n)\r\n',
    'package.json': '{"name": "chronicle-skse-ui", "version": "2.3.4"}',
    'vcpkg.json': '{"name": "chronicle", "version-string": "2.3.4"}',
    'README.md': '# Chronicle 2.3.4\r\n\r\nText.\r\n\r\n## Build\r\n\r\n## 2.3.4-beta\r\n\r\n## 2.3.3-beta\r\n',
  };
  assert.deepEqual(disagreements(versionFields(good)), []);
  // The minimum CMake version above is not mistaken for the project's.
  assert.equal(versionFields(good)[0].value, '2.3.4');

  const planted = [
    ['package.json', '{"name": "chronicle-skse-ui", "version": "2.3.3"}', 'package.json "version" is 2.3.3'],
    ['package.json', '{"name": "chronicle-skse-ui"}', 'package.json "version" is missing'],
    ['package.json', 'not json', 'package.json "version" is missing'],
    ['vcpkg.json', '{"name": "chronicle", "version-string": "1.2.37"}', 'vcpkg.json "version-string" is 1.2.37'],
    ['README.md', good['README.md'].replace('# Chronicle 2.3.4', '# Chronicle 2.3.3'), 'README.md title is 2.3.3'],
    ['README.md', good['README.md'].replace('## 2.3.4-beta\r\n\r\n', ''), 'README.md newest release notes is 2.3.3'],
  ];
  for (const [file, text, expected] of planted) {
    const found = disagreements(versionFields({...good, [file]: text}));
    assert.equal(found.length, 1, `${file}: ${expected}`);
    assert.ok(found[0].startsWith(expected) && found[0].endsWith('CMakeLists.txt project VERSION is 2.3.4'), found[0]);
  }
  assert.deepEqual(disagreements(versionFields({...good, 'CMakeLists.txt': 'project(Other VERSION 2.3.4)'})),
    ['CMakeLists.txt project VERSION was not found']);
});

test('the release script checks the versions before it builds, and the DLL after', () => {
  const script = fs.readFileSync(path.join(ROOT, 'package.ps1'), 'utf8');
  const check = script.indexOf('version.test.cjs');
  const build = script.indexOf('build.ps1');
  const zip = script.indexOf('ZipFile]::Open(');
  const dll = script.indexOf('VersionInfo.FileVersion');
  assert.ok(check >= 0 && check < build, 'the version test runs before the build');
  assert.ok(dll > build && dll < zip, 'the DLL FileVersion is checked after the build and before the archive is written');
});
