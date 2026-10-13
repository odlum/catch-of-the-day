// Pin map for the Seeed TRMNL 7.5" (OG) DIY Kit: XIAO ePaper driver board
// with a XIAO ESP32-S3 Plus. Taken from Seeed's schematic
// "XIAO ePaper Monitor Kit v1.0" (XIAO_ePaper_driver_board_sch.pdf,
// sheets /XIAO/ and /Power/).
#pragma once

// E-paper panel: GDEY075T7, 800x480 B/W, UC8179 controller, on SPI0.
// GxEPD2 driver class: GxEPD2_750_GDEY075T7.
constexpr int PIN_EPD_SCK = 7;    // D8,  SPI0_CLK
constexpr int PIN_EPD_MOSI = 9;   // D10, SPI0_MOSI
constexpr int PIN_EPD_CS = 44;    // D7,  SPI0_CS  (U0RXD)
constexpr int PIN_EPD_DC = 10;    // EDP_DC   (B2B pad)
constexpr int PIN_EPD_RST = 38;   // EDP_RES  (B2B pad)
constexpr int PIN_EPD_BUSY = 4;   // D3,  EDP_BUSY, low while busy

// PWR_EN drives a TPS22916 load switch (U16) from +3.3V to VCC_3V3, which
// feeds the panel and the board's SPI flash/font chips. 100K pull-down
// (R26), so the panel is unpowered unless we drive this high. It is also
// U0TXD, so Serial must be USB CDC, never UART0.
constexpr int PIN_PWR_EN = 43;    // D6

// Battery: ADC_EN drives a second TPS22916 (U17) that connects VBAT to a
// 10K/10K divider (R28/R29, 1%) on BAT_ADC. 100K pull-down (R27), so the
// divider draws nothing while we are asleep.
constexpr int PIN_BAT_ADC = 1;    // D0/A0
constexpr int PIN_ADC_EN = 6;     // D5
constexpr float BATTERY_DIVIDER = 2.0f;

// User buttons (K2..K4): 10K pull-ups to ESP_3V3, pressed = LOW. All three
// are RTC GPIOs, so any of them can wake the chip from deep sleep.
constexpr int PIN_BUTTON1 = 2;    // D1, net BUTON1
constexpr int PIN_BUTTON2 = 3;    // D2, net BUTON2
constexpr int PIN_BUTTON3 = 5;    // D4, net BUTON3
