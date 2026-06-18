
# BC-250 REMOTE PSU POWER CONTROLLER
ESP32 2-Channel 5V Relay Module-based device for remote control of the BC-250 atx power supply with web interface and PS5 controller support.

> **Credits / Attribution**
> This project is a modified version of [BC-250-PC-Remote-Control](https://github.com/PetteriLah/BC-250-PC-Remote-Control)
> by **[PetteriLah](https://github.com/PetteriLah)**. All original work and the core design are his.
> This fork contains my own modifications. See [`NOTICE`](NOTICE) for details.

<img width="909" height="541" alt="wires" src="https://github.com/user-attachments/assets/8f6ec507-48f0-44cf-aa08-43292f0b47fc" />

Key Features

Remote Power Control: Turn PC on/off via web interface or physical button
PS5 Controller Integration: Use DualSense controller to power on PC with PS button
WiFi Configuration: Web-based setup with network scanning
MAC Address Locking: Restrict controller access to specific devices
Over-the-Air Updates: Firmware updates via web interface
NeoPixel Lighting: Animated WS2812 strip that lights up while the PC is on

# Installation

Before installing libraries, you need to add the [ESP32_bluepad32](https://github.com/ricardoquesada/bluepad32) platform to your Arduino IDE.

Open Arduino IDE.
Go to File > Preferences (or Arduino IDE > Settings on macOS).
In the "Additional Boards Manager URLs" field, paste the following URL:
```
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json ,https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json 
```

Go to Tools > Board > Boards Manager.
In the search bar, type "esp32_bluepad32".


# Install libraries
- [LittleFS (for ESP32)](https://github.com/lorol/LITTLEFS) 
- ArduinoJson 
- Adafruit NeoPixel 

# NeoPixel wiring
- WS2812 / NeoPixel strip, 22 LEDs, 5V.
- Data (DIN) -> ESP32 **GPIO 19** (`NEOPIXEL_PIN` in pins.h; LED count is `NUM_PIXELS` in neopixel.h).
- Strip 5V is powered from the PSU 5V rail, which only turns on once the ESP32
  switches the PSU on - so the strip only has power while the system is running.
  The firmware drives the animation unconditionally; it is only ever visible when
  the strip is powered.
- The strip GND **must** share a common ground with the ESP32.
- Recommended: ~330-470 ohm resistor in series on the data line, a ~1000uF cap across
  the strip 5V/GND, and a 3.3V->5V level shifter if the data signal is unreliable.
- 22 LEDs at full white draw ~1.3A @ 5V; size the rail accordingly.


# SETUP Notes
- WIFI AP mode is laggy, be patient. IP: http://192.168.4.1
- https not work!
- MAC-Lock in setup. Lock current cotroller button, is use short time when controller is connected to esp32, be quick.
- Testing without TPMS1 pin 9 connected causes instability.
  
- 
<img width="294" height="542" alt="kuva" src="https://github.com/user-attachments/assets/1544a9e2-1a29-4ba2-bede-efac3149f9f3" />
Web
