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

// Local-only preview with a mock PrismaUI host. Never shipped in the mod ZIP.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../Data/PrismaUI/views/Chronicle');
const mock = `<script>
window.previewCalls = [];
window.focusAcquired = () => { previewCalls.push('focusAcquired'); setTimeout(() => focusResult(true, 'focus.active'), 0); };
window.focusReleased = () => previewCalls.push('focusReleased');
window.settingsChanged = value => { previewCalls.push('settingsChanged'); window.previewSettings=JSON.parse(value); setTimeout(() => settingsSaveResult(true, 'Preview: settings accepted'),0); };
window.playerInput = value => previewCalls.push(['playerInput',value]);
window.consoleCommand = value => previewCalls.push(['consoleCommand',value]);
window.exportHistory = value => { previewCalls.push(['exportHistory',value]); saveResult(true, 'Preview: export accepted'); };
window.diagnostics = () => diagnosticsResult({version:'1.2.0 (preview)',api:1,bridge:true,gameLoaded:true,visible:true,focus:true,poller:true,hudPath:'Preview HUD path',messages:3,lastSubtitle:'I am right behind you.',settingsFile:'Preview only'});
window.uiReady = () => setTimeout(() => {
  nativeBootstrap({settings:null,warning:'',version:'1.2.0 (preview)'});
  setRuntime({loaded:true,profile:'preview-lydia',character:'Preview'}); setVisibility(true);
  if (messages.length===0) {
    addMessage({channel:'dialogue',speaker:'Lydia',message:'I am right behind you.'});
    addMessage({channel:'dialogue',speaker:'Farengar',message:'Have you come to discuss the ongoing hostilities?'});
    addMessage({channel:'notification',speaker:'',message:'Quest updated: Dragon Rising'});
  }
  // ?lang=fr&settings=channels opens settings on a tab in a language; ?backdrop=1
  // puts a game-like picture behind the page (screenshots only).
  const query = new URLSearchParams(location.search);
  if (query.get('backdrop')) document.body.style.background = '#0b0d0f url("backdrop.svg") center / cover no-repeat';
  if (query.get('lang')) { config.general.language = query.get('lang'); applyConfig(); }
  if (query.get('interact')) {
    addMessage({channel:'combat',speaker:'Bandit',message:'hits you with an Iron War Axe'});
    addMessage({channel:'system',speaker:'',message:'Game saved'});
    beginInteraction();
  }
  if (query.get('settings')) {
    openSettings();
    const tab = document.querySelector('[data-sp="' + query.get('settings') + '"]');
    if (tab) tab.click();
  }
},0);
</script>`;
// The suite's landscape from a DovahHub checkout beside this one, when there is one.
const backdrop = path.resolve(__dirname, '../../DovahHub/web/art/landscape.svg');
const TYPES = {'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8','.json':'application/json; charset=utf-8',
  '.js':'application/javascript; charset=utf-8','.ttf':'font/ttf','.svg':'image/svg+xml'};
http.createServer((req,res) => {
  const name = new URL(req.url, 'http://localhost').pathname.slice(1) || 'index.html';
  const file = name === 'backdrop.svg' ? backdrop : path.join(root, name);
  const known = ['index.html','styles.css','config.json','bridge.js','controls.js','pad.js','script.js','i18n.js','backdrop.svg'].includes(name) ||
    /^theme\/(dovah-theme\.css|fonts\/[A-Za-z-]+\.ttf)$/.test(name);
  if (!known || !fs.existsSync(file)) {
    res.writeHead(404); return res.end();
  }
  let content = fs.readFileSync(file);
  if (name==='index.html') content=content.toString('utf8').replace('<script src="bridge.js">',mock+'<script src="bridge.js">');
  res.setHeader('Content-Type', TYPES[path.extname(name)]);
  res.end(content);
}).listen(47831,'127.0.0.1',() => console.log('Chronicle preview: http://127.0.0.1:47831'));
