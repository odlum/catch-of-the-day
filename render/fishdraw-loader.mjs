// Loads the vendored, unmodified fishdraw.js and exposes it as a library.
//
// fishdraw.js is a plain script that keeps its PRNG state (`jsr`) and its
// Perlin noise table in top-level variables. Two consequences:
//   * main() seeds the name generator from Math.random()*10000, so it can
//     only ever produce 10,000 different fish.
//   * the Perlin table is built once per process, so in a warm Lambda the
//     same name would draw a different fish than in a cold one.
// Compiling the script as a function body and calling it once per fish
// avoids both: every fish starts from clean state, so a given name always
// draws the same fish, and a one-line tail appended here (fishdraw.js itself
// is untouched) lets us seed `jsr` with a full 31-bit random value before
// asking for a name.

import { readFileSync } from 'node:fs';
import { createHash, randomInt } from 'node:crypto';
import vm from 'node:vm';

const SOURCE_URL = new URL('./vendor/fishdraw/fishdraw.js', import.meta.url);
const SEED_SETTER = '\n;return (seed) => { jsr = seed; };\n';
const factory = vm.compileFunction(
  readFileSync(SOURCE_URL, 'utf8') + SEED_SETTER,
  ['module', 'require'],
  { filename: 'fishdraw.js' },
);

// Returns a fresh copy of fishdraw's exports plus a PRNG seed setter.
function loadFishdraw() {
  const module = { exports: {} };
  // `require.main !== module` keeps fishdraw's CLI block from running.
  const setSeed = factory(module, { main: null });
  return { api: module.exports, setSeed };
}

// Returns { name, polylines } where polylines is an array of [[x, y], ...]
// in fishdraw's 500x300 frame, with the name lettered under the fish.
//   drawFish()                           a new random fish
//   drawFish({ seed: '2026-10-04' })     the same fish every time for this seed
//   drawFish({ name: 'Biggus fishus' })  the fish with this name
export function drawFish({ name, seed } = {}) {
  const { api, setSeed } = loadFishdraw();
  if (name === undefined) {
    setSeed(seed === undefined ? randomInt(1, 2 ** 31) : seedFrom(seed));
    name = api.binomen();
  }
  // main(seed) prints `seed + '.'` under the fish, matching the CLI output.
  const polylines = api.main(name);
  return { name, polylines };
}

// Hash the seed text so that neighbouring dates give unrelated fish
// (fishdraw's own str_to_seed barely mixes the last characters).
function seedFrom(text) {
  const n = createHash('sha256').update(String(text)).digest().readUInt32BE(0) >>> 1;
  return n || 1; // xorshift state must be non-zero
}
