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

'use strict';

// The controller in the chat window, with the suite's pad layout (DovahHub,
// DovahMenu): the d-pad or left stick moves, A accepts, B goes back, LB / RB
// switch tabs, X and Y are extras, the right stick scrolls. The prompts and
// the gold ring show only while the pad is the input in use
// (html[data-meridian-input="gamepad"], from the theme kit).
//
// PrismaUI hands the focused page the controller as a standard browser
// gamepad, read here. Magelight's gamepad layer types it as keys instead
// (d-pad = arrows, A = Enter, B = Escape, right stick = wheel); script.js
// sends those keys to padAction, so both hosts behave the same.

const PAD_REPEAT_DELAY = 380;
const PAD_REPEAT_RATE = 90;
const PAD_STICK_ON = 0.5;
const PAD_STICK_OFF = 0.35;
const PAD_SCROLL_DEAD = 0.15;
const PAD_SCROLL_SPEED = 22;    // pixels per frame at full right-stick tilt
const PAD_LINE_STEP = 48;       // pixels per d-pad step through the log
// Standard-mapping buttons: 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 12-15 the d-pad.
const PAD_BUTTONS = {0: 'accept', 1: 'cancel', 2: 'secondary', 3: 'tertiary', 4: 'previousTab', 5: 'nextTab',
  12: 'up', 13: 'down', 14: 'left', 15: 'right'};
const PAD_REPEATS = new Set(['up', 'down', 'left', 'right']);

const padState = {held: new Set(), stale: new Set(), stick: '', repeatAt: 0, repeatAction: '', wasOpen: false};

function setInputDevice(device) {
  const value = device === 'gamepad' ? 'gamepad' : 'keyboardMouse';
  const root = document.documentElement;
  if (!root || !root.dataset || root.dataset.meridianInput === value) return;
  root.dataset.meridianInput = value;
  renderPadPrompts();
}

function padVisibleTabs() {
  return [...document.querySelectorAll('.tab[data-channel]')]
    .filter(tab => !tab.classList.contains('hidden-channel'))
    .map(tab => tab.dataset.channel);
}

function padStep(list, current, step) {
  if (!list.length) return current;
  const at = list.indexOf(current);
  return list[((at < 0 ? 0 : at + step) % list.length + list.length) % list.length];
}

function padScroll(pixels) {
  const list = document.getElementById('message-list');
  if (list && pixels) list.scrollTop = Math.max(0, list.scrollTop + pixels);
}

// Y: the next speaker in the speaker filter, round to "All speakers".
function padNextSpeaker() {
  refreshSpeakerOptions();
  const select = document.getElementById('speaker-filter');
  const values = [...select.options].map(option => option.value);
  if (values.length < 2) return;
  select.value = padStep(values, select.value, 1);
  refreshFilters();
}

// One pad action in the chat window. True when Chronicle used it.
function padAction(action) {
  if (!interactionOpen || !runtime.visible || !runtime.loaded) return false;
  if (settingsOpen) {
    if (action !== 'cancel') return false;
    closeSettings();
    return true;
  }
  if (inputOpen) {
    if (action === 'up' || action === 'down') { recallInput(action === 'up' ? 1 : -1); return true; }
    if (action === 'previousTab' || action === 'nextTab') {
      setInputTarget(padStep(enabledChannels(), inputTarget, action === 'nextTab' ? 1 : -1));
      return true;
    }
    if (action === 'accept') { submitInput(); return true; }
    if (action === 'cancel') { closeInput(); renderPadPrompts(); return true; }
    return false;
  }
  switch (action) {
  case 'up': padScroll(-PAD_LINE_STEP); return true;
  case 'down': padScroll(PAD_LINE_STEP); return true;
  case 'left': case 'previousTab': case 'right': case 'nextTab': {
    const channel = padStep(padVisibleTabs(), activeChannel, action === 'left' || action === 'previousTab' ? -1 : 1);
    switchChannel(channel);
    setInputTarget(channel);
    return true;
  }
  // A opens the chat bar, where up and down go through the recent commands.
  case 'accept': if (openInput()) renderPadPrompts(); return true;
  case 'cancel': closeInteraction(); return true;
  case 'secondary': scrollToBottom(); return true;
  case 'tertiary': padNextSpeaker(); return true;
  default: return false;
  }
}

function padPrompt(keys, label) {
  const prompt = document.createElement('span');
  prompt.className = 'dv-prompt';
  for (const key of keys) {
    const cap = document.createElement('span');
    cap.className = 'dv-keycap';
    cap.textContent = key;
    prompt.appendChild(cap);
  }
  const text = document.createElement('span');
  text.textContent = t(label);
  prompt.appendChild(text);
  return prompt;
}

