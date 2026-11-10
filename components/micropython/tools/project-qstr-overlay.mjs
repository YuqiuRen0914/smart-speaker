import {readFileSync,writeFileSync,mkdirSync,readdirSync} from 'node:fs';
import {resolve,join} from 'node:path';
const [material,target]=process.argv.slice(2),owner=resolve(import.meta.dirname,'..');
if(!material||!target)throw Error('Usage: project-qstr-overlay material target');
const original=readFileSync(join(material,'generated/embed/genhdr/qstrdefs.generated.h'),'utf8');
const known=new Set([...original.matchAll(/QDEF[01]\((MP_QSTR\w*),/g)].map(row=>row[1]));
const names=new Set();
function scan(folder){for(const entry of readdirSync(folder,{withFileTypes:true})){const file=join(folder,entry.name);if(entry.isDirectory())scan(file);else if(/\.[ch]$/.test(entry.name))for(const [name]of readFileSync(file,'utf8').matchAll(/\bMP_QSTR_\w+/g))if(!known.has(name)&&!['MP_QSTRnumber_of','MP_QSTRnumber_of_static','MP_QSTRstart_of_main','MP_QSTR_maximum_space_recursion_space_depth_space_exceeded'].includes(name))names.add(name);}}
scan(join(material,'generated/embed/py'));scan(join(owner,'port'));
const symbols={_lt_:'<',_gt_:'>',_dot_:'.',_hyphen_:'-',_slash_:'/',_space_:' ',_star_:'*',_colon_:':',_equals_:'='};
const rows=[...names].sort().map(id=>{let value=id.slice(8);for(const [symbol,text]of Object.entries(symbols))value=value.replaceAll(symbol,text);let hash=5381;for(const byte of new TextEncoder().encode(value))hash=((hash*33)^byte)&65535;return `QDEF1(${id}, ${hash||1}, ${Buffer.byteLength(value)}, ${JSON.stringify(value)})`;});
const text=row=>JSON.parse('"'+row.match(/, "(.*)"\)$/)[1].replace(/\\x([a-f\d]{2})/gi,'\\u00$1')+'"');
const main=[...original.split('\n').filter(row=>row.startsWith('QDEF1(')),...rows].sort((a,b)=>Buffer.compare(Buffer.from(text(a)),Buffer.from(text(b))));
mkdirSync(join(target,'genhdr'),{recursive:true});
const file=join(target,'genhdr/qstrdefs.generated.h'),content=original.split('\n').filter(row=>!row.startsWith('QDEF1(')).join('\n')+'\n'+main.join('\n')+'\n';
let previous;try{previous=readFileSync(file,'utf8');}catch(error){if(error.code!=='ENOENT')throw error;}
if(previous!==content)writeFileSync(file,content);
