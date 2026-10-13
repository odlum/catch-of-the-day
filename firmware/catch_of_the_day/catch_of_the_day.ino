// Catch of the Day: an e-ink wall picture that shows a new procedurally
// generated fish each morning.
//
// Each wake: read the battery; if critical, show the slashed battery icon
// (partial refresh, no WiFi) and sleep. Otherwise connect to WiFi, sync NTP,
// fetch the 800x480 1bpp latest.bin that the GitHub Actions renderer
// publishes (HTTPS to raw.githubusercontent.com, validated against ISRG
// Root X1), show it with a full refresh, and deep-sleep until
// WAKE_HOUR:WAKE_MINUTE local time. On failure, or if no new fish has been
// published yet, keep the current picture and retry every
// RETRY_INTERVAL_MIN, up to MAX_RETRIES.
//
// Board: XIAO_ESP32S3_PLUS (esp32 core 3.3.x), USB CDC On Boot: Enabled.
// Library: GxEPD2.

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <SPI.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <time.h>

#include <GxEPD2.h>  // lets the IDE find the library; the driver is in gdey/
#include <gdey/GxEPD2_750_GDEY075T7.h>

#include "board.h"
#include "isrg_root_x1.h"

#if __has_include("config.h")
#include "config.h"
#else
#error "Copy config.example.h to config.h and fill in your settings."
#endif

#if DEBUG && !ARDUINO_USB_CDC_ON_BOOT
#error "Set Tools > USB CDC On Boot > Enabled: UART0 shares pins with PWR_EN and the panel CS."
#endif

#if DEBUG
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) do { if (0) Serial.printf(__VA_ARGS__); } while (0)  // still type-checked
#endif

// --- Constants -------------------------------------------------------------

constexpr int WIDTH = 800;
constexpr int HEIGHT = 480;
constexpr size_t FRAME_BYTES = WIDTH * HEIGHT / 8;  // 48,000

// Battery icon: 20x10 outline with a terminal nub, 8 px from the
// bottom-right corner. The renderer keeps a 32 px margin, so it never
// overlaps the fish.
constexpr int ICON_W = 20;
constexpr int ICON_H = 10;
constexpr int ICON_MARGIN = 8;
constexpr int ICON_X = WIDTH - ICON_MARGIN - ICON_W;
constexpr int ICON_Y = HEIGHT - ICON_MARGIN - ICON_H;

// How long to try the cached AP/channel before falling back to a full scan.
constexpr uint32_t FAST_CONNECT_TIMEOUT_MS = 5000;
// The RTC drifts, so a scheduled wake can come a little early or late.
// Never schedule a wake closer than this, so one morning gets one fish.
constexpr time_t MIN_SLEEP_TO_SCHEDULED_S = 2 * 3600;
// Any time after this means the RTC has been set by NTP at some point.
constexpr time_t VALID_TIME = 1704067200;  // 2024-01-01

constexpr uint64_t WAKE_BUTTON_MASK = WAKE_BUTTON_PIN >= 0 ? 1ULL << (WAKE_BUTTON_PIN & 63) : 0;

enum Icon : uint8_t { ICON_NONE, ICON_LOW, ICON_CRITICAL };
enum FetchResult { FETCH_OK, FETCH_UNCHANGED, FETCH_FAILED };

// Declared up front: the IDE's auto-generated prototypes would drop [[noreturn]].
[[noreturn]] static void deepSleep(uint64_t seconds);
[[noreturn]] static void sleepUntilScheduled();

// --- State kept across deep sleep (reset on power loss) --------------------

RTC_DATA_ATTR static int32_t rtcChannel = 0;  // 0 = no AP cached
RTC_DATA_ATTR static uint8_t rtcBssid[6];
RTC_DATA_ATTR static uint8_t rtcRetries = 0;
RTC_DATA_ATTR static uint8_t rtcIcon = ICON_NONE;  // icon currently on the panel
RTC_DATA_ATTR static char rtcEtag[96] = "";       // ETag of the fish on the panel
RTC_DATA_ATTR static bool rtcLow = false;
RTC_DATA_ATTR static bool rtcCritical = false;

static uint8_t frame[FRAME_BYTES];  // 1 = black, row-major, MSB first
static GxEPD2_750_GDEY075T7 epd(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY);

// --- Battery -----------------------------------------------------------------

