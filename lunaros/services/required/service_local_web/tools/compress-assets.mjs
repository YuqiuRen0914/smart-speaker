/** Deterministic gzip; complete decoding must reproduce the input bytes. */
import fs from 'node:fs';
import { gzipSync, gunzipSync } from 'node:zlib';
const [source, output, ...extra] = process.argv.slice(2);
if (!source || !output || extra.length) throw new Error('Use compress-assets.mjs <source> <output>');
const data = fs.readFileSync(source), encoded = gzipSync(data, { level: 9 });
// gzip.compress(mtime=0) used the host zlib OS marker; retain deterministic
// metadata rather than wall-clock timestamps or an original filename.
if (!gunzipSync(encoded).equals(data)) throw new Error('Asset round-trip failed: ' + source);
fs.writeFileSync(output, encoded);
