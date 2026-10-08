const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const i18nSource = fs.readFileSync(path.join(__dirname, '../Data/PrismaUI/views/Chronicle/i18n.js'), 'utf8');
const source=fs.readFileSync(path.join(__dirname,'../Data/PrismaUI/views/Chronicle/controls.js'),'utf8');
function setup() {
  const saved=new Map(),nodes=new Map();
  const node=()=>({textContent:'',value:'',options:[],classList:{add(){},remove(){},toggle(){}},replaceChildren(){this.options=[];},appendChild(v){this.options.push(v);}});
  const ctx=vm.createContext({
    messages:[],inputHistory:[],inputHistoryIndex:-1,unreadCount:0,userScrolledUp:false,config:{general:{persist_history:true,max_messages:100}},
    document:{getElementById(id){if(!nodes.has(id))nodes.set(id,node());return nodes.get(id);},createElement:node},
    localStorage:{setItem:(key,v)=>saved.set(key,v)},clearTimeout(){},
    showUnread(){},restoreHistory(){ctx.messages.push(...JSON.parse(saved.get(vm.runInContext('historyKey()',ctx))||'[]'));}
  });
  vm.runInContext(i18nSource,ctx); vm.runInContext(source,ctx);
  return {ctx,saved};
}
test('character switches flush pending history and restore the correct profile',()=>{
  const {ctx,saved}=setup();
  ctx.setRuntime({profile:'character-A',loaded:true,character:'Dragonborn'});
  ctx.messages.push({channel:'dialogue',speaker:'Lydia',message:'A only',timestamp:1});
  ctx.setRuntime({profile:'character-B',loaded:true,character:'Dragonborn'});
  assert.equal(ctx.messages.length,0);
  ctx.messages.push({channel:'dialogue',speaker:'Lydia',message:'B only',timestamp:2});
  ctx.setRuntime({profile:'character-A',loaded:true,character:'Dragonborn'});
  assert.equal(ctx.messages[0].message,'A only');
  assert.equal(JSON.parse(saved.get('chronicle_history_character-B'))[0].message,'B only');
});
test('disabled persistence does not write history',()=>{
  const {ctx,saved}=setup();
  ctx.setRuntime({profile:'A',loaded:true,character:'A'});
  ctx.config.general.persist_history=false;
  ctx.messages.push({message:'not saved'}); ctx.flushHistory();
  assert.equal(saved.size,0);
});
