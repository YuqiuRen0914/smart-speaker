// Export the pinned CharacterMotionLab paths and spanAt tables for native LCD
// rasterization. Run this when the upstream runtime revision changes.
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const source = process.argv[2] || path.resolve(here,
  '../../../../../../../lunaredu-platform/apps/tools/components/character-motion/runtime');
const native = path.resolve(here, '../../src/render');
const sandbox = { window: {} };
for (const file of ['geometry-data.js', 'math.js'])
  vm.runInNewContext(readFileSync(path.join(source, file), 'utf8'), sandbox,
    { filename: file });
const geo = sandbox.window.GROK_GEO;
const math = sandbox.window.GROK_MATH;
const catalog = readFileSync(path.resolve(native, '../../include/lunaredu_pet_catalog.h'), 'utf8');
const names = [...catalog.match(/kCharacterShapes\s*=\s*\{([^}]+)\}/)[1]
  .matchAll(/"([^"]+)"/g)].map(match => match[1]);
assert.deepEqual(names, ['soft-pet', ...Object.keys(geo.shapes)]);
const shapes = names.map(name => {
  const shape = geo.shapes[name === 'soft-pet' ? 'blob' : name];
  const points = math.flattenPath(shape.path, 4);
  const top = Math.min(...points.map(point => point[1]));
  const bottom = Math.max(...points.map(point => point[1]));
  const span = math.spanAt(shape.path, geo.Re);
  const spans = Array.from({ length: 160 }, (_, index) =>
    span(top + (bottom - top) * (index + 0.5) / 160));
  return { points, top, bottom, spans };
});
const maxPoints = Math.max(...shapes.map(shape => shape.points.length));
assert.ok(maxPoints <= 384);
const f = value => `${Number((value / geo.Re).toFixed(7)).toFixed(7)}f`;
const point = ([x, y]) => `{${f(x - geo.Re)},${f(y - geo.Re)}}`;
const lines = [
  '// Generated from pinned CharacterMotionLab runtime; do not hand edit.',
  '// Upstream 647e9bd7c60290c42a738fad586589b3f36a4680.',
  '#pragma once',
  '#include <cstdint>',
  'namespace lunaredu::pet::render {',
  'struct PathMeta { uint16_t count; float top; float bottom; };',
  `inline constexpr int PathPointCapacity=${maxPoints};`,
  'inline constexpr int PathSpanCount=160;',
  'inline constexpr PathMeta path_meta[]={',
  ...shapes.map(shape =>
    `  {${shape.points.length},${f(shape.top - geo.Re)},${f(shape.bottom - geo.Re)}},`),
  '};',
  `inline constexpr float path_points[][${maxPoints}][2]={`,
  ...shapes.map(shape => `  {${shape.points.map(point).join(',')}},`),
  '};',
  'inline constexpr float path_spans[][160][2]={',
  ...shapes.map(shape => `  {${shape.spans.map(point).join(',')}},`),
  '};',
  '} // namespace lunaredu::pet::render',
  '',
];
writeFileSync(path.join(native, 'path_data.hpp'), lines.join('\n'));
console.log(`Exported ${shapes.length} source paths, max ${maxPoints} points`);
