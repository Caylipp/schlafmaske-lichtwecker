# Sunrise Sleep Mask

[Deutsche Version](README.de.md) · [User guide (German)](BEDIENUNGSANLEITUNG.md)

A sleep mask with built-in LEDs that simulates a **sunrise** before your alarm time and wakes you up gently. Hardware prototype with ESP32 firmware written in C++ (Arduino).

## Hardware

| Part | Details |
|---|---|
| Microcontroller | Seeed Studio XIAO ESP32C3 |
| LEDs | 2× WS2812 ring (8 LEDs each, one per eye), data on GPIO 2 |
| Button | on-board BOOT button (GPIO 9) |
| Battery | 500 mAh LiPo, charged via USB-C |

## How it works

- Gets the time over **Wi-Fi via NTP** and falls back to the internal RTC.
- Sleeps in **deep sleep** until 2 minutes before the sunrise starts.
- Sunrise in four phases (default alarm 08:00): red → orange (07:30), orange → peach (07:45), → white (07:55), pulsing white until 08:30. Brightness ramps up with gamma correction.
- A button press during the alarm stops it; a press shortly before it starts skips today's alarm.
- **Settings via phone**: alarm time, duration, brightness and Wi-Fi are changed on a small web page served by the ESP32 and stored in flash. No reflashing needed.

## Status

Prototype. v1 ran on hardware (NTP sync, RTC fallback, timer wake-up, sunrise, button cancel). v1.1 contains bug fixes from a code review. Battery life and a hardware upgrade plan (MOSFET-switched LED power) are analyzed in [docs/akku-und-ausbauplan.md](docs/akku-und-ausbauplan.md) (German).

## Build

Arduino IDE or `arduino-cli`, board `esp32:esp32:XIAO_ESP32C3`. Copy `lichtwecker/secrets.example.h` to `secrets.h` and add your Wi-Fi credentials.