static uint32_t readBatteryMv() {
  digitalWrite(PIN_ADC_EN, HIGH);
  pinMode(PIN_ADC_EN, OUTPUT);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);
  delay(10);  // load switch turn-on and divider settling
  analogReadMilliVolts(PIN_BAT_ADC);  // discard the first conversion
  uint32_t sum = 0;
  constexpr int SAMPLES = 16;
  for (int i = 0; i < SAMPLES; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  digitalWrite(PIN_ADC_EN, LOW);
  return lroundf(sum / float(SAMPLES) * BATTERY_DIVIDER * BATTERY_CALIBRATION);
}

// Set below `threshold`, cleared only above threshold + hysteresis.
static void updateLatch(bool &latched, uint32_t mv, uint32_t threshold) {
  if (!latched && mv < threshold) latched = true;
  else if (latched && mv >= threshold + BATTERY_HYSTERESIS_MV) latched = false;
}

// --- Display -----------------------------------------------------------------

static void setPixel(int x, int y) {
  frame[y * (WIDTH / 8) + x / 8] |= 0x80 >> (x & 7);
}

static void drawIcon(Icon icon) {
  if (icon == ICON_NONE) return;
  constexpr int BODY_W = ICON_W - 2;  // the nub takes the last 2 columns
  for (int x = 0; x < BODY_W; x++) {
    setPixel(ICON_X + x, ICON_Y);
    setPixel(ICON_X + x, ICON_Y + ICON_H - 1);
  }
  for (int y = 0; y < ICON_H; y++) {
    setPixel(ICON_X, ICON_Y + y);
    setPixel(ICON_X + BODY_W - 1, ICON_Y + y);
  }
  for (int y = 3; y < ICON_H - 3; y++) {
    setPixel(ICON_X + BODY_W, ICON_Y + y);
    setPixel(ICON_X + BODY_W + 1, ICON_Y + y);
  }
  if (icon == ICON_CRITICAL) {
    // 45-degree slash, 2 px wide, bottom-left to top-right.
    for (int i = 0; i < ICON_H; i++) {
      setPixel(ICON_X + 4 + i, ICON_Y + ICON_H - 1 - i);
      setPixel(ICON_X + 5 + i, ICON_Y + ICON_H - 1 - i);
    }
  }
}

// `initial` = true clears the controller's buffers first (needed before a
// full refresh); false leaves them for us to write both old and new data.
static void panelBegin(bool initial) {
  digitalWrite(PIN_PWR_EN, HIGH);
  delay(10);  // load switch turn-on and controller power-up
  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  epd.init(0, initial, 10, false);
}

static void panelEnd() {
  epd.hibernate();
  epd.end();  // SPI off, CS/DC/RST to inputs
  // Don't back-power the unpowered panel through the SPI pins.
  pinMode(PIN_EPD_SCK, INPUT);
  pinMode(PIN_EPD_MOSI, INPUT);
  digitalWrite(PIN_PWR_EN, LOW);
}

// Full refresh of `frame` with the given icon drawn on top.
static void showFrame(Icon icon) {
  drawIcon(icon);
  panelBegin(true);
  epd.writeImage(frame, 0, 0, WIDTH, HEIGHT, true);  // panel RAM: 1 = white
  epd.refresh(false);
  panelEnd();
  rtcIcon = icon;
}

// Change only the battery icon, keeping the fish, without WiFi.
//
// The panel lost power since the last refresh, so its controller RAM is
// garbage. GxEPD2's partial refresh on this panel is a differential
// waveform over the whole screen: only pixels whose "previous" and
// "current" bits differ get driven. So write both buffers in full: blank
// except for the old icon (previous) and the new icon (current). The fish
// area is identical (white) in both and is left untouched.
static void showIconOnly(Icon icon) {
  memset(frame, 0, sizeof(frame));
  drawIcon(Icon(rtcIcon));
  panelBegin(false);
  epd.writeImageToPrevious(frame, 0, 0, WIDTH, HEIGHT, true);
  memset(frame, 0, sizeof(frame));
  drawIcon(icon);
  epd.writeImage(frame, 0, 0, WIDTH, HEIGHT, true);
  epd.refresh(ICON_X, ICON_Y, ICON_W, ICON_H);
  panelEnd();
  rtcIcon = icon;
}

// --- WiFi and time -----------------------------------------------------------

static bool waitForWifi(uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > timeoutMs) return false;
    delay(20);
  }
  return true;
}

