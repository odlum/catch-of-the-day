// Local checks for the renderer. Writes only to a temp directory.
//
// Usage: node render/test.mjs

import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { drawFish } from './fishdraw-loader.mjs';
import { fitToPanel, rasterize, HEIGHT, MARGIN, WIDTH } from './raster.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

// The loader matches fishdraw's own CLI exactly.
const cli = execFileSync(process.execPath,
  [here('vendor/fishdraw/fishdraw.js'), '--seed', 'Biggus fishus', '--format', 'json'], { encoding: 'utf8' });
assert.equal(JSON.stringify(drawFish({ name: 'Biggus fishus' }).polylines), cli.trim());
console.log('matches fishdraw CLI');

// Same date, same fish; different dates, different fish; random is random.
const a = drawFish({ seed: '2026-10-04' });
assert.equal(drawFish({ seed: '2026-10-04' }).name, a.name);
assert.notEqual(drawFish({ seed: '2026-10-05' }).name, a.name);
assert.notEqual(drawFish().name, drawFish().name);
console.log(`date seeding ok (2026-10-04: ${a.name})`);

// Bitmap size, and both pens stay inside the margin (a 2 px pen may
// overhang the margin line by 1 px), well clear of the firmware's battery
// icon at x >= 772, y >= 462.
for (const w of [1, 2]) {
  const buf = rasterize(fitToPanel(a.polylines), w);
  assert.equal(buf.length, 48000);
  for (let y = 0; y < HEIGHT; y++) {
    for (let x = 0; x < WIDTH; x++) {
      const inside = x >= MARGIN - 1 && x <= WIDTH - MARGIN + 1 && y >= MARGIN - 1 && y <= HEIGHT - MARGIN + 1;
      if (inside) continue;
      assert.equal((buf[y * 100 + (x >> 3)] >> (7 - (x & 7))) & 1, 0, `ink at ${x},${y} (${w} px)`);
    }
  }
}
console.log('bitmap size and margins ok');

// End to end: the script's output, run twice for the same date, is identical.
const out = mkdtempSync(join(tmpdir(), 'cotd-'));
const run = () => execFileSync(process.execPath,
  [here('render.mjs'), '--date', '2026-10-04', '--out', out], { encoding: 'utf8' });
const snapshot = () => Object.fromEntries(['latest.bin', 'latest.svg', 'index.html', 'archive/2026-10-04.svg']
  .map((f) => [f, readFileSync(join(out, f))]));
process.stdout.write(run());
const first = snapshot();
run();
assert.deepEqual(snapshot(), first, 'a re-run for the same date should change nothing');
assert.equal(first['latest.bin'].length, 48000);
assert.match(first['latest.svg'].toString(), new RegExp(`<title>${a.name}\\.</title>`));
assert.match(first['index.html'].toString(), /archive\/2026-10-04\.svg/);
assert.deepEqual(readdirSync(join(out, 'archive')), ['2026-10-04.svg']);
rmSync(out, { recursive: true });
console.log('render.mjs output ok and repeatable');
