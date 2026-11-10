"use strict";
const $ = id => document.getElementById(id);
const stateText = {unavailable:'不可用',ready:'待检查',running:'检查中',awaiting_confirmation:'等待确认',passed:'通过',failed:'失败'};
const detailText = {operator_confirmed_pass:'人工确认通过',operator_confirmed_fail:'人工确认失败',cancelled_on_exit:'已取消'};
let config = null, status = null, catalog = null, busy = false, refreshing = false, selectedTab = 'hardware';
const expandedGroups = new Map();
const selectedChecks = new Map();

function notice(value, type = '') {
  $('message').textContent = value;
  $('message').className = `factory-message ${type}`;
  $('message').hidden = !value;
}
function failure(error) {
  const code = String(error?.message || error || '');
  return ({factory_check_busy:'请先确认或取消当前项目，再启动其他检查。',
    factory_mode_inactive:'请先开始工厂检查。',pet_not_active:'宠物应用尚未运行。',
    factory_check_unavailable:'当前项目不可用。',bound_access_required:'此入口尚未获得设备访问授权。'})[code]
    || (/Failed to fetch|NetworkError|Timeout|Abort/i.test(code) ? '设备连接中断，请刷新后重试。' : code || '检查未完成，请重试。');
}
async function api(operation, payload) {
  return LunarDeviceClient.api('factory_mode',operation,payload);
}
function currentCheck() {
  return catalog?.checks?.find(check => ['running','awaiting_confirmation'].includes(check.state)) || null;
}
function applyCheckUpdate(update) {
  if (!catalog || !update?.check || !Number.isInteger(update.revision)) return;
  const index = catalog.checks.findIndex(check => check.id === update.check.id);
  if (index < 0) return;
  catalog.checks[index] = update.check;
  catalog.revision = update.revision;
  if (['passed','failed'].includes(update.check.state)) {
    const group = catalog.checks.filter(check => check.owner === update.check.owner);
    const position = group.findIndex(check => check.id === update.check.id);
    if (position >= 0 && position + 1 < group.length)
      selectedChecks.set(update.check.owner, group[position + 1].id);
  }
}
function cardsFor(items, current) {
  return items.map(check => {
    const card = document.createElement('div');
    card.className = `factory-card${current?.id === check.id ? ' current' : ''}`;
    card.title = `${check.owner} · ${check.id}`;
    const info = document.createElement('div');
    const label = document.createElement('strong'); label.textContent = check.label || check.id;
    const detail = document.createElement('small');
    detail.className = `state-${check.state}`;
    detail.textContent = stateText[check.state] || check.state;
    if (check.state === 'unavailable' && check.detail) detail.textContent += ` · ${check.detail}`;
    info.append(label,detail);
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = current?.id === check.id ? '展示中' : '启动';
    button.disabled = busy || !status?.active || check.state === 'unavailable' || check.state === 'running' || current?.id === check.id ||
      (Boolean(current) && !(check.kind === 'application' && current.kind === 'application' && current.state === 'awaiting_confirmation'));
    button.addEventListener('click',() => run(async () => {
      // Expression previews may be switched directly. Only an unconfirmed
      // application check is cancelled; confirmed results stay intact.
      const previous = currentCheck();
      if (previous?.id !== check.id && previous?.kind === 'application' &&
          check.kind === 'application' && previous.state === 'awaiting_confirmation') {
        applyCheckUpdate(await api('reset',{id:previous.id}));
      }
      applyCheckUpdate(await api('start',{id:check.id}));
      notice(`已启动 ${check.label || check.id}。请观察设备并确认结果。`,'success');
    }));
    card.append(info,button);
    return card;
  });
}
function renderGroups(items, target, current) {
  const groups = new Map();
  for (const check of items) {
    const owner = check.owner || check.id;
    if (!groups.has(owner)) groups.set(owner, []);
    groups.get(owner).push(check);
  }
  $(target).replaceChildren(...[...groups].map(([owner, checks]) => {
    const section = document.createElement('details');
    section.className = 'factory-group';
    section.dataset.owner = owner;
    section.open = expandedGroups.has(owner) ? expandedGroups.get(owner) :
      checks[0].kind === 'device';
    if (checks.some(check => check.id === current?.id)) section.open = true;
    section.addEventListener('toggle', () => expandedGroups.set(owner, section.open));
    const title = document.createElement('summary');
    const passed = checks.filter(check => check.state === 'passed').length;
    title.textContent = `${checks[0].owner_label || owner} · ${passed}/${checks.length} 项通过`;
    const list = document.createElement('div');
    list.className = 'factory-list';
    if (checks[0].kind === 'application') {
      const selected = selectedChecks.get(owner);
      const position = Math.max(0, checks.findIndex(check => check.id === selected));
      const show = index => { selectedChecks.set(owner, checks[index].id); render(); };
      const navigation = document.createElement('div');
      navigation.className = 'factory-sequence';
      const previous = document.createElement('button');
      previous.type = 'button'; previous.textContent = '上一个';
      previous.disabled = position === 0;
      previous.addEventListener('click', () => show(position - 1));
      const picker = document.createElement('select');
      picker.setAttribute('aria-label', `${checks[0].owner_label || owner}检查项目`);
      for (const check of checks) {
        const option = document.createElement('option');
        option.value = check.id;
        option.textContent = check.label || check.id;
        picker.append(option);
      }
      picker.value = checks[position].id;
      picker.addEventListener('change', () => {
        const index = checks.findIndex(check => check.id === picker.value);
        if (index >= 0) show(index);
      });
      const next = document.createElement('button');
      next.type = 'button'; next.textContent = '下一个';
      next.disabled = position + 1 === checks.length;
      next.addEventListener('click', () => show(position + 1));
      const progress = document.createElement('span');
      progress.textContent = `${position + 1}/${checks.length}`;
      navigation.append(previous, picker, next, progress);
      list.append(...cardsFor([checks[position]], current));
      section.append(title, navigation, list);
    } else {
      list.append(...cardsFor(checks, current));
      section.append(title, list);
    }
    return section;
  }));
}
function render() {
  const checks = Array.isArray(catalog?.checks) ? catalog.checks : [];
  const hardware = checks.filter(check => check.kind === 'device');
  const software = checks.filter(check => check.kind === 'application');
  const passed = checks.filter(check => check.state === 'passed').length;
  const current = currentCheck();
  $('summary').textContent = status?.active
    ? `已开始 · ${passed}/${checks.length} 项通过${current ? ` · 当前 ${current.label}` : ''}`
    : `共 ${checks.length || status?.count || 0} 项。开始后可逐项检查。`;
  $('enter').hidden = Boolean(status?.active);
  $('exit').hidden = !status?.active;
  $('enter').disabled = busy;
  $('exit').disabled = busy;
  $('hardwareCount').textContent = `${hardware.length}`;
  $('softwareCount').textContent = `${software.length}`;
  $('currentPanel').hidden = !current;
  if (current) {
    $('currentTitle').textContent = current.label || current.id;
    $('currentHelp').textContent = detailText[current.detail] || current.detail || '请观察设备反馈。';
    $('currentPass').disabled = busy || current.state !== 'awaiting_confirmation';
    $('currentFail').disabled = busy || current.state !== 'awaiting_confirmation';
    $('currentReset').disabled = busy || current.state !== 'awaiting_confirmation';
  }
  renderGroups(hardware,'hardwareList',current);
  renderGroups(software,'softwareList',current);
}
function selectTab(tab) {
  selectedTab = tab;
  for (const name of ['hardware','software']) {
    $(`${name}Panel`).hidden = name !== tab;
    $(`${name}Tab`).setAttribute('aria-pressed',String(name === tab));
  }
}
async function refresh(force = false) {
  if (refreshing || busy || (document.hidden && !force)) return;
  refreshing = true;
  try {
    if (force || !config) {
      config = await LunarDeviceClient.request('/device-web/config');
      if (!config.features?.factory_mode) throw new Error('当前设备未提供本机工厂检查。');
      $('productName').textContent = config.product_name || 'LunarOS 设备';
    }
    const next = await api('status.get');
    if (force || !catalog || catalog.revision !== next.revision) {
      catalog = await api('catalog.get');
      status = next;
      render();
    } else if (status?.active !== next.active) {
      status = next;
      render();
    } else status = next;
  } catch (error) { notice(failure(error),'error'); }
  finally { refreshing = false; }
}
async function run(operation) {
  if (busy) return;
  busy = true;
  notice('正在向设备发送操作…');
  render();
  try {
    await operation();
    status = await api('status.get');
    if (!catalog || catalog.revision !== status.revision)
      catalog = await api('catalog.get');
    render();
  } catch (error) {
    notice(failure(error),'error');
    try { status = await api('status.get'); catalog = await api('catalog.get'); } catch {}
  } finally { busy = false; render(); }
}
$('hardwareTab').addEventListener('click',() => selectTab('hardware'));
$('softwareTab').addEventListener('click',() => selectTab('software'));
$('refresh').addEventListener('click',() => refresh(true));
$('enter').addEventListener('click',() => run(async () => { await api('enter',{}); notice('工厂检查已开始。','success'); }));
$('exit').addEventListener('click',() => run(async () => { await api('exit',{}); notice('工厂检查已结束。','success'); }));
for (const [id,operation,extra] of [['currentPass','confirm',{passed:true}],['currentFail','confirm',{passed:false}],['currentReset','reset',{}]]) {
  $(id).addEventListener('click',() => run(async () => {
    const check = currentCheck();
    if (!check) throw new Error('当前没有等待确认的项目。');
    applyCheckUpdate(await api(operation,{id:check.id,...extra}));
    notice(operation === 'reset' ? '当前项目已取消。' : `${check.label} 已记录${extra.passed ? '通过' : '失败'}。`,'success');
  }));
}
selectTab(selectedTab);
void refresh(true);
setInterval(() => { void refresh(); },1800);