// Magelight's layer has no shoulder buttons or X / Y for a page, so its prompts are the keys it types.
function renderPadPrompts() {
  const bar = document.getElementById('pad-prompts');
  if (!bar) return;
  const typed = runtime.host === 'Magelight';
  const tabs = typed ? ['←', '→'] : ['LB', 'RB'];
  // Most needed first: a narrow window shows the first ones and clips the rest.
  const prompts = inputOpen ? [
    padPrompt(['↑', '↓'], 'pad.recent'),
    padPrompt(['A'], 'pad.send'),
    padPrompt(['B'], typed ? 'pad.close' : 'pad.cancel'),
    ...(typed ? [] : [padPrompt(tabs, 'pad.channel')]),
  ] : [
    padPrompt(['A'], 'pad.recent'),
    padPrompt(['B'], 'pad.close'),
    padPrompt(tabs, 'pad.channel'),
    padPrompt(['↑', '↓'], 'pad.scroll'),
    ...(typed ? [] : [padPrompt(['Y'], 'pad.speaker'), padPrompt(['X'], 'pad.newest')]),
  ];
  bar.replaceChildren(...prompts);
}

function padFirstGamepad() {
  if (typeof navigator === 'undefined' || typeof navigator.getGamepads !== 'function') return null;
  for (const pad of navigator.getGamepads() || []) {
    if (pad && pad.connected !== false && pad.buttons && pad.buttons.length >= 16) return pad;
  }
  return null;
}

function padStickAction(x, y) {
  const limit = padState.stick ? PAD_STICK_OFF : PAD_STICK_ON;
  if (Math.abs(x) < limit && Math.abs(y) < limit) return '';
  if (Math.abs(y) >= Math.abs(x)) return y < 0 ? 'up' : 'down';  // the gamepad API points down
  return x < 0 ? 'left' : 'right';
}

function padFire(action) {
  setInputDevice('gamepad');
  padAction(action);
}

// One frame of the pad (PrismaUI). Buttons held when the window opens count
// only after they are let go: the combo that opened it presses nothing here.
function padTick(now) {
  const open = interactionOpen && hostRole !== 'hud';
  const pad = open ? padFirstGamepad() : null;
  if (!open || !pad) {
    padState.held.clear(); padState.stale.clear(); padState.stick = ''; padState.repeatAction = '';
    padState.wasOpen = false;
    return;
  }
  const pressed = new Set();
  pad.buttons.forEach((button, index) => {
    if (PAD_BUTTONS[index] && (button.pressed || button.value > 0.5)) pressed.add(index);
  });
  if (!padState.wasOpen) {
    padState.wasOpen = true;
    padState.stale = new Set(pressed);
    padState.held = new Set(pressed);
    padState.stick = padStickAction(pad.axes[0] || 0, pad.axes[1] || 0);
    return;
  }
  for (const index of [...padState.stale]) if (!pressed.has(index)) padState.stale.delete(index);
  let direction = '';
  for (const index of pressed) {
    if (padState.stale.has(index)) continue;
    const action = PAD_BUTTONS[index];
    if (PAD_REPEATS.has(action)) direction = direction || action;
    else if (!padState.held.has(index)) padFire(action);
  }
  padState.held = pressed;
  const stick = padStickAction(pad.axes[0] || 0, pad.axes[1] || 0);
  const stickNew = stick && stick !== padState.stick;
  padState.stick = stick;
  if (!direction && stick && (stickNew || padState.repeatAction === stick)) direction = stick;
  if (!direction) {
    padState.repeatAction = '';
  } else if (direction !== padState.repeatAction) {
    padState.repeatAction = direction;
    padState.repeatAt = now + PAD_REPEAT_DELAY;
    padFire(direction);
  } else if (now >= padState.repeatAt) {
    padState.repeatAt = now + PAD_REPEAT_RATE;
    padFire(direction);
  }
  const scroll = -(pad.axes[3] || 0);  // up positive
  if (!inputOpen && !settingsOpen && Math.abs(scroll) > PAD_SCROLL_DEAD) {
    setInputDevice('gamepad');
    padScroll(-scroll * PAD_SCROLL_SPEED);
  }
}

function startPad() {
  if (typeof navigator === 'undefined' || typeof navigator.getGamepads !== 'function') return;
  const frame = () => padTick(typeof performance !== 'undefined' ? performance.now() : Date.now());
  setInterval(frame, 16);
}
