import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const html = readFileSync(new URL('../main/web/home.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Parse the entire page, including event handlers.
const transport = script.slice(script.indexOf('    async function bus('), script.indexOf('    function debugLine('));
const calls = [];
let fail = false;
const context = vm.createContext({
  fetch: async (url, options) => {
    calls.push({ url, options });
    return { ok: !fail, statusText: 'failure', json: async () => fail
      ? { ok: false, error: { message: 'capability_unavailable' } }
      : { ok: true, data: { value: 42 } } };
  },
  uploadState: {},
  XMLHttpRequest: class {
    upload = {};
    status = 200;
    responseText = '{"ok":true,"data":{"uploaded":true}}';
    open(method, url) { calls.push({ url, options: { method } }); }
    setRequestHeader() {}
    send() { this.onload(); }
  },
});
vm.runInContext(transport, context);
for (const [topic, method, path] of [
  ['smart_speaker.status.get', 'GET', 'smart_speaker/status.get'],
  ['smart_speaker.logs.get', 'POST', 'smart_speaker/logs.get'],
  ['smart_speaker.control', 'POST', 'smart_speaker/control'],
  ['smart_speaker.audio.settings.set', 'POST', 'smart_speaker/audio.settings.set'],
  ['network.provision.status', 'GET', 'network/provision.status'],
  ['network.provision.scan', 'POST', 'network/provision.scan'],
  ['network.provision.submit', 'POST', 'network/provision.submit'],
]) {
  const result = await context.bus(topic, { action: 'play' });
  assert.equal(result.value, 42);
  assert.equal(calls.at(-1).url, `/api/v1/modules/${path}`);
  assert.equal(calls.at(-1).options.method, method);
  assert.equal(calls.at(-1).options.body, method === 'GET' ? undefined : '{"action":"play"}');
}
fail = true;
await assert.rejects(() => context.bus('smart_speaker.status.get'), /capability_unavailable/);
assert.equal((await context.bulkUpload({ name: 'a & b.mp3' })).uploaded, true);
assert.equal(calls.at(-1).url, '/api/v1/modules/smart_speaker/file.upload?name=a%20%26%20b.mp3');
assert.equal(calls.at(-1).options.method, 'PUT');
assert(!html.includes('/api/workbench/'));
assert(!html.includes('maintenance.unlock'));
console.log('speaker web API tests passed');
