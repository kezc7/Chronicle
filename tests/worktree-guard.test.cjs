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

// A clean never deletes a git worktree. Every folder the build and release
// scripts empty is checked first by tools\worktree-guard.ps1, which asks git
// for the repository's worktrees and stops if one lies inside that folder.

'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ROOT = path.join(__dirname, '..');
const GUARD = path.join(ROOT, 'tools', 'worktree-guard.ps1');
const read = name => fs.readFileSync(path.join(ROOT, name), 'utf8');
const windows = {skip: process.platform !== 'win32' && 'needs Windows PowerShell'};

// Runs PowerShell with the guard loaded; values reach it through the environment.
function powershell(command, env = {}) {
  const run = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
    `$ErrorActionPreference = 'Stop'; . $env:GUARD_SCRIPT; ${command}`],
  {encoding: 'utf8', env: {...process.env, GUARD_SCRIPT: GUARD, ...env}});
  return {status: run.status, out: (run.stdout || '').trim(), err: (run.stderr || '').trim()};
}

function git(cwd, ...args) {
  const run = spawnSync('git', ['-C', cwd, ...args], {encoding: 'utf8'});
  assert.equal(run.status, 0, `git ${args.join(' ')}: ${run.stderr}`);
}

test('every folder the scripts empty is checked for worktrees first', () => {
  for (const name of ['build.ps1', 'package.ps1']) {
    const script = read(name);
    const loaded = script.indexOf('\\tools\\worktree-guard.ps1"');
    assert.ok(loaded >= 0, `${name} loads the guard`);
    const removals = [...script.matchAll(/Remove-Item -LiteralPath (\$\w+) -Recurse/g)];
    assert.ok(removals.length > 0, `${name} empties a folder`);
    for (const removal of removals) {
      const before = script.slice(loaded, removal.index);
      const check = before.lastIndexOf(`Assert-NoWorktreeInside -Folder ${removal[1]} -Repo `);
      assert.ok(check >= 0, `${name}: ${removal[1]} is emptied without the worktree check`);
      assert.doesNotMatch(before.slice(check), /\r?\n\s*\}\s*\r?\n/, `${name}: the check on ${removal[1]} is in another block`);
    }
  }
  // build.ps1 empties its folder only when asked, and package.ps1 passes the request on.
  assert.match(read('build.ps1'), /if \(\$Clean -and \(Test-Path -LiteralPath \$build\)\) \{/);
  assert.match(read('package.ps1'), /build\.ps1" -Config \$Config -UiHost \$UiHost -Clean:\$Clean/);
});

test('a worktree is inside a folder only by whole folder names', windows, () => {
  const porcelain = [
    'worktree K:/Repo', 'HEAD 99b3d48a041a1115033544a65145154ae92b06ca', 'branch refs/heads/main', '',
    'worktree K:/Repo/out/worktrees/fix-a', 'HEAD 90cc6454c85fbce8e3c0c2fdb33a3aa2f604daab', 'branch refs/heads/fix-a', '',
    'worktree K:/Repo/out/build-two', 'HEAD 90cc6454c85fbce8e3c0c2fdb33a3aa2f604daab', 'detached', '',
  ].join('\n');
  const inside = folder => powershell(
    "(@(Get-WorktreesInside -Folder $env:GUARD_FOLDER -Porcelain ($env:GUARD_PORCELAIN -split \"`n\")) -join '|')",
    {GUARD_FOLDER: folder, GUARD_PORCELAIN: porcelain});
  const cases = [
    ['K:\\Repo\\out', 'K:/Repo/out/worktrees/fix-a|K:/Repo/out/build-two'],
    ['K:\\Repo\\out\\', 'K:/Repo/out/worktrees/fix-a|K:/Repo/out/build-two'],
    ['k:/repo/OUT/worktrees', 'K:/Repo/out/worktrees/fix-a'],
    ['K:\\Repo\\out\\worktrees\\fix-a', 'K:/Repo/out/worktrees/fix-a'],
    ['K:\\Repo\\out\\build', ''],
    ['K:\\Repo\\out\\build\\x64-Release', ''],
    ['K:\\Repo\\out\\worktrees\\fix', ''],
    ['K:\\Other', ''],
  ];
  for (const [folder, expected] of cases) {
    const result = inside(folder);
    assert.equal(result.status, 0, result.err);
    assert.equal(result.out, expected, folder);
  }
});

test('a real worktree inside the folder stops the clean, and the folder stays', windows, t => {
  const repo = fs.mkdtempSync(path.join(os.tmpdir(), 'chronicle-guard-'));
  t.after(() => fs.rmSync(repo, {recursive: true, force: true}));
  git(repo, 'init', '-q');
  git(repo, '-c', 'user.name=test', '-c', 'user.email=test@example.invalid', 'commit', '-q', '--allow-empty', '-m', 'start');
  git(repo, 'worktree', 'add', '-q', '-b', 'side', path.join(repo, 'build', 'worktrees', 'side'));
  fs.mkdirSync(path.join(repo, 'out'));

  // The error goes to stdout whole: PowerShell wraps stderr at the console width.
  const guarded = 'try { Assert-NoWorktreeInside -Folder $env:GUARD_FOLDER -Repo $env:GUARD_REPO; "clear" } ' +
    'catch { $_.Exception.Message; exit 1 }';
  const check = (folder, repoDir = repo) => powershell(guarded, {GUARD_FOLDER: path.join(repo, folder), GUARD_REPO: repoDir});
  const stopped = check('build');
  assert.equal(stopped.status, 1, stopped.err);
  assert.match(stopped.out, /holds a git worktree \(.*build\/worktrees\/side\); not emptied/);
  assert.ok(fs.existsSync(path.join(repo, 'build', 'worktrees', 'side', '.git')));

  const clear = check('out');
  assert.equal(clear.status, 0, clear.err);
  assert.equal(clear.out, 'clear');

  const noRepo = check('out', path.join(repo, 'no-such-folder'));
  assert.equal(noRepo.status, 1, 'a folder whose worktrees git cannot list is never emptied');
  assert.match(noRepo.out, /couldn't list the worktrees/);
});
