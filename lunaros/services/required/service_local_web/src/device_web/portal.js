// BEGIN GENERATED LUNARWIRE EVENTS
const wireEventsSource="(function createEventChannelRuntime(wire){\n return function createEventChannel({family='channel',profile='host',binding,maxBytes=262144,binary=false}={}){\n  if(!binding||typeof binding.scope!=='string'||!binding.scope||!Number.isSafeInteger(maxBytes)||maxBytes<1)throw new TypeError('Invalid event channel binding');\n  const bound={...binding},limits={profile,binding:bound,maxBytes,binary};\n  return {\n   encode(payload){return wire.create(family,{profile,type:'event',binding:bound,method:'channel.event',payload},limits);},\n   decode(value){const packet=wire.read(family,value,limits);return packet?.type==='event'&&packet.method==='channel.event'?packet.payload:null;},\n  };\n };\n})((function createWireRuntime(contract) {\n    const record = value => {\n        if (!value || typeof value !== 'object' || Array.isArray(value)) return false;\n        const prototype = Object.getPrototypeOf(value);\n        return prototype === null || Object.getPrototypeOf(prototype) === null;\n    };\n    const identifier = value => typeof value === 'string' && /^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,199}$/.test(value);\n    const method = (family, value) => identifier(value) || family === 'service' && typeof value === 'string' && value.length <= 200\n        && /^[A-Z]+:\\/(?:[a-zA-Z0-9._:/-]|%[0-9a-fA-F]{2})*$/.test(value);\n    const arrayBufferSize=Object.getOwnPropertyDescriptor(ArrayBuffer.prototype,'byteLength').get;\n    const typedBuffer=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype),'buffer').get;\n    const dataViewBuffer=Object.getOwnPropertyDescriptor(DataView.prototype,'buffer').get;\n    const handleReaders=[];\n    for(const [name,fields] of [['Blob',['size']],['ImageBitmap',['width','height']],['OffscreenCanvas',['width','height']],['ImageData',['width','height']]]){\n        const prototype=globalThis[name]?.prototype;\n        const getters=fields.map(field=>prototype&&Object.getOwnPropertyDescriptor(prototype,field)?.get);\n        if(getters.every(Boolean))handleReaders.push(value=>{const numbers=getters.map(get=>get.call(value));return numbers.length===1?numbers[0]:numbers[0]*numbers[1]*4;});\n    }\n    function dataBytes(value, limit, binary = true, {maxNodes=100000,handles=false,nativeValues=false}={}) {\n        let bytes = 0, nodes = 0;\n        const parents = new Set(), buffers = new Set();\n        const add = size => { bytes += size; if (bytes > limit) throw new RangeError('Communication payload budget exceeded'); };\n        const visit = (item, depth) => {\n            if (++nodes > maxNodes || depth > 64) throw new RangeError('Communication structure budget exceeded');\n            add(4);\n            if (item === null || item === undefined || typeof item === 'boolean') return;\n            if (typeof item === 'number') { if (!nativeValues && !Number.isFinite(item)) throw new TypeError('Non-finite communication number'); add(8); return; }\n            if (typeof item === 'string') { add(item.length * 2); return; }\n            if (binary && (item instanceof ArrayBuffer || ArrayBuffer.isView(item))) {\n                let buffer=item;\n                if (!(item instanceof ArrayBuffer)) {try {buffer=typedBuffer.call(item);}catch {buffer=dataViewBuffer.call(item);}}\n                const length=arrayBufferSize.call(buffer); // Native brand check; never invoke a payload's buffer/byteLength getters.\n                if (!buffers.has(buffer)) { buffers.add(buffer); add(length); } return;\n            }\n            if(nativeValues && item && typeof item==='object'){\n                if(item instanceof Date){add(8);Date.prototype.getTime.call(item);return;}\n                if(item instanceof Map||item instanceof Set){\n                    if(parents.has(item))throw new TypeError('Cyclic native communication state');parents.add(item);\n                    if(item instanceof Map)Map.prototype.forEach.call(item,(value,key)=>{visit(key,depth+1);visit(value,depth+1)});\n                    else Set.prototype.forEach.call(item,value=>visit(value,depth+1));\n                    parents.delete(item);return;\n                }\n            }\n            if(handles && item && typeof item==='object' && !Array.isArray(item) && !record(item)) {\n                let size;\n                for(const read of handleReaders){try{size=read(item);break;}catch{/* native brand mismatch */}}\n                if(size!==undefined){if(!buffers.has(item)){buffers.add(item);add(size);}return;}\n            }\n            if ((!Array.isArray(item) && !record(item)) || parents.has(item)) throw new TypeError('Communication data must be acyclic plain data');\n            if (Array.isArray(item)) add(item.length * 4);\n            parents.add(item);\n            for (const [key, property] of Object.entries(Object.getOwnPropertyDescriptors(item))) {\n                if (Array.isArray(item) && key === 'length') continue;\n                if (['__proto__', 'prototype', 'constructor', 'undefined'].includes(key) || !('value' in property)) throw new TypeError('Unsafe communication property');\n                add(key.length * 2); visit(property.value, depth + 1);\n            }\n            if (Object.getOwnPropertySymbols(item).length) throw new TypeError('Communication symbols are not supported');\n            parents.delete(item);\n        };\n        if(nativeValues&&!binary)throw new TypeError('Native state requires a binary channel');\n        if (!Number.isSafeInteger(maxNodes) || maxNodes < 1) throw new RangeError('Invalid communication structure budget');\n        if (!Number.isSafeInteger(limit) || limit < 1) throw new RangeError('Invalid communication byte budget');\n        visit(value, 0); return bytes;\n    }\n    function failure(reason, code = 'remote_failure', outcome = 'failed') {\n        const object = record(reason) || reason instanceof Error ? reason : null;\n        const message = typeof reason === 'string' ? reason : typeof object?.message === 'string' ? object.message : 'Communication operation failed';\n        return { code: identifier(object?.code) ? object.code : code, message: message.slice(0, 1000),\n            ...(typeof object?.name === 'string' ? { name: object.name.slice(0, 100) } : {}),\n            ...(record(object?.details) ? { details: object.details } : {}),\n            ...(Number.isInteger(object?.status) && object.status >= 400 && object.status <= 599 ? { status: object.status } : {}), outcome: contract.outcomes.includes(object?.outcome) ? object.outcome : outcome };\n    }\n    function header(family, value, {profile,binding}={}) {\n        try {\n            if (!record(value) || !contract.families.includes(family) || value.protocol !== contract.protocol || value.family !== family || value.version !== contract.version\n                || !contract.profiles[family]?.includes(value.profile) || profile && value.profile !== profile\n                || !contract.types.includes(value.type) || Object.keys(value).some(key => !contract.fields.includes(key)) || !record(value.binding)) return null;\n            if (binding && (!record(binding) || Object.entries(binding).some(([key, expected]) => value.binding[key] !== expected))) return null;\n            if (contract.correlated.includes(value.type) && !identifier(value.requestId)) return null;\n            for (const name of ['requestId', 'operationId', 'taskId']) if (value[name] !== undefined && !identifier(value[name])) return null;\n            if (value.method !== undefined && !method(family, value.method)) return null;\n            if (value.type === 'request' && !method(family, value.method)) return null;\n            for (const name of ['generation', 'sequence']) if (value[name] !== undefined && (!Number.isSafeInteger(value[name]) || value[name] < 0)) return null;\n            if (value.error !== undefined) {\n                const error = value.error;\n                if (value.type !== 'result' || !record(error) || Object.keys(error).some(key => !contract.errorFields.includes(key))\n                    || !identifier(error.code) || typeof error.message !== 'string' || error.message.length > 1000\n                    || error.name !== undefined && (typeof error.name !== 'string' || error.name.length > 100)\n                    || error.details !== undefined && !record(error.details)\n                    || error.status !== undefined && (!Number.isInteger(error.status) || error.status < 400 || error.status > 599)\n                    || !contract.outcomes.includes(error.outcome) || value.payload !== undefined) return null;\n            }\n            return value;\n        } catch { return null; }\n    }\n    function read(family,value,options={}) {\n        try {dataBytes(value,options.maxBytes ?? 262144,options.binary ?? true,options);return header(family,value,options);}catch{return null;}\n    }\n    function create(family, message, options={}) {\n        if (!record(message)) throw new TypeError('Communication envelope must be a plain record');\n        const descriptors=Object.getOwnPropertyDescriptors(message),entries=[];\n        for(const key of Reflect.ownKeys(descriptors)) {\n            const descriptor=descriptors[key];\n            if (typeof key!=='string' || !('value' in descriptor)) throw new TypeError('Unsafe communication property');\n            entries.push([key,descriptor.value]);\n        }\n        const value={...Object.fromEntries(entries),protocol:contract.protocol,version:contract.version,family};\n        // Validate the completed wire once. Descriptors avoid invoking getters before validation.\n        dataBytes(value,options.maxBytes ?? 262144,options.binary ?? true,options);\n        if (!header(family,value,options)) throw new TypeError('Invalid or oversized communication envelope');\n        return value;\n    }\n    return { create, read, failure, dataBytes, identifier };\n})({\"revision\":3,\"version\":1,\"types\":[\"hello\",\"ready\",\"request\",\"accepted\",\"progress\",\"result\",\"event\",\"cancel\",\"close\"],\"correlated\":[\"request\",\"accepted\",\"progress\",\"result\",\"cancel\"],\"fields\":[\"protocol\",\"version\",\"family\",\"profile\",\"type\",\"binding\",\"requestId\",\"operationId\",\"taskId\",\"method\",\"payload\",\"error\",\"generation\",\"sequence\"],\"errorFields\":[\"code\",\"message\",\"name\",\"outcome\",\"status\",\"details\"],\"outcomes\":[\"unknown\",\"rejected\",\"failed\",\"canceled\"],\"profiles\":{\"service\":[\"request\",\"task\",\"control\",\"execution\",\"manager\"],\"channel\":[\"host\",\"rpc\",\"task\"],\"realtime\":[\"state\",\"event\",\"collaborative\",\"media\",\"bulk\"],\"device\":[\"management\",\"remote_control\",\"operation\",\"activity\",\"media\",\"course_onboard\",\"serial_stream\",\"media_udp\",\"authorization\",\"mqtt\",\"local_http\"]},\"identity\":{\"requestId\":\"one transport attempt; unique within the bound lifetime\",\"operationId\":\"domain-owned durable idempotency identity; never inferred from requestId\",\"taskId\":\"owner-issued recoverable task identity\",\"binding\":\"compared with authenticated transport context; never grants authority\",\"generation\":\"local compute lifetime; distinct from access, binding and delivery epochs\"},\"device\":{\"protocol\":\"lunaredu.wire\",\"revision\":4,\"sourceRepository\":\"lunaredu-hardware\",\"source\":\"protocol/wire/v1/contract.json\",\"encoding\":\"hardware-owned profile; no generic JSON wrapper for binary, media or device frames\",\"contract\":\"lunaredu.wire / device revision 4\",\"family\":\"device\",\"version\":1},\"invariants\":[\"four-families-share-semantics-not-one-binary-layout\",\"only-one-active-wire-shape-per-version-and-profile\",\"unknown-version-or-profile-is-rejected-without-format-guessing\",\"domain-methods-and-payloads-remain-domain-owned\",\"local-timeout-is-not-owner-expiry-or-business-rejection\",\"cancel-request-is-not-cancellation-confirmation-or-rollback\",\"received-accepted-completed-persisted-played-are-distinct\",\"recovery-and-replay-follow-the-registered-flow-policy\",\"credentials-do-not-travel-in-diagnostic-errors\",\"budgets-and-safe-release-are-profile-owned\"],\"socketNamespaces\":{\"message-runtime\":\"state\",\"message-inbox\":\"event\",\"classroom\":\"state\",\"platform-state\":\"state\",\"workspace-presence\":\"state\"},\"execution\":{\"maxFrameBytes\":234881024,\"maxPending\":16,\"maxSeenRequests\":10000,\"maxNodes\":2000000,\"timeoutMs\":20000,\"writeTimeoutMs\":5000,\"connectTimeoutMs\":5000},\"protocol\":\"lunaredu.wire\",\"families\":[\"service\",\"channel\",\"realtime\",\"device\"]}))";
const createWireEvents=(function createEventChannelRuntime(wire){
 return function createEventChannel({family='channel',profile='host',binding,maxBytes=262144,binary=false}={}){
  if(!binding||typeof binding.scope!=='string'||!binding.scope||!Number.isSafeInteger(maxBytes)||maxBytes<1)throw new TypeError('Invalid event channel binding');
  const bound={...binding},limits={profile,binding:bound,maxBytes,binary};
  return {
   encode(payload){return wire.create(family,{profile,type:'event',binding:bound,method:'channel.event',payload},limits);},
   decode(value){const packet=wire.read(family,value,limits);return packet?.type==='event'&&packet.method==='channel.event'?packet.payload:null;},
  };
 };
})((function createWireRuntime(contract) {
    const record = value => {
        if (!value || typeof value !== 'object' || Array.isArray(value)) return false;
        const prototype = Object.getPrototypeOf(value);
        return prototype === null || Object.getPrototypeOf(prototype) === null;
    };
    const identifier = value => typeof value === 'string' && /^[a-zA-Z0-9][a-zA-Z0-9._:/-]{0,199}$/.test(value);
    const method = (family, value) => identifier(value) || family === 'service' && typeof value === 'string' && value.length <= 200
        && /^[A-Z]+:\/(?:[a-zA-Z0-9._:/-]|%[0-9a-fA-F]{2})*$/.test(value);
    const arrayBufferSize=Object.getOwnPropertyDescriptor(ArrayBuffer.prototype,'byteLength').get;
    const typedBuffer=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(Uint8Array.prototype),'buffer').get;
    const dataViewBuffer=Object.getOwnPropertyDescriptor(DataView.prototype,'buffer').get;
    const handleReaders=[];
    for(const [name,fields] of [['Blob',['size']],['ImageBitmap',['width','height']],['OffscreenCanvas',['width','height']],['ImageData',['width','height']]]){
        const prototype=globalThis[name]?.prototype;
        const getters=fields.map(field=>prototype&&Object.getOwnPropertyDescriptor(prototype,field)?.get);
        if(getters.every(Boolean))handleReaders.push(value=>{const numbers=getters.map(get=>get.call(value));return numbers.length===1?numbers[0]:numbers[0]*numbers[1]*4;});
    }
    function dataBytes(value, limit, binary = true, {maxNodes=100000,handles=false,nativeValues=false}={}) {
        let bytes = 0, nodes = 0;
        const parents = new Set(), buffers = new Set();
        const add = size => { bytes += size; if (bytes > limit) throw new RangeError('Communication payload budget exceeded'); };
        const visit = (item, depth) => {
            if (++nodes > maxNodes || depth > 64) throw new RangeError('Communication structure budget exceeded');
            add(4);
            if (item === null || item === undefined || typeof item === 'boolean') return;
            if (typeof item === 'number') { if (!nativeValues && !Number.isFinite(item)) throw new TypeError('Non-finite communication number'); add(8); return; }
            if (typeof item === 'string') { add(item.length * 2); return; }
            if (binary && (item instanceof ArrayBuffer || ArrayBuffer.isView(item))) {
                let buffer=item;
                if (!(item instanceof ArrayBuffer)) {try {buffer=typedBuffer.call(item);}catch {buffer=dataViewBuffer.call(item);}}
                const length=arrayBufferSize.call(buffer); // Native brand check; never invoke a payload's buffer/byteLength getters.
                if (!buffers.has(buffer)) { buffers.add(buffer); add(length); } return;
            }
            if(nativeValues && item && typeof item==='object'){
                if(item instanceof Date){add(8);Date.prototype.getTime.call(item);return;}
                if(item instanceof Map||item instanceof Set){
                    if(parents.has(item))throw new TypeError('Cyclic native communication state');parents.add(item);
                    if(item instanceof Map)Map.prototype.forEach.call(item,(value,key)=>{visit(key,depth+1);visit(value,depth+1)});
                    else Set.prototype.forEach.call(item,value=>visit(value,depth+1));
                    parents.delete(item);return;
                }
            }
            if(handles && item && typeof item==='object' && !Array.isArray(item) && !record(item)) {
                let size;
                for(const read of handleReaders){try{size=read(item);break;}catch{/* native brand mismatch */}}
                if(size!==undefined){if(!buffers.has(item)){buffers.add(item);add(size);}return;}
            }
            if ((!Array.isArray(item) && !record(item)) || parents.has(item)) throw new TypeError('Communication data must be acyclic plain data');
            if (Array.isArray(item)) add(item.length * 4);
            parents.add(item);
            for (const [key, property] of Object.entries(Object.getOwnPropertyDescriptors(item))) {
                if (Array.isArray(item) && key === 'length') continue;
                if (['__proto__', 'prototype', 'constructor', 'undefined'].includes(key) || !('value' in property)) throw new TypeError('Unsafe communication property');
                add(key.length * 2); visit(property.value, depth + 1);
            }
            if (Object.getOwnPropertySymbols(item).length) throw new TypeError('Communication symbols are not supported');
            parents.delete(item);
        };
        if(nativeValues&&!binary)throw new TypeError('Native state requires a binary channel');
        if (!Number.isSafeInteger(maxNodes) || maxNodes < 1) throw new RangeError('Invalid communication structure budget');
        if (!Number.isSafeInteger(limit) || limit < 1) throw new RangeError('Invalid communication byte budget');
        visit(value, 0); return bytes;
    }
    function failure(reason, code = 'remote_failure', outcome = 'failed') {
        const object = record(reason) || reason instanceof Error ? reason : null;
        const message = typeof reason === 'string' ? reason : typeof object?.message === 'string' ? object.message : 'Communication operation failed';
        return { code: identifier(object?.code) ? object.code : code, message: message.slice(0, 1000),
            ...(typeof object?.name === 'string' ? { name: object.name.slice(0, 100) } : {}),
            ...(record(object?.details) ? { details: object.details } : {}),
            ...(Number.isInteger(object?.status) && object.status >= 400 && object.status <= 599 ? { status: object.status } : {}), outcome: contract.outcomes.includes(object?.outcome) ? object.outcome : outcome };
    }
    function header(family, value, {profile,binding}={}) {
        try {
            if (!record(value) || !contract.families.includes(family) || value.protocol !== contract.protocol || value.family !== family || value.version !== contract.version
                || !contract.profiles[family]?.includes(value.profile) || profile && value.profile !== profile
                || !contract.types.includes(value.type) || Object.keys(value).some(key => !contract.fields.includes(key)) || !record(value.binding)) return null;
            if (binding && (!record(binding) || Object.entries(binding).some(([key, expected]) => value.binding[key] !== expected))) return null;
            if (contract.correlated.includes(value.type) && !identifier(value.requestId)) return null;
            for (const name of ['requestId', 'operationId', 'taskId']) if (value[name] !== undefined && !identifier(value[name])) return null;
            if (value.method !== undefined && !method(family, value.method)) return null;
            if (value.type === 'request' && !method(family, value.method)) return null;
            for (const name of ['generation', 'sequence']) if (value[name] !== undefined && (!Number.isSafeInteger(value[name]) || value[name] < 0)) return null;
            if (value.error !== undefined) {
                const error = value.error;
                if (value.type !== 'result' || !record(error) || Object.keys(error).some(key => !contract.errorFields.includes(key))
                    || !identifier(error.code) || typeof error.message !== 'string' || error.message.length > 1000
                    || error.name !== undefined && (typeof error.name !== 'string' || error.name.length > 100)
                    || error.details !== undefined && !record(error.details)
                    || error.status !== undefined && (!Number.isInteger(error.status) || error.status < 400 || error.status > 599)
                    || !contract.outcomes.includes(error.outcome) || value.payload !== undefined) return null;
            }
            return value;
        } catch { return null; }
    }
    function read(family,value,options={}) {
        try {dataBytes(value,options.maxBytes ?? 262144,options.binary ?? true,options);return header(family,value,options);}catch{return null;}
    }
    function create(family, message, options={}) {
        if (!record(message)) throw new TypeError('Communication envelope must be a plain record');
        const descriptors=Object.getOwnPropertyDescriptors(message),entries=[];
        for(const key of Reflect.ownKeys(descriptors)) {
            const descriptor=descriptors[key];
            if (typeof key!=='string' || !('value' in descriptor)) throw new TypeError('Unsafe communication property');
            entries.push([key,descriptor.value]);
        }
        const value={...Object.fromEntries(entries),protocol:contract.protocol,version:contract.version,family};
        // Validate the completed wire once. Descriptors avoid invoking getters before validation.
        dataBytes(value,options.maxBytes ?? 262144,options.binary ?? true,options);
        if (!header(family,value,options)) throw new TypeError('Invalid or oversized communication envelope');
        return value;
    }
    return { create, read, failure, dataBytes, identifier };
})({"revision":3,"version":1,"types":["hello","ready","request","accepted","progress","result","event","cancel","close"],"correlated":["request","accepted","progress","result","cancel"],"fields":["protocol","version","family","profile","type","binding","requestId","operationId","taskId","method","payload","error","generation","sequence"],"errorFields":["code","message","name","outcome","status","details"],"outcomes":["unknown","rejected","failed","canceled"],"profiles":{"service":["request","task","control","execution","manager"],"channel":["host","rpc","task"],"realtime":["state","event","collaborative","media","bulk"],"device":["management","remote_control","operation","activity","media","course_onboard","serial_stream","media_udp","authorization","mqtt","local_http"]},"identity":{"requestId":"one transport attempt; unique within the bound lifetime","operationId":"domain-owned durable idempotency identity; never inferred from requestId","taskId":"owner-issued recoverable task identity","binding":"compared with authenticated transport context; never grants authority","generation":"local compute lifetime; distinct from access, binding and delivery epochs"},"device":{"protocol":"lunaredu.wire","revision":4,"sourceRepository":"lunaredu-hardware","source":"protocol/wire/v1/contract.json","encoding":"hardware-owned profile; no generic JSON wrapper for binary, media or device frames","contract":"lunaredu.wire / device revision 4","family":"device","version":1},"invariants":["four-families-share-semantics-not-one-binary-layout","only-one-active-wire-shape-per-version-and-profile","unknown-version-or-profile-is-rejected-without-format-guessing","domain-methods-and-payloads-remain-domain-owned","local-timeout-is-not-owner-expiry-or-business-rejection","cancel-request-is-not-cancellation-confirmation-or-rollback","received-accepted-completed-persisted-played-are-distinct","recovery-and-replay-follow-the-registered-flow-policy","credentials-do-not-travel-in-diagnostic-errors","budgets-and-safe-release-are-profile-owned"],"socketNamespaces":{"message-runtime":"state","message-inbox":"event","classroom":"state","platform-state":"state","workspace-presence":"state"},"execution":{"maxFrameBytes":234881024,"maxPending":16,"maxSeenRequests":10000,"maxNodes":2000000,"timeoutMs":20000,"writeTimeoutMs":5000,"connectTimeoutMs":5000},"protocol":"lunaredu.wire","families":["service","channel","realtime","device"]}));
// END GENERATED LUNARWIRE EVENTS
"use strict";
const $ = id => document.getElementById(id);
const assistantPreferencesModule = () => config?.features.assistant_preferences ? 'assistant_preferences' : 'assistant';
const routes = {network:'/setup', server:'/server', version:'/version', application:'/app', binding:'/binding', maintenance:'/maintenance', debug:'/debug', factory:'/factory'};
const recovery = location.pathname === '/recovery';
// Cosmetic cache only. Live config/identity remain authoritative for actions.
$('deviceName').textContent = sessionStorage.getItem('lunar_device_name') || '设备';
let currentPage = 'network', config = null, net = null, ota = null, binding = null;
let diagnostics = null, debugLogs = null;
let debugToken = sessionStorage.getItem('lunar_debug_token') || '', debugScreenUrl = '';
let refreshing = false, busy = false, nameDirty = false, contactReady = false;
let bootUpdateSelected = false;
let desktopSettingsDirty = false;
let applicationCatalogLoaded = false;
let savedName = '', nameSaving = false, mutationRevision = 0, refreshQueued = false;
let pendingSsid = '', bindingExpiryTimer;
let pairingCancelPending = false;
let unbindSubmitted = false;
let networkSubmitting = false;
let networkEditing = false, environmentDraft = false, environmentSaving = false;
let assistantEndpoint = null, assistantEndpointDraft = false;
let tlsPolicy = null, tlsDraft = false, tlsDraftOrigin = "", tlsCaPem = "", tlsLoading = false;
let savedNetworks = [], networkOrder = null, editingNetwork = null;
let nearbyNetworks = [], scanBusy = false, scanAttempted = false;
let accessGrant = sessionStorage.getItem('lunar_access_grant') || '';
let adminGrant = sessionStorage.getItem('lunar_admin_grant') || '';
const assistantEndpointDefaults = {production:'https://gateway.lunaredu.cn'};
const controlRows = new Map();
const factoryButtonIds = ['factoryFist','factoryOpen','factoryStop','factoryOutputOff',
  'factoryThumbClose','factoryThumbOpen','factoryIndexClose','factoryIndexOpen',
  'factoryMiddleClose','factoryMiddleOpen','factoryRingClose','factoryRingOpen',
  'factoryPinkyClose','factoryPinkyOpen'];
