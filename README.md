# Catch of the Day

An e-ink wall picture that shows a new procedurally generated fish each
morning. The fish come from LingDong-'s
[fishdraw](https://github.com/LingDong-/fishdraw) (MIT, vendored unmodified
in `render/vendor/fishdraw/`).

```
GitHub Actions, 08:00 UTC ──▶ render/render.mjs ──▶ commit docs/latest.bin, SVGs, gallery
                                                              │
frame wakes 06:00 local ──HTTPS GET raw.githubusercontent.com─┘──▶ full refresh ──▶ deep sleep
```

## Part 1: Renderer (`render/`, `.github/workflows/render.yml`)

| File | Purpose |
| --- | --- |
| `render/render.mjs` | Draws a fish and writes everything under `docs/` |
| `render/fishdraw-loader.mjs` | Loads `fishdraw.js` as a library (see below) |
| `render/raster.mjs` | Fits polylines to 800x480 (32 px margin), rasterizes, SVG/PBM output |
| `render/test.mjs` | Local checks |
| `render/vendor/fishdraw/` | `fishdraw.js` and its `LICENSE`, unmodified from upstream `a514372` |
| `.github/workflows/render.yml` | Daily run at 08:00 UTC, plus manual runs |
| `docs/` | Published output, served by raw.githubusercontent.com and GitHub Pages |

No npm dependencies. Requires Node.js 24 (the workflow uses the same).

### Output

| File | Contents |
| --- | --- |
| `docs/latest.bin` | 800x480, 1bpp, row-major, MSB first, 1 = black: 48,000 bytes. What the frame downloads |
| `docs/latest.svg` | The same fish as vector |
| `docs/archive/YYYY-MM-DD.svg` | One per day (UTC date). The fish name is in its `<title>` |
| `docs/index.html` | Gallery of the archive, newest first, for GitHub Pages |
| `render/preview.pbm` | The bitmap as an image, for local checks (gitignored) |

### Running locally

```bash
node render/test.mjs
node render/render.mjs
```

`test.mjs` checks four things, writing only to a temp directory:

- the loader's output matches fishdraw's own CLI
- date seeding is stable
- the bitmap is exactly 48,000 bytes and stays inside the margin
- re-running for the same date changes nothing

`render.mjs` options:

| Option | Effect |
| --- | --- |
| *(none)* | Today's fish. The UTC date picks the fish, so a re-run for the same date gives identical files |
| `--random` | A new random fish |
| `--name "Biggus fishus"` | The fish with that name (a fish's name determines its drawing) |
| `--date 2026-10-04` | Seed with, and file under, a different date |
| `--width 1` | 1 px pen instead of the default 2 px |
| `--out dir` | Write somewhere other than `docs/` |

### Workflow

- **Schedule:** `cron: '0 8 * * *'`, which is 03:00 EST / 04:00 EDT. It draws
  that UTC date's fish.
- **Run workflow** (manual): optionally type a fish name; leave it blank for
  a new random fish. A manual run replaces that day's `latest.bin` and
  archive entry.
- **Steps:**
  1. Check out the repo.
  2. Set up Node 24.
  3. Render.
  4. `git add docs`. If nothing is staged (for example, a repeated scheduled
     run), skip the commit. Otherwise commit as `github-actions[bot]` with
     the message `Fish of <date>: <name>`, and push.

### GitHub setup

1. Create a **public** repository and push this project to `main`. The
   frame downloads `latest.bin` without credentials, which only works for
   public repos.
2. The workflow asks for `contents: write` itself. If the push step fails
   with a 403, open *Settings > Actions > General > Workflow permissions*,
   choose **Read and write**, and save.
3. Run it once: *Actions > Render fish > Run workflow*.
4. Optional gallery: under *Settings > Pages*, set *Deploy from a branch* to
   `main` and the `/docs` folder. The gallery is then at
   `https://USER.github.io/REPO/`.
5. In the firmware's `config.h`, set
   `FISH_URL` to `https://raw.githubusercontent.com/USER/REPO/main/docs/latest.bin`.

Notes:

- GitHub can start scheduled runs late, especially on the hour. 08:00 UTC
  leaves two to three hours before a 06:00 Eastern wake. If the fish still
  isn't there, the frame retries (see Part 2).
- raw.githubusercontent.com caches files for up to 5 minutes
  (`Cache-Control: max-age=300`).
- In public repos, GitHub disables scheduled workflows after 60 days
  without repository activity. The daily bot commits should count as
  activity. If the workflow is ever disabled, GitHub sends an email; turn
  it back on from the Actions tab.
- History grows by one bitmap and one SVG a day, roughly 10–20 MB a year
  after Git's compression.

### Notes on fishdraw

`fishdraw.js` keeps its PRNG state and Perlin noise table in top-level
variables, and `main()` seeds the name generator with
`Math.random() * 10000`. Used as-is, that would allow only 10,000 possible
fish, and a second fish in the same process would come out differently
from the first.

`fishdraw-loader.mjs` gets around both without editing the file:

- It compiles the file as a function body with `vm.compileFunction`, plus a
  one-line tail that can set the PRNG seed, and calls it once per fish, so
  every fish starts from clean state.
- The seed is a SHA-256 hash of the date, or a random 31-bit value.
- A name always draws the same fish, identical to
  `node fishdraw.js --seed "<name>" --format json`.

The fish name is lettered under the drawing, in fishdraw's own Hershey
font. The default 2 px pen reads well from across a room. 1 px keeps more
of the fine scale and stipple texture, but looks faint.

## Part 2: Firmware (`firmware/catch_of_the_day/`)

| File | Purpose |
| --- | --- |
| `catch_of_the_day.ino` | The whole wake cycle |
| `board.h` | Pin map, from Seeed's schematic |
| `isrg_root_x1.h` | ISRG Root X1, the only trusted CA |
| `config.example.h` | Settings template. Copy to `config.h` (gitignored) |

### Hardware notes

From Seeed's driver board schematic (`XIAO_ePaper_driver_board_sch.pdf`,
"XIAO ePaper Monitor Kit v1.0"):

