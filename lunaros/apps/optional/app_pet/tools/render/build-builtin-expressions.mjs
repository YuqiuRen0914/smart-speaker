import assert from 'node:assert/strict';
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const petRoot = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const sourceDir = join(petRoot, 'resources/expressions/builtin');
const catalogPath = join(petRoot, 'include/lunaredu_pet_builtin_expression_catalog.inc');
const configPath = join(petRoot, 'src/render/builtin_expression_configs.inc');
const check = process.argv.includes('--check');
assert.ok(process.argv.length === (check ? 3 : 2), 'usage: node build-builtin-expressions.mjs [--check]');

const groups = new Set([
  'Conversation', 'Task', 'System', 'Notice', 'Reaction', 'Cue', 'InteractionDemo',
]);
const files = readdirSync(sourceDir).filter(name => name.endsWith('.json'));
assert.ok(files.length > 0, 'no built-in expression files');
const definitions = files.map(name => {
  const value = JSON.parse(readFileSync(join(sourceDir, name), 'utf8'));
  assert.deepEqual(Object.keys(value).sort(),
    ['blinkMs', 'eyePlaylist', 'format', 'group', 'holdMs', 'id', 'motion', 'slot'].sort(),
    `${name}: unexpected or missing fields`);
  assert.equal(value.format, 'lunaredu.pet.expression.v1', `${name}: unsupported format`);
  assert.match(value.id, /^[a-z][a-z0-9]*(?:-[a-z0-9]+)*$/, `${name}: invalid id`);
  assert.equal(name, `${value.id}.json`, `${name}: filename must match id`);
  assert.ok(groups.has(value.group), `${name}: invalid group`);
  assert.ok(Number.isSafeInteger(value.slot) && value.slot >= 0, `${name}: invalid slot`);
  assert.deepEqual(value.motion, { kind: 'builtin', preset: value.id },
    `${name}: built-in motion preset must match the legacy renderer state`);
  assert.ok(Array.isArray(value.eyePlaylist) && value.eyePlaylist.length >= 1 &&
    value.eyePlaylist.length <= 6 && value.eyePlaylist.every(eye =>
      Number.isSafeInteger(eye) && eye >= 0 && eye < 25), `${name}: invalid eye playlist`);
  for (const field of ['holdMs', 'blinkMs']) {
    const range = value[field];
    assert.ok(Array.isArray(range) && range.length === 2 &&
      range.every(time => Number.isSafeInteger(time) && time >= 0 && time <= 60000) &&
      range[0] <= range[1], `${name}: invalid ${field}`);
  }
  assert.ok(value.holdMs[0] > 0, `${name}: hold must be positive`);
  return value;
}).sort((left, right) => left.slot - right.slot);

assert.equal(new Set(definitions.map(value => value.id)).size, definitions.length,
  'duplicate expression id');
definitions.forEach((value, index) => assert.equal(value.slot, index,
  `${value.id}: built-in slots must be contiguous and preserve renderer enum order`));

const catalog = [
  '// Generated from resources/expressions/builtin/*.json; do not hand edit.',
  'inline constexpr CharacterStateEntry kCharacterStateEntries[] = {',
  ...definitions.map(value =>
    `    {"${value.id}", CharacterStateGroup::${value.group}},`),
  '};',
  '',
].join('\n');
const config = [
  '// Generated from resources/expressions/builtin/*.json; do not hand edit.',
  'inline constexpr Config configs[] = {',
  ...definitions.map(value => {
    const playlist = [...value.eyePlaylist, ...Array(6 - value.eyePlaylist.length).fill(0)];
    return `    {${value.eyePlaylist.length},{${playlist.join(',')}},{${value.holdMs.map(time => `${time}.0f`).join(',')}},{${value.blinkMs.map(time => `${time}.0f`).join(',')}}},`;
  }),
  '};',
  '',
].join('\n');

for (const [path, content] of [[catalogPath, catalog], [configPath, config]]) {
  if (check) {
    assert.equal(readFileSync(path, 'utf8').replaceAll('\r\n', '\n'), content,
      `${path} is stale; run build-builtin-expressions.mjs`);
  } else {
    writeFileSync(path, content);
  }
}
console.log(`${check ? 'Verified' : 'Generated'} ${definitions.length} built-in Pet expressions`);
