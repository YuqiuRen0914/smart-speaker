import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createHash } from 'node:crypto';

const html = readFileSync(new URL('../main/web/home.html', import.meta.url), 'utf8');
const sdkDefaults = readFileSync(new URL('../sdkconfig.defaults', import.meta.url), 'utf8');
const pageLimit = Number(sdkDefaults.match(/^CONFIG_LUNAREDU_DEVICE_WEB_MAX_PAGE_BYTES=(\d+)$/m)?.[1]);
assert(Number.isInteger(pageLimit) && Buffer.byteLength(html) <= pageLimit,
  'embedded device home page must fit the configured web page limit');
const projection = readFileSync(new URL('../main/smart_speaker/smart_speaker_projection.cpp', import.meta.url), 'utf8');
const registry = readFileSync(new URL('../lunaros/src/device_api/projections/device_projection_registry.cpp', import.meta.url), 'utf8');
const operationBlock = projection.match(/constexpr DeviceOperationDefinition kOperations\[\] = \{([\s\S]*?)\n\};/);
const operationLimit = Number(registry.match(/kMaximumOperationsPerModule\s*=\s*(\d+)/)?.[1]);
const operationCount = operationBlock?.[1].split('\n').filter(line => /^\s*make_device_/.test(line)).length ?? 0;
assert(operationCount > 0 && Number.isInteger(operationLimit) && operationCount <= operationLimit,
  `smart_speaker projection has ${operationCount} operations but registry limit is ${operationLimit}`);
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
new vm.Script(script); // Parse the entire page, including event handlers.
const integrity = script.slice(script.indexOf('    function sha256Hex('), script.indexOf('    function setBusy('));
const integrityContext = vm.createContext({ Uint8Array, Uint32Array, DataView });
vm.runInContext(integrity, integrityContext);
const sample = Buffer.alloc(51200, 0x5a);
assert.equal(integrityContext.sha256Hex(sample), createHash('sha256').update(sample).digest('hex'));
sample[0] ^= 1;
assert.notEqual(integrityContext.sha256Hex(sample), createHash('sha256').update(Buffer.alloc(51200, 0x5a)).digest('hex'));
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
  ['smart_speaker.cover.remove', 'POST', 'smart_speaker/cover.remove'],
  ['smart_speaker.audio.settings.set', 'POST', 'smart_speaker/audio.settings.set'],
  ['smart_speaker.wallpaper.get', 'GET', 'smart_speaker/wallpaper.get'],
  ['smart_speaker.wallpaper.select', 'POST', 'smart_speaker/wallpaper.select'],
  ['smart_speaker.product.desktop.set', 'POST', 'smart_speaker/product.desktop.set'],
  ['smart_speaker.product.weather.configure', 'POST', 'smart_speaker/product.weather.configure'],
  ['smart_speaker.product.weather.refresh', 'POST', 'smart_speaker/product.weather.refresh'],
  ['xiaozhi.status.get', 'GET', 'xiaozhi/status.get'],
  ['xiaozhi.enabled.set', 'POST', 'xiaozhi/enabled.set'],
  ['xiaozhi.microphone.enabled.set', 'POST', 'xiaozhi/microphone.enabled.set'],
  ['xiaozhi.activation.start', 'POST', 'xiaozhi/activation.start'],
  ['xiaozhi.activation.clear', 'POST', 'xiaozhi/activation.clear'],
  ['xiaozhi.activation.endpoint.set', 'POST', 'xiaozhi/activation.endpoint.set'],
  ['xiaozhi.conversation.test', 'POST', 'xiaozhi/conversation.test'],
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
assert(html.includes('/api/v1/modules/smart_speaker/pet.frame.upload?clip='));
assert(html.includes('bigfish-manifest.json'));
assert(html.includes('结果未确认，设备可能已提交'));
assert(html.includes('已确认提交 ${stored} 帧'));
const bitmapScript = script.slice(script.indexOf('    function encodeWallpaperBmp('), script.indexOf('    async function convertWallpaper('));
vm.runInContext(bitmapScript, context);
const rgba = new Uint8ClampedArray(240 * 240 * 4);
rgba.set([255, 0, 0, 255], 0);
rgba.set([0, 0, 255, 255], (239 * 240) * 4);
const bmp = context.encodeWallpaperBmp(rgba);
assert.equal(bmp.length, 172854);
assert.equal(bmp[0], 66); assert.equal(bmp[1], 77);
assert.equal(bmp[54], 255); // bottom row BGR blue
assert.equal(bmp[54 + 239 * 720 + 2], 255); // top row BGR red
assert.equal(new DataView(bmp.buffer).getUint32(10, true), 54);
assert.throws(() => context.encodeWallpaperBmp(new Uint8ClampedArray(4)), /invalid_wallpaper_pixels/);
assert(!html.includes('maintenance.unlock'));
console.log('speaker web API tests passed');