| Signal | GPIO | Notes |
| --- | --- | --- |
| EPD SCK / MOSI / CS | 7 / 9 / 44 | SPI0. CS is D7 (U0RXD) |
| EPD DC / RST / BUSY | 10 / 38 / 4 | BUSY is low while busy |
| PWR_EN | 43 | D6 (U0TXD). Load switch for the panel's 3.3 V; 100K pull-down |
| BAT_ADC | 1 | VBAT through a 10K/10K divider (x2) |
| ADC_EN | 6 | Load switch that connects VBAT to the divider; 100K pull-down |
| Buttons | 2, 3, 5 | Active low, 10K pull-ups. All can wake from deep sleep |

The panel is a GDEY075T7 (UC8179). The GxEPD2 class is `GxEPD2_750_GDEY075T7`.
No hardware mod is needed for battery sensing. Because `PWR_EN` and the
panel CS sit on the UART0 pins, debug output must go over USB CDC.

### Arduino IDE setup

1. **Boards Manager:** add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json` under
   *File > Preferences > Additional boards manager URLs*, then install
   **esp32 by Espressif Systems**, version 3.3.x (3.3.12 tested). The
   download is several GB.
2. **Library Manager:** install **GxEPD2** by Jean-Marc Zingg (1.6.9 tested).
   Accept its dependencies (Adafruit GFX Library, Adafruit BusIO).
3. **Tools menu:**
   - Board: **XIAO_ESP32S3_PLUS**
   - USB CDC On Boot: **Enabled** (the default for this board)
   - Leave the rest at defaults (PSRAM is not needed)
4. Copy `config.example.h` to `config.h` and fill in:
   - WiFi SSID and password
   - `FISH_URL`
   - Optionally the thresholds, wake time, button and `DEBUG`

### Flashing

1. Open `firmware/catch_of_the_day/catch_of_the_day.ino`.
2. Connect the XIAO by USB-C and pick its port.
3. Click Upload.

If no port shows up, the board is probably in deep sleep, with USB off.
Hold **BOOT** on the XIAO, tap **RESET**, release BOOT, then upload. Press
RESET again afterwards to run the sketch.

Right after flashing (or any reset), the device fetches and shows the
current `latest.bin`. After that it wakes at 06:00 local time. Set
`DEBUG 1` to watch a cycle in the Serial Monitor at 115200. It logs:

- the battery voltage
- WiFi and HTTP timings
- the HTTP status
- `awake N ms; sleeping S s` at the end of each cycle

### Behavior

| Situation | What happens |
| --- | --- |
| Normal wake | Battery read (16 samples) → WiFi (cached BSSID/channel, full scan as a fallback) → NTP in the background → HTTPS GET with `If-None-Match` → full refresh → sleep until 06:00 |
| `latest.bin` unchanged (304) | The workflow hasn't published today's fish yet. Picture unchanged; retried like a failure |
| Fetch fails (WiFi, TLS, HTTP, wrong size) | Picture unchanged. Retry in 30 min, up to 3 times, then wait for 06:00 |
| Button (BUTON1 by default) | Fetches `latest.bin` right away and shows it if it's newer than the fish on screen. It doesn't make a new fish; use *Run workflow* for that, then press the button |
| Battery < 3.50 V | Next fish is drawn with an empty battery outline in the bottom-right corner. Clears above 3.55 V |
| Battery < 3.30 V | No WiFi. Partial refresh of just the icon to a slashed outline (only once), then sleep until 06:00. Clears above 3.35 V |

How the ETag check works: the frame keeps the `ETag` of the fish it is
showing in RTC memory and sends it as `If-None-Match`. An unchanged file
costs a 304 with no download and no refresh. After a reset the ETag is
forgotten, so the next fetch always downloads.

The sleep length is recomputed from the clock every cycle, and the clock is
synced by NTP on every wake that has WiFi. The ESP32-S3's RC-based RTC can
drift by around a percent, so 06:00 may land some minutes early or late. A
wake is never scheduled less than 2 h away, so an early wake can't cause a
second fetch the same morning.

How the icon-only refresh avoids disturbing the fish: the panel's power is
switched off between wakes, so its controller memory is lost. The firmware
rewrites both the "previous" and "current" buffers as blank, apart from the
old and new icon, so the differential refresh only drives the icon's pixels.

### TLS root

`raw.githubusercontent.com` currently presents a 90-day Let's Encrypt
certificate. Checked 2026-10-03:

```
*.github.io  ←  Let's Encrypt YR1  ←  ISRG Root YR  ←  ISRG Root X1
```

The server sends Root YR cross-signed by X1. Root YR isn't in trust stores
yet, so the firmware trusts only **ISRG Root X1**, which is valid until 2035.
Let's Encrypt's leaf and intermediate rotations don't affect this.

If GitHub ever switches CA, every fetch fails with `HTTP -1`. To check the
current chain, run:

```bash
openssl s_client -connect raw.githubusercontent.com:443 -servername raw.githubusercontent.com -showcerts </dev/null
```

Then put the new root's PEM in place of the one in `isrg_root_x1.h`.

### Measuring sleep current

Measure on battery, with USB unplugged. USB powers the board and charger
directly, so a reading taken with USB connected means nothing.

1. Put the meter in series with the battery's positive lead. Use a JST
   extension or an adapter; don't cut the battery leads.
2. Power on. The board fetches, refreshes and goes to sleep. The panel
   stops flashing when the refresh is done; give it 2 more seconds.
3. Read the current.

Range matters. A wake peaks at 300–500 mA (WiFi TX), but sleep is in the
tens of µA. A multimeter's µA range has a high burden voltage and will
brown the board out if it's in circuit during a wake. So either:

- **Multimeter:** clip a jumper across the meter's terminals while the board
  wakes. Once it's asleep, select the µA range and remove the jumper. Put
  the jumper back before pressing the button or reset.
- **Power profiler** (Nordic PPK2 in ammeter mode, Joulescope, µCurrent):
  record a whole cycle. That gives both the sleep current and the charge
  per wake (mAh), which together give the battery life:

  ```
  days ≈ 2000 mAh × 0.85 / (I_sleep_mA × 24 + mAh_per_wake + 1.5)
  ```

  The 0.85 is the usable capacity down to 3.3 V. The 1.5 mAh/day is Li-ion
  self-discharge (about 2–3% a month), which at these currents is the
  largest single drain.

What to expect:

- The ESP32-S3 itself sleeps at around 8–15 µA. The driver board adds the
  buck-boost regulator, charger and supervisor; their share is what this
  measurement finds out.
- In sleep, the panel and the battery divider are both switched off
  (`PWR_EN` and `ADC_EN` are held low).
- A wake takes a few seconds (WiFi, TLS, a 48 KB download, a full refresh)
  and costs about 0.1 mAh.

At 30 µA sleep that's about 0.7 + 0.1 + 1.5 ≈ 2.3 mAh/day, or roughly
2 years per charge.
