const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname,'../Data/PrismaUI/views/Chronicle/bridge.js'),'utf8');
function host(window) {
  const context=vm.createContext({window,console:{error(){}}}); vm.runInContext(source,context); return context;
}
test('PrismaUI v1/v2 named listeners receive focus, input, settings and release', () => {
  const calls=[];
  const window=Object.fromEntries(['focusAcquired','focusReleased','playerInput','settingsChanged','uiReady','diagnostics','exportHistory']
    .map(name=>[name,value=>calls.push([name,value])]));
  const ctx=host(window);
  assert.equal(ctx.sendToGame('focusAcquired',''),true);
  assert.equal(ctx.sendToGame('playerInput','Hello Lydia'),true);
  ctx.sendToGame('settingsChanged',{general:{hide_in_menus:false}});
  ctx.sendToGame('focusReleased','');
  assert.deepEqual(calls,[['focusAcquired',''],['playerInput','Hello Lydia'],
    ['settingsChanged','{"general":{"hide_in_menus":false}}'],['focusReleased','']]);
});
test('missing bridge and native failures are reported without throwing', () => {
  assert.equal(host({}).sendToGame('focusAcquired',''),false);
  assert.equal(host({focusAcquired(){throw Error('not ready');}}).sendToGame('focusAcquired',''),false);
  assert.equal(host({alert(){throw Error('unexpected');}}).sendToGame('alert',''),false);
});
test('readiness can recover when the native listener arrives late', () => {
  const window={},ctx=host(window); let calls=0;
  assert.equal(ctx.sendToGame('uiReady',''),false);
  window.uiReady=()=>calls++;
  assert.equal(ctx.sendToGame('uiReady',''),true);
  assert.equal(calls,1);
});