static bool connectWifi() {
  WiFi.persistent(false);  // don't write credentials to flash every boot
  WiFi.mode(WIFI_STA);
  if (rtcChannel > 0) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, rtcChannel, rtcBssid);
    if (waitForWifi(FAST_CONNECT_TIMEOUT_MS)) return true;
    LOG("cached AP failed, scanning\n");
    WiFi.disconnect();
    rtcChannel = 0;
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (!waitForWifi(WIFI_TIMEOUT_MS)) return false;
  memcpy(rtcBssid, WiFi.BSSID(), sizeof(rtcBssid));
  rtcChannel = WiFi.channel();
  return true;
}

static bool timeValid() {
  return time(nullptr) > VALID_TIME;
}

static bool waitForNtp(uint32_t startMs) {
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - startMs > NTP_TIMEOUT_MS) return false;
    delay(20);
  }
  return true;
}

// Next WAKE_HOUR:WAKE_MINUTE local time at least MIN_SLEEP_TO_SCHEDULED_S
// away. mktime() with tm_isdst = -1 handles DST changes.
static time_t nextScheduledWake(time_t now) {
  struct tm t;
  localtime_r(&now, &t);
  time_t target;
  for (int day = 0;; day++) {
    struct tm d = t;
    d.tm_mday += day;
    d.tm_hour = WAKE_HOUR;
    d.tm_min = WAKE_MINUTE;
    d.tm_sec = 0;
    d.tm_isdst = -1;
    target = mktime(&d);
    if (target - now >= MIN_SLEEP_TO_SCHEDULED_S) return target;
  }
}

// --- HTTPS -------------------------------------------------------------------

static bool expired(uint32_t deadline) {
  return int32_t(millis() - deadline) > 0;
}

static bool readExactly(NetworkClient &c, uint8_t *dst, size_t len, uint32_t deadline) {
  while (len > 0) {
    const int avail = c.available();
    if (avail <= 0) {
      if (!c.connected() || expired(deadline)) return false;
      delay(5);
      continue;
    }
    const int got = c.read(dst, min(len, size_t(avail)));
    if (got <= 0) return false;
    dst += got;
    len -= got;
  }
  return true;
}

// GET FISH_URL into `frame`. Sends the ETag of the fish on the panel, so an
// unchanged latest.bin comes back as 304 with no body. On success `etag`
// gets the new ETag; the caller saves it once the fish is on screen.
static FetchResult fetchFish(char *etag, size_t etagSize) {
  NetworkClientSecure client;
  client.setCACert(ISRG_ROOT_X1);
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setUserAgent("catch-of-the-day");
  if (!http.begin(client, FISH_URL)) {
    LOG("bad FISH_URL\n");
    return FETCH_FAILED;
  }
  if (rtcEtag[0]) http.addHeader("If-None-Match", rtcEtag);
  const char *keep[] = {"ETag"};
  http.collectHeaders(keep, 1);

  const uint32_t start = millis();
  const int code = http.GET();
  LOG("HTTP %d after %lu ms\n", code, millis() - start);
  FetchResult result = FETCH_FAILED;
  if (code == HTTP_CODE_NOT_MODIFIED) {
    result = FETCH_UNCHANGED;
  } else if (code == HTTP_CODE_OK) {
    const int size = http.getSize();
    if (size != int(FRAME_BYTES)) {
      LOG("Content-Length %d, expected %u\n", size, unsigned(FRAME_BYTES));
    } else if (!readExactly(client, frame, FRAME_BYTES, millis() + HTTP_TIMEOUT_MS)) {
      LOG("body read failed\n");
    } else {
      strlcpy(etag, http.header("ETag").c_str(), etagSize);
      result = FETCH_OK;
      LOG("got fish in %lu ms\n", millis() - start);
    }
  } else if (code < 0) {
    LOG("%s\n", HTTPClient::errorToString(code).c_str());
  }
  http.end();
  return result;
}

// --- Sleep -------------------------------------------------------------------

