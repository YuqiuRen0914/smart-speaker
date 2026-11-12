// Synthetic tags only; the user's music and lyrics stay outside the repository.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const html = readFileSync(new URL('../main/web/home.html', import.meta.url), 'utf8');
const start = html.indexOf('    async function readEmbeddedMetadata(');
assert(start >= 0, 'MP3 upload must extract artwork/lyrics before firmware strips ID3');
const ctx = vm.createContext({ Uint8Array, DataView, TextDecoder, TextEncoder, Blob, File });
vm.runInContext(html.slice(start, html.indexOf('    function escapeHtml(', start)), ctx);
const ss = n => [n >>> 21 & 127, n >>> 14 & 127, n >>> 7 & 127, n & 127];
const be = n => [n >>> 24, n >>> 16 & 255, n >>> 8 & 255, n & 255];
const bytes = text => [...Buffer.from(text)];
const frame = (id, data, version = 4, flags = 0) => [...bytes(id), ...(version === 4 ? ss(data.length) : be(data.length)), 0, flags, ...data];
const tag = (frames, version = 4, flags = 0) => new File([new Uint8Array([...bytes('ID3'), version, 0, flags, ...ss(frames.length), ...frames])], 'song.mp3');
const jpeg = [255,216,255,224,1,2,3];
const pic = type => [3, ...bytes('image/jpeg'), 0, type, 0, ...jpeg];
const lyric = '[00:01.000] 测试歌词\n[00:02.000] 第二句';
for (const version of [3,4]) {
 const out = await ctx.readEmbeddedMetadata(tag([...frame('APIC',pic(3),version), ...frame('USLT',[3,...bytes('und'),0,...bytes(lyric)],version)],version));
 assert.equal(out.cover.type, 'image/jpeg');
 assert.deepEqual([...new Uint8Array(await out.cover.arrayBuffer())], jpeg);
 assert.equal(out.lyrics, lyric);assert.equal(out.warnings.length,0);
}
const utf16 = [255,254,...Buffer.from('描述\0'+lyric+'\0','utf16le')];
assert.equal((await ctx.readEmbeddedMetadata(tag(frame('USLT',[1,...bytes('zho'),...utf16])))).lyrics, lyric);
// v2.4 unsynchronisation is per frame, lengths include inserted zeros.
const raw = pic(3).flatMap((x,i,a) => x === 255 && i+1<a.length && a[i+1]>=224 ? [255,0] : [x]);
assert.deepEqual([...new Uint8Array(await (await ctx.readEmbeddedMetadata(tag(frame('APIC',raw,4,2)))).cover.arrayBuffer())], jpeg);
const plain = await ctx.readEmbeddedMetadata(tag(frame('USLT',[3,...bytes('eng'),0,...bytes('no timestamps')])));
assert.equal(plain.lyrics, '');assert(plain.warnings.length);
const empty = await ctx.readEmbeddedMetadata(new File([new Uint8Array([255,251,1])],'empty.mp3'));
assert.equal(empty.cover,null);assert.equal(empty.lyrics,'');
await assert.rejects(() => ctx.readEmbeddedMetadata(tag([...bytes('APIC'),...ss(9999),0,0,1])), /标签/);
await assert.rejects(() => ctx.readEmbeddedMetadata(new File([new Uint8Array([...bytes('ID3'),4,0,0,...ss(17*1024*1024)])],'huge.mp3')), /标签/);
// Malformed picture description must never turn arbitrary bytes into an image.
await assert.rejects(() => ctx.readEmbeddedMetadata(tag(frame('APIC',[3,...bytes('image/jpeg'),0,3,...bytes('no terminator')]))), /标签/);
console.log('Embedded metadata: ID3v2.3/v2.4, JPEG, timed UTF-8/UTF-16, unsync, limits and malformed tags passed');

// Run the real upload handler: existing tracks gain missing sidecars, retries
// preserve manual artwork, and partial failures remain visible to the user.
const original = tag([...frame('APIC',pic(3)), ...frame('USLT',[3,...bytes('und'),0,...bytes(lyric)])]);
const controls = {file:{files:[original],value:'song.mp3'},upload:{}};
const track = {name:'song.mp3',path:'/sdcard/music/song.mp3',has_cover:false,has_lyrics:false};
let calls=[],message='',failLyrics=false;
Object.assign(ctx, {
 $:id=>controls[id], uploadState:{active:false,cancelled:false}, otaState:{},wallpaperState:{},ambientUpload:{active:false},
 setUploadActive:active=>{ctx.uploadState.active=active;},setUploadProgress:()=>{},
 setMessage:text=>{message=text;},refreshAll:async()=>{},
 bus:async()=>({tracks:[track]}),convertWallpaper:async()=>new Blob(['bitmap']),
 bulkUpload:async(file,options)=>{calls.push(options?.url||'audio');track.has_cover=true;},
 sendLyric:async(file,t)=>{assert.equal(t.path,track.path);assert.equal(await file.text(),lyric);
   if(failLyrics) throw Error('lyric_write_failed');calls.push('lyrics');track.has_lyrics=true;}
});
const handler=html.slice(html.indexOf('    $("upload").onclick ='),html.indexOf('    refreshAll().catch('));
vm.runInContext(handler,ctx);
await controls.upload.onclick();
assert.equal(calls.length,2);assert(calls[0].includes('cover.upload?path='));assert.equal(calls[1],'lyrics');
assert(!calls.includes('audio'));assert(!ctx.uploadState.active);
calls=[];await controls.upload.onclick();assert.equal(calls.length,0);
track.has_lyrics=false;failLyrics=true;await controls.upload.onclick();assert(message.includes('lyric_write_failed'));
assert(!ctx.uploadState.active);assert(track.has_cover && !track.has_lyrics);
console.log('MP3 upload handler: repair existing track, preserve manual sidecars and report partial failure passed');
