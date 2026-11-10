import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import {join, resolve} from 'node:path';
const html=readFileSync(new URL('../main/web/home.html',import.meta.url),'utf8');
const client=readFileSync(new URL('../lunaros/services/required/service_local_web/src/device_web/device-client.js',import.meta.url),'utf8');
const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const ids=new Set([...html.matchAll(/id="([^"]+)"/g)].map(m=>m[1]));
const nodes=new Map(),events={},timers=new Map(),calls=[],uploads=[];
let timerId=0,revision=1,reply=null,slow=null,uploadFailure=null;
const store=new Map([['lunar_access_grant','owner'],['lunar_admin_grant','admin']]);
const element=()=>({textContent:'',value:'',checked:true,open:false,disabled:false,classList:{toggle(){}},
 addEventListener(name,fn){this[name]=fn;},click(){},get innerHTML(){return this._html??this.textContent.replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;').replaceAll('"','&quot;');},set innerHTML(v){this._html=v;}});
const get=id=>{assert(ids.has(id),`Missing DOM element ${id}`);if(!nodes.has(id))nodes.set(id,element());return nodes.get(id);};
const context=vm.createContext({console,AbortController,AbortSignal,TextDecoder,TextEncoder,Blob,Uint8Array,
 document:{hidden:true,getElementById:get,createElement:element,querySelectorAll:()=>[],addEventListener:(name,fn)=>events[name]=fn},
 window:{addEventListener:(name,fn)=>events[name]=fn},
 sessionStorage:{getItem:k=>store.get(k)||null,removeItem:k=>store.delete(k)},
 setTimeout:(fn,ms)=>{timers.set(++timerId,{fn,ms});return timerId;},clearTimeout:id=>timers.delete(id),
 fetch:async(url,options={})=>{
   calls.push({url,options});if(slow)await slow;
   if(reply)return reply;
   let data={};
   if(url.endsWith('/status.get'))data={playlist_revision:revision,volume:10,audio_ready:true,sd_ready:true};
   if(url.endsWith('/playlist.get'))data={status:{playlist_revision:revision},tracks:[{name:'<test>.mp3',path:'/sdcard/music/test.mp3',size:417}]};
   return {ok:true,status:200,json:async()=>({ok:true,result:data})};
 },
 XMLHttpRequest:class {
   upload={};headers={};status=200;responseText='{"ok":true,"result":{"uploaded":true}}';
   open(method,url){this.method=method;this.url=url;}
   setRequestHeader(k,v){this.headers[k]=v;}
   send(body){uploads.push({xhr:this,body});if(uploadFailure){this[uploadFailure]();return;}this.onload();}
   abort(){this.onabort();}
 }
});
vm.runInContext(client,context);vm.runInContext(script,context);
assert.equal(calls.length,0,'Hidden pages do not request device state');
context.document.hidden=false;await context.poll();
assert.equal(calls.length,2);assert.match(get('tracks').innerHTML,/&lt;test&gt;/);
await context.poll();assert.equal(calls.length,3,'Unchanged library uses only a small status query');
revision++;await context.poll();assert.equal(calls.length,5);
assert.equal(calls.filter(c=>c.url.endsWith('/debug.get')).length,0,'Closed diagnostics do no work');
get('audioPanel').open=true;await context.poll();
assert.equal(calls.filter(c=>c.url.endsWith('/audio.settings.get')).length,1);
await context.poll();assert.equal(calls.filter(c=>c.url.endsWith('/audio.settings.get')).length,1);
get('debugPanel').open=true;await context.poll();assert(calls.some(c=>c.url.endsWith('/debug.get')));
get('debugPanel').open=false;
let release;slow=new Promise(resolve=>release=resolve);
const before=calls.length,first=context.poll();
await context.poll();await context.poll();assert.equal(calls.length,before+1,'Slow polling never overlaps');
release();slow=null;await first;assert.equal(timers.size,1);
context.document.hidden=true;const hidden=calls.length;await context.poll();assert.equal(calls.length,hidden);
context.document.hidden=false;
await context.bus('control',{action:'pause'});
assert.equal(calls.at(-1).options.method,'POST');
assert.equal(calls.at(-1).options.headers['X-Lunar-Access-Grant'],'owner');
assert.equal(calls.at(-1).options.headers['X-Lunar-Debug-Token'],'admin');
assert(calls.at(-1).options.signal,'JSON requests have a timeout');
await context.bulkUpload({name:'a & b.mp3'});
assert.match(uploads.at(-1).xhr.url,/name=a%20%26%20b.mp3/);
assert.equal(uploads.at(-1).xhr.headers['X-Lunar-Debug-Token'],'admin');
assert.equal(uploads.at(-1).xhr.timeout,300000);
for(const [event,code] of [['onabort','upload_cancelled'],['ontimeout','upload_timeout'],['onerror','upload_network_error']]){
 uploadFailure=event;await assert.rejects(context.bulkUpload({name:'x.mp3'}),error=>error.code===code);
}
uploadFailure=null;
context.setUploadActive(true);let encodeDone;
const uploadCount=uploads.length;
const pendingLyric=context.sendLyric({size:12,arrayBuffer:()=>new Promise(resolve=>encodeDone=resolve)},{path:'/sdcard/music/test.mp3'});
get('cancelUpload').onclick();encodeDone(new TextEncoder().encode('[00:01]test').buffer);
await assert.rejects(pendingLyric,error=>error.code==='upload_cancelled');
assert.equal(uploads.length,uploadCount,'Cancellation during lyric conversion must not start a later request');
context.setUploadActive(false);context.setUploadActive(true);context.setUploadActive(false);
reply={ok:false,status:401,json:async()=>({ok:false,error:{code:'debug_session_required'}})};
await assert.rejects(context.bus('debug.get'),error=>error.code==='debug_session_required'&&error.status===401);
assert(!store.has('lunar_admin_grant'));assert(store.has('lunar_access_grant'));
reply=null;events.pagehide();assert.equal(timers.size,0);
const final=calls.length;await context.poll();assert.equal(calls.length,final);
events.pageshow();await new Promise(resolve=>setImmediate(resolve));
assert(calls.length>final,'A browser back/forward cache restoration resumes polling with fresh cancellation state');
assert(!calls.at(-1).options.signal.aborted);
events.pagehide();
for(const legacy of ['smart_speaker.ota','smart_speaker.logs','smart_speaker.provision','upload.batch.finish','spectrum_mode'])assert(!html.includes(legacy));
for(const path of ['/setup','/server','/version','/debug','/device-client.js'])assert(html.includes(path));
console.log('PASS speaker web + shared OS transport: hidden/closed idle, one in-flight poll, library revisions, escaped names, timeout, owner/admin headers, cancellation and retirement');
