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
test('a search or speaker filter does not carry over to another character',()=>{
  const {ctx}=setup();
  ctx.setRuntime({profile:'character-A',loaded:true,character:'Dragonborn'});
  ctx.document.getElementById('history-search').value='dragon';
  ctx.document.getElementById('speaker-filter').value='Lydia';
  vm.runInContext("searchText='dragon'; speakerFilter='Lydia';",ctx);
  ctx.setRuntime({profile:'character-B',loaded:true,character:'Dragonborn'});
  assert.equal(vm.runInContext('searchText',ctx),'');
  assert.equal(vm.runInContext('speakerFilter',ctx),'');
  assert.equal(ctx.document.getElementById('history-search').value,'');
  assert.equal(ctx.document.getElementById('speaker-filter').value,'');
});
test('disabled persistence does not write history',()=>{
  const {ctx,saved}=setup();
  ctx.setRuntime({profile:'A',loaded:true,character:'A'});
  ctx.config.general.persist_history=false;
  ctx.messages.push({message:'not saved'}); ctx.flushHistory();
  assert.equal(saved.size,0);
});
test('the self-test learns which character the window shows',()=>{
  const {ctx}=setup();
  const sent=[];
  ctx.sendToGame=(name,data)=>{sent.push([name,data]);return true;};
  ctx.setRuntime({profile:'1731254400',loaded:true,character:'Dragonborn'});
  ctx.messages.push({channel:'dialogue',speaker:'Lydia',message:'I am sworn to carry your burdens.',timestamp:1});
  vm.runInContext("searchText='burdens'; speakerFilter='Lydia';",ctx);
  ctx.chronicleSelfTest(5);
  assert.equal(sent.length,1);
  assert.equal(sent[0][0],'selfTestReply');
  assert.deepEqual(JSON.parse(JSON.stringify(sent[0][1])),
    {token:'5',profile:'1731254400',loaded:true,lines:1,search:'burdens',speaker:'Lydia',
      pad:{api:false,connected:0,id:''}});
  ctx.setRuntime({profile:'',loaded:false,character:''});
  ctx.chronicleSelfTest(6);
  assert.equal(sent[1][1].profile,'');
  assert.equal(sent[1][1].loaded,false);
  assert.equal(sent[1][1].search,'');
});
test('the self-test learns whether the page sees a controller',()=>{
  const {ctx}=setup();
  const sent=[];
  ctx.sendToGame=(name,data)=>{sent.push([name,data]);return true;};
  ctx.navigator={getGamepads:()=>[null,{id:'Skyrim Controller (Standard Mapping)',connected:true}]};
  ctx.chronicleSelfTest(7);
  assert.deepEqual(JSON.parse(JSON.stringify(sent[0][1].pad)),{api:true,connected:1,id:'Skyrim Controller (Standard Mapping)'});
  ctx.navigator={getGamepads:()=>[]};
  ctx.chronicleSelfTest(8);
  assert.deepEqual(JSON.parse(JSON.stringify(sent[1][1].pad)),{api:true,connected:0,id:''});
});
