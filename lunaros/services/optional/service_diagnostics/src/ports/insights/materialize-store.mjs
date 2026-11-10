import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
const base=path.resolve(process.argv[3]), output=path.resolve(process.argv[2]);
const manifest=JSON.parse(fs.readFileSync(path.join(base,'UPSTREAM.json'),'utf8'));
for(const [name,sha] of Object.entries(manifest.files))if(createHash('sha256').update(fs.readFileSync(path.join(base,'upstream',name))).digest('hex')!==sha)throw Error('Frozen Insights original differs: '+name);
let source=fs.readFileSync(path.join(base,'upstream/esp_diag_data_store/src/rtc_store/rtc_store.c'),'utf8');
const start=source.indexOf('    uint8_t gen_id = 0, boot_cnt = 0;'), end=source.indexOf('skip_nvs_read_write:',start);
if(start<0||end<start||source.indexOf('skip_nvs_read_write:',end+1)>=0)throw Error('Insights NVS port anchor changed');
source=source.slice(0,start)+'    // RAM-only store. Configuration and NVS remain owned by LunarOS.\n    uint8_t gen_id = (uint8_t)esp_random(), boot_cnt = 0;\n'+source.slice(end+'skip_nvs_read_write:'.length);
source=source.replace('#include <nvs_flash.h>','');
fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'rtc_store.c'),source);

