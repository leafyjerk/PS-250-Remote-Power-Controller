# PS-250 Remote Power Controller

An **integrated ESP32 + 2-channel relay board** that remotely powers an AMD **BC-250**
on and off, with a web interface, a physical power button, a **PS5 DualSense
controller wake**, and an animated **NeoPixel** strip that lights up while the machine
is running.

> **Credits / Attribution**
> This is a modified version of [BC-250-PC-Remote-Control](https://github.com/PetteriLah/BC-250-PC-Remote-Control)
> by **[PetteriLah](https://github.com/PetteriLah)** — all the original work and core
> design are his. This fork adds a NeoPixel animation, English translation of the
> source, and the hardware fixes documented below. See [`NOTICE`](NOTICE).

---

## Features

- **Remote power control** — turn the BC-250 on/off via web UI or a physical momentary button.
- **PS5 controller wake** — power the machine on with a paired DualSense (PS button).
- **NeoPixel lighting** — WS2812 strip animates only **while the BC-250 is actually running**.
- **Web configuration** — WiFi setup with network scanning; AP mode on first boot.
- **MAC-address locking** — restrict controller access to one specific DualSense.
- **OTA updates** — firmware updates via the web interface.

---

## ⚠️ Read this first: the relay board and its quirks

### The board
This project is built on a common **"ESP32 2-channel relay" board** — a single PCB with
an ESP32-WROOM-32, two blue **SONGLE SRD-05VDC-SL-C** relays, screw terminals for the
relay contacts, a `VCC / GND / 5V` power header, and `IO0` + `EN` buttons.

> **How to tell if you have the same board:** they're sold on **Amazon (USA) for ~$20**,
> usually listed as something like *"ESP32 WROOM 2-channel relay module / development
> board."* If your board has the ESP32 and **two relays integrated on one PCB** (not a
> separate ESP32 + separate relay module), this is almost certainly it, and the GPIO
> assignments below will match. See the wiring image for reference.

### The two quirks that will bite you (already handled — but you must wire the pull-ups)

1. **The relays are ACTIVE-LOW.** Driving a relay input LOW *energizes* it. The firmware
   is written for this (see `OPTO_ON`/`EXTRA_ON` macros in `pins.h`). If you ever use an
   active-high relay board, flip those macros to `HIGH`.

2. **The relay inputs float during the ~2 s ESP32 boot**, which briefly energizes the
   active-low relays — that would auto-start the PSU and hold the BC-250 power button down
   on every boot. **You MUST add a pull-up resistor on each relay GPIO** so they stay OFF
   during boot:

   | Resistor | From | To | Purpose |
   |---|---|---|---|
   | ~10 kΩ | **GPIO 16** (OPTO / relay 1) | **3.3 V** | keep PS_ON relay off during boot |
   | ~10 kΩ | **GPIO 17** (EXTRA / relay 2) | **3.3 V** | keep power-button relay off during boot |

   Without these two pull-ups the board auto-starts the BC-250 on plug-in and the PS5
   wake won't work (it can only detect the controller while the machine is off). **They
   are required**, not optional.

---

## Pin map (`pins.h`)

| GPIO | Name | Connects to |
|---|---|---|
| 16 | `OPTO_PIN` | **Relay 1** input — relay grounds PSU **PS_ON** (green) to turn the PSU on |
| 17 | `EXTRA_PIN` | **Relay 2** input — relay shorts the **BC-250 power-button** pads |
| 4  | `PC_MONITOR_PIN` | **BC-250 TPMS1 pin 9** (3 V "running" signal) — tells the firmware the BC-250 is on |
| 22 | `BUTTON_PIN` | Momentary power button (button **NO → GPIO 22**, **COM → GND**) |
| 23 | `POWER_LED_PIN` | Power-button LED |
| 2  | `STATUS_LED_PIN` | Optional status LED |
| 19 | `NEOPIXEL_PIN` | NeoPixel strip **DIN** |

---

## Wiring

### PSU (standard ATX, e.g. FSP500-30AS)
- **PS_ON (green)** and a **PSU GND (black)** → **Relay 1** contacts, wired so the relay
  **grounds PS_ON when activated** (PSU turns on). With the active-low relay + firmware
  inversion, the relay is energized only during a commanded power-on.
- **+5VSB (purple)** → powers the **ESP32 board** (`5V`/`VIN`) and the **NeoPixel strip**.
  5VSB is always on, so the ESP32 (and the controller-wake) work while the BC-250 is off.
- **PSU GND (black)** → ESP32 GND. **All grounds must be common.**

### BC-250
- **TPMS1 pin 9 (3 V)** → **GPIO 4** (the "is it running?" signal — required for stable
  operation, NeoPixel timing, and power-on confirmation).
- **TPMS1 pin 17 (GND)** → common ground.
- **Power-button pads** → **Relay 2** contacts (relay shorts them to "press" the button).

### Momentary power button
- **NO → GPIO 22**, **COM → GND** (NC unused). The button LED → GPIO 23.

### NeoPixel strip (WS2812, 22 LEDs)
- **DIN → GPIO 19** (LED count = `NUM_PIXELS` in `neopixel.h`, brightness = `MASTER`).
- **5 V → PSU 5VSB**, **GND → common ground**.
- The strip is **always powered** (5VSB); the firmware animates it only while the BC-250
  is running and blanks it when off.
- Recommended: **330–470 Ω** resistor in series on the data line, **~1000 µF** cap across
  the strip's 5V/GND, and a 3.3 V→5 V level shifter if the data signal is unreliable.
- 22 LEDs at full white ≈ **1.3 A @ 5 V** — make sure 5VSB can supply it (it's blanked
  when the machine is off, so idle standby draw is just ~20 mA).

### Resistor summary (don't skip these)
- **GPIO 16 → 3.3 V, ~10 kΩ** (relay 1 pull-up) — **required**
- **GPIO 17 → 3.3 V, ~10 kΩ** (relay 2 pull-up) — **required**
- **NeoPixel data series resistor 330–470 Ω** — recommended
- **NeoPixel 1000 µF cap** across 5V/GND — recommended
- GPIO 4 uses the ESP32's **internal** pull-down (set in firmware) — just wire pin 9.

---

## Software setup

### 1. Add the Bluepad32 board package
Bluepad32 is **not** a library — it's a special ESP32 board core. In Arduino IDE:
**File → Preferences → Additional Boards Manager URLs**, add (comma-separated):
```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
```
Then **Tools → Board → Boards Manager**, search **`esp32_bluepad32`**, install it, and
select your board from the **esp32_bluepad32** group (not the plain `esp32` group).

### 2. Install libraries (Library Manager)
- **ArduinoJson** (Benoit Blanchon)
- **Adafruit NeoPixel**
- *(LittleFS and Bluepad32 come with the board core — no separate install.)*

### 3. Flash the firmware
- Open `ota_pc_remote/ota_pc_remote.ino`. All the `.h` tabs load automatically.
- Select a **partition scheme with a filesystem** (default 4 MB "with spiffs" is fine).
- Upload. **Note:** these boards have no auto-reset for flashing — hold **IO0 to GND**,
  tap **EN**, then upload (manual download mode).

### 4. Upload the web UI (LittleFS) — required, separate step
The web pages live in `ota_pc_remote/data/` on the LittleFS partition. Flash them with the
**"Arduino ESP32 LittleFS Data Upload"** tool (Arduino IDE 2.x: the *arduino-littlefs-upload*
plugin), or with `esptool` / `mklittlefs` from the command line. Without this the web UI
won't load (you'll see `index.html NOT FOUND` on serial).

