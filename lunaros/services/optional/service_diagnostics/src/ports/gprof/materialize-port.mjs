import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
const root=path.resolve(process.argv[3]),output=path.resolve(process.argv[2]);
const manifest=JSON.parse(fs.readFileSync(path.join(root,'UPSTREAM.json'),'utf8'));
for(const [file,hash] of Object.entries(manifest.files))if(createHash('sha256').update(fs.readFileSync(path.join(root,'upstream',file))).digest('hex')!==hash)throw Error('Frozen GProf source differs: '+file);
let source=fs.readFileSync(path.join(root,'upstream/src/esp_gprof.c'),'utf8');
function replace(before,after){if(source.split(before).length!==2)throw Error('GProf port anchor drift');source=source.replace(before,after);}
function functionBody(name,body){const start=source.indexOf(name);if(start<0)throw Error('Missing GProf function');const open=source.indexOf('{',start);let depth=1,index=open+1;while(depth&&index<source.length){if(source[index]==='{')++depth;else if(source[index]==='}')--depth;++index;}source=source.slice(0,open+1)+'\n'+body+'\n'+source.slice(index-1);}
replace('#include "gprof.h"','#include "gprof.h"\n#include "lunar_gprof_port.h"\n#include <esp_memory_utils.h>');
replace('esp_partition_write(gp->part, off, buf, n)','lunar_gprof_store_write(off, buf, n)');
replace('esp_partition_write(gp->part, 0, &hdr, sizeof(hdr))','lunar_gprof_store_write(0, &hdr, sizeof(hdr))');
replace('gp->task_handle = NULL;','gp->task_handle = NULL;\n    gp->saved_size = 0; /* A failed bounded save may be retried without appending duplicates. */');
functionBody('static esp_err_t gprof_storage_init(esp_gprof_t *gp)','    gp->part = lunar_gprof_store_begin();\n    return gp->part ? ESP_OK : ESP_ERR_NO_MEM;');
functionBody('esp_err_t esp_gprof_dump(void)','    return ESP_ERR_NOT_SUPPORTED; /* Private file export replaces raw console dumping. */');
replace('static void IRAM_ATTR hist_sample_cb(void)','static void IRAM_ATTR hist_sample_inner(void)');
const next='static int gprof_data_init';
replace(next,'static void IRAM_ATTR hist_sample_cb(void) {\n    if (lunar_gprof_expired() || !lunar_gprof_enter()) return;\n    hist_sample_inner();\n    lunar_gprof_leave();\n}\n\n'+next);
replace('void IRAM_ATTR _mcount(uintptr_t from_pc)','static void IRAM_ATTR mcount_inner(uintptr_t from_pc)');
replace('overflow:\n    abort();','overflow:\n    lunar_gprof_overflow();');
source+='\nvoid IRAM_ATTR _mcount(uintptr_t from_pc) {\n    if (!lunar_gprof_enter()) return;\n    mcount_inner(from_pc);\n    lunar_gprof_leave();\n}\n';
// Raw profiling is restricted to the starting owner task. The port closes
// admission and drains callbacks before save/deinit; no SDK Flash writes.
let cmake=fs.readFileSync(path.join(root,'upstream/gprof.cmake'),'utf8');
const partitionAnchor=cmake.indexOf('    partition_table_get_partition_info');
if(partitionAnchor<0)throw Error('GProf build anchor drift');
cmake=cmake.slice(0,partitionAnchor)+'endif()\n';
cmake=cmake.replace('idf_build_get_property(${project_elf} EXECUTABLE)','idf_build_get_property(project_elf EXECUTABLE)');
cmake=cmake.replace('"${CMAKE_CURRENT_LIST_DIR}/scripts/gprof.py"',JSON.stringify(path.join(root,'upstream/scripts/gprof.py').replaceAll('\\','/')));
fs.mkdirSync(output,{recursive:true});fs.writeFileSync(path.join(output,'esp_gprof.c'),source);fs.writeFileSync(path.join(output,'gprof.cmake'),cmake);
