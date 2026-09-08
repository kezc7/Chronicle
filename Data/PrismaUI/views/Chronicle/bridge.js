'use strict';

// RegisterJSListener installs named functions on window (PrismaUI v1 and v2).
function sendToGame(eventName, data) {
  const allowed = ['focusAcquired', 'focusReleased', 'playerInput', 'consoleCommand',
    'settingsChanged', 'uiReady', 'diagnostics', 'exportHistory'];
  if (!allowed.includes(eventName) || typeof window[eventName] !== 'function') return false;
  try {
    window[eventName](typeof data === 'string' ? data : JSON.stringify(data));
    return true;
  } catch (error) {
    console.error('Chronicle bridge call failed: ' + eventName, error);
    return false;
  }
}