[[noreturn]] static void deepSleep(uint64_t seconds) {
  if (WAKE_BUTTON_PIN >= 0) {
    // Don't wake straight back up if the button is still held.
    pinMode(WAKE_BUTTON_PIN, INPUT);  // external 10K pull-up
    const uint32_t start = millis();
    while (digitalRead(WAKE_BUTTON_PIN) == LOW && millis() - start < 5000) delay(10);
    esp_sleep_enable_ext1_wakeup(WAKE_BUTTON_MASK, ESP_EXT1_WAKEUP_ANY_LOW);
  }
  // Keep the panel supply and battery divider switched off while asleep.
  digitalWrite(PIN_PWR_EN, LOW);
  digitalWrite(PIN_ADC_EN, LOW);
  gpio_hold_en(gpio_num_t(PIN_PWR_EN));
  gpio_hold_en(gpio_num_t(PIN_ADC_EN));
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
  LOG("awake %lu ms; sleeping %llu s\n", millis(), seconds);
#if DEBUG
  Serial.flush();
#endif
  esp_deep_sleep_start();
}

[[noreturn]] static void sleepUntilScheduled() {
  rtcRetries = 0;
  if (!timeValid()) deepSleep(24 * 3600);  // never had NTP; best effort
  const time_t now = time(nullptr);
  const time_t target = nextScheduledWake(now);
#if DEBUG
  char buf[32];
  struct tm t;
  localtime_r(&target, &t);
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M %Z", &t);
  LOG("next wake %s\n", buf);
#endif
  deepSleep(target - now);
}

// --- Main --------------------------------------------------------------------

void setup() {
  // Release the pins held low through deep sleep, then keep them low.
  gpio_hold_dis(gpio_num_t(PIN_PWR_EN));
  gpio_hold_dis(gpio_num_t(PIN_ADC_EN));
  gpio_deep_sleep_hold_dis();
  digitalWrite(PIN_PWR_EN, LOW);
  pinMode(PIN_PWR_EN, OUTPUT);
  digitalWrite(PIN_ADC_EN, LOW);
  pinMode(PIN_ADC_EN, OUTPUT);

#if DEBUG
  Serial.begin(115200);
  while (!Serial && millis() < 2000) delay(10);
#endif
  setenv("TZ", TZ_INFO, 1);
  tzset();

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  LOG("\nwake: %s\n", cause == ESP_SLEEP_WAKEUP_TIMER  ? "timer"
                      : cause == ESP_SLEEP_WAKEUP_EXT1 ? "button"
                                                       : "power-on/reset");

  const uint32_t mv = readBatteryMv();
  updateLatch(rtcCritical, mv, BATTERY_CRITICAL_MV);
  updateLatch(rtcLow, mv, BATTERY_LOW_MV);
  LOG("battery %lu mV%s%s\n", mv, rtcLow ? " LOW" : "", rtcCritical ? " CRITICAL" : "");

  if (rtcCritical) {
    if (rtcIcon != ICON_CRITICAL) showIconOnly(ICON_CRITICAL);
    sleepUntilScheduled();
  }

  char etag[sizeof(rtcEtag)] = "";
  FetchResult result = FETCH_FAILED;
  if (connectWifi()) {
    LOG("WiFi up in %lu ms (ch %ld)\n", millis(), long(rtcChannel));
    const uint32_t ntpStart = millis();
    configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2);
    // TLS needs a sane clock. If the RTC already has one, let NTP finish
    // in the background while we fetch.
    if (timeValid() || waitForNtp(ntpStart)) result = fetchFish(etag, sizeof(etag));
    // Refresh the clock before computing the next wake (usually done by now).
    const bool synced = waitForNtp(ntpStart);
    LOG("NTP %s\n", synced ? "synced" : "failed, using RTC time");
  } else {
    LOG("WiFi failed\n");
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  if (result == FETCH_OK) {
    showFrame(rtcLow ? ICON_LOW : ICON_NONE);
    strlcpy(rtcEtag, etag, sizeof(rtcEtag));
    sleepUntilScheduled();
  }
  if (result == FETCH_UNCHANGED) {
    LOG("latest.bin unchanged; no new fish yet\n");
    // A button press that finds nothing new changes nothing.
    if (cause == ESP_SLEEP_WAKEUP_EXT1 && rtcRetries == 0) sleepUntilScheduled();
    // Otherwise today's fish isn't published yet: retry as for a failure.
  }
  if (rtcRetries < MAX_RETRIES) {
    rtcRetries++;
    LOG("no new fish, retry %u of %u\n", rtcRetries, MAX_RETRIES);
    deepSleep(RETRY_INTERVAL_MIN * 60ULL);
  }
  LOG("no new fish, giving up until tomorrow\n");
  sleepUntilScheduled();
}

void loop() {}  // never reached: setup() always ends in deep sleep
