// Copy this file to config.h (gitignored) and fill in your values.
#pragma once

// --- WiFi -----------------------------------------------------------------
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-password"

// --- Fish -----------------------------------------------------------------
// latest.bin as published by the GitHub Actions renderer. The repo must be
// public. Replace USER and REPO (and main, if your default branch differs).
#define FISH_URL "https://raw.githubusercontent.com/USER/REPO/main/docs/latest.bin"

// --- Schedule -------------------------------------------------------------
#define TZ_INFO "EST5EDT,M3.2.0,M11.1.0"   // POSIX TZ string
#define WAKE_HOUR 6                        // local time of the daily fish
#define WAKE_MINUTE 0
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.google.com"

// If the fetch fails, or latest.bin hasn't changed since the last fish (the
// workflow ran late), keep the current picture and retry this often, up to
// MAX_RETRIES times, then wait for the next day. The workflow runs at
// 08:00 UTC; keep WAKE_HOUR at least an hour or two after that.
#define RETRY_INTERVAL_MIN 30
#define MAX_RETRIES 3

// --- Battery (millivolts) -------------------------------------------------
#define BATTERY_LOW_MV 3500        // below: draw the empty-battery icon
#define BATTERY_CRITICAL_MV 3300   // below: no WiFi, slashed icon only
#define BATTERY_HYSTERESIS_MV 50   // must rise this far above a threshold to clear it
// Multiply readings by this to match a multimeter (e.g. 1.02).
#define BATTERY_CALIBRATION 1.0f

// --- Button ---------------------------------------------------------------
// Press to fetch latest.bin right away (shows it if it is newer than the
// fish on screen). Set to -1 to disable, or use
// PIN_BUTTON2 / PIN_BUTTON3 (see board.h) for one of the other keys.
#define WAKE_BUTTON_PIN PIN_BUTTON1

// --- Timeouts (ms) --------------------------------------------------------
#define WIFI_TIMEOUT_MS 15000
#define NTP_TIMEOUT_MS 10000
#define HTTP_TIMEOUT_MS 15000

// --- Debug ----------------------------------------------------------------
// 1 = log to USB serial at 115200 (waits up to 2 s for a host on boot).
#define DEBUG 0