---

## First-time use

1. Power on. The ESP32 boots in **AP mode** — connect to WiFi **`BC-250-POWER-CONTROL`**
   (no password) and open **http://192.168.4.1**.
2. **SETUP page** → scan and select your **2.4 GHz** WiFi (ESP32 is 2.4 GHz only), enter
   the password, save. It reboots onto your network.
3. **PS5 controller:** on the SETUP page, enable PS5 support. Put the DualSense in
   **pairing mode** (hold **PS + Create** until the light bar double-flashes), with the
   **machine off**, and lock it. *(A new ESP32 has a new Bluetooth address, so the
   controller must be re-paired to it.)*

### Using ONE controller for both wake and gaming (important)

A Bluetooth controller normally bonds to only **one** host at a time, and this setup has
**two** hosts: the **ESP32** (used to *wake* the machine while it's off) and the
**BC-250's own Bluetooth** (used by Steam to *game* while it's on). Pairing to one
overwrites the other — so out of the box you'd have to re-pair the controller every time
you switch between waking and playing.

**The fix: update your DualSense firmware.** Recent DualSense firmware adds **multiple
saved Bluetooth pairings with button-shortcut switching**, so the controller can stay
paired to *both* devices at once:

- Update the controller by plugging it into a **PS5**, or via the **PlayStation
  Accessories** app on Windows.
