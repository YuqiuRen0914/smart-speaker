import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const base=import.meta.dirname, output=path.resolve(process.argv[2]);
const manifest=JSON.parse(fs.readFileSync(path.join(base,'UPSTREAM.json'),'utf8'));
for(const [name,sha] of Object.entries(manifest.files))if(createHash('sha256').update(fs.readFileSync(path.join(base,'upstream',name))).digest('hex')!==sha)throw Error('Frozen Insights original differs: '+name);
fs.mkdirSync(output,{recursive:true});
let encoder=fs.readFileSync(path.join(base,'upstream/esp_insights/src/esp_insights_cbor_encoder.c'),'utf8');
for(const name of ['s_encoder','s_meta_encoder']){const anchor='return cbor_encoder_get_buffer_size(&'+name+', data);';if(!encoder.includes(anchor))throw Error('Insights CBOR bounds anchor changed');encoder=encoder.replace(anchor,'return cbor_encoder_get_extra_bytes_needed(&'+name+') ? 0 : cbor_encoder_get_buffer_size(&'+name+', data);');}
fs.writeFileSync(path.join(output,'esp_insights_cbor_encoder.c'),encoder);