let controlsLoaded = false;
let assistantSettings = null, receiverSettings = null, assistantSettingsDraft = false;
let factoryAudio = null, factoryAssistant = null, factoryRun = null, factoryTextRun = null;
let factoryModeStatus = null, factoryModeCatalog = null;
let factoryRenderedRevision = null, factoryRenderedActive = null, factoryRenderedAppOpen = null;
let factoryRenderedHasCatalog = false;
let factoryTools = null, factoryMedia = null, factoryNetwork = null;
let factoryRefreshing = false;
let lastFactoryCapabilitiesRefreshAt = 0;
const audioFactoryButtons = ['audioSpeakerStart','audioSpeakerPass','audioSpeakerFail',
  'audioMicrophoneStart','audioMicrophonePass','audioMicrophoneFail',
  'voiceManual','voicePrepare','voiceStart','voiceStop','voiceHeard','voiceNotHeard',
  'textProbeStart','textProbeHeard','textProbeNotHeard','factoryCapabilitiesRefresh'];
function hasApplications() {
  return Boolean(config?.features?.application_page || config?.features?.applications || config?.custom_pages?.includes('application'));
}
function pageFromLocation() {
  const page = recovery ? new URLSearchParams(location.search).get('view') || 'network'
    : Object.keys(routes).find(key => routes[key] === location.pathname) || 'network';
  if (page === 'application' && config && !hasApplications()) return 'network';
  return ['binding','maintenance'].includes(page) && !config?.custom_pages?.includes(page) ? 'network' : page;
}
function applyPage() {
  const features = config?.features || {};
  $('applicationNav').textContent = config?.application_name || '应用';
  $('applicationSettingsLink').hidden = !features.application_page;
  $('applicationSettingsLink').textContent = config?.application_name || '应用设置';
  document.querySelectorAll('header a[data-page]').forEach(a => {
    const page = a.dataset.page;
    a.hidden = page === 'factory' || (page === 'application' && !hasApplications());
    a.href = recovery ? `/recovery?view=${page}` : routes[page];
    const primaryPage = currentPage === 'maintenance' || currentPage === 'debug' ? 'version' : currentPage === 'binding' ? 'server' : currentPage;
    if (page === primaryPage) a.setAttribute('aria-current', 'page');
    else a.removeAttribute('aria-current');
  });
  Object.keys(routes).forEach(page => { $(page).hidden = page !== currentPage && !(currentPage === 'server' && page === 'binding') && !(currentPage === 'version' && page === 'maintenance'); });
  if (currentPage === 'server') {
    $('binding').hidden = !features.binding || (!recovery && config?.custom_pages?.includes('binding'));
  }
  $('customBindingLink').hidden = recovery || !config?.custom_pages?.includes('binding');
  $('customMaintenanceLink').hidden = recovery || !config?.custom_pages?.includes('maintenance');
  document.querySelectorAll('a[data-page]').forEach(a => {
    a.href = recovery ? `/recovery?view=${a.dataset.page}` : routes[a.dataset.page];
  });
  $('desktopSettings').hidden = !features.desktop_settings || !canManage();
  $('settingsDebug').hidden = !features.debug;
  $('deviceSettings').hidden = !canManage() || !(features.identity || features.controls || features.debug || features.desktop_settings);
  $('assistantSettings').hidden = !features.assistant_settings || !canManage();
  $('remoteSettings').hidden = !features.remote_receiver;
  $('localSettings').hidden = !features.remote_receiver || !canManage();
  $('settingsDebug').hidden ||= !canAdmin();
  $('versionDebug').hidden = !features.debug || !canAdmin();
  $('versionCustomMaintenance').hidden = recovery || !config?.custom_pages?.includes('maintenance');
  $('versionAdmin').hidden = !canAdmin();

  const passportUrl = safeBindingUrl(config?.passport_authorization_url);
  $('passportLink').hidden = !passportUrl;
  $('passportLink').disabled = busy || !passportUrl || config?.administrator_authentication_ready===false;
  $('serverPassport').hidden=!passportUrl || canAdmin();
  $('serverPassport').disabled=busy || !passportUrl;
  $('serverAuthenticationState').textContent=config?.administrator_authentication_error ?
    errorText(config.administrator_authentication_error) : canAdmin() ? '已通过 Passport 管理员认证' : '通过 Passport 核验管理员权限后管理这台设备。';
  if(config?.summary?.platform_error_code)
    $('serverAuthenticationState').textContent+=' '+errorText(config.summary.platform_error_code);
  if(config?.access_required===false && !canAdmin())
    $('serverAuthenticationState').textContent='本地管理和固件更新无需管理员认证。'+$('serverAuthenticationState').textContent;
  if (currentPage === 'version') $('maintenance').hidden = !features.ota || (!recovery && config?.custom_pages?.includes('maintenance'));
  $('firmwareMaintenance').hidden = !features.ota;
  $('restart').hidden = !features.ota;
  $('platformEndpointPanel').hidden = !features.environment || Boolean(features.assistant_endpoint);
  $('assistantEndpointPanel').hidden = !features.assistant_endpoint || !canManage();
  $('serverAddress').hidden = false;
  $('changeServer').hidden = !(features.environment || features.assistant_endpoint);
  $('aboutProduct').textContent = config?.product_name || '—';
  $('applicationOpen').hidden = !recovery || !features.application_page;
  if (currentPage === 'application') {
    $('applicationMessage').textContent = !canManage() ? '需要授权' : config?.provisioning ? '配网期间暂不可读取应用' : features.application_page && recovery ? '请从应用页查看' : features.applications ? applicationCatalogLoaded ? $('applicationList').children.length ? '' : '暂无应用' : '正在读取应用…' : '暂无应用';
  }
}
function syncPage() {
  currentPage = pageFromLocation();
  // The URL alone owns navigation; refreshed provisioning/binding state never
  // changes the selected page. Root and recovery views default to Wi-Fi.
  if (!(currentPage in routes)) currentPage = 'network';
  applyPage();
}
function navigate(page) {
  if (['binding','maintenance'].includes(page) && !config?.custom_pages?.includes(page)) return;
  if (!(page in routes) || page === currentPage) return;
  if (busy) { message('notice','正在处理当前操作，请稍候再切换页面。'); return; }
  if (!recovery && page === 'factory') { location.assign(routes.factory); return; }
  // Product control/maintenance pages own their safety workflow. Do not replace
  // those overrides with a client-side switch to a generic portal section.
  if (!recovery && (config?.custom_pages?.includes(page) || page === 'application')) {
    if (nameDirty && !confirm('设备名称尚未保存。离开此页并放弃修改？')) return;
    nameDirty = false; location.assign(routes[page]); return;
  }
  const next = recovery ? `/recovery?view=${page}` : routes[page];
  history.pushState({page}, '', next);
  currentPage = page;
  applyPage();
  const titles = {application:'applicationTitle',server:'serverTitle',version:'versionTitle',settings:'settingsTitle',network:'networkTitle',binding:'bindingTitle',maintenance:'updateTitle',debug:'debugTitle',factory:'audioFactoryTitle'};
  $(titles[page]).setAttribute('tabindex','-1'); $(titles[page]).focus({preventScroll:true});
  refresh();
}
const utf8Length = value => new TextEncoder().encode(value).length;
function validDeviceName(value) { return Boolean(value) && utf8Length(value) <= 64 && !/[\x00-\x1f\x7f]/.test(value); }
function message(id, text, kind = '') { const node = $(id); node.textContent = text; node.className = `message ${kind}`; if (id === 'notice') node.hidden = !text; }
function errorText(error) {
  const text = String(error?.message || error || '');
  const accountErrors = {
    tls_invalid_origin:'请输入有效的 HTTPS 服务器地址。',
    tls_invalid_policy_or_ca:'CA 证书无效。请选择 PEM 格式的 CA 证书，不能包含私钥。',
    tls_ca_too_large:'CA 证书不能超过 4 KiB。',
    tls_save_failed:'证书设置未能保存，原设置保留。',
    invalid_development_base_url:'请输入可访问的 HTTPS 开发站地址，不支持 localhost。',
    invalid_assistant_base_url:'请输入有效的 HTTPS 服务地址。',
    assistant_base_url_save_failed:'地址未能保存，请重试。',
    assistant_endpoint_boot_verification_pending:'固件正在确认启动，请稍后再修改地址。',
    assistant_endpoint_restart_pending:'设备即将重启，请重连后查看状态。',
    environment_unbind_required:'请先解除当前账号绑定。',
    environment_save_failed:'环境未保存，原配置保留。请重试。',
    platform_link_environment_switch_busy:'正在处理连接请求，请稍后重试。',
    environment_confirmation_required:'请先确认切换环境。',
    environment_boot_verification_pending:'新固件正在确认启动状态，请稍后再切换。',
    unbind_confirmation_required:'请先确认是否解除当前账号绑定。',
    unbind_unavailable:'暂时无法解除绑定，请检查联网状态或等待当前请求完成。',
    unbind_request_failed:'官网尚未确认解除绑定，原绑定保留，请检查网络后重试。',
    device_already_bound:'设备已有绑定账号，请核对当前账号与设备身份。',
    device_credential_rejected:'平台未接受当前设备凭据，请使用原绑定账号确认恢复。',
    pairing_service_unreachable:'暂时无法连接官网，请检查网络后重试。',
    time_sync_failed:'设备正在校准时间，请稍后重试。'
  };
  const recoveryErrors = {
    maintenance_identity_unavailable:'设备硬件标识暂不可用，请稍后重试。',
    maintenance_protocol_unavailable:'生产 Passport 尚不支持独立维护认证；本地 OTA、网络与服务器设置仍可使用。',
    maintenance_authority_unavailable:'暂时无法连接生产 Passport；本地 OTA、网络与服务器设置仍可使用。',
    maintenance_trust_loading:'正在读取生产 Passport 的维护公钥，请稍后重试。',
    maintenance_trust_admission_unavailable:'网络服务正忙，请稍后重试维护认证。',
    recovery_identity_unavailable:'设备缺少有效恢复身份，需要恢复出厂备份或现场维护。',
    device_credential_unavailable:'设备尚未取得平台凭据，暂时不能申请 Passport 管理员授权。',
    device_credential_invalid:'原设备凭据格式不可用，已保留原值，需要经过确认的身份恢复。',
    device_time_unavailable:'设备时间尚未校准，请确认网络允许时间同步后重试。',
    device_authority_unavailable:'设备的平台地址不可用，需要恢复正确的平台配置。',
    device_binding_unavailable:'设备正在恢复平台身份，请待平台连接完成后重试。',
    administrator_required:'此修复需要 Passport 管理员授权。',
    recovery_configuration_unavailable:'没有可用于修复的有效配置副本。',
    platform_endpoint_not_found:'平台身份服务入口不可用，请检查平台部署。',
    platform_rate_limited:'平台正在限制请求，请稍后重试。',
    platform_service_unavailable:'平台身份服务暂不可用，请稍后重试。',
    platform_response_invalid:'平台身份响应无效，暂时不能确认设备权限。',
    platform_transport_unavailable:'暂时无法连接平台身份服务。',
    device_identity_conflict:'当前设备凭据与平台身份不一致，需要原账号确认恢复。'
  };
  if (recoveryErrors[text]) return recoveryErrors[text];
  if (accountErrors[text]) return accountErrors[text];
  const known = {invalid_device_name:'设备名称不能为空，且不能超过 64 字节。', identity_save_failed:'设备未能保存名称，请重试。', pairing_unavailable:'暂时无法获取绑定码，请检查网络及当前绑定状态。', platform_link_not_ready:'绑定服务尚未就绪，请稍后重试。', ota_upload_in_progress:'正在更新固件，请等待完成。', invalid_ota_project:'固件与当前产品不匹配。', forbidden:'当前连接不允许此操作。', bound_access_required:'设备已绑定，此管理操作需要已授权的入口。', provisioning_surface_restricted:'当前连接只允许出厂测试和配网操作。', hand_not_ready:'舵机模块尚未就绪，请刷新后重试。', servo_write_failed:'舵机动作失败，请停止输出并检查供电。', debug_session_required:'调试授权已失效，请重新输入串口调试码。', debug_code_invalid:'调试码不正确，请核对设备串口。', debug_session_rate_limited:'尝试过于频繁，请稍后再试。', display_frame_not_available:'显示驱动尚未产生可读取的真实画面。', screen_capture_failed:'读取真实画面失败，请重试。'};
  if (known[text]) return known[text];
  if (/Abort|Timeout|Failed to fetch|NetworkError|Load failed/i.test(text)) return '设备暂时无法访问，请检查连接后重试。';
  return text || '操作失败，请重试。';
}
async function json(url, options = {}) {
  try { return await LunarDeviceClient.request(url, options); }
  catch (error) {
    if (error.status === 401) { debugToken=''; invalidateAdminAccess(); }
    throw error;
  }
}
async function api(module, operation, payload) {
  try { return await LunarDeviceClient.api(module, operation, payload); }
  catch(error) { if(error.status===401){debugToken='';invalidateAdminAccess();}throw error; }
}
function bytes(value) {
  if (typeof value !== 'number' || !Number.isFinite(value)) return '—';
  return value >= 1048576 ? `${(value/1048576).toFixed(2)} MiB` : `${Math.round(value/1024)} KiB`;
}
function safeLocalAddress(ip) {
  if (typeof ip !== 'string' || !/^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/.test(ip) || ip.split('.').some(n => Number(n) > 255) || ip === '0.0.0.0') return '';
  return `http://${ip}/`;
}
function safeBindingUrl(value) {
  try { const url = new URL(value); return url.protocol === 'https:' && !url.username && !url.password ? url.href : ''; } catch { return ''; }
}
function validateSsid(value) {
  if (!value.length || utf8Length(value) > 32 || value.includes('\0')) throw new Error('Wi-Fi 名称不能为空，且不能超过 32 字节。');
  return value;
}
function accessChanged() {
  const restricted = Boolean(config?.provisioning && !config?.administrator_authorized) || !contactReady || !canManage();
  const networkConfigDisabled = busy || !contactReady || !config?.features.network_config || !canManage() || Boolean(net?.saved_wifi?.fixed_credentials);
  $('networkMaintenance').hidden=!config?.features.provisioning;
  $('startProvisioning').disabled=busy || !contactReady || !canManage() || Boolean(config?.provisioning);
  $('connectFirstSaved').disabled=busy || !contactReady || !canManage() || !net?.saved_wifi?.credentials?.length;
  $('sortNetworks').disabled = networkConfigDisabled;
  $('saveNetworkSort').disabled = networkConfigDisabled || !networkOrder;
  $('cancelNetworkSort').disabled = busy;
  $('saveNetwork').disabled = networkConfigDisabled || Boolean(networkOrder);
  $('forgetNetwork').hidden = !editingNetwork;
  $('forgetNetwork').disabled = networkConfigDisabled || !editingNetwork;
  $('managementRow').hidden = canManage();
  $('bluetoothEnabled').disabled=busy || !contactReady || !canManage() || !config?.features.bluetooth_control || !net?.bluetooth_available;
  $('bluetoothEnabled').hidden=!config?.features.bluetooth_control || !net?.bluetooth_available;
  renderTlsSettings();
  $('changeServer').disabled=busy || !contactReady || !canManage() || environmentSaving;
  $('closeServer').disabled=busy || environmentSaving;


  $('managementHelp').textContent = canManage() ? '' : '请从绑定账号的设备管理入口连接。';
  $('configurationRecoveryHelp').hidden = !config?.configuration_recovery_active;
  $('configurationRecoveryHelp').textContent = config?.configuration_recovery_snapshot_available
    ? '设备使用恢复配置联网，原配置保留。管理员认证后可修复原配置。'
    : '原配置不可用，且缺少有效恢复身份。需要恢复出厂备份或现场维护，无法直接获取远程管理员授权。';
  $('restoreConfiguration').hidden = !config?.configuration_recovery_active || !config?.configuration_recovery_snapshot_available;
  $('restoreConfiguration').disabled = busy || !canAdmin();
  $('repairRecoveryState').hidden = !(canManage() && ota?.deployment_mode === 'recovery' && ota?.firmware_role === 'recovery' && ota?.deployment?.ready === false);
  $('repairRecoveryState').disabled = busy || Boolean(ota?.upload_active || ota?.pull_active || ota?.pull_queued || ota?.restart_pending);
  for (const row of controlRows.values()) row.button.disabled = busy || restricted || !row.readable;
  for(const id of ['assistantMode','assistantVolume','saveAssistantSettings']) $(id).disabled = busy || restricted || !assistantSettings;
  $('receiverEnabled').disabled = busy || restricted || !receiverSettings;
  $('receiverPair').disabled = busy || restricted || !receiverSettings?.enabled || !receiverSettings?.ready;
  $('receiverForget').disabled = busy || restricted || !receiverSettings?.enabled || !receiverSettings?.paired;
  $('receiverForgetConfirm').disabled = $('receiverForget').disabled;
  $('receiverForgetCancel').disabled = busy;
  $('apRestriction').hidden = !config?.provisioning || canAdmin();
  $('pair').disabled = busy || restricted || pairingCancelPending || binding?.environment_switch_pending || binding?.environment_restart_pending || !config?.binding_ready || !net?.connected || ['bound','connecting'].includes(binding?.state) || (binding?.state === 'pairing' && !bindingExpired());
  $('cancelPair').disabled = busy || restricted || pairingCancelPending || binding?.state !== 'pairing';
  const unbindPending = unbindSubmitted || binding?.unbind_state === 'pending';
  const unbindDisabled = busy || restricted || !config?.features.binding_unbind || !config?.binding_ready || !config?.binding_unbind_allowed || !net?.connected || binding?.state !== 'bound' || unbindPending;
  $('showUnbind').disabled = unbindDisabled;
  $('confirmUnbind').disabled = unbindDisabled;
  $('cancelUnbind').disabled = busy || unbindPending;
  for (const id of ['checkUpdate','localUpdate','bootUpdate','upload','firmwareFile','updateOnline','restart']) $(id).disabled = busy || !contactReady || !canManage() || !config?.ota_ready || Boolean(ota?.upload_active || ota?.pull_active || ota?.pull_queued || ota?.restart_pending);
  $('localUpdate').hidden = !canManage();
  $('localUpdate').disabled ||= !canManage();
  $('localUpdate').disabled ||= ota?.deployment?.ready === false;
  $('bootUpdate').hidden = !canManage() || ota?.boot_update?.protocol_version !== 1 || ota?.project_name === 'lunaros-bootstrap';
  $('bootUpdate').disabled ||= !canManage() || ota?.deployment?.ready === false;
  $('upload').disabled ||= ota?.deployment?.ready === false || (!bootUpdateSelected && Boolean(ota?.deployment?.recovery_required));
  $('updateOnline').disabled ||= ota?.deployment?.ready === false || Boolean(ota?.deployment?.recovery_required);
  $('upload').disabled ||= !canManage();
  $('firmwareFile').disabled ||= !canManage();
  if (!contactReady || !canManage() || (!busy && (ota?.upload_active || ota?.pull_active || ota?.pull_queued || ota?.restart_pending))) {
    $('localUpdateDialog').close();
    $('firmwareFile').value = '';
  }
  $('checkUpdate').disabled ||= binding?.firmware_check_state === 'pending' || !config?.features.cloud_update || !net?.connected;
  $('checkUpdate').textContent = binding?.firmware_check_state === 'pending' ? '检查中…' : '检查更新';
  $('updateOnline').disabled ||= binding?.firmware_check_state === 'pending';
  $('saveName').disabled = busy || !contactReady || !canManage() || !config?.features.identity || !nameDirty || !validDeviceName($('name').value.trim());
  $('saveName').textContent = nameSaving ? '正在保存…' : '保存';
  $('name').disabled = busy || !config?.features.identity;
  $('cancelName').hidden = !nameDirty && !nameSaving;
  $('cancelName').disabled = busy;
  $('nameForm').setAttribute('aria-busy', String(nameSaving));
  $('scan').disabled = busy || scanBusy || !contactReady || !canManage() || !config?.features.network;
  renderSavedNetworks();
  $('connect').disabled = busy || !contactReady || !canManage() || !config?.features.network || Boolean(networkOrder) || Boolean(net?.saved_wifi?.fixed_credentials);
  for (const id of ['wifiName','password','showPassword']) $(id).disabled = busy;
  $('connect').textContent = networkSubmitting ? '正在连接…' : editingNetwork && net?.connected && net.sta_ssid===editingNetwork ? '重新连接' : '连接';
  $('closeWifi').disabled = busy;
  $('changeNetwork').disabled = busy || !contactReady || Boolean(networkOrder) || !canManage() || Boolean(net?.saved_wifi?.fixed_credentials);
  $('copyCode').disabled = restricted || $('codeBlock').hidden;
  $('refreshLogs').disabled = busy || restricted || !(config?.features.debug_logs ?? config?.features.debug);
  $('exportFeedback').disabled = busy || restricted || !config?.features.debug || !diagnostics;
  $('captureScreen').disabled = busy || restricted || !(debugToken || canAdmin());
  $('closeDebug').disabled = busy || !debugToken;
  $('closeDebug').hidden = canAdmin();
  $('openDebug').disabled = busy || restricted;
  $('debugCode').disabled = busy || restricted;
  const factoryDisabled = busy || !contactReady || !recovery || !config?.provisioning || !config?.features.factory_test;
  for (const id of factoryButtonIds) $(id).disabled = factoryDisabled;
  renderEnvironment();
  renderAssistantEndpoint();
  const audioDisabled = busy || !contactReady || config?.provisioning ||
    currentPage !== 'factory' || !config?.features.factory_page;
  for (const id of audioFactoryButtons) $(id).disabled = audioDisabled;
  const audioReady = Boolean(config?.features.factory_audio && factoryAudio?.available && !factoryAudio.active);
  $('audioSpeakerStart').disabled = audioDisabled || !audioReady;
  $('audioMicrophoneStart').disabled = audioDisabled || !audioReady;
  for (const [test,prefix] of [['speaker','audioSpeaker'],['microphone','audioMicrophone']]) {
    const confirmable = Boolean(audioReady && factoryAudio?.[test]?.state === 'awaiting_confirmation');
    $(prefix+'Pass').disabled = audioDisabled || !confirmable;
    $(prefix+'Fail').disabled = audioDisabled || !confirmable;
  }
  const voiceReady = Boolean(config?.features.factory_voice && factoryAssistant?.enabled &&
    !factoryAssistant?.suspended && factoryAssistant?.transport_connected &&
    factoryAssistant?.activation_ready);
  $('voiceManual').disabled = audioDisabled || !voiceReady || factoryAssistant?.listening;
  $('voicePrepare').disabled = audioDisabled || !voiceReady || factoryAssistant?.listening || factoryAssistant?.speaking;
  $('voiceStart').disabled = audioDisabled || !voiceReady || !factoryRun ||
    factoryAssistant?.listening || factoryAssistant?.standby_mode === 'continuous_conversation' ||
    (factoryAssistant?.voice_turn_revision || 0) > factoryRun?.baseline.revision;
  $('voiceStop').disabled = audioDisabled || !factoryRun || !factoryAssistant?.listening;
  const canHear = Boolean(factoryRun && factoryRun.incoming > 0 && factoryRun.output > 0);
  $('voiceHeard').disabled = audioDisabled || !canHear;
  $('voiceNotHeard').disabled = audioDisabled || !canHear;
  $('textProbeStart').disabled = audioDisabled || !voiceReady || factoryAssistant?.listening || factoryAssistant?.speaking;
  const canHearText = Boolean(factoryTextRun && factoryTextRun.incoming > 0 && factoryTextRun.output > 0);
  $('textProbeHeard').disabled = audioDisabled || !canHearText;
  $('textProbeNotHeard').disabled = audioDisabled || !canHearText;
}
function showControl(row, result) {
  if (typeof result.value !== 'boolean') throw new Error('设备没有返回有效的开关状态，请刷新重试。');
  row.value = result.value; row.readable = true;
  row.button.textContent = result.value ? '关闭' : '开启';
  row.button.setAttribute('aria-pressed', String(result.value));
  row.status.textContent = result.value ? '已开启' : '已关闭';
}
async function refreshControls(stale = () => false) {
  $('controlsPanel').hidden = !config?.features.controls || Boolean(config.provisioning);
  if ($('controlsPanel').hidden) return;
  if (!controlsLoaded) {
    message('controlsMessage', '正在读取设备控制…');
    const catalog = await json('/api/v1/capabilities');
    if(stale())return;
    for (const module of (catalog.result?.modules || []).filter(m => m.kind === 'boolean')) {
      if (!/^[a-z0-9_]+$/.test(module.id)) continue;
      const node = document.createElement('div'); node.className = 'control-row';
      const label = document.createElement('span'); label.textContent = module.label || module.id;
      const status = document.createElement('span'); status.textContent = '正在读取'; status.setAttribute('role','status');
      const button = document.createElement('button'); button.type = 'button'; button.textContent = '读取中'; button.disabled = true;
      button.setAttribute('aria-label', `${module.label || module.id} 开关`);
      const row = {button, status, value:false, readable:false};
      button.addEventListener('click', () => action('controlsMessage', async () => {
        try { showControl(row, await api(module.id, 'state.set', {value:!row.value})); message('controlsMessage','设备已确认操作。','success'); }
        catch (error) { row.readable = false; row.status.textContent = '状态待确认，请刷新'; throw error; }
      }));
      node.append(label,status,button); $('controlsList').append(node); controlRows.set(module.id,row);
    }
    controlsLoaded = true;
  }
  let failed = false;
  await Promise.all([...controlRows].map(async ([id,row]) => {
    try { const value=await api(id,'state.get');if(!stale())showControl(row,value); }
    catch (error) { if(stale())return;failed = true; row.readable = false; row.status.textContent = errorText(error); }
  }));
  if(stale())return;
  message('controlsMessage', failed ? '部分控制暂不可用，可点击“刷新状态”重试。' : '', failed ? 'error' : '');
}
function renderFactoryStatus(status) {
  if (!status || typeof status !== 'object') return;
  $('factoryTestState').textContent = status.moving
    ? `动作进行中 · ${status.gesture || 'custom'}`
    : status.output_enabled ? '动作已完成，输出保持开启' : '输出已关闭';
}
async function refreshFactoryTest() {
  const visible = Boolean(recovery && config?.features.factory_test && config?.provisioning);
  $('factoryTestPanel').hidden = !visible;
  if (!visible) return;
  try {
    renderFactoryStatus(await api('bionic_hand', 'state.get'));
  } catch (error) {
    message('factoryTestMessage', errorText(error), 'error');
  }
}
async function runFactoryTest(run, successMessage) {
  await run();
  renderFactoryStatus(await api('bionic_hand', 'state.get'));
  message('factoryTestMessage', successMessage, 'success');
}
const factoryDelta = (now, before) => Math.max(0, (Number(now) || 0) - (Number(before) || 0));
function factoryBaseline(status) {
  return {revision:status.voice_turn_revision || 0, transcript:status.transcript_revision || 0,
    input:status.input_frames || 0, sent:status.sent_audio_packets || 0,
    incoming:status.incoming_audio_packets || 0, output:status.output_frames || 0};
}
const factoryStateText = {unavailable:'不可用',ready:'待检查',running:'检查中',
  awaiting_confirmation:'等待人工确认',passed:'通过',failed:'失败'};
