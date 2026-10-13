// Fits fishdraw polylines to the panel and rasterizes them to a 1-bit
// bitmap: row-major, MSB first, 1 = black.

export const WIDTH = 800;
export const HEIGHT = 480;
export const BYTES_PER_ROW = WIDTH / 8;

// 32 px keeps the drawing clear of the firmware's battery icon
// (20x10 px, 8 px from the bottom-right corner).
export const MARGIN = 32;

// Scales and centers polylines into WIDTH x HEIGHT minus MARGIN on each
// side, preserving aspect ratio. Returns new polylines.
export function fitToPanel(polylines, margin = MARGIN) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const line of polylines) {
    for (const [x, y] of line) {
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
    }
  }
  const w = maxX - minX || 1;
  const h = maxY - minY || 1;
  const s = Math.min((WIDTH - 2 * margin) / w, (HEIGHT - 2 * margin) / h);
  const ox = (WIDTH - w * s) / 2 - minX * s;
  const oy = (HEIGHT - h * s) / 2 - minY * s;
  return polylines.map((line) => line.map(([x, y]) => [x * s + ox, y * s + oy]));
}

// Rasterizes polylines with Bresenham lines and a square pen of
// lineWidth (1 or 2) pixels. Returns a Uint8Array of WIDTH*HEIGHT/8 bytes.
export function rasterize(polylines, lineWidth = 2) {
  const buf = new Uint8Array(BYTES_PER_ROW * HEIGHT);
  // A 2 px pen covers the pixel and its right/bottom neighbours; shift by
  // half a pixel so the stroke stays centred on the path.
  const shift = (lineWidth - 1) / 2;

  const plot = (x, y) => {
    for (let dy = 0; dy < lineWidth; dy++) {
      const py = y + dy;
      if (py < 0 || py >= HEIGHT) continue;
      for (let dx = 0; dx < lineWidth; dx++) {
        const px = x + dx;
        if (px < 0 || px >= WIDTH) continue;
        buf[py * BYTES_PER_ROW + (px >> 3)] |= 0x80 >> (px & 7);
      }
    }
  };

  const line = (x0, y0, x1, y1) => {
    const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    const dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (;;) {
      plot(x0, y0);
      if (x0 === x1 && y0 === y1) return;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  };

  for (const pl of polylines) {
    let prev = null;
    for (const [x, y] of pl) {
      const p = [Math.round(x - shift), Math.round(y - shift)];
      if (prev) line(prev[0], prev[1], p[0], p[1]);
      else plot(p[0], p[1]);
      prev = p;
    }
  }
  return buf;
}

// SVG of the fitted drawing. `title` (the fish name) goes in <title>, which
// the gallery reads back. Coordinates are rounded to 0.1 px and use
// implicit lineto to keep the archive small.
export function toSVG(polylines, lineWidth = 2, title = '') {
  const num = (v) => String(Math.round(v * 10) / 10);
  const d = polylines
    .map((pl) => 'M' + pl.map(([x, y]) => `${num(x)} ${num(y)}`).join(' '))
    .join('');
  const esc = (t) => t.replace(/&/g, '&amp;').replace(/</g, '&lt;');
  return (
    `<svg xmlns="http://www.w3.org/2000/svg" width="${WIDTH}" height="${HEIGHT}" viewBox="0 0 ${WIDTH} ${HEIGHT}">` +
    (title ? `<title>${esc(title)}</title>` : '') +
    `<rect width="${WIDTH}" height="${HEIGHT}" fill="white"/>` +
    `<path d="${d}" fill="none" stroke="black" stroke-width="${lineWidth}" stroke-linecap="round" stroke-linejoin="round"/>` +
    `</svg>
`
  );
}

// Binary PBM (P4) uses the same packing as our buffer, so it is just a header.
export function toPBM(buf) {
  return Buffer.concat([Buffer.from(`P4\n${WIDTH} ${HEIGHT}\n`), buf]);
}
