// Renders the fish of the day for the e-ink frame.
//
//   node render/render.mjs [--random | --name "Fish name"] [--date YYYY-MM-DD]
//                          [--width 1|2] [--out docs]
//
// By default the fish is chosen by the date (UTC today), so re-running on
// the same day gives the same files. Writes:
//   <out>/latest.bin             800x480 1bpp, row-major, MSB first, 1 = black
//   <out>/latest.svg
//   <out>/archive/YYYY-MM-DD.svg
//   <out>/index.html             gallery of the archive (GitHub Pages)
//   render/preview.pbm           the bitmap as an image, for local checks

import { appendFileSync, mkdirSync, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseArgs } from 'node:util';
import { drawFish } from './fishdraw-loader.mjs';
import { fitToPanel, rasterize, toPBM, toSVG, WIDTH, HEIGHT } from './raster.mjs';

const { values: args } = parseArgs({
  options: {
    random: { type: 'boolean', default: false },
    name: { type: 'string' },
    date: { type: 'string', default: new Date().toISOString().slice(0, 10) },
    width: { type: 'string', default: '2' },
    out: { type: 'string', default: 'docs' },
  },
});
if (!/^\d{4}-\d{2}-\d{2}$/.test(args.date)) throw new Error(`--date must be YYYY-MM-DD, got ${args.date}`);
if (!['1', '2'].includes(args.width)) throw new Error(`--width must be 1 or 2, got ${args.width}`);
const lineWidth = Number(args.width);

const t0 = Date.now();
const { name, polylines } = args.name
  ? drawFish({ name: args.name })
  : drawFish(args.random ? {} : { seed: args.date });
const fitted = fitToPanel(polylines);
const bitmap = rasterize(fitted, lineWidth);
const svg = toSVG(fitted, lineWidth, `${name}.`);

const archiveDir = join(args.out, 'archive');
mkdirSync(archiveDir, { recursive: true });
writeFileSync(join(args.out, 'latest.bin'), bitmap);
writeFileSync(join(args.out, 'latest.svg'), svg);
writeFileSync(join(archiveDir, `${args.date}.svg`), svg);
writeFileSync(join(args.out, 'index.html'), gallery(archiveDir));
writeFileSync(fileURLToPath(new URL('preview.pbm', import.meta.url)), toPBM(bitmap));

console.log(`${args.date}: ${name} (${polylines.length} polylines, ${lineWidth} px, ` +
  `${bitmap.length} bytes, ${Date.now() - t0} ms)`);

// Hand the name to later workflow steps (used in the commit message).
if (process.env.GITHUB_OUTPUT) {
  appendFileSync(process.env.GITHUB_OUTPUT, `name=${name}\ndate=${args.date}\n`);
}

// Static gallery page, newest first. Names come from each SVG's <title>
// (already XML-escaped there).
function gallery(dir) {
  const items = readdirSync(dir)
    .filter((f) => /^\d{4}-\d{2}-\d{2}\.svg$/.test(f))
    .sort()
    .reverse()
    .map((f) => {
      const title = readFileSync(join(dir, f), 'utf8').match(/<title>(.*?)<\/title>/)?.[1] ?? '';
      const date = f.slice(0, 10);
      return `<figure><a href="archive/${f}"><img src="archive/${f}" alt="${title.replace(/"/g, '&quot;')}" width="${WIDTH}" height="${HEIGHT}" loading="lazy"></a>` +
        `<figcaption><i>${title}</i> <time>${date}</time></figcaption></figure>`;
    });
  return `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Catch of the Day</title>
<style>
  :root { color-scheme: light dark; --bg: #f4f1ea; --fg: #222; --muted: #777; }
  @media (prefers-color-scheme: dark) { :root { --bg: #1b1b1b; --fg: #eee; --muted: #999; } }
  body { margin: 0 auto; max-width: 1100px; padding: 24px 16px; background: var(--bg); color: var(--fg);
         font: 16px/1.4 Georgia, serif; }
  h1 { font-weight: normal; margin: 0 0 4px; }
  p { margin: 0 0 24px; color: var(--muted); }
  .grid { display: grid; gap: 24px; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); }
  figure { margin: 0; }
  img { display: block; width: 100%; height: auto; background: #fff; border: 1px solid #ccc; }
  figcaption { display: flex; justify-content: space-between; gap: 8px; margin-top: 6px; }
  time { color: var(--muted); font-size: 14px; }
</style>
</head>
<body>
<h1>Catch of the Day</h1>
<p>A new fish every morning, drawn by <a href="https://github.com/LingDong-/fishdraw">fishdraw</a>.</p>
<div class="grid">
${items.join('\n')}
</div>
</body>
</html>
`;
}