function renderLocalFactory(force = false) {
  const available = Boolean(config?.features.factory_mode);
  $('localFactoryPanel').hidden = !available;
  if (!available) return;
  const active = Boolean(factoryModeStatus?.active);
  const revision = factoryModeCatalog?.revision ?? factoryModeStatus?.revision ?? null;
  const appOpen = $('localFactoryAppDetails').open;
  if (!force && revision === factoryRenderedRevision && active === factoryRenderedActive &&
      appOpen === factoryRenderedAppOpen &&
      Boolean(factoryModeCatalog) === factoryRenderedHasCatalog) return;
  factoryRenderedRevision = revision;
  factoryRenderedActive = active;
  factoryRenderedAppOpen = appOpen;
  factoryRenderedHasCatalog = Boolean(factoryModeCatalog);
  $('localFactoryEnter').hidden = active;
  $('localFactoryExit').hidden = !active;
  const checks = Array.isArray(factoryModeCatalog?.checks) ? factoryModeCatalog.checks : [];
  const passed = checks.filter(check => check.state === 'passed').length;
  $('localFactorySummary').textContent = active
    ? `已开始 · ${passed}/${checks.length} 项通过。选择一项启动，然后核对设备反馈。`
    : `共 ${checks.length || factoryModeStatus?.count || 0} 项；开始后可逐项检查。`;
  const rowsFor = items => items.map(check => {
    const row = document.createElement('div');
    row.className = 'factory-check-row';
    const info = document.createElement('div');
    const title = document.createElement('strong');
    title.textContent = check.label || check.id;
    const description = document.createElement('small');
    const detail = {operator_confirmed_pass:'人工确认通过',operator_confirmed_fail:'人工确认失败',
      cancelled_on_exit:'已取消'}[check.detail] || check.detail;
    description.textContent = `${factoryStateText[check.state] || check.state}${detail ? ' · '+detail : ''}`;
    row.title = `${check.owner} · ${check.id}`;
    info.append(title, description);
    const actions = document.createElement('div');
    actions.className = 'actions';
    const add = (label, operation, extra = {}) => {
      const button = document.createElement('button');
      button.type = 'button'; button.textContent = label;
      button.disabled = !active;
      button.addEventListener('click', () => action('localFactoryMessage', async () => {
        await api('factory_mode', operation, {id:check.id,...extra});
        factoryModeCatalog = null;
        await refreshFactoryChecks();
      }));
      actions.append(button);
    };
    if (active && !['running','awaiting_confirmation','unavailable'].includes(check.state)) add('启动', 'start');
    if (active && check.state === 'awaiting_confirmation') {
      add('通过', 'confirm', {passed:true}); add('失败', 'confirm', {passed:false});
    }
    if (active && ['passed','failed','awaiting_confirmation'].includes(check.state)) add('重置', 'reset');
    row.append(info, actions);
    return row;
  });
  const deviceChecks = active ? checks.filter(check => check.kind !== 'application') : [];
  const appChecks = active ? checks.filter(check => check.kind === 'application') : [];
  $('localFactoryDeviceList').replaceChildren(...rowsFor(deviceChecks));
  const appPassed = appChecks.filter(check => check.state === 'passed').length;
  $('localFactoryAppSummary').textContent = `应用检查 · ${appChecks.length} 项表情 · ${appPassed} 项通过`;
  $('localFactoryAppDetails').hidden = !active || appChecks.length === 0;
  $('localFactoryAppList').replaceChildren(...(appOpen ? rowsFor(appChecks) : []));
}
function renderFactoryAudio() {
  for (const [test,prefix] of [['speaker','audioSpeaker'],['microphone','audioMicrophone']]) {
    const result = factoryAudio?.[test];
    const label = {ready:'尚未测试',running:'测试中',awaiting_confirmation:'等待你确认',
      passed:'人工确认通过',failed:'未通过'}[result?.state] || '设备未提供此项';
    $(prefix+'State').textContent = result?.detail ? `${label} · ${result.detail}` : label;
    $(prefix+'Pass').hidden = $(prefix+'Fail').hidden = result?.state !== 'awaiting_confirmation';
  }
  const mic = factoryAudio?.microphone;
  $('audioMicrophoneLevel').textContent = mic?.run_count
    ? `录音 ${mic.frames || 0} 帧 · RMS ${mic.rms || 0} · 峰值 ${mic.peak || 0}`
    : '录音电平尚无数据';
  if (!config?.features.factory_audio)
    message('audioFactoryMessage','当前固件尚无独立音频硬件测试，请更新设备固件后测试扬声器与麦克风。');
  else if (!factoryAudio?.available)
    message('audioFactoryMessage','音频硬件未初始化，暂时不能进行本地音频测试。','error');
  else message('audioFactoryMessage','');
}
function renderFactoryVoice() {
  const status = factoryAssistant;
  if (!config?.features.factory_voice) {
    $('factoryVoiceState').textContent = '当前固件未提供语音助手测试能力。';
    $('textProbeState').textContent = '当前固件未提供文字探针。';
    return;
  }
  if (!status) {
    $('factoryVoiceState').textContent = '正在读取助手状态。';
    return;
  }
  if (!status.ready || !status.transport_connected || !status.activation_ready) {
    $('factoryVoiceState').textContent = `助手未就绪：${status.last_error || status.session_state || '请检查绑定和网络'}`;
  } else if (!factoryRun) {
    $('factoryVoiceState').textContent = status.standby_mode === 'continuous_conversation'
      ? '先切为手动对话，避免连续对话覆盖本轮结果。'
      : '助手已连接，可以准备真人语音测试。';
  }
  if (!factoryRun) {
    for (const id of ['voiceCapture','voiceUplink','voiceAsr','voiceLlm','voiceTts','voicePlayback'])
      $(id).textContent = '待检查';
    $('factoryVoicePrompt').textContent = '准备后按“开始收音”，读出测试句，再按“结束并提交”。也可用设备 MODE 键开始和结束。';
  }
  if (factoryRun) {
    const base = factoryRun.baseline;
    const currentTurn = (status.voice_turn_revision || 0) > base.revision;
    const frames = factoryDelta(status.input_frames,base.input);
    const sent = factoryDelta(status.sent_audio_packets,base.sent);
    const incoming = factoryDelta(status.incoming_audio_packets,base.incoming);
    const output = factoryDelta(status.output_frames,base.output);
    if (currentTurn && !factoryRun.startedAt) factoryRun.startedAt = Date.now();
    const elapsed = factoryRun.startedAt ? Date.now()-factoryRun.startedAt : 0;
    const ended = currentTurn && !status.listening && status.voice_turn_state !== 'listening';
    const failed = currentTurn && (status.voice_turn_state === 'failed' || status.voice_turn_error);
    factoryRun.incoming = incoming; factoryRun.output = output;
    $('voiceCapture').textContent = frames
      ? `${frames} 帧，当前 RMS ${status.microphone_rms || 0} / 峰值 ${status.microphone_peak || 0}`
      : status.listening ? '正在收音，尚无采样' : '尚无本轮采样';
    $('voiceUplink').textContent = sent
      ? `${status.voice_turn_uplink_packets || sent} 包已上行`
      : ended || failed ? '未观测到本轮上行音频' : '等待上传';
    $('voiceAsr').textContent = currentTurn && status.voice_turn_recognized_text
      ? `识别原文：${status.voice_turn_recognized_text}`
      : failed || elapsed > 60000 ? '未收到本轮 ASR 原文' : '等待识别';
    $('voiceLlm').textContent = currentTurn && status.voice_turn_response_text
      ? `回复：${status.voice_turn_response_text}`
      : failed || elapsed > 60000 ? '未收到本轮 AI 回复' : '等待回复';
    $('voiceTts').textContent = incoming
      ? `本轮收到 ${incoming} 个音频包`
      : failed || elapsed > 60000 || status.voice_turn_state === 'completed'
        ? '未收到本轮 TTS 音频包' : '等待音频下行';
    $('voicePlayback').textContent = factoryRun.heard === true
      ? `你已确认听到清晰语音；本轮输出 ${output} 帧`
      : factoryRun.heard === false
        ? `你确认未听到清晰语音；本轮输出 ${output} 帧`
        : `本轮输出 ${output} 帧；需收到 TTS 包后再亲耳确认`;
    $('factoryVoicePrompt').textContent = `请说：“${factoryRun.phrase}”。按网页按钮或设备 MODE 开始/结束收音。`;
    $('factoryVoiceState').textContent = failed
      ? `本轮失败：${status.voice_turn_error || status.last_error || '设备未给出原因'}`
      : elapsed > 60000 ? '已等待 60 秒。未观测到的环节不能判定通过，请重新准备测试。'
        : status.listening ? '正在收音；说完后请结束并提交。'
          : currentTurn ? `本轮状态：${status.voice_turn_state || '处理中'} · 活动 ${status.voice_turn_activity_id || '待分配'}`
            : '已准备测试句，等待开始收音。';
  }
  if (factoryTextRun) {
    const base = factoryTextRun.baseline;
    const incoming = factoryDelta(status.incoming_audio_packets,base.incoming);
    const output = factoryDelta(status.output_frames,base.output);
    factoryTextRun.incoming=incoming; factoryTextRun.output=output;
    const replied = status.transcript_revision > base.transcript && status.transcript_role === 'assistant';
    const hearing = factoryTextRun.heard === true ? '你已确认听到清晰语音。'
      : factoryTextRun.heard === false ? '你确认未听到清晰语音。'
        : incoming && output ? '请亲耳确认是否听到语音。' : '未收到音频时不能认定已发声。';
    $('textProbeState').textContent = `${replied ? `AI 回复：${status.transcript_text || '空文本'}` : '等待 AI 回复'}；TTS ${incoming} 包；设备输出 ${output} 帧。${hearing}`;
  }
  accessChanged();
}
function renderFactoryCapabilities() {
  const network = factoryNetwork;
  $('factoryNetworkState').textContent = network
    ? network.connected
      ? `${network.sta_ssid || '已连接'} · ${network.ip || '无地址'} · ${typeof network.wifi_rssi === 'number' ? `${network.wifi_rssi} dBm` : '信号未知'}`
      : `未联网 · ${network.wifi_last_disconnect_reason ? `断开原因 ${network.wifi_last_disconnect_reason}` : '等待连接'}`
    : '当前未取得网络状态';
  $('factoryToolsState').textContent = factoryTools
    ? `已注册 ${factoryTools.count ?? factoryTools.tools?.length ?? 0} 项；云端 MCP 开关与实际调用仍需单独检查`
    : '当前未取得工具目录';
  const media = factoryMedia;
  $('factoryCameraState').textContent = media
    ? `${media.camera_state || (media.camera_ready ? 'ready' : 'idle')} · ${media.camera_frames || 0} 帧${media.camera_error ? ` · ${media.camera_error}` : ''}；拍照识别尚未接入模型`
    : '当前产品未提供摄像头状态';
  const perf = factoryAssistant?.performance;
  $('factoryMemoryState').textContent = perf
    ? `可用 ${bytes(perf.internal_free_bytes)} · 最低 ${bytes(perf.internal_minimum_free_bytes)} · 最大连续块 ${bytes(perf.internal_largest_block_bytes)} · PSRAM ${bytes(perf.psram_free_bytes)}${perf.internal_minimum_free_bytes < 8192 ? ' · 内部 RAM 水位偏低，需压测' : ''}`
    : '当前未取得内存快照';
  $('factoryStackState').textContent = perf
    ? `DMA 最大连续块 ${bytes(perf.internal_dma_largest_block_bytes)} · 管理任务内部栈 ${bytes(perf.managed_internal_stack_bytes)} · 外部栈 ${bytes(perf.managed_external_stack_bytes)} · 请求任务最低剩余栈 ${bytes(perf.request_task_stack_high_water_bytes)}`
    : '当前未取得任务栈快照';
  const assistant = factoryAssistant;
  $('factoryAudioHealth').textContent = assistant
    ? `采集失败 ${assistant.input_failures || 0} · 输出失败 ${assistant.output_failures || 0} · 下行丢包 ${assistant.downlink_queue_drops || 0} · 解码失败 ${assistant.downlink_decode_failures || 0} · 播放失败 ${assistant.downlink_playback_failures || 0}`
    : '当前未取得音频状态';
  const tools = Array.isArray(factoryTools?.tools) ? factoryTools.tools : [];
  const list = tools.map(tool => {
    const item = document.createElement('li');
    item.textContent = `${tool.name || '未命名工具'}：${tool.description || '无描述'}`;
    return item;
  });
  $('factoryToolsList').replaceChildren(...list);
  $('factoryInternalTasks').replaceChildren(...(perf?.internal_task_stacks || []).map(task => {
    const item = document.createElement('li');
    item.textContent = `${task.id || '未命名任务'}：分配 ${bytes(task.stack_bytes)}，最低剩余 ${bytes(task.minimum_free_stack_bytes)}`;
    return item;
  }));
}
async function refreshFactoryChecks(forceCapabilities = false) {
  if (factoryRefreshing || currentPage !== 'factory' || config?.provisioning) return;
  factoryRefreshing = true;
  const additionalOpen = $('factoryAdditionalChecks').open;
  try {
    // Voice turns need frequent feedback. Tool schemas and network/media state
    // do not; repeatedly fetching them costs scarce internal RAM on S31.
    const capabilitiesDue = forceCapabilities ||
      Date.now() - lastFactoryCapabilitiesRefreshAt >= 15000;
    if (capabilitiesDue) lastFactoryCapabilitiesRefreshAt = Date.now();
    const [mode,audio,assistant,tools,media,network] = await Promise.allSettled([
      config?.features.factory_mode ? api('factory_mode','status.get') : null,
      additionalOpen && config?.features.factory_audio ? api('factory_audio','status.get') : null,
      additionalOpen && config?.features.factory_voice ? api('assistant','status.get') : null,
      additionalOpen && capabilitiesDue && config?.features.factory_voice ? api('assistant','tools.get') : null,
      additionalOpen && capabilitiesDue && config?.features.factory_media ? api('media','status.get') : null,
      additionalOpen && capabilitiesDue && config?.features.network ? readNetworkStatus() : null]);
    factoryModeStatus = mode.status === 'fulfilled' ? mode.value : null;
    if (mode.status === 'rejected')
      message('localFactoryMessage',errorText(mode.reason),'error');
    else if (factoryModeStatus) message('localFactoryMessage','');
    if (factoryModeStatus && (!factoryModeCatalog ||
        factoryModeCatalog.revision !== factoryModeStatus.revision))
      factoryModeCatalog = await api('factory_mode','catalog.get');
    renderLocalFactory();
    factoryAudio = audio.status === 'fulfilled' ? audio.value : null;
    factoryAssistant = assistant.status === 'fulfilled' ? assistant.value : null;
    if (capabilitiesDue) {
      factoryTools = tools.status === 'fulfilled' ? tools.value : null;
      factoryMedia = media.status === 'fulfilled' ? media.value : null;
      factoryNetwork = network.status === 'fulfilled' ? network.value : null;
    }
    if (additionalOpen) {
      renderFactoryAudio(); renderFactoryVoice();
      if (capabilitiesDue) renderFactoryCapabilities();
    }
    accessChanged();
    if (audio.status === 'rejected' || assistant.status === 'rejected')
      message('audioFactoryMessage',errorText(audio.status === 'rejected' ? audio.reason : assistant.reason),'error');
    if (capabilitiesDue) {
      const missing = [tools,media,network].filter(result => result.status === 'rejected');
      message('factoryCapabilitiesMessage',missing.length ? `有 ${missing.length} 项能力状态读取失败；请刷新后重试。` : '',missing.length ? 'error' : '');
    }
  } catch(error) {
    message(additionalOpen ? 'audioFactoryMessage' : 'localFactoryMessage',errorText(error),'error');
  } finally { factoryRefreshing = false; }
}
function renderNetwork(status) {
  net = status;
  const connected = Boolean(status.connected);
  $('networkState').textContent = connected ? `已连接 ${status.sta_ssid || 'Wi-Fi'}` : '未连接';
  $('networkState').className = connected ? 'success' : 'help';
  renderOverview();
  renderBluetooth(status);
  if (!networkOrder && Array.isArray(status.saved_wifi?.credentials)) savedNetworks = status.saved_wifi.credentials;
  renderSavedNetworks();
  renderNetworkEditor();
  // Only changing from the device hotspot to LAN requires another address.
  // Normal Wi-Fi use stays here; account binding has its own primary tab.
  const address = config?.provisioning && connected && (!pendingSsid || pendingSsid === status.sta_ssid) ? safeLocalAddress(status.ip) : '';
  $('handoff').hidden = !address;
  $('lanRow').hidden = !address;
  $('lanLink').removeAttribute('href');
  if (address) {
    $('lanLink').href = address + 'setup';
    $('lanLink').textContent = '打开局域网页面';
    $('handoffTitle').textContent = '网络已连接';
    $('handoffText').textContent = config?.provisioning ? `将手机或电脑连接到「${status.sta_ssid}」后继续。` : '';
    $('handoffAddress').textContent = '';
  }
  accessChanged();
}
function renderNetworkEditor() {
  const dialog = $('networkEditor');
  if (networkEditing && canManage()) {
    dialog.hidden = false;
    if (!dialog.open) dialog.showModal();
  } else {
    if (dialog.open) dialog.close();
    dialog.hidden = true;
  }
  $('changeNetwork').hidden = !canManage();
  accessChanged();
}
function closeNetworkEditor() {
  networkEditing = false; editingNetwork = null;
  $('password').value = ''; $('password').type = 'password';
  $('showPassword').textContent = '显示'; $('showPassword').setAttribute('aria-pressed','false');
  renderNetworkEditor();
}
function openNetworkEditor(ssid = '', secure = true) {
  if (busy || !contactReady || networkOrder || !canManage() || net?.saved_wifi?.fixed_credentials) return;
  const saved = savedNetworks.find(item=>item.ssid===ssid);
  editingNetwork = saved ? ssid : null;
  networkEditing = true;
  $('wifiName').value = ssid; $('wifiName').type = ssid ? 'hidden' : 'text';
  $('wifiNameField').hidden = Boolean(ssid);
  $('wifiDialogTitle').textContent = ssid || '添加其他网络';
  const connected = net?.connected && net.sta_ssid===ssid;
  $('wifiDialogDetail').textContent = connected ? '已连接 · 可重新连接或忘记此网络' : saved ? '已保存 · 留空使用已保存的密码' : secure ? '输入密码以连接此网络' : '开放网络，无需密码';
  $('password').value = ''; $('password').type = 'password';
  $('password').placeholder = saved?.password_set ? '使用已保存的密码' : '输入密码';
  $('password').required = Boolean(ssid && secure && !saved);
  $('wifiPasswordField').hidden = Boolean(ssid && !secure && !saved?.password_set);
  $('showPassword').textContent = '显示'; $('showPassword').setAttribute('aria-pressed','false');
  message('wifiMessage','');
  renderNetworkEditor();
  (ssid ? secure ? $('password') : $('connect') : $('wifiName')).focus();
}
function canManage() {
  return Boolean(config && (typeof config.management_authorized === 'boolean' ? config.management_authorized : !config.bound));
}
function canAdmin() { return Boolean(config?.administrator_authorized && contactReady); }
function bluetoothText(status) {
  if(typeof status?.bluetooth_available !== 'boolean') return '状态暂不可读';
  return !status.bluetooth_available ? '当前固件未启用' : status.bluetooth_connected ? '已连接' : status.bluetooth_enabled ? '已开启' : '未开启';
}
function renderBluetooth(status) {
  $('bluetoothState').textContent = bluetoothText(status);
  $('bluetoothEnabled').checked=Boolean(status?.bluetooth_enabled);
  $('bluetoothConnection').textContent = status?.bluetooth_connected ? '配网客户端' : '蓝牙配网';
  $('bluetoothProvisioning').textContent = status?.ble_active ? '正在广播，等待客户端连接' : status?.bluetooth_enabled ? '尚未广播' : '';
  $('bluetoothProvisioning').hidden=!$('bluetoothProvisioning').textContent;
  $('bluetoothHelp').textContent = status?.ble_error ? '蓝牙配网暂不可用，请通过 Wi-Fi 网页或 USB 配网。' : status?.ble_active ? '在平台的设备连接页面选择蓝牙配网。' : status?.bluetooth_available && status?.bluetooth_enabled ? '需要配网时，在设备中打开配网模式。' : status?.bluetooth_available === false ? '当前固件未启用蓝牙，请使用 Wi-Fi 网页或 USB 配网。' : '';
  $('bluetoothHelp').hidden=!$('bluetoothHelp').textContent;
}
function platformConnectionText() {
  const state=binding?.platform_state || config?.summary?.platform_state;
  if(state==='connected')return '已连接';
  if(state==='connecting')return '正在连接';
  if(state==='error')return '连接失败';
  if(state)return '未连接';
  return binding?.online ? '已连接' : binding?.error ? '连接失败' : '正在读取';
}
function renderServerConnection() {
  const connected=typeof net?.connected==='boolean' ? net.connected : config?.summary?.network_connected;
  $('serverConnectionState').textContent = !contactReady ? '正在读取连接状态' : connected===false ? '请先连接 Wi-Fi' :
    platformConnectionText();
}
function renderOverview(unavailable = false) {
  const summary=config?.summary || {};
  $('networkOverviewState').textContent = unavailable ? '设备无法访问' : net ? net.connected ? '已连接' : '未连接' : typeof summary.network_connected==='boolean' ? summary.network_connected ? '已连接' : '未连接' : '正在读取';
  $('networkOverviewDetail').textContent = unavailable ? '正在重新连接' : `Wi-Fi · 蓝牙：${bluetoothText(net || summary)}`;
  $('platformOverviewState').textContent = unavailable ? '状态暂不可读' : platformConnectionText();
  $('platformOverviewDetail').textContent = unavailable ? '连接恢复后重新读取' : binding?.state === 'pairing' ? bindingExpired() ? '绑定码已过期' : '等待配对' : config?.binding_authority==='unknown' ? '绑定状态待核验' : binding?.state === 'bound' || config?.bound ? '已绑定' : '未绑定';
  $('versionOverviewState').textContent = unavailable ? '版本暂不可读' : summary.version || ota?.version || '正在读取';
  $('versionOverviewDetail').textContent = unavailable ? '连接恢复后重新读取' : ota?.upload_active || ota?.pull_active || ota?.pull_queued ? '正在更新，请勿断电' : ota?.available_update?.available ? '发现可用更新' : canAdmin() ? '管理员维护已授权' : '当前固件';
}
function invalidateAdminAccess() {
  adminGrant='';sessionStorage.removeItem('lunar_admin_grant');
  if(config)config.administrator_authorized=false;
  clearVersionDetails();$('debugContent').hidden=true;accessChanged();
}
function clearVersionDetails() {
  $('versionAdmin').hidden=true;

  for(const id of ['versionProject','versionBuild','versionPartition','versionBoot']) $(id).textContent='—';
}
let savedNetworksView = "";
function renderSavedNetworks() {
  const connected = net?.connected && net.sta_ssid;
  const networks = networkOrder || [
    ...savedNetworks.filter(item=>item.ssid===connected),
    ...(connected && !savedNetworks.some(item=>item.ssid===connected) ? [{ssid:connected,password_set:true}] : []),
    ...savedNetworks.filter(item=>item.ssid!==connected)
  ];
  $('networkState').hidden = networks.length > 0;
  $('sortNetworks').hidden = !canManage() || !config?.features.network_config || savedNetworks.length < 2 || Boolean(networkOrder) ;
  $('networkSortActions').hidden = !networkOrder;
  const view=JSON.stringify([networks,net?.connected,net?.sta_ssid,Boolean(networkOrder),busy,contactReady,canManage(),Boolean(net?.saved_wifi?.fixed_credentials)]);
  if(view===savedNetworksView) {
    for(const row of $('savedNetworks').children) if(row.children[1].textContent===connected)row.children[0].dataset.strength=String(wifiSignalStrength(net.wifi_rssi));
    renderNearbyNetworks(); return;
  }
  savedNetworksView=view;
  $('savedNetworks').replaceChildren(...networks.map((network,index) => {
    const row = document.createElement('li');
    const number = wifiSignalIcon(net?.connected && net.sta_ssid===network.ssid ? net.wifi_rssi : undefined);
    const button = document.createElement('button'); button.className='saved-network';
    button.textContent=network.ssid; button.disabled=busy || !contactReady || Boolean(networkOrder) || !canManage() || Boolean(net?.saved_wifi?.fixed_credentials);
    button.addEventListener('click',()=>openNetworkEditor(network.ssid,Boolean(network.password_set)));
    const status = document.createElement('span'); status.className='help';status.textContent=net?.connected && net.sta_ssid===network.ssid?'已连接':'已保存';
    row.className = net?.connected && net.sta_ssid===network.ssid ? 'wifi-connected' : '';
    row.append(number,button,status);
    if(networkOrder) for(const [label,offset] of [['上移',-1],['下移',1]]) {
      const move=document.createElement('button');move.textContent=label;move.setAttribute('aria-label',`${label} ${network.ssid}`);move.disabled=busy || index+offset<0 || index+offset>=networks.length;
      move.addEventListener('click',()=>{if(busy)return;[networkOrder[index],networkOrder[index+offset]]=[networkOrder[index+offset],networkOrder[index]];renderSavedNetworks();});row.append(move);
    }
    return row;
  }));
  renderNearbyNetworks();
}
function networkCredentials() {
  return savedNetworks.map(item=>({ssid:item.ssid,password_masked:true}));
}
async function saveNetworkDraft() {
  const ssid=validateSsid($('wifiName').value), password=$('password').value;
  if(utf8Length(password)>64)throw new Error('Wi-Fi 密码过长。');
  const credentials=networkCredentials(), index=credentials.findIndex(item=>item.ssid===ssid);
  const preserve=index>=0 && editingNetwork===ssid && !password;
  const item=preserve?credentials[index]:{ssid,password,password_masked:false};
  if(index<0) {
    if(credentials.length >= (net?.saved_wifi?.max_credentials || 5))throw new Error('已达到网络数量上限，请先移除一个网络。');
    credentials.push(item);
  } else credentials[index]=item;
  const result=await api('network','config.set',{credentials,apply:false,apply_index:index<0?credentials.length-1:index});
  const stored=result.wifi?.credentials;
  if(!Array.isArray(stored) || stored.length!==credentials.length || stored.some((item,i)=>item.ssid!==credentials[i].ssid))throw new Error('设备未确认保存网络，请刷新后核对。');
  savedNetworks=stored; $('password').value=''; editingNetwork=ssid;
  $('password').required=false;
  $('password').placeholder=stored.find(item=>item.ssid===ssid)?.password_set?'使用已保存的密码':'开放网络无需密码';
  if(net)net.saved_wifi={...net.saved_wifi,...result.wifi};
  renderSavedNetworks();
  closeNetworkEditor();message('networkFeedback','网络已保存。','success');
}
function renderEnvironment() {
  const pending = Boolean(binding?.environment_switch_pending || binding?.environment_restart_pending || environmentSaving);
  const locked = busy || !contactReady || !config?.binding_ready || !config?.features.environment;
  const bound = binding?.state === 'bound';
  const pairing = binding?.state === 'pairing';
  const development = $('environmentTarget').value === 'development';
  const url = $('environmentUrl').value.trim();
  const changed = $('environmentTarget').value !== binding?.environment || (development && url !== binding?.platform_base_url);
  $('saveEnvironment').disabled = locked || !binding || !config?.environment_allowed || pairing || pending || !environmentDraft || !changed || (development && !url);
  $('confirmEnvironment').disabled = $('saveEnvironment').disabled;
  $('cancelEnvironment').disabled = busy;
  $('environmentUrl').required = development;
  $('restartEnvironment').hidden = !binding?.environment_restart_pending;
  $('restartEnvironment').disabled = locked || !config?.environment_allowed;
  $('environmentForm').hidden = Boolean(binding?.environment_restart_pending);
  // Editing is only a local draft. Keep it usable while the device is parked
  // outside maintenance; the destructive submit remains guarded above.
  $('environmentTarget').disabled = busy || pending || !contactReady || !config?.features.environment;
  $('environmentUrl').disabled = $('environmentTarget').disabled;
  $('environmentUrlRow').hidden = !development;
  $('environmentMaintenance').hidden = Boolean(config?.environment_allowed) || !config?.custom_pages?.includes('maintenance');
  $('environmentBinding').hidden = !(bound || pairing) || !config?.custom_pages?.includes('binding');
  const prerequisites = [];
  if (pairing) prerequisites.push('完成或取消当前绑定');
  if (!config?.environment_allowed) prerequisites.push('停止设备并进入维护模式');
  renderTlsSettings();
  $('environmentHelp').textContent = !binding ? '正在读取连接站点…' : pending ? '配置已提交，等待设备确认…' : prerequisites.length ? `切换前请先${prerequisites.join('，再')}。` : '';

}
function renderAssistantEndpoint() {
  const panel = $('assistantEndpointPanel');
  if (panel.hidden) return;
  if (assistantEndpoint && !assistantEndpointDraft) {
    const current = assistantEndpoint.base_url;
    const preset = Object.entries(assistantEndpointDefaults).find(([,url]) => url === current);
    $('assistantEndpointTarget').value = preset?.[0] || 'custom';
    $('assistantEndpointCustom').value = preset ? '' : current;
  }
  const custom = $('assistantEndpointTarget').value === 'custom';
  const selected = custom ? $('assistantEndpointCustom').value.trim() : assistantEndpointDefaults[$('assistantEndpointTarget').value];
  $('assistantEndpointCustomRow').hidden = !custom;
  $('assistantEndpointCustom').required = custom;
  const disabled = busy || !contactReady || !canManage() || !assistantEndpoint ||
    Boolean(assistantEndpoint.restart_pending) || !selected || selected === assistantEndpoint.base_url;
  $('saveAssistantEndpoint').disabled = disabled;
  $('assistantEndpointTarget').disabled = busy || !contactReady || !canManage() || Boolean(assistantEndpoint?.restart_pending);
  $('assistantEndpointCustom').disabled = $('assistantEndpointTarget').disabled;
  renderTlsSettings();
}
function selectedTlsOrigin() {
  const assistant = Boolean(config?.features.assistant_endpoint);
  const value = assistant ? ($('assistantEndpointTarget').value === 'custom' ? $('assistantEndpointCustom').value.trim() : assistantEndpointDefaults[$('assistantEndpointTarget').value]) : ($('environmentTarget').value === 'development' ? $('environmentUrl').value.trim() : 'https://board.lunaredu.cn');
  try { const url = new URL(value?.includes('://') ? value : `https://${value}`); return url.protocol === 'https:' && !url.username && !url.password ? url.origin : ''; } catch { return ''; }
}
function renderTlsStatus(mode) {
  $('serverTlsStatus').textContent = mode === 'skip_verify' ? '证书策略：跳过校验' : mode === 'custom_ca' ? '证书策略：自定义 CA' : '';
  $('serverTlsStatus').className = mode === 'skip_verify' ? 'message error' : 'help';
}
function renderTlsSettings() {
  $('tlsSettings').hidden = !config?.features.network_tls;
  $('tlsForm').hidden = !canManage();
  if (!canManage()) return;
  const origin = selectedTlsOrigin();
  const stored = tlsPolicy?.origin === origin;
  if (!tlsDraft || tlsDraftOrigin !== origin) {
    $('tlsMode').value = stored ? tlsPolicy.mode : 'standard';
    tlsCaPem = ''; $('tlsCaFile').value = ''; tlsDraft = false; tlsDraftOrigin = origin;
  }
  const mode = $('tlsMode').value;
  const preset = ['https://gateway.lunaredu.cn'].includes(origin) ||
    config?.features.assistant_endpoint && $('assistantEndpointTarget').value === 'production' || !config?.features.assistant_endpoint && $('environmentTarget').value === 'production';
  $('tlsOrigin').textContent = origin ? `作用地址：${origin}` : '请先填写服务器地址。';
  $('tlsCaRow').hidden = mode !== 'custom_ca';
  $('tlsWarning').hidden = mode !== 'skip_verify';
  $('tlsCaState').textContent = tlsCaPem ? '已选择新 CA 证书。' : stored && tlsPolicy.ca_present ? '已保存 CA 证书；不选择新文件将保留它。' : '请选择 CA 证书。';
  const locked = busy || !contactReady || tlsLoading || !tlsPolicy || preset;
  $('tlsMode').disabled = locked;
  $('tlsCaFile').disabled = locked;
  const changed = mode !== (stored ? tlsPolicy.mode : 'standard') || Boolean(tlsCaPem);
  $('tlsDraftHint').hidden = !changed;
  if(changed) {
    (config?.features.assistant_endpoint ? $('saveAssistantEndpoint') : $('saveEnvironment')).disabled = true;
    if(!config?.features.assistant_endpoint) $('confirmEnvironment').disabled = true;
  }
  $('saveTls').disabled = locked || !origin || !tlsDraft || !changed || mode === 'custom_ca' && !tlsCaPem && !(stored && tlsPolicy.ca_present);
}
async function refreshTlsPolicy() {
  if (!config?.features.network_tls || !canManage() || tlsLoading) return;
  tlsLoading = true; renderTlsSettings();
  message('tlsMessage','正在读取证书设置…');
  try { tlsPolicy = await api('network','tls.get'); message('tlsMessage',''); }
  catch(error) { message('tlsMessage',errorText(error),'error'); }
  finally { tlsLoading = false; renderTlsSettings(); }
}
function renderIdentity(info) {
  savedName = info.device_name || '';
  $('deviceName').textContent = info.device_name || config?.product_name || 'LunarOS 设备';
  sessionStorage.setItem('lunar_device_name', $('deviceName').textContent);
  if (!nameDirty) $('name').value = info.device_name || '';

  config.summary ||= {};config.summary.version=info.fw_version || config.summary.version;
  renderOverview();


}
function bindingExpired() {
  return binding?.state === 'pairing' && Boolean(binding.code) && Number(binding.expires_at_ms) <= Date.now();
}
function clearBindingCode() {
  clearTimeout(bindingExpiryTimer);
  $('codeBlock').hidden = true;
  $('bindingState').hidden = false;
  $('code').textContent = ''; $('expiry').textContent = '';
  $('bindLink').hidden = true; $('bindLink').removeAttribute('href');
  message('copyMessage','');
}
function renderBinding(state) {
  const previousCode = binding?.code;
  binding = state;
  if(!config?.features.assistant_endpoint) renderTlsStatus(state.tls_mode);
  $('serverAddress').textContent = (config?.features.assistant_endpoint?assistantEndpoint?.base_url:state.platform_base_url) || '未配置';
  renderServerConnection();

  $('bindingSite').textContent = state.state === 'pairing' && state.code && state.platform_base_url ? `绑定至 ${state.platform_base_url}` : '';
  if (!environmentDraft) {
    $('environmentTarget').value = state.environment || 'production';
    if (state.environment === 'development') $('environmentUrl').value = state.platform_base_url || '';
  }
  const unbindPending = state.unbind_state === 'pending';
  if (unbindSubmitted && ['succeeded','failed'].includes(state.unbind_state)) unbindSubmitted = false;
  $('unbindPanel').hidden = !(config?.features.binding_unbind && (state.state === 'bound' || unbindPending || state.unbind_state === 'succeeded'));
  $('showUnbind').hidden = state.state !== 'bound' || unbindPending;
  if (state.state !== 'bound' || unbindPending) $('unbindConfirmation').hidden = true;
  $('unbindHelp').textContent = !net?.connected ? '请先连接 Wi-Fi。' : !config?.binding_unbind_allowed ? '请先停止设备并进入维护模式。' : '';
  $('unbindMaintenance').hidden = Boolean(config?.binding_unbind_allowed) || !config?.custom_pages?.includes('maintenance');
  if (unbindPending || unbindSubmitted) message('unbindMessage','解除请求已提交，正在等待官网确认…');
  else if (state.unbind_state === 'failed') message('unbindMessage','官网尚未确认解除绑定，原绑定保留。请检查网络后重试。','error');
  else if (state.unbind_state === 'succeeded' && state.state === 'unbound') message('unbindMessage','官网已确认解除绑定，可以重新绑定账号。','success');
  else message('unbindMessage','');
  if (state.state !== 'pairing') pairingCancelPending = false;
  const names = {bound:'已绑定', unbound:'未配对', pairing:'正在获取配对码…', connecting:'未配对', error:'未配对'};
  $('bindingState').textContent = names[state.state] || '正在读取绑定状态';
  if (state.error === 'device_credential_rejected') $('bindingState').textContent = '请重新绑定设备';
  $('bindingState').className = state.state === 'bound' ? 'state-panel success' : 'state-panel';
  $('bindingPreparation').hidden = Boolean(net?.connected);
  $('bindingPreparationText').textContent = '请先连接 Wi-Fi。';
  const active = state.state === 'pairing' && state.code && Number(state.expires_at_ms) > Date.now() && !config?.provisioning && !pairingCancelPending;
  clearTimeout(bindingExpiryTimer);
  if (!active) clearBindingCode();
  else {
    if (previousCode !== state.code) message('copyMessage','');
    bindingExpiryTimer = setTimeout(() => renderBinding(binding), Math.min(2147483647,Math.max(1,Number(state.expires_at_ms)-Date.now())));
  }
  $('codeBlock').hidden = !active; $('code').textContent = active ? state.code : '';
  $('bindingState').hidden = Boolean(active) || (['bound','unbound'].includes(state.state) && !state.error);
  $('expiry').textContent = active ? `有效至 ${new Date(state.expires_at_ms).toLocaleTimeString()}` : '';
  if (bindingExpired()) $('bindingState').textContent = '绑定码已过期，请重新获取';
  if (pairingCancelPending) $('bindingState').textContent = '正在取消绑定…';
  const url = active ? safeBindingUrl(state.bind_url) : '';
  $('bindLink').hidden = !url; $('bindLink').removeAttribute('href'); if (url) $('bindLink').href = url;
  $('bindingAccount').textContent = state.state === 'bound' ? state.account_name || '' : '';
  $('pair').textContent = bindingExpired() ? '重新获取绑定码' : '获取绑定码';
  $('pair').hidden = state.state === 'bound' || Boolean(active); $('cancelPair').hidden = state.state !== 'pairing';
  if (state.environment_restart_pending) message('bindingMessage','请到设置中重启并应用连接环境。');
  else if (state.error) message('bindingMessage', errorText(state.error), 'error');
  else message('bindingMessage', '');
  accessChanged();
  renderOverview();
}
function renderOta(state) {
  ota = state;

  const recoveryHandoff = state.message === 'recovery_restart_pending';
  $('updateState').textContent = state.restart_pending ? (recoveryHandoff ? '正在进入恢复安装，请重连设备。' : '固件已安装，等待重启。') : state.upload_active || state.pull_active || state.pull_queued ? '正在更新，请勿断电。' : state.pending_verify ? '正在确认新固件。' : '';
  const deployment = state.deployment;
  $('updatePolicy').textContent = deployment?.ready === false ? '设备更新状态不可用，请检查后重试。'
    : state.deployment_mode === 'recovery' ? deployment?.recovery_required
      ? '先进入恢复程序，再选择固件安装。设备业务将暂停。'
      : '正在恢复程序中。安装成功后启动业务；失败可重新安装。'
    : state.deployment_mode === 'dual_slot' ? '更新失败时可返回上一版业务固件。' : '';
  $('localUpdate').textContent = deployment?.recovery_required ? '进入恢复安装' : '本地安装固件';
  if (canAdmin()) {
    $('versionProject').textContent=state.project_name || '—';
    $('versionBuild').textContent=[state.build_date,state.build_time].filter(Boolean).join(' ') || '—';
    $('versionPartition').textContent=state.running_label || '—';
    $('versionBoot').textContent=state.pending_verify ? '正在确认新固件' : state.rollback_enabled ? '已启用启动确认与回滚' : '未启用回滚';
  } else clearVersionDetails();
  renderOverview();
  const update = state.available_update;
  $('updateOnline').hidden = !update?.available;
  const checkState = binding?.firmware_check_state;
  message('updateCheckMessage', checkState === 'pending' ? '正在检查…'
    : checkState === 'failed' ? '检查失败，请重试。'
    : checkState === 'succeeded' && !update?.available ? '已是最新版本。' : '',
    checkState === 'failed' ? 'error' : '');
  $('availableUpdate').textContent = update?.available ? `可用版本 ${update.version} · ${update.summary || ''}` : '';
  if (!busy && state.has_result) message('otaMessage', state.success ? (recoveryHandoff ? '正在进入恢复安装，请重连设备后选择固件。' : '固件已校验并安装。') : `更新未完成：${errorText(state.message)}`, state.success ? 'success' : 'error');
  accessChanged();
}
function renderApplications(value) {
  applicationCatalogLoaded = true;
  const applications = (Array.isArray(value.applications) ? value.applications : []).filter(item => item.kind === 'application');
  const rows = applications.map(item => {
    const row = document.createElement('div'); row.className = 'application-row';
    const name = textNode(item.title || item.id || '应用');
    const status = textNode(item.pending ? '启动中' : item.active ? '运行中' : item.available === false ? '暂不可用' : '', 'help');
    row.append(name, status); return row;
  });
  $('applicationList').replaceChildren(...rows);
  $('applicationMessage').textContent = rows.length ? '' : '暂无应用';
}
function renderNetworks(aps) {
  const byName = new Map();
  for (const ap of Array.isArray(aps) ? aps : []) {
    if (typeof ap.ssid !== 'string' || !ap.ssid.length) continue;
    const old = byName.get(ap.ssid);
    if (!old || Number(ap.rssi) > Number(old.rssi)) byName.set(ap.ssid, ap);
  }
  nearbyNetworks = [...byName.values()].sort((a,b) => Number(b.rssi)-Number(a.rssi));
  renderNearbyNetworks();
}
function wifiSignalStrength(rssi) { return typeof rssi!=='number' ? 0 : rssi>=-55 ? 3 : rssi>=-70 ? 2 : 1; }
function wifiSignalIcon(rssi) {
  const icon=document.createElement('span'); icon.className='wifi-signal';
  icon.setAttribute('aria-hidden','true');
  icon.dataset.strength=String(wifiSignalStrength(rssi));
  for(let i=0;i<3;i++)icon.append(document.createElement('i'));
  return icon;
}
let nearbyNetworksView = "";
function renderNearbyNetworks() {
  const view=JSON.stringify([nearbyNetworks,savedNetworks,net?.connected,net?.sta_ssid,busy,contactReady,canManage(),Boolean(networkOrder),Boolean(net?.saved_wifi?.fixed_credentials),scanBusy,scanAttempted]);
  if(view===nearbyNetworksView)return;
  nearbyNetworksView=view;
  const nodes = nearbyNetworks.filter(ap=>!savedNetworks.some(saved=>saved.ssid===ap.ssid) && !(net?.connected && net.sta_ssid===ap.ssid)).map(ap => {
    const row = document.createElement('button'); row.type = 'button'; row.className = 'wifi'; row.setAttribute('aria-pressed','false');
    const name = document.createElement('span'); name.className = 'wifi-name'; name.textContent = ap.ssid;
    const detail = document.createElement('small'); detail.textContent = ap.secure === false ? '开放' : '需要密码';
    row.disabled=busy || !contactReady || !canManage() || Boolean(networkOrder) || Boolean(net?.saved_wifi?.fixed_credentials);
    row.append(name,detail,wifiSignalIcon(ap.rssi)); row.addEventListener('click', () => openNetworkEditor(ap.ssid,ap.secure!==false)); return row;
  });
  if (!nodes.length) { const empty = document.createElement('p'); empty.className = 'wifi-empty help'; empty.textContent = scanBusy ? '正在搜索附近网络…' : scanAttempted ? '没有发现其他网络。可以重新搜索，或添加隐藏网络。' : '搜索后会在这里显示附近的网络。'; nodes.push(empty); }
  $('networks').replaceChildren(...nodes);
}
async function scanNetworks() {
  if(scanBusy || busy || !contactReady || !canManage() || !config?.features.network)return;
  scanBusy=true;scanAttempted=true;
  $('scanState').textContent='正在搜索…';$('networks').setAttribute('aria-busy','true');message('scanMessage','');
  accessChanged();renderNearbyNetworks();
  try {
    const result=await api(config?.provisioning ? 'provisioning' : 'network', config?.provisioning ? 'scan' : 'wifi.scan', {});
    if(!canManage() || !contactReady)return;
    if(result.status)renderNetwork(result.status);
    renderNetworks(result.aps);
    $('scanState').textContent='搜索完成';
  } catch(error) {
    $('scanState').textContent='搜索失败';message('scanMessage',`${errorText(error)} 请重新搜索。`,'error');
  } finally {
    scanBusy=false;$('networks').setAttribute('aria-busy','false');accessChanged();renderNearbyNetworks();
  }
}
function textNode(value, className = '') { const node = document.createElement('span'); node.textContent = value; node.className = className; return node; }
function renderInspectList(id, values, describe) {
  const rows = (values || []).map(value => { const row = document.createElement('div'); row.className = 'inspect-row'; const [name, detail, bad] = describe(value); row.append(textNode(name), textNode(detail,bad?'bad':'')); return row; });
  if (!rows.length) { const empty = document.createElement('p'); empty.className = 'help'; empty.textContent = '没有可显示的记录。'; rows.push(empty); }
  $(id).replaceChildren(...rows);
}
function renderDiagnostics(value) {
  diagnostics = value;
  const system = value.system || {}, services = value.service_summary || {}, tasks = value.task_summary || {};
  $('debugBoot').textContent = value.boot_id ? `启动会话 ${value.boot_id}` : '启动会话不可用';
  $('debugServices').textContent = `${services.unavailable || 0} 项`;
  $('debugTasks').textContent = `${tasks.active ?? '—'} / ${tasks.maximum_active ?? '—'}`;
  $('debugHeap').textContent = bytes(system.internal_free_bytes);
  $('debugFirmware').textContent = [system.firmware,system.compile_time].filter(Boolean).join(' · ') || '固件标识不可用';
  const facts = [['应用',system.application],['ELF SHA-256',system.elf_sha256],['运行分区',system.running_partition],['最低可用内存',bytes(system.minimum_free_heap_bytes)],['PSRAM',system.psram_total_bytes ? `${bytes(system.psram_free_bytes)} / ${bytes(system.psram_total_bytes)}` : '未配置'],['内部任务栈',bytes(tasks.internal_stack_bytes)],['外部任务栈',bytes(tasks.external_stack_bytes)]];
  const nodes = []; for (const [label,value] of facts) { const dt=document.createElement('dt');dt.textContent=label;const dd=document.createElement('dd');dd.textContent=value||'—';nodes.push(dt,dd); } $('debugFacts').replaceChildren(...nodes);
  renderInspectList('debugServiceList',value.services,service=>[service.id || '未命名服务',`${service.state || 'unknown'}${service.detail ? ` · ${service.detail}` : ''}`,!service.available]);
  renderInspectList('debugExecutorList',value.executors,executor=>[executor.id || '未命名执行器',`${executor.bound ? '已绑定' : '未绑定'} · 队列 ${executor.queue_depth ?? 0}`,!executor.bound]);
}
async function readDebugLogs(after = 0) {
  const envelope = await json(`/api/v1/modules/debug/logs.read?after=${encodeURIComponent(after)}&limit=12`);
  debugLogs = envelope.result || {records:[]};
  $('debugCursor').textContent = String(debugLogs.next_cursor ?? '—');
  const lines = (debugLogs.records || []).map(record => `${String(record.uptime_ms ?? 0).padStart(8,' ')}ms  ${String(record.message || '').trimEnd()}`);
  $('debugLogs').textContent = lines.length ? lines.join('\n') : '当前环形缓存中没有日志。';
  message('debugMessage',debugLogs.cursor_reset ? '旧日志已被覆盖，已从当前最早记录继续。' : `已读取 ${lines.length} 条近期日志。`,debugLogs.cursor_reset ? 'error' : '');
  return debugLogs;
}
async function refreshDebug() {
  const session = await api('system','diagnostics.session.status');
  const authorized = Boolean(config?.administrator_authorized || (debugToken && session.active));
  $('debugAuth').hidden = authorized; $('debugContent').hidden = !authorized;
  $('debugSessionState').textContent = config?.administrator_authorized ? '使用 Passport 管理员维护授权' : authorized ? `调试会话剩余约 ${Math.max(1,Math.ceil(session.session_remaining_ms/60000))} 分钟` : '';
  if (!authorized) { diagnostics=null;if(session.active)message('debugAuthMessage','设备已有调试会话，请回到原标签页结束会话，或等待其自动过期。','error');accessChanged();return; }
  renderDiagnostics(await api('system','diagnostics.get'));
  if(config?.features.debug_logs ?? config?.features.debug) await readDebugLogs(0);
  else {debugLogs={records:[]};$('debugLogs').textContent='当前固件未提供日志缓存。';}
  $('captureScreen').hidden = config?.features.screen_capture === false;
  if(canAdmin() && config?.features.service_recovery){
    const status=await api('service','status.get');
    for(const service of status.services||[]){
      if(!service.restartable || service.available)continue;
      const row=document.createElement('div');row.className='inspect-item';
      const label=document.createElement('span');label.textContent=service.title||service.id;
      const button=document.createElement('button');button.type='button';button.textContent='恢复服务';
      button.addEventListener('click',()=>action('debugMessage',async()=>{await api('service','restart',{id:service.id});await refreshDebug();}));
      row.append(label,button);$('debugServiceList').append(row);
    }
  }
}
async function captureScreen() {
  $('screenPlaceholder').hidden=false;$('screenPlaceholder').textContent='正在读取真实设备画面…';$('screenUnavailable').hidden=true;
  const headers={'X-Lunar-Debug-Token':adminGrant || debugToken};if(accessGrant)headers['X-Lunar-Access-Grant']=accessGrant;
  const response=await fetch('/api/v1/modules/screen/frame.capture',{headers,cache:'no-store',signal:AbortSignal.timeout(15000)});
  if(!response.ok){$('screenUnavailable').hidden=false;$('screenPlaceholder').textContent='尚未获取到真实设备画面。';if(response.status===401){debugToken='';sessionStorage.removeItem('lunar_debug_token');$('debugContent').hidden=true;$('debugAuth').hidden=false;}throw new Error(response.status===503?'display_frame_not_available':'screen_capture_failed');}
  const blob=await response.blob();if(debugScreenUrl)URL.revokeObjectURL(debugScreenUrl);debugScreenUrl=URL.createObjectURL(blob);$('debugScreen').src=debugScreenUrl;$('screenPlaceholder').hidden=true;message('screenMessage','已读取真实像素画面。','success');
}
async function refresh() {
  // Routing is local: even an offline history change must show the right page.
  syncPage();
  if (refreshing || busy) { refreshQueued = true; return; }
  if (document.hidden) return;
  refreshing = true;
  refreshQueued = false;
  const revision = mutationRevision;
  let page = currentPage;
  const stale = () => revision !== mutationRevision || page !== currentPage;
  accessChanged();
  try {
    const nextConfig = await json('/device-web/config');
    if (stale()) return;
    if(config && config.bound !== nextConfig.bound) {binding=null;clearBindingCode();}
    config = nextConfig;
    const f = config.features || {}; config.features = f;
    syncPage(); page = currentPage;
    if(!config.administrator_authorized) {
      adminGrant='';sessionStorage.removeItem('lunar_admin_grant');clearVersionDetails();
    }

    renderOverview();
    $('nameForm').hidden = !f.identity || !canManage();
    $('cloudUpdate').hidden = !f.cloud_update;
    $('checkUpdate').hidden = !f.cloud_update;
    message('notice', '');
    if (!canManage() && currentPage !== 'factory') {
      assistantSettings=null;receiverSettings=null;assistantSettingsDraft=false;
      $('localSettings').hidden=true;$('receiverForgetConfirmation').hidden=true;
      accessGrant='';sessionStorage.removeItem('lunar_access_grant');
      net=null; binding=null; ota=null; savedNetworks=[];networkOrder=null;
      editingNetwork=null;networkEditing=false;nearbyNetworks=[];scanAttempted=false;$('wifiName').value='';$('password').value='';
      renderNetworkEditor();
      $('networkState').textContent='需要授权';
      $('networks').replaceChildren();
      applicationCatalogLoaded=false;$('applicationList').replaceChildren();$('applicationMessage').textContent='需要授权';
      for(const id of ['wifiMessage','scanMessage','networkFeedback','savedNetworkMessage','bindingMessage','otaMessage','updateState','availableUpdate','updateCheckMessage']) $(id).textContent='';
      $('name').value='';
      $('factoryTestPanel').hidden=true;$('handoff').hidden=true;$('bindingPreparation').hidden=true;
      $('lanRow').hidden=true;$('lanLink').removeAttribute('href');
      clearBindingCode();renderSavedNetworks();renderNetworkEditor();
      $('deviceName').textContent=config.product_name || '设备';

      $('serverAddress').textContent='需要授权';
      $('bindingState').textContent=config.binding_authority==='unknown'?'绑定状态待核验':config.bound?'已绑定':'未绑定';$('bindingAccount').textContent='';
      $('unbindPanel').hidden=true;$('pair').hidden=true;$('cancelPair').hidden=true;
      $('cloudUpdate').hidden=true;$('firmwareMaintenance').hidden=true;$('deviceSettings').hidden=true;

      clearVersionDetails();renderBluetooth(config.summary);renderOverview();
      contactReady=true;accessChanged();renderServerConnection();return;
    }
    if ((currentPage === 'binding' && !f.binding) || (currentPage === 'maintenance' && !(f.ota || f.system)) || (currentPage === 'network' && !f.network) || (currentPage === 'debug' && !f.debug) || (currentPage === 'factory' && (!f.factory_page || config.provisioning))) {
      $(currentPage).hidden = true; message('notice','当前产品未提供此功能。'); return;
    }
    if (f.identity) {
      const identity = await api('identity','status.get');
      if (stale()) return;
      renderIdentity(identity);
    }
    if (f.network && currentPage !== 'factory') {
      const network = await readNetworkStatus();
      if (stale()) return;
      renderNetwork(network);
    }
    if (currentPage === 'network') await refreshFactoryTest();
    if (currentPage === 'application' && f.applications && !config.provisioning && !(f.application_page && recovery)) {
      const applications = await api('app','list');
      if (stale()) return;
      renderApplications(applications);
    }
    if (currentPage === 'factory') await refreshFactoryChecks();
    if (['server','version'].includes(currentPage)) await refreshLocalSettings(stale);
    if (currentPage === 'version') await refreshControls(stale);
    if (currentPage === 'server') {
      if (f.assistant_endpoint) {
        assistantEndpoint = await api('assistant_endpoint','status.get');
        if (stale()) return;
        renderAssistantEndpoint();
        if(f.assistant_endpoint) $('serverAddress').textContent=assistantEndpoint.base_url || '未配置';
        renderTlsStatus(assistantEndpoint.tls_mode);
      }
    }
    if (['server','binding','version'].includes(currentPage) && f.binding) {
      if (config.provisioning && currentPage !== 'server') {
        binding = null; clearBindingCode();
        $('unbindPanel').hidden = true; $('unbindConfirmation').hidden = true;
        $('bindingState').textContent = '先连接 Wi-Fi，再绑定账号';
        $('bindingPreparation').hidden = false;
        $('bindingPreparationText').textContent = '当前连接是设备热点，仅用于配网。完成后，将手机或电脑切换到设备所在的 Wi-Fi，再打开局域网设备地址。';
        $('bindingAccount').textContent = ''; $('cancelPair').hidden = true; $('pair').hidden = true;
      }
      else {
        const state = await api('platform_link','status.get');
        if (stale()) return;
        renderBinding(state);
      }
    }
    if (['server','version','maintenance'].includes(currentPage) && f.ota) renderOta(await api('ota','status.get'));
    if(currentPage==='version' && f.desktop_settings && canManage() && !desktopSettingsDirty){
      const p=await api('desktop_settings','preferences.get');
      const apps=await api('app','list');if(stale() || desktopSettingsDirty)return;
      const option=(label,value)=>{const node=document.createElement('option');node.textContent=label;node.value=value;return node;};
      const options=[option('桌面','')];
      for(const app of apps.applications||[])if(app.kind==='application')options.push(option(app.title||app.name||app.id,app.id));
      $('startupApplication').replaceChildren(...options);$('startupApplication').value=p.startup_application||'';
      $('desktopAnimations').checked=p.animations;$('desktopLabels').checked=p.show_labels;$('desktopTextSize').value=String(p.text_size);
    }
    if (currentPage === 'debug' && f.debug && !config.provisioning) await refreshDebug();
    if (stale()) return;
    contactReady = true; applyPage();renderServerConnection();
    if(ota && ['version','maintenance'].includes(currentPage))renderOta(ota);
    renderOverview();accessChanged();
  } catch(error) {
    if (stale()) return;
    if (currentPage === 'application') { $('applicationList').replaceChildren();$('applicationMessage').textContent='暂时无法读取应用'; }
    binding = null; clearBindingCode();
    $('unbindPanel').hidden = true; $('unbindConfirmation').hidden = true;
    $('bindingState').textContent = '暂时无法读取绑定状态'; $('bindingState').className = 'state-panel';
    $('bindingAccount').textContent = '';
    $('networkState').textContent = '暂时无法读取网络状态'; $('networkState').className = 'error';
    $('lanRow').hidden = true; $('lanLink').removeAttribute('href');
    contactReady = false; accessChanged();
    clearVersionDetails();renderBluetooth(null);renderOverview(true);
    message('notice', `${errorText(error)} 正在尝试重新连接。`, 'error');
  } finally {
    refreshing = false; accessChanged();
    if (refreshQueued || stale()) {
      refreshQueued = true;
      if (!busy) { refreshQueued = false; Promise.resolve().then(refresh); }
    }
  }
}
async function action(id, run) {
  if (busy) return;
  ++mutationRevision;
  networkSubmitting = run === submitNetwork;
  busy = true; accessChanged(); message(id,'正在处理…');
  try { await run(); } catch(error) { message(id,errorText(error),'error'); }
  finally {
    busy = false; networkSubmitting = false; accessChanged();
    renderSavedNetworks();
    if (refreshQueued && !refreshing) { refreshQueued = false; refresh(); }
  }
}
const wait = ms => new Promise(resolve => setTimeout(resolve,ms));
function renderAssistantSettings(value) {
  assistantSettings=value;
  if(assistantSettingsDraft)return;
  $('assistantMode').value=value.standby_mode==='continuous_conversation'?'continuous_conversation':'wake_word';
  $('assistantVolume').value=String(Math.max(0,Math.min(100,Number(value.speaker_volume_percent)||0)));
  $('assistantVolumeValue').textContent=`${$('assistantVolume').value}%`;
}
function renderReceiverSettings(value) {
  receiverSettings=value;$('receiverEnabled').checked=Boolean(value.enabled);
  const labels={unpaired:'未配对',pairing:'等待配对',linked:'已连接',lost:'等待连接',error:'暂不可用'};
  $('receiverState').textContent=!value.enabled?'已关闭':!value.ready?'暂不可用':labels[value.link_state]||'未知';
  $('receiverPeer').textContent=value.paired?(value.peer_mac||'已配对'):'未配对';
  $('receiverPairingHelp').textContent=value.link_state==='pairing'?`配对窗口剩余 ${Number(value.pairing_remaining_seconds)||0} 秒，请在遥控器中搜索并选择这台设备。`:'开始配对后，在遥控器中搜索并选择这台设备。摇杆回中后才能控制运动。';
  if(!value.paired)$('receiverForgetConfirmation').hidden=true;
}
async function refreshLocalSettings(stale=()=>false) {
  if(config?.provisioning&&!canAdmin())return;
  for(const [feature,module,operation,render,id] of [
    ['assistant_settings',assistantPreferencesModule(),'preferences.get',renderAssistantSettings,'assistantSettingsMessage'],
    ['remote_receiver','remote_receiver','status.get',renderReceiverSettings,'receiverMessage']]) {
    if(!config?.features[feature] || (module==='assistant' ? currentPage!=='version' : currentPage!=='server'))continue;
    try {const value=await api(module,operation);if(stale())return;render(value);}
    catch(error){if(stale())return;if(module==='assistant')assistantSettings=null;else receiverSettings=null;message(id,errorText(error),'error');}
  }
}
for(const id of ['assistantMode','assistantVolume']) $(id).addEventListener('input',()=>{assistantSettingsDraft=true;$('assistantVolumeValue').textContent=`${$('assistantVolume').value}%`;});
$('assistantSettingsForm').addEventListener('submit',event=>{
  event.preventDefault();if(busy)return;
  const draft={mode:$('assistantMode').value,volume:Number($('assistantVolume').value)};
  action('assistantSettingsMessage',async()=>{
    // Each owner confirms its setting. A later failure must not claim an atomic save.
    let saved=0,pending=false;
    try {
      for(const [operation,payload] of [['standby_mode.set',{mode:draft.mode}],['volume.set',{percent:draft.volume}]]) {
        const result=await api(assistantPreferencesModule(),operation,payload);pending ||= Boolean(result.apply_pending);++saved;
      }
      assistantSettingsDraft=false;renderAssistantSettings(await api(assistantPreferencesModule(),'preferences.get'));
      message('assistantSettingsMessage',pending?'设置已保存，等待语音服务恢复后生效。':'设置已保存。','success');
    }catch(error){throw new Error(`${saved?'部分设置已保存，':''}${errorText(error)} 请重试。`);}
  });
});
$('receiverEnabled').addEventListener('change',()=>{
  if(busy)return;const enabled=$('receiverEnabled').checked;
  action('receiverMessage',async()=>{try{renderReceiverSettings(await api('remote_receiver','enabled.set',{enabled}));message('receiverMessage',enabled?'已启用后台接收。':'后台接收已关闭，运动已停止。','success');}catch(error){$('receiverEnabled').checked=Boolean(receiverSettings?.enabled);throw error;}});
});
$('receiverPair').addEventListener('click',()=>action('receiverMessage',async()=>{renderReceiverSettings(await api('remote_receiver','pairing.begin',{}));message('receiverMessage','配对窗口已开启，新遥控器连接成功后替换原配对。','success');}));
$('receiverForget').addEventListener('click',()=>{$('receiverForgetConfirmation').hidden=false;});
$('receiverForgetCancel').addEventListener('click',()=>{$('receiverForgetConfirmation').hidden=true;});
$('receiverForgetConfirm').addEventListener('click',()=>action('receiverMessage',async()=>{renderReceiverSettings(await api('remote_receiver','pairing.clear',{}));$('receiverForgetConfirmation').hidden=true;message('receiverMessage','配对已解除，运动已停止。','success');}));
function readNetworkStatus() {
  return config?.provisioning && config?.features.provisioning
    ? api('provisioning','status') : api('network','wifi.status');
}
async function submitNetwork() {
  const ssid = validateSsid($('wifiName').value), password = $('password').value;
  if (utf8Length(password) > 63 || (password && utf8Length(password) < 8)) throw new Error('Wi-Fi 密码应为 8–63 字节。');
  const use_saved = editingNetwork === ssid && !password && savedNetworks.some(item=>item.ssid===ssid);
  return connectNetwork(ssid,password,use_saved);
}
async function connectNetwork(ssid,password,use_saved) {
  pendingSsid = ssid;
  $('handoff').hidden = true; $('lanRow').hidden = true; $('lanLink').removeAttribute('href');
  accessChanged();
  const result = await api(config?.provisioning ? 'provisioning' : 'network',
    config?.provisioning ? 'submit' : 'wifi.connect', {ssid,password,use_saved});
  const attempt = result.attempt || result.status?.attempt;
  if (result.result !== 'accepted' || result.submitted_ssid !== ssid || !attempt) throw new Error('设备未接受本次连接，请重试。');
  $('password').value = '';
  await waitForNetwork(ssid,attempt);
}
async function waitForNetwork(ssid,attempt) {
  let disconnected = 0;
  for (let i=0;i<40;i++) {
    message('wifiMessage',`正在连接 ${ssid}…`); await wait(1000);
    let status;
    try { status = await readNetworkStatus(); disconnected = 0; }
    catch { if (++disconnected >= 3) { networkHandoffUnknown(ssid, '设备连接已断开，尚未确认联网成功。'); return; } continue; }
    renderNetwork(status);
    if (status.attempt !== attempt || status.submitted_ssid !== ssid) throw new Error('本次连接状态已变化，请重新读取设备状态。');
    if (status.phase === 'failed') throw new Error(status.error === 'wifi_auth_failed' ? 'Wi-Fi 密码不正确，请检查后重试。' : '连接失败，请检查 Wi-Fi 名称、密码和 2.4 GHz 网络。');
    if (status.phase === 'cancelled') throw new Error('本次连接已取消。');
    if (['connected','connected_unsaved'].includes(status.phase) && status.result_ip && status.sta_ssid === ssid) {
      closeNetworkEditor(); renderNetwork(status);
      const text = status.phase === 'connected_unsaved' ? `已连接 ${ssid}，但设备未能保存网络。` : `已连接并保存 ${ssid}。`;
      message('wifiMessage',text,status.phase === 'connected_unsaved' ? '' : 'success');
      message('networkFeedback',text,status.phase === 'connected_unsaved' ? '' : 'success'); return;
    }
  }
  throw new Error('尚未确认连接成功，请刷新状态后核对。');
}
function networkHandoffUnknown(ssid, text) {
  contactReady = false;
  net = null;
  closeNetworkEditor();
  message('wifiMessage',text);
  message('networkFeedback',text);
  $('handoff').hidden = false; $('lanRow').hidden = true; $('lanLink').removeAttribute('href');
  $('handoffTitle').textContent = '重新连接设备';
  $('handoffText').textContent = `请将手机或电脑连接到「${ssid}」。热点消失不代表联网成功，需要重新读取设备状态确认。`;
  $('handoffAddress').textContent = '若不知道设备的新地址，请在路由器的已连接设备中查找；如果设备热点重新出现，请重新连接热点并检查 Wi-Fi 密码。';
  accessChanged();
}
async function upload(file, bootstrap = false) {
  try {
    const value = await LunarDeviceClient.upload(`/api/v1/modules/ota/${bootstrap ? 'boot.upload' : 'firmware.upload'}`, file, {
      onProgress(percent) {
        $('progress').hidden=false; $('progress').value=Math.round(percent);
        message('otaMessage',percent===100?'传输完成，等待设备校验与安装…':`正在上传 ${Math.round(percent)}%`);
      }
    });
    if(!value.success)throw new Error(value.message || '设备未确认固件安装成功。');
    return value;
  } catch(error) {
    if(error.status===401){debugToken='';invalidateAdminAccess();}
    if(['upload_network_error','upload_timeout','device_response_invalid'].includes(error.code))
      throw new Error('尚未确认更新成功，请重新连接后检查设备版本，勿重复提交。');
    throw error;
  }
}
$('startProvisioning').addEventListener('click',()=>action('networkMaintenanceMessage',async()=>{
  await api('provisioning','start',{mode:'softap',suspend_station:true});
  message('networkMaintenanceMessage','已请求开启热点，请连接设备热点后打开配网设置。');
}));
$('connectFirstSaved').addEventListener('click',()=>action('networkMaintenanceMessage',async()=>{
  const first=net?.saved_wifi?.credentials?.[0];
  if(!first?.ssid)throw new Error('没有已保存的网络。');
  await connectNetwork(first.ssid,'',true);
}));
$('changeNetwork').addEventListener('click', () => {
  if(networkEditing)closeNetworkEditor();else openNetworkEditor();
});
$('closeWifi').addEventListener('click',()=>{if(!busy)closeNetworkEditor();});
$('networkEditor').addEventListener('cancel',event=>{event.preventDefault();if(!busy)closeNetworkEditor();});
$('saveNetwork').addEventListener('click',()=>{if(!$('saveNetwork').disabled)action('wifiMessage',()=>saveNetworkDraft());});
$('forgetNetwork').addEventListener('click',()=>{if($('forgetNetwork').disabled)return;action('wifiMessage',async()=>{
  if(!confirm(`移除 ${editingNetwork}？`))return;
  const remaining=savedNetworks.filter(item=>item.ssid!==editingNetwork);
  const result=await api('network','wifi.forget',{ssid:editingNetwork});
  const stored=result.wifi?.credentials;
  if(!Array.isArray(stored) || stored.length!==remaining.length || stored.some((item,i)=>item.ssid!==remaining[i].ssid))throw new Error('设备未确认移除网络，请刷新核对。');
  savedNetworks=stored;if(net)net.saved_wifi={...net.saved_wifi,...result.wifi};closeNetworkEditor();renderSavedNetworks();
  message('networkFeedback','已忘记此网络。','success');
});});
$('sortNetworks').addEventListener('click',()=>{if($('sortNetworks').disabled)return;networkOrder=[...savedNetworks];renderSavedNetworks();renderNetworkEditor();accessChanged();});
$('cancelNetworkSort').addEventListener('click',()=>{if(busy)return;networkOrder=null;renderSavedNetworks();accessChanged();});
$('saveNetworkSort').addEventListener('click',()=>{if($('saveNetworkSort').disabled)return;action('savedNetworkMessage',async()=>{
  const expected=networkOrder.map(item=>item.ssid);
  const result=await api('network','config.set',{credentials:networkOrder.map(item=>({ssid:item.ssid,password_masked:true})),apply:false});
  const stored=result.wifi?.credentials;
  if(!Array.isArray(stored) || stored.length!==expected.length || stored.some((item,i)=>item.ssid!==expected[i]))throw new Error('设备未确认网络顺序，请刷新核对。');
  savedNetworks=stored;networkOrder=null;if(net)net.saved_wifi={...net.saved_wifi,...result.wifi};renderSavedNetworks();message('savedNetworkMessage','排序已保存。','success');
});});
const adminPendingKey = 'lunar_admin_pending';
const productionPassportAuthorization = 'https://passport.lunaredu.cn/device/authorize';
const maintenanceUuid = /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/;
function adminState() {
  const bytes = new Uint8Array(32); crypto.getRandomValues(bytes);
  return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');
}
async function startPassportAuthorization() {
  if (busy) return;
  return action('managementMessage', async () => {
    if (config?.administrator_authentication_protocol !== 2) throw new Error('设备尚不支持独立维护认证，请先通过本地 OTA 更新固件。');
    const state = adminState();
    const callback_origin = location.origin;
    const launch = await maintenanceRequest('/device-web/admin/request?' + new URLSearchParams({state, callback_origin}));
    if (launch.protocol !== 2 || launch.state !== state || launch.callback_origin !== callback_origin || launch.serial !== config.serial
        || launch.device_uuid !== config.device_uuid || !maintenanceUuid.test(launch.device_uuid)
        || !/^[a-f0-9]{64}$/.test(launch.challenge) || !Number.isSafeInteger(launch.expires)
        || launch.expires <= Date.now()/1000 || launch.expires > Date.now()/1000 + 121) throw new Error('设备认证请求校验失败，请重试。');
    const target = new URL(productionPassportAuthorization);
    for (const key of ['protocol','serial','device_uuid','challenge','state','callback_origin']) target.searchParams.set(key, String(launch[key]));
    sessionStorage.setItem(adminPendingKey, JSON.stringify({ state, origin: callback_origin, serial: launch.serial, device_uuid: launch.device_uuid, expires: launch.expires }));
    message('managementMessage', '正在前往 Passport…');
    location.assign(target.toString());
  });
}
async function maintenanceRequest(url, options) {
  // Only trust loading is retried. An ambiguous accepted exchange must not
  // replay a consumed challenge, and an old authority never falls back to v1.
  for (let attempt=0; ; ++attempt) {
    try { return await json(url, options); }
    catch (error) {
      if (error.code !== 'maintenance_trust_loading' || attempt >= 20) throw error;
      await new Promise(resolve=>setTimeout(resolve,1000));
    }
  }
}
async function consumePassportAuthorization() {
  const fragment = new URLSearchParams(location.hash.slice(1));
  if (!fragment.has('lunar-maintenance') && !fragment.has('lunar-admin')) return;
  // Remove credentials before any API request, polling, navigation or error UI.
  history.replaceState(null, '', location.pathname + location.search);
  let pending;
  try { pending = JSON.parse(sessionStorage.getItem(adminPendingKey) || 'null'); } catch {}
  sessionStorage.removeItem(adminPendingKey);
  const token = fragment.get('lunar-maintenance') || '';
  const state = fragment.get('state') || '', serial=fragment.get('serial') || '', uuid=fragment.get('device_uuid') || '';
  try {
    if (fragment.get('protocol') !== '2' || !/^[a-f0-9]{64}$/.test(state) || !serial || !maintenanceUuid.test(uuid)
        || token.length > 4096 || !/^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$/.test(token)
        || (pending && (pending.state !== state || pending.origin !== location.origin || pending.serial !== serial
          || pending.device_uuid !== uuid || pending.expires <= Date.now()/1000))
        || (!pending && !window.opener)) throw new Error('认证返回已过期或不属于本次请求，请重新认证。');
    const accepted=await maintenanceRequest('/device-web/admin/exchange',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({protocol:2,state,token})});
    if (accepted.protocol !== 2 || accepted.state !== state || accepted.serial !== serial || accepted.device_uuid !== uuid
        || !/^lma2\.[a-f0-9]{64}$/.test(accepted.session) || !Number.isSafeInteger(accepted.expires)
        || accepted.expires <= Date.now()/1000 || accepted.expires > Date.now()/1000+121) throw new Error('设备未接受管理员授权，请重新认证。');
    if (!pending) {
      const origin=new URL(accepted.opener_origin);
      if (!window.opener || origin.origin !== accepted.opener_origin || origin.origin === location.origin
          || !['https:','http:'].includes(origin.protocol)) throw new Error('维护授权来源校验失败，请重新认证。');
      window.opener.postMessage(createWireEvents({binding:{scope:'device.maintenance',state}}).encode({type:'lunar-maintenance-authorized',protocol:2,state,serial,device_uuid:uuid,session:accepted.session,expires:accepted.expires}),origin.origin);
      window.close();return;
    }
    if (accepted.opener_origin !== location.origin) throw new Error('维护授权来源校验失败，请重新认证。');
    const grant=accepted.session;
    const response = await fetch('/device-web/config', {cache:'no-store', credentials:'omit', redirect:'error',
      headers:{'X-Lunar-Debug-Token':grant}, signal:AbortSignal.timeout(15000)});
    const result = await response.json();
    if (!response.ok || result.serial !== serial || result.device_uuid !== uuid || !result.administrator_authorized) throw new Error('设备未接受管理员授权，请重新认证。');
    adminGrant = grant; debugToken = ''; config = result;
    sessionStorage.removeItem('lunar_debug_token');
    sessionStorage.setItem('lunar_admin_grant', grant);
    message('notice', 'Passport 管理员认证成功。', 'success');
  } catch (error) {
    invalidateAdminAccess();
    message('managementMessage', errorText(error), 'error');
    $('managementDialog').showModal();
  }
}

