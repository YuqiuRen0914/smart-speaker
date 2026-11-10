import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const script=readFileSync(new URL('../../src/device_web/portal.js',import.meta.url),'utf8');
function harness(path='/', ap=false) {
  const nodes=new Map(), requests=[];
  const element=() => ({textContent:'',className:'',hidden:false,value:'',type:'password',disabled:false,files:[],children:[],dataset:{},attributes:{},handlers:{},setAttribute(k,v){this.attributes[k]=v},removeAttribute(k){delete this.attributes[k]},addEventListener(k,v){this.handlers[k]=v},append(...v){this.children.push(...v)},replaceChildren(...v){this.children=v},focus(){}});
  const get=id=>{if(!nodes.has(id))nodes.set(id,element());return nodes.get(id)};
  const nav=['home','network','binding','maintenance'].map(page=>{const e=element();e.dataset.page=page;return e});
  const state={ap, fail:false, submit:null, statuses:[], identity:{device_name:'测试设备',fw_version:'1.2.3'}, upload:{ok:true,data:{success:true,restart_pending:true}}, uploadStatus:200};
  class Xhr {upload={};open(method,url){this.method=method;this.url=url}setRequestHeader(){}send(file){requests.push({url:this.url,options:{method:this.method},file});this.upload.onprogress?.({lengthComputable:true,loaded:file.size,total:file.size});this.status=state.uploadStatus;this.responseText=JSON.stringify(state.upload);this.onload();}}
  const context=vm.createContext({document:{hidden:false,getElementById:get,createElement:element,querySelectorAll:q=>q==='nav a'?nav:[]},location:{pathname:path,search:''},URL,URLSearchParams,TextEncoder,Date,AbortSignal:{timeout:()=>undefined},confirm:()=>true,XMLHttpRequest:Xhr,setTimeout:(fn,ms)=>{if(ms<5000)queueMicrotask(fn)},fetch:async(url,options={})=>{
    requests.push({url,options});if(state.fail)throw new Error('NetworkError');
    if(url==='/device-web/config')return {ok:true,json:async()=>({product_name:'产品',provisioning:state.ap,features:state.features||{identity:true,system:true,network:true,binding:true,ota:true,cloud_update:true},binding_ready:true,ota_ready:true})};
    let data={};
    if(url.endsWith('identity/status.get'))data=state.identity;
    else if(url.endsWith('network/provision.status')) { const next=state.statuses.shift();if(next instanceof Error)throw next;data=next||{connected:true,sta_ssid:'Lab',ip:'192.168.1.9'}; }
    else if(url.endsWith('network/provision.submit'))data=state.submit;
    else if(url.endsWith('device_link/status.get'))data={state:'unbound',online:true,ready:true};
    else if(url.endsWith('ota/status.get'))data={version:'1.2.3',rollback_enabled:true};
    return {ok:true,json:async()=>({ok:true,data})};
  }});
  vm.runInContext(script,context);
  return {context,state,get,requests,nav,flush:()=>new Promise(resolve=>setImmediate(resolve))};
}
const h=harness('/');await h.flush();
assert.equal(h.get('deviceName').textContent,'测试设备');assert.equal(h.get('home').hidden,false);
assert.equal(h.context.validateSsid(' Lab '),' Lab ');
assert.throws(()=>h.context.validateSsid('x'.repeat(33)));
assert.throws(()=>h.context.validateSsid('坏'.repeat(11)));
assert.equal(h.context.safeLocalAddress('192.168.1.9@evil.example'),'');assert.equal(h.context.safeLocalAddress('999.1.1.1'),'');
assert.equal(h.context.safeBindingUrl('javascript:alert(1)'),'');
h.context.renderNetworks([{ssid:' Lab ',rssi:-30},{ssid:'Lab',rssi:-40},{ssid:' Lab ',rssi:-90},{ssid:'<img src=x>',rssi:-80}]);
const rows=h.get('networks').children;assert.equal(rows.length,3);assert.equal(rows[0].children[0].textContent,' Lab ');rows[0].handlers.click();assert.equal(h.get('wifiName').value,' Lab ');
h.get('password').value='pw';h.state.submit={result:'ok',submitted_ssid:' Lab ',status:{saved_wifi:{credentials:[{ssid:' Lab '}]}}};
h.state.statuses=[{connected:true,sta_ssid:'Old'},{connected:true,sta_ssid:' Lab '}];await h.context.submitNetwork();
assert.match(h.get('wifiMessage').textContent,/已确认连接/);assert.equal(JSON.parse(h.requests.find(r=>r.url.endsWith('provision.submit')).options.body).ssid,' Lab ');
h.state.submit.submitted_ssid='Wrong';await assert.rejects(()=>h.context.submitNetwork(),/未确认保存/);
h.state.submit.submitted_ssid=' Lab ';h.state.statuses=[new Error('offline'),new Error('offline'),new Error('offline')];await h.context.submitNetwork();
assert.match(h.get('wifiMessage').textContent,/尚未确认联网成功/);assert.doesNotMatch(h.get('wifiMessage').className,/success/);
h.state.submit.restart_scheduled=true;await h.context.submitNetwork();assert.match(h.get('wifiMessage').textContent,/尚未确认联网成功/);
await h.context.upload({name:'fw.bin',size:100});assert.equal(h.requests.at(-1).options.method,'PUT');
h.state.upload={ok:true,data:{success:false,message:'rejected'}};await assert.rejects(()=>h.context.upload({size:100}),/rejected/);
h.context.renderBinding({state:'pairing',code:'123456',expires_at_ms:Date.now()-100,online:true});assert.equal(h.get('codeBlock').hidden,true);
h.state.fail=true;await h.context.refresh();assert.equal(h.get('saveName').disabled,true);assert.equal(h.get('upload').disabled,true);
const ap=harness('/',true);await ap.flush();assert.equal(ap.get('network').hidden,false);assert.equal(ap.get('pair').disabled,true);assert.equal(ap.get('upload').disabled,true);
assert(!ap.requests.some(r=>/device_link\/|ota\//.test(r.url)));
const recover=harness('/recovery');await recover.flush();assert.equal(recover.nav[1].href,'/recovery?view=network');
const limited=harness('/binding');await limited.flush();limited.state.features={identity:true,network:true,system:true,binding:false,ota:false};
limited.requests.length=0;await limited.context.refresh();assert(limited.nav[2].hidden&&limited.nav[3].hidden);assert(limited.get('binding').hidden);assert.match(limited.get('notice').textContent,/未提供/);
assert(!limited.requests.some(r=>/device_link\/|ota\//.test(r.url)));
recover.context.location.search='?view=maintenance';await recover.context.refresh();assert.equal(recover.get('maintenance').hidden,false);
console.log('Device Web routes, exact SSIDs, injection safety, connection acknowledgement, recovery, OTA result and offline/AP restrictions passed');
