// Local-only preview with a mock PrismaUI host. Never shipped in the mod ZIP.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '../Data/PrismaUI/views/Chronicle');
const mock = `<script>
window.previewCalls = [];
window.focusAcquired = () => { previewCalls.push('focusAcquired'); setTimeout(() => focusResult(true, 'Mouse active — Escape to return'), 0); };
window.focusReleased = () => previewCalls.push('focusReleased');
window.settingsChanged = value => { previewCalls.push('settingsChanged'); window.previewSettings=JSON.parse(value); setTimeout(() => saveResult(true, 'Preview: settings accepted'),0); };
window.playerInput = value => previewCalls.push(['playerInput',value]);
window.consoleCommand = value => previewCalls.push(['consoleCommand',value]);
window.exportHistory = value => { previewCalls.push(['exportHistory',value]); saveResult(true, 'Preview: export accepted'); };
window.diagnostics = () => diagnosticsResult({version:'1.1.0 (preview)',api:1,bridge:true,gameLoaded:true,visible:true,focus:true,poller:true,messages:3,lastSubtitle:'I am right behind you.',settingsFile:'Preview only'});
window.uiReady = () => setTimeout(() => {
  nativeBootstrap({settings:null,warning:'',version:'1.1.0 (preview)'});
  setRuntime({loaded:true,profile:'preview-lydia',character:'Preview'}); setVisibility(true);
  if (messages.length===0) {
    addMessage({channel:'dialogue',speaker:'Lydia',message:'I am right behind you.'});
    addMessage({channel:'dialogue',speaker:'Farengar',message:'Have you come to discuss the ongoing hostilities?'});
    addMessage({channel:'notification',speaker:'',message:'Quest updated: Dragon Rising'});
  }
},0);
</script>`;
http.createServer((req,res) => {
  const name = new URL(req.url, 'http://localhost').pathname.slice(1) || 'index.html';
  if (!['index.html','styles.css','config.json','bridge.js','controls.js','script.js'].includes(name)) {
    res.writeHead(404); return res.end();
  }
  let content = fs.readFileSync(path.join(root,name),'utf8');
  if (name==='index.html') content=content.replace('<script src="bridge.js">',mock+'<script src="bridge.js">');
  res.setHeader('Content-Type', ({'.html':'text/html','.css':'text/css','.json':'application/json','.js':'application/javascript'})[path.extname(name)] + '; charset=utf-8');
  res.end(content);
}).listen(47831,'127.0.0.1',() => console.log('Chronicle preview: http://127.0.0.1:47831'));