$('closeManagement').addEventListener('click',()=>{$('managementDialog').close();});
$('restoreConfiguration').addEventListener('click',()=>{
  if (busy || !canAdmin() || !config?.configuration_recovery_snapshot_available) return;
  if (!confirm('使用已验证的恢复副本替换损坏的原配置？此操作会保留 Wi-Fi 和平台身份，不会格式化存储。')) return;
  action('configurationRecoveryHelp',async()=>{
    await json('/device-web/configuration/restore',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
    message('configurationRecoveryHelp','配置已修复。');
    await refresh();
  });
});
$('repairRecoveryState').addEventListener('click',()=>{
  if (busy || !canManage() || ota?.firmware_role !== 'recovery') return;
  if (!confirm('重建损坏的恢复安装记录？旧的安装进度将清除，业务镜像和设备凭据保留，之后需重新安装固件。')) return;
  action('recoveryRepairMessage',async()=>{
    await json('/device-web/recovery/repair',{method:'POST',headers:{'Content-Type':'application/json'},body:'{}'});
    message('recoveryRepairMessage','恢复记录已修复，可以重新安装固件。');
    await refresh();
  });
});
$('passportLink').addEventListener('click',startPassportAuthorization);
$('serverPassport').addEventListener('click',()=>{
  message('managementMessage',config?.administrator_authentication_error ? errorText(config.administrator_authentication_error) : '');
  $('managementDialog').showModal();
});
$('clearManagement').addEventListener('click',async()=>{if(busy)return;try{await json('/device-web/admin/revoke',{method:'POST'});}catch{}accessGrant='';adminGrant='';sessionStorage.removeItem('lunar_access_grant');sessionStorage.removeItem('lunar_admin_grant');sessionStorage.removeItem(adminPendingKey);if(config)config.administrator_authorized=false;clearVersionDetails();$('managementDialog').close();refresh();});
$('changeServer').addEventListener('click',()=>{
  if($('changeServer').disabled)return;
  $('developerSettings').showModal();
  refreshTlsPolicy();
  (config?.features.assistant_endpoint?$('assistantEndpointTarget'):$('environmentTarget')).focus();
});
$('closeServer').addEventListener('click',()=>{if(!busy&&!environmentSaving)$('developerSettings').close();});
$('developerSettings').addEventListener('cancel',event=>{if(busy||environmentSaving)event.preventDefault();});
$('bluetoothEnabled').addEventListener('change',()=>{
  if($('bluetoothEnabled').disabled)return;
  const enabled=$('bluetoothEnabled').checked;
  action('bluetoothMessage',async()=>{
    try {
      await api('network','bluetooth.set',{enabled});
      for(let i=0;i<8;i++){
        const status=await readNetworkStatus();renderNetwork(status);
        if(status.bluetooth_enabled===enabled && (enabled || !status.bluetooth_stop_pending)){message('bluetoothMessage',enabled?'蓝牙已开启。':'蓝牙已关闭。','success');return;}
        await wait(250);
      }
      throw new Error('尚未确认蓝牙状态，请稍后刷新。');
    }finally{renderBluetooth(net);}
  });
});
for (const id of ['assistantEndpointTarget','assistantEndpointCustom']) $(id).addEventListener('input', () => {
  assistantEndpointDraft = true;
  renderAssistantEndpoint();
});
function renderServerDraft() { if(config?.features.assistant_endpoint) renderAssistantEndpoint(); else renderEnvironment(); }
$('tlsMode').addEventListener('change', () => { tlsDraft = true; renderServerDraft(); });
$('tlsCaFile').addEventListener('change', async () => {
  const file = $('tlsCaFile').files[0];
  tlsCaPem = ''; tlsDraft = true;
  if (file) {
    const origin = selectedTlsOrigin();
    try {
      if (file.size > 4096) throw new Error('CA 证书不能超过 4 KiB。');
      const pem = await file.text();
      if (origin !== selectedTlsOrigin() || !canManage()) return;
      if (!pem.includes('-----BEGIN CERTIFICATE-----') || pem.includes('PRIVATE KEY')) throw new Error('请选择 PEM 格式的 CA 证书，不能包含私钥。');
      tlsCaPem = pem; message('tlsMessage','');
    } catch(error) { message('tlsMessage',errorText(error),'error'); }
  }
  renderServerDraft();
});
$('tlsForm').addEventListener('submit', event => {
  event.preventDefault();
  if ($('saveTls').disabled || !canManage()) return;
  const origin = selectedTlsOrigin(), mode = $('tlsMode').value;
  const request = {origin,mode};
  if (mode === 'custom_ca') {
    if (tlsCaPem) request.ca_pem = tlsCaPem;
    else request.keep_ca = true;
  }
  return action('tlsMessage', async () => {
    tlsPolicy = await api('network','tls.set',request);
    tlsDraft = false; renderTlsSettings();
    message('tlsMessage','证书设置已保存。请重启设备，使已有连接使用新设置。','success');
  });
});
$('assistantEndpointForm').addEventListener('submit', event => {
  event.preventDefault();
  if ($('saveAssistantEndpoint').disabled) return;
  const custom = $('assistantEndpointTarget').value === 'custom';
  const baseUrl = custom ? $('assistantEndpointCustom').value.trim() : assistantEndpointDefaults[$('assistantEndpointTarget').value];
  return action('assistantEndpointMessage', async () => {
    const result = await api('assistant_endpoint','base_url.set',{base_url:baseUrl});
    assistantEndpoint = result;
    assistantEndpointDraft = false;
    renderAssistantEndpoint();
    message('assistantEndpointMessage',result.restart_pending ? '地址已保存，设备即将重启。请重新打开设备网页确认连接状态。' : '地址未改变。','success');
  });
});
for (const id of ['environmentTarget','environmentUrl']) $(id).addEventListener('input', () => { environmentDraft = true; $('environmentConfirmation').hidden = true; renderEnvironment(); });
$('environmentForm').addEventListener('submit', event => {
  event.preventDefault();
  if ($('saveEnvironment').disabled) return;
  const development = $('environmentTarget').value === 'development';
  const url = $('environmentUrl').value.trim();
  $('environmentConfirmTarget').textContent = development ? url : 'https://board.lunaredu.cn';
  $('environmentConfirmation').hidden = false;
  $('cancelEnvironment').focus();
});
$('cancelEnvironment').addEventListener('click', () => { $('environmentConfirmation').hidden = true; $('saveEnvironment').focus(); });
$('confirmEnvironment').addEventListener('click', () => {
  if ($('confirmEnvironment').disabled || $('environmentConfirmation').hidden) return;
  return action('environmentMessage', async () => {
    environmentSaving = true;
    try {
      const target = $('environmentTarget').value;
      await api('platform_link','environment.set',{environment:target,development_base_url:target === 'development' ? $('environmentUrl').value.trim() : '',confirm:true});
      for (let i=0;i<15;i++) {
        renderBinding(await api('platform_link','status.get'));
        if (binding.environment_restart_pending) {
          environmentDraft = false;
          message('environmentMessage','已保存。重启后生效。','success');
          return;
        }
        if (binding.error) throw new Error(binding.error);
        if (!binding.environment_switch_pending) throw new Error('配置未改变，请检查目标环境。');
        await wait(500);
      }
      throw new Error('尚未确认保存结果，请刷新后查看，勿重复提交。');
    } finally { environmentSaving = false; $('environmentConfirmation').hidden = true; }
  });
});
$('restartEnvironment').addEventListener('click', () => {
  if ($('restartEnvironment').disabled) return;
  return action('environmentMessage', async () => {
    await api('platform_link','environment.restart',{});
    contactReady = false;
    message('environmentMessage','正在重启，请稍候…');
  });
});
document.querySelectorAll('a[data-page]').forEach(link => link.addEventListener('click', event => {
  if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || (event.button !== undefined && event.button !== 0)) return;
  event.preventDefault();
  navigate(link.dataset.page);
}));
if (typeof addEventListener === 'function') {
  addEventListener('popstate', () => refresh());
  addEventListener('beforeunload', event => {
    if (nameDirty || nameSaving) { event.preventDefault(); event.returnValue = ''; }
  });
}
$('name').addEventListener('input',() => {
  const value = $('name').value.trim();
  nameDirty = value !== savedName;
  $('name').setAttribute('aria-invalid',String(nameDirty && !validDeviceName(value)));
  message('nameMessage', nameDirty ? validDeviceName(value) ? '名称尚未保存。' : errorText('invalid_device_name') : '', nameDirty && !validDeviceName(value) ? 'error' : '');
  accessChanged();
});
$('cancelName').addEventListener('click', () => {
  if (busy) return;
  nameDirty = false; $('name').value = savedName;
  $('name').setAttribute('aria-invalid','false');
  message('nameMessage','已取消修改。'); accessChanged(); $('name').focus();
});
$('nameForm').addEventListener('submit',e => {
  e.preventDefault(); if ($('saveName').disabled) return;
  const draft = $('name').value, name = draft.trim();
  nameSaving = true;
  return action('nameMessage',async () => {
    try {
      if (!validDeviceName(name)) throw new Error('invalid_device_name');
      const result = await api('identity','name.set',{device_name:name});
      if (result.device_name !== name) throw new Error('设备未确认名称，请刷新后重试。');
      // Preserve even a programmatically changed draft while acknowledging only
      // the submitted value. Polls from before this write are revision-guarded.
      nameDirty = $('name').value !== draft;
      renderIdentity(result);
      message('nameMessage',nameDirty ? '上一名称已保存，当前修改尚未保存。' : '设备名称已保存。',nameDirty ? '' : 'success');
    } finally { nameSaving = false; }
  });
});
$('scan').addEventListener('click',()=>scanNetworks());
$('wifiForm').addEventListener('submit',e => { e.preventDefault(); if($('connect').disabled)return;action('wifiMessage',async()=>{networkSubmitting=true;accessChanged();await submitNetwork();}); });
$('showPassword').addEventListener('click',() => { const show = $('password').type === 'password'; $('password').type = show ? 'text' : 'password'; $('showPassword').textContent = show ? '隐藏' : '显示'; $('showPassword').setAttribute('aria-pressed',String(show)); });
$('localFactoryEnter').addEventListener('click',()=>action('localFactoryMessage',async()=>{
  await api('factory_mode','enter',{});
  factoryModeCatalog = null;
  await refreshFactoryChecks();
}));
$('localFactoryAppDetails').addEventListener('toggle',()=>renderLocalFactory(true));
$('factoryAdditionalChecks').addEventListener('toggle',()=>{
  if ($('factoryAdditionalChecks').open) void refreshFactoryChecks(true);
});
$('localFactoryExit').addEventListener('click',()=>action('localFactoryMessage',async()=>{
  await api('factory_mode','exit',{});
  factoryModeCatalog = null;
  await refreshFactoryChecks();
}));
for (const [test,prefix] of [['speaker','audioSpeaker'],['microphone','audioMicrophone']]) {
  $(prefix+'Start').addEventListener('click',() => action('audioFactoryMessage',async()=>{
    if (config?.features.factory_mode) {
      if (!factoryModeStatus?.active) await api('factory_mode','enter',{});
      await api('factory_mode','start',{id:'s31.audio.'+test});
    } else await api('factory_audio',test+'.start',{});
    factoryModeCatalog = null; await refreshFactoryChecks();
  }));
  for (const [suffix,passed] of [['Pass',true],['Fail',false]]) {
    $(prefix+suffix).addEventListener('click',() => action('audioFactoryMessage',async()=>{
      if (config?.features.factory_mode)
        await api('factory_mode','confirm',{id:'s31.audio.'+test,passed});
      else await api('factory_audio','confirm',{test,passed});
      factoryModeCatalog = null; await refreshFactoryChecks();
    }));
  }
}
$('voiceManual').addEventListener('click',()=>action('audioFactoryMessage',async()=>{
  await api('assistant','standby_mode.set',{mode:'wake_word'}); await refreshFactoryChecks();
}));
$('voicePrepare').addEventListener('click',()=>action('audioFactoryMessage',async()=>{
  const latest=await api('assistant','status.get');
  if (!latest.enabled || latest.suspended || !latest.transport_connected ||
      !latest.activation_ready || latest.listening || latest.speaking)
    throw new Error('助手忙碌或尚未就绪，请稍后重试。');
  factoryAssistant=latest; factoryTextRun=null;
  factoryRun={baseline:factoryBaseline(latest),phrase:`今天的测试号码是 ${Math.floor(1000+Math.random()*9000)}`,
    startedAt:0,incoming:0,output:0,heard:null};
  renderFactoryVoice();
}));
$('voiceStart').addEventListener('click',()=>action('audioFactoryMessage',async()=>{
  await api('assistant','listening.toggle',{}); await refreshFactoryChecks();
}));
$('voiceStop').addEventListener('click',()=>action('audioFactoryMessage',async()=>{
  await api('assistant','listening.toggle',{}); await refreshFactoryChecks();
}));
$('voiceHeard').addEventListener('click',()=>{if(factoryRun){factoryRun.heard=true;renderFactoryVoice();}});
$('voiceNotHeard').addEventListener('click',()=>{if(factoryRun){factoryRun.heard=false;renderFactoryVoice();}});
$('textProbeStart').addEventListener('click',()=>action('audioFactoryMessage',async()=>{
  const latest=await api('assistant','status.get');
  if (!latest.enabled || latest.suspended || !latest.transport_connected ||
      !latest.activation_ready || latest.listening || latest.speaking)
    throw new Error('助手忙碌或尚未就绪，请稍后重试。');
  factoryAssistant=latest; factoryRun=null;
  factoryTextRun={baseline:factoryBaseline(latest),startedAt:Date.now(),incoming:0,output:0,heard:null};
  await api('assistant','conversation.test',{text:'请用一句简短的中文回答：测试成功'});
  await refreshFactoryChecks();
}));
$('textProbeHeard').addEventListener('click',()=>{if(factoryTextRun){factoryTextRun.heard=true;renderFactoryVoice();}});
$('textProbeNotHeard').addEventListener('click',()=>{if(factoryTextRun){factoryTextRun.heard=false;renderFactoryVoice();}});
$('factoryCapabilitiesRefresh').addEventListener('click',()=>refreshFactoryChecks(true));
$('factoryFist').addEventListener('click',() => action('factoryTestMessage',() => runFactoryTest(() => api('bionic_hand','gesture.set',{gesture:'fist'}),'握住动作已下发。')));
$('factoryOpen').addEventListener('click',() => action('factoryTestMessage',() => runFactoryTest(() => api('bionic_hand','gesture.set',{gesture:'open'}),'张开动作已下发。')));
$('factoryStop').addEventListener('click',() => action('factoryTestMessage',() => runFactoryTest(() => api('bionic_hand','motion.stop',{}),'动作已停止。')));
$('factoryOutputOff').addEventListener('click',() => action('factoryTestMessage',() => runFactoryTest(() => api('bionic_hand','output.off',{}),'输出已关闭。')));
for (const [id, finger, angle, label] of [
  ['factoryThumbClose','thumb',30,'拇指收拢'], ['factoryThumbOpen','thumb',130,'拇指张开'],
  ['factoryIndexClose','index',0,'食指收拢'], ['factoryIndexOpen','index',180,'食指张开'],
  ['factoryMiddleClose','middle',0,'中指收拢'], ['factoryMiddleOpen','middle',180,'中指张开'],
  ['factoryRingClose','ring',0,'无名指收拢'], ['factoryRingOpen','ring',180,'无名指张开'],
  ['factoryPinkyClose','pinky',0,'小指收拢'], ['factoryPinkyOpen','pinky',180,'小指张开']]) {
  $(id).addEventListener('click',() => action('factoryTestMessage',() => runFactoryTest(
    () => api('bionic_hand','finger.set',{finger,angle}), `${label}动作已下发。`)));
}
$('pair').addEventListener('click',() => action('bindingMessage',async () => {
  if (bindingExpired()) {
    await api('platform_link','pairing.cancel',{});
    for (let i=0;i<10;i++) {
      renderBinding(await api('platform_link','status.get'));
      if (binding.state !== 'pairing') break;
      await wait(1000);
    }
    if (binding.state === 'pairing') throw new Error('上一次绑定尚未结束，请稍后重试。');
    if (binding.state === 'bound') return;
  }
  renderBinding(await api('platform_link','pairing.start',{}));
  if (!binding.code) message('bindingMessage','已请求绑定码，正在等待官网响应。');
}));
$('copyCode').addEventListener('click',async () => {
  if (!contactReady || $('codeBlock').hidden || bindingExpired()) { clearBindingCode(); return; }
  const code = $('code').textContent;
  try {
    if (!globalThis.navigator?.clipboard?.writeText) throw new Error('clipboard_unavailable');
    await navigator.clipboard.writeText(code);
    if ($('code').textContent === code) message('copyMessage','绑定码已复制。','success');
  } catch { message('copyMessage','当前浏览器不支持自动复制，请长按或选中上方绑定码复制。'); }
});
$('cancelPair').addEventListener('click',() => action('bindingMessage',async () => {
  await api('platform_link','pairing.cancel',{});
  pairingCancelPending = true; clearBindingCode();
  $('bindingState').textContent = '正在取消绑定…';
  message('bindingMessage','取消请求已提交，正在等待确认。');
}));
$('showUnbind').addEventListener('click', () => {
  if ($('showUnbind').disabled) return;
  $('unbindConfirmation').hidden = false; $('cancelUnbind').focus();
});
$('cancelUnbind').addEventListener('click', () => { $('unbindConfirmation').hidden = true; $('showUnbind').focus(); });
$('confirmUnbind').addEventListener('click', () => {
  if ($('confirmUnbind').disabled || $('unbindConfirmation').hidden) return;
  return action('unbindMessage', async () => {
    renderBinding(await api('platform_link','binding.unbind',{confirm:true}));
    unbindSubmitted = binding.unbind_state === 'pending';
    $('unbindConfirmation').hidden = true;
    if (unbindSubmitted) message('unbindMessage','解除请求已提交，正在等待官网确认…');
  });
});
$('localUpdate').addEventListener('click',() => {
  if ($('localUpdate').disabled || !canManage()) return;
  bootUpdateSelected = false;
  if (ota?.deployment?.recovery_required) {
    if (!confirm('进入恢复程序安装固件？当前业务将停止，重启后需重新连接设备网页并选择固件。')) return;
    return action('otaMessage',async () => {
      const state = await api('ota','recovery.enter',{}); renderOta(state);
      message('otaMessage','已请求进入恢复程序。重启后请重新连接设备网页并选择固件。','success');
    });
  }
  $('localUpdateTitle').textContent = '本地更新';
  message('localUpdateMessage',''); accessChanged(); $('localUpdateDialog').showModal();
});
$('bootUpdate').addEventListener('click',() => {
  if ($('bootUpdate').disabled || !canManage()) return;
  bootUpdateSelected = true; $('localUpdateTitle').textContent = '更新恢复固件';
  message('localUpdateMessage','请选择适用于这台设备的 Boot .bin 文件。');
  accessChanged(); $('localUpdateDialog').showModal();
});
$('closeLocalUpdate').addEventListener('click',() => $('localUpdateDialog').close());
$('otaForm').addEventListener('submit',e => { e.preventDefault(); if(!canManage() || $('upload').disabled)return; action('localUpdateMessage',async () => {
  if (ota?.deployment?.ready === false || (!bootUpdateSelected && ota?.deployment?.recovery_required)) throw new Error('请先进入可用的恢复程序，再安装固件。');
  const file = $('firmwareFile').files[0]; if (!file || !/\.bin$/i.test(file.name) || file.size === 0) throw new Error('请选择有效的 .bin 固件。');
  const capacity = bootUpdateSelected && ota?.deployment_mode === 'recovery' ? ota?.boot_update?.factory_bytes : ota?.next_size;
  if (capacity && file.size > capacity) throw new Error('固件超过可用应用分区容量。');
  if (!confirm(`将安装 ${file.name}。请确认适用于当前产品，并保持设备供电。`)) { message('localUpdateMessage','已取消。'); return; }
  const isBoot = bootUpdateSelected;
  const result = await upload(file,isBoot); renderOta(result); $('localUpdateDialog').close(); $('firmwareFile').value='';
  message('otaMessage',isBoot ? (result.restart_pending ? '恢复固件已接收，设备将重启完成更新；重连后请核对版本。' : '恢复固件已更新，当前业务继续运行。') : '设备已确认固件校验与安装，正在重启。重连后请核对版本。','success');
}); });
$('checkUpdate').addEventListener('click',() => action('updateCheckMessage',async () => {
  if (!canManage() || config?.provisioning || !config?.features.cloud_update) return;
  await api('platform_link','firmware.check',{});
  if (binding) binding.firmware_check_state='pending';
  message('updateCheckMessage','正在检查…');
}));
$('updateOnline').addEventListener('click',() => action('otaMessage',async () => { if (!confirm('安装平台为此设备提供的官方更新？')) { message('otaMessage','已取消。'); return; } await api('platform_link','firmware.update',{}); message('otaMessage','更新检查已提交，请等待设备返回结果。'); }));
$('restart').addEventListener('click',() => {
  if ($('restart').disabled || !confirm('现在重启这台设备？')) return;
  return action('notice',async () => { await api('ota','restart',{}); message('notice','重启请求已接受，请稍后重新连接设备。'); });
});
$('refreshLogs').addEventListener('click',() => action('debugMessage',async () => { await readDebugLogs(0); }));
$('debugAuth').addEventListener('submit',e=>{e.preventDefault();action('debugAuthMessage',async()=>{const code=$('debugCode').value.trim();if(!/^\d{6}$/.test(code))throw new Error('请输入串口显示的六位调试码。');const result=await api('system','diagnostics.session.open',{code});debugToken=result.token;sessionStorage.setItem('lunar_debug_token',debugToken);$('debugCode').value='';await refreshDebug();try{await captureScreen();}catch(error){message('screenMessage',errorText(error),'error');}});});
$('closeDebug').addEventListener('click',()=>action('debugAuthMessage',async()=>{await api('system','diagnostics.session.close',{});debugToken='';sessionStorage.removeItem('lunar_debug_token');diagnostics=null;await refreshDebug();message('debugAuthMessage','调试会话已结束。');}));
$('captureScreen').addEventListener('click',()=>action('screenMessage',captureScreen));
$('exportFeedback').addEventListener('click',() => action('feedbackMessage',async () => {
  if (!diagnostics) throw new Error('请先刷新设备诊断。');
  const bundle = {schema:'lunaredu.feedback.v1',created_at:new Date().toISOString(),description:$('feedbackDescription').value.trim(),diagnostics,logs:debugLogs};
  const blob = new Blob([JSON.stringify(bundle,null,2)],{type:'application/json'}), url = URL.createObjectURL(blob), link = document.createElement('a');
  link.href=url;link.download=`lunaros-feedback-${diagnostics.boot_id || 'device'}.json`;link.click();URL.revokeObjectURL(url);message('feedbackMessage','诊断 JSON 已生成，仅保存到当前浏览器。','success');
}));
async function poll() { await refresh(); setTimeout(poll,5000); }
function openMaintenanceAuthorization() {
  if (location.hash !== '#maintenance-auth') return;
  history.replaceState(null, '', location.pathname + location.search);
  message('managementMessage', '');
  $('managementDialog').showModal();
}
window.addEventListener('hashchange', openMaintenanceAuthorization);
openMaintenanceAuthorization();
accessChanged();
consumePassportAuthorization().then(poll);
setInterval(()=>{if(currentPage==='factory' && !busy && !refreshing) void refreshFactoryChecks();},1600);

$('desktopSettingsForm').addEventListener('change',()=>{desktopSettingsDirty=true;});
$('desktopSettingsForm').addEventListener('submit',event=>{event.preventDefault();action('desktopSettingsMessage',async()=>{
 await api('desktop_settings','preferences.set',{startup_application:$('startupApplication').value,animations:$('desktopAnimations').checked,show_labels:$('desktopLabels').checked,text_size:Number($('desktopTextSize').value)});
 desktopSettingsDirty=false;message('desktopSettingsMessage','已保存；开机应用在下次启动时生效。','success');
});});