- Pair it to **both** the **ESP32** (machine off → wakes it) **and** the **BC-250**
  (machine on → Steam gaming).
- Switch between the saved devices with the controller's **button combo** (see Sony's
  instructions for the exact shortcut).

Result: **one controller wakes the machine *and* plays games**, no re-pairing.

---

## How it works

- **Boot:** PSU stays **off** (pull-ups hold the relays off during boot). Bluetooth comes
  up clean, ready to detect the controller.
- **Power on:** button or controller → relay grounds PS_ON (PSU on) + taps the BC-250
  power button. When the BC-250 boots and asserts **pin 9**, the firmware confirms it's
  running and keeps it on.
- **PS5 wake** only works while the machine is **off** (the firmware disables Bluetooth and
  rejects controllers while it's on — the controller's job is to *wake* it).
- **Power off:** button / web / OS shutdown → relay drops PS_ON. The ESP32 then **restarts**
  so Bluetooth re-initializes cleanly, ready for the next controller wake.
- **NeoPixel:** animates only while pin 9 is HIGH (BC-250 running); blanked otherwise.

---

## Changes in this fork

- Translated all source comments and serial/log strings from Finnish to English.
- Added the **NeoPixel** animation module (`neopixel.h`), driven on GPIO 19.
- Added relay-polarity macros (`OPTO_ON`/`OPTO_OFF`/`EXTRA_ON`/`EXTRA_OFF`) for the
  active-low relay board.
- `PC_MONITOR_PIN` set to `INPUT_PULLDOWN` (stable PC-off reading).
- Restart-for-clean-Bluetooth after a button/web shutdown (so PS5 wake is repeatable).
- Attribution to the original author.

---

## Troubleshooting

- **BC-250 auto-starts on plug-in / boots in a loop** → the relay GPIO pull-ups (GPIO 16/17
  → 3.3 V) are missing. This is the #1 gotcha with these active-low boards.
- **PS5 controller won't pair / wake** → the machine must be **off** (Bluetooth is disabled
  while it's on), and a new ESP32 needs the controller re-paired (pairing mode). After a
  shutdown, give it ~3 s to reboot/re-init Bluetooth before waking.
- **Controller keeps needing to be re-paired when switching between waking and gaming** →
  that's the one-controller / two-Bluetooth-hosts conflict. Update the DualSense firmware
  for multi-device pairing and switch with the button combo — see
  *"Using ONE controller for both wake and gaming"* above.
- **Web page blank / `NOT FOUND` on serial** → the LittleFS `data/` upload was skipped.
- **WiFi won't connect** → must be a **2.4 GHz** network; double-check the password.
- **Can't flash** → hold IO0 to GND + tap EN for manual download mode; these boards don't
  auto-reset for flashing.

<img width="909" height="541" alt="wiring reference" src="https://github.com/user-attachments/assets/8f6ec507-48f0-44cf-aa08-43292f0b47fc" />
</content>
