"use strict";
const $ = id => document.getElementById(id);
const routes = {home:'/', network:'/setup', binding:'/binding', maintenance:'/maintenance'};
const recovery = location.pathname === '/recovery';
let currentPage = 'home', config = null, net = null, ota = null, binding = null;
let refreshing = false, busy = false, nameDirty = false, contactReady = false;
const utf8Length = value => new TextEncoder().encode(value).length;
function message(id, text, kind = '') { $(id).textContent = text; $(id).className = `message ${kind}`; }
function errorText(error) {
  const text = String(error?.message || error || '');
  const known = {invalid_device_name:'设备名称不能为空，且不能超过 64 字节。', identity_save_failed:'设备未能保存名称，请重试。', pairing_unavailable:'暂时无法获取绑定码，请检查网络及当前绑定状态。', device_link_not_ready:'绑定服务尚未就绪，请稍后重试。', ota_upload_in_progress:'正在更新固件，请等待完成。', invalid_ota_project:'固件与当前产品不匹配。', forbidden:'当前连接不允许此操作。'};
  if (known[text]) return known[text];
  if (/Abort|Timeout|Failed to fetch|NetworkError|Load failed/i.test(text)) return '设备暂时无法访问，请检查连接后重试。';
  return text || '操作失败，请重试。';
}
async function json(url, options = {}) {
  const response = await fetch(url, {...options, cache:'no-store', signal:AbortSignal.timeout(15000)});
  let result;
  try { result = await response.json(); } catch { throw new Error('设备返回了无法识别的响应，请重试。'); }
  if (!response.ok || result.ok === false) throw new Error(result.error?.message || result.error?.code || (response.status === 403 ? 'forbidden' : response.statusText));
  return result;
}
async function api(module, operation, payload) {
  const options = payload === undefined ? {} : {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(payload)};
  const envelope = await json(`/api/v1/modules/${module}/${operation}`, options);
  if (!envelope.data || typeof envelope.data !== 'object') throw new Error('设备响应缺少结果，请重试。');
  return envelope.data;
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
  const restricted = Boolean(config?.provisioning) || !contactReady;
  $('apRestriction').hidden = !config?.provisioning;
  $('pair').disabled = busy || restricted || !config?.binding_ready || !net?.connected || ['bound','pairing','connecting'].includes(binding?.state);
  $('cancelPair').disabled = busy || restricted || binding?.state !== 'pairing';
  for (const id of ['upload','firmwareFile','updateOnline','restart']) $(id).disabled = busy || restricted || !config?.ota_ready || Boolean(ota?.upload_active || ota?.pull_active || ota?.pull_queued || ota?.restart_pending);
  $('saveName').disabled = busy || !contactReady || !config?.features.identity;
  $('scan').disabled = busy || !contactReady || !config?.features.network;
  $('connect').disabled = busy || !contactReady || !config?.features.network;
}
function renderNetwork(status) {
  net = status;
  const connected = Boolean(status.connected);
  $('connection').textContent = connected ? `已连接 ${status.sta_ssid || 'Wi-Fi'}` : '设备尚未联网';
  $('connection').className = connected ? 'status online' : 'status';
  for (const id of ['ssid','networkSsid']) $(id).textContent = status.sta_ssid || '未连接';
  $('address').textContent = status.ip || status.ap_ip || '—';
  $('networkIp').textContent = connected ? status.ip || '—' : '尚未获取';
  $('ap').textContent = status.ap_ssid ? `${status.ap_ssid} · ${status.ap_ip || '192.168.4.1'}` : '未开启';
  $('rssi').textContent = connected && typeof status.wifi_rssi === 'number' ? `${status.wifi_rssi} dBm` : '—';
  $('networkState').textContent = connected ? `当前已连接 ${status.sta_ssid || 'Wi-Fi'}。可以在这里更换网络。` : '请将设备连接到可用的 Wi-Fi。';
  const address = connected ? safeLocalAddress(status.ip) : '';
  $('lanRow').hidden = !address;
  if (address) $('lanLink').href = address;
  accessChanged();
}
function renderIdentity(info) {
  $('deviceName').textContent = info.device_name || config?.product_name || 'LunarOS 设备';
  if (!nameDirty) $('name').value = info.device_name || '';
  $('firmware').textContent = info.fw_version || '—';
}
function renderSystem(info) {
  $('chip').textContent = `${info.chip || '—'}${info.cores ? ` · ${info.cores} 核` : ''}`;
  $('flash').textContent = bytes(info.flash_bytes);
  $('internal').textContent = bytes(info.internal_free_bytes);
  $('psram').textContent = info.psram_total_bytes ? `${bytes(info.psram_free_bytes)} / ${bytes(info.psram_total_bytes)}` : '未配置';
  $('mac').textContent = info.mac || '—'; $('idf').textContent = info.idf_version || '—';
  $('partition').textContent = info.running_partition || '—';
  $('uptime').textContent = typeof info.uptime_ms === 'number' ? `${Math.floor(info.uptime_ms/3600000)} 小时 ${Math.floor(info.uptime_ms/60000)%60} 分钟` : '—';
  const nodes = (info.partitions || []).map(p => { const li = document.createElement('li'); const label = document.createElement('span'); const size = document.createElement('span'); label.textContent = p.label; size.textContent = bytes(p.bytes); li.append(label,size); return li; });
  $('partitions').replaceChildren(...nodes);
}
function renderBinding(state) {
  binding = state;
  const names = {bound:'设备已绑定账号', unbound:'设备尚未绑定账号', pairing:'绑定码已准备好', connecting:'正在连接平台，请稍候', error:'绑定服务暂时不可用'};
  $('bindingState').textContent = names[state.state] || '正在读取绑定状态';
  const active = state.state === 'pairing' && state.code && Number(state.expires_at_ms) > Date.now();
  $('codeBlock').hidden = !active; $('code').textContent = active ? state.code : '';
  $('expiry').textContent = active ? `有效至 ${new Date(state.expires_at_ms).toLocaleTimeString()}` : '';
  if (state.state === 'pairing' && !active) $('bindingState').textContent = '绑定码已过期，请等待状态刷新后重新获取。';
  const url = active ? safeBindingUrl(state.bind_url) : '';
  $('bindLink').hidden = !url; if (url) $('bindLink').href = url;
  $('bindingAccount').textContent = state.state === 'bound' ? `绑定账号：${state.account_name || '已绑定'}` : '在你的账号中输入设备绑定码，或打开绑定页面完成绑定。';
  $('pair').hidden = state.state === 'bound'; $('cancelPair').hidden = state.state !== 'pairing';
  if (state.error) message('bindingMessage', errorText(state.error), 'error');
  else message('bindingMessage', state.online ? '' : '设备离线，联网后可以获取绑定码。');
  accessChanged();
}
function renderOta(state) {
  ota = state;
  $('updateVersion').textContent = `当前版本 ${state.version || '—'}`;
  $('updateState').textContent = state.restart_pending ? '固件已准备好，设备正在重启。' : state.upload_active || state.pull_active || state.pull_queued ? '正在处理固件更新，请保持供电。' : state.pending_verify ? '新固件正在进行启动健康检查。' : '设备可用时，可选择官方更新或上传本地固件。';
  $('updatePolicy').textContent = state.rollback_enabled ? '已启用启动失败回滚。' : '当前固件未启用启动失败回滚。';
  const update = state.available_update;
  $('availableUpdate').textContent = update?.available ? `可用版本 ${update.version} · ${update.summary || ''}` : '联网后可以检查平台为本设备提供的固件。';
  if (!busy && state.has_result) message('otaMessage', state.success ? '固件已校验并安装。' : `更新未完成：${errorText(state.message)}`, state.success ? 'success' : 'error');
  accessChanged();
}
function renderNetworks(aps) {
  const byName = new Map();
  for (const ap of Array.isArray(aps) ? aps : []) {
    if (typeof ap.ssid !== 'string' || !ap.ssid.length) continue;
    const old = byName.get(ap.ssid);
    if (!old || Number(ap.rssi) > Number(old.rssi)) byName.set(ap.ssid, ap);
  }
  const nodes = [...byName.values()].sort((a,b) => Number(b.rssi)-Number(a.rssi)).slice(0,16).map(ap => {
    const row = document.createElement('button'); row.type = 'button'; row.className = 'wifi'; row.setAttribute('aria-pressed','false');
    const name = document.createElement('span'); name.className = 'wifi-name'; name.textContent = ap.ssid;
    const detail = document.createElement('small'); detail.textContent = `${ap.secure === false ? '开放 · ' : ''}${ap.rssi >= -55 ? '强' : ap.rssi >= -70 ? '中' : '弱'}`;
    row.append(name,detail); row.addEventListener('click', () => { document.querySelectorAll('.wifi').forEach(n => n.setAttribute('aria-pressed','false')); row.setAttribute('aria-pressed','true'); $('wifiName').value = ap.ssid; if (ap.secure === false) $('password').value = ''; $('password').focus(); }); return row;
  });
  if (!nodes.length) { const empty = document.createElement('p'); empty.className = 'help'; empty.textContent = '未找到网络，请重新扫描，或手动输入隐藏网络的名称。'; nodes.push(empty); }
  $('networks').replaceChildren(...nodes);
}
async function refresh() {
  if (refreshing || busy || document.hidden) return;
  refreshing = true;
  try {
    config = await json('/device-web/config');
    const f = config.features || {}; config.features = f;
    $('product').textContent = config.product_name || '本地设备';
    currentPage = recovery ? new URLSearchParams(location.search).get('view') || 'home' : Object.keys(routes).find(key => routes[key] === location.pathname) || 'home';
    if (location.pathname === '/' && config.provisioning) currentPage = 'network';
    if (!(currentPage in routes)) currentPage = 'home';
    document.querySelectorAll('nav a').forEach(a => { const page = a.dataset.page; a.hidden = page === 'binding' ? !f.binding : page === 'maintenance' ? !f.ota : page === 'network' ? !f.network : false; a.href = recovery ? `/recovery?view=${page}` : routes[page]; if (page === currentPage) a.setAttribute('aria-current','page'); else a.removeAttribute('aria-current'); });
    Object.keys(routes).forEach(p => $(p).hidden = p !== currentPage);
    $('nameForm').hidden = !f.identity;
    $('cloudUpdate').hidden = !f.cloud_update;
    message('notice', config.provisioning ? '当前通过配网入口访问。联网后可继续绑定和更新。' : '通过局域网直接管理这台设备。');
    if ((currentPage === 'binding' && !f.binding) || (currentPage === 'maintenance' && !f.ota) || (currentPage === 'network' && !f.network)) {
      $(currentPage).hidden = true; message('notice','当前产品未提供此功能。'); return;
    }
    if (f.identity) renderIdentity(await api('identity','status.get'));
    if (f.network) renderNetwork(await api('network','provision.status'));
    if (currentPage === 'home' && f.system) renderSystem(await api('system','info.get'));
    if (currentPage === 'binding' && f.binding) {
      if (config.provisioning) { $('bindingState').textContent = '请先完成配网，再通过局域网访问绑定页面。'; $('codeBlock').hidden = true; }
      else renderBinding(await api('device_link','status.get'));
    }
    if (currentPage === 'maintenance' && f.ota && !config.provisioning) renderOta(await api('ota','status.get'));
    contactReady = true; accessChanged();
  } catch(error) {
    contactReady = false; accessChanged();
    $('connection').textContent = '设备连接中断'; $('connection').className = 'status';
    message('notice', `${errorText(error)} 点击“刷新状态”重试。`, 'error');
  } finally { refreshing = false; }
}
async function action(id, run) {
  if (busy) return;
  busy = true; accessChanged(); message(id,'正在处理…');
  try { await run(); } catch(error) { message(id,errorText(error),'error'); }
  finally { busy = false; accessChanged(); }
}
const wait = ms => new Promise(resolve => setTimeout(resolve,ms));
async function submitNetwork() {
  const ssid = validateSsid($('wifiName').value), password = $('password').value;
  if (utf8Length(password) > 64) throw new Error('Wi-Fi 密码过长。');
  const result = await api('network','provision.submit',{ssid,password});
  const saved = result.status?.saved_wifi?.credentials;
  if (result.result !== 'ok' || result.submitted_ssid !== ssid || !Array.isArray(saved) || !saved.some(c => c.ssid === ssid)) throw new Error('设备未确认保存此 Wi-Fi，请重试。');
  $('password').value = '';
  if (result.restart_scheduled) { message('wifiMessage','配置已保存，设备即将重启。尚未确认联网成功；请将手机连接到目标 Wi-Fi 后访问设备。'); return; }
  let disconnected = 0;
  for (let i=0;i<30;i++) {
    message('wifiMessage',`配置已保存，正在连接 ${ssid}…`); await wait(1000);
    let status;
    try { status = await api('network','provision.status'); disconnected = 0; }
    catch { if (++disconnected >= 3) { message('wifiMessage','设备连接已断开，尚未确认联网成功。请切换到目标 Wi-Fi 后访问；连接失败时设备会重新开启热点。'); return; } continue; }
    renderNetwork(status);
    if (status.connected && status.sta_ssid === ssid) { message('wifiMessage',`已确认连接 ${ssid}。`,'success'); return; }
  }
  const detail = net?.wifi_last_disconnect_reason ? `（设备断开原因码 ${net.wifi_last_disconnect_reason}）` : '';
  throw new Error(`尚未确认连接成功${detail}，请检查密码、信号和 2.4 GHz 网络后重试。`);
}
function upload(file) {
  return new Promise((resolve,reject) => {
    const xhr = new XMLHttpRequest(); xhr.open('PUT','/api/v1/modules/ota/firmware.upload'); xhr.setRequestHeader('Content-Type','application/octet-stream'); xhr.timeout = 300000;
    xhr.upload.onprogress = e => { if (e.lengthComputable) { $('progress').hidden = false; $('progress').value = Math.round(e.loaded/e.total*100); message('otaMessage',e.loaded === e.total ? '传输完成，等待设备校验与安装…' : `正在上传 ${Math.round(e.loaded/e.total*100)}%`); } };
    xhr.onerror = xhr.ontimeout = () => reject(new Error('传输连接中断，尚未确认更新成功。请重新连接后查看设备版本，勿重复提交。'));
    xhr.onload = () => { let value; try { value = JSON.parse(xhr.responseText); } catch { reject(new Error('尚未收到设备安装结果，请重新连接后检查版本。')); return; }
      if (xhr.status < 200 || xhr.status >= 300 || value.ok === false || !value.data?.success) { reject(new Error(value.error?.message || value.error?.code || value.data?.message || '设备未确认固件安装成功。')); return; }
      resolve(value.data);
    }; xhr.send(file);
  });
}
$('refresh').addEventListener('click',refresh);
$('name').addEventListener('input',() => { nameDirty = true; });
$('nameForm').addEventListener('submit',e => { e.preventDefault(); action('nameMessage',async () => {
  const name = $('name').value.trim(); if (!name || utf8Length(name) > 64 || /[\x00-\x1f\x7f]/.test(name)) throw new Error('invalid_device_name');
  const result = await api('identity','name.set',{device_name:name}); if (result.device_name !== name) throw new Error('设备未确认名称，请刷新后重试。'); nameDirty = false; renderIdentity(result); message('nameMessage','设备名称已保存。','success');
}); });
$('scan').addEventListener('click',() => action('wifiMessage',async () => { const result = await api('network','provision.scan',{}); renderNetworks(result.aps); if (result.status) renderNetwork(result.status); message('wifiMessage','扫描完成。'); }));
$('wifiForm').addEventListener('submit',e => { e.preventDefault(); action('wifiMessage',submitNetwork); });
$('showPassword').addEventListener('click',() => { const show = $('password').type === 'password'; $('password').type = show ? 'text' : 'password'; $('showPassword').textContent = show ? '隐藏' : '显示'; $('showPassword').setAttribute('aria-pressed',String(show)); });
$('pair').addEventListener('click',() => action('bindingMessage',async () => { renderBinding(await api('device_link','pairing.start',{})); message('bindingMessage','已请求绑定码，正在等待平台响应。'); }));
$('cancelPair').addEventListener('click',() => action('bindingMessage',async () => { await api('device_link','pairing.cancel',{}); message('bindingMessage','取消请求已提交，正在等待确认。'); }));
$('otaForm').addEventListener('submit',e => { e.preventDefault(); action('otaMessage',async () => {
  const file = $('firmwareFile').files[0]; if (!file || !/\.bin$/i.test(file.name) || file.size === 0) throw new Error('请选择有效的 .bin 固件。');
  if (ota?.next_size && file.size > ota.next_size) throw new Error('固件超过可用应用分区容量。');
  if (!confirm(`将安装 ${file.name}。请确认适用于当前产品，并保持设备供电。`)) { message('otaMessage','已取消。'); return; }
  const result = await upload(file); renderOta(result); message('otaMessage','设备已确认固件校验与安装，正在重启。重连后请核对版本。','success');
}); });
$('updateOnline').addEventListener('click',() => action('otaMessage',async () => { if (!confirm('检查并安装平台为此设备提供的官方更新？')) { message('otaMessage','已取消。'); return; } await api('device_link','firmware.update',{}); message('otaMessage','更新检查已提交，请等待设备返回结果。'); }));
$('restart').addEventListener('click',() => action('otaMessage',async () => { if (!confirm('现在重启这台设备？')) { message('otaMessage','已取消。'); return; } await api('ota','restart',{}); message('otaMessage','重启请求已接受，请稍后重新连接设备。'); }));
async function poll() { await refresh(); setTimeout(poll,5000); }
accessChanged();
poll();
