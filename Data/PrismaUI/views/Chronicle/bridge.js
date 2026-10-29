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

// RegisterJSListener installs named functions on window (PrismaUI v1 and v2).
function sendToGame(eventName, data) {
  const allowed = ['focusAcquired', 'focusReleased', 'playerInput', 'consoleCommand',
    'settingsChanged', 'uiReady', 'diagnostics', 'exportHistory'];
  if (!allowed.includes(eventName) || typeof window[eventName] !== 'function') return false;
  // Magelight's display-only copy of the page (controls.js hostRole) only answers the handshake.
  if (eventName !== 'uiReady' && typeof hostRole === 'string' && hostRole === 'hud') return false;
  try {
    window[eventName](typeof data === 'string' ? data : JSON.stringify(data));
    return true;
  } catch (error) {
    console.error('Chronicle bridge call failed: ' + eventName, error);
    return false;
  }
}
