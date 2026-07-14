#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include "LittleFS.h"
#include <Bluepad32.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

#include "version.h"
#include "pins.h"
#include "neopixel.h"
#include "ps5_simple.h"

// IMPORTANT: Include pc_control.h FIRST so PowerState is known
#include "pc_control.h"

// Global variables
WebServer server(80);

bool pcIsOn = false;
bool shutdownRequested = false;
bool forceShutdown = false;
unsigned long forceShutdownStartTime = 0;
const unsigned long forceShutdownDuration = 5000;

// WiFi variables
String wifiSSID = "";
String wifiPassword = "";
bool wifiConfigured = false;
bool apMode = false;

// PS5 variables
String ps5MacAddress = "";
bool ps5Enabled = false;
bool ps5AutoConnect = false;
unsigned long lastPS5ConnectionAttempt = 0;

// INA219 current sensing (Steam Controller puck)
Adafruit_INA219 ina219;
bool ina219Present = false;
unsigned long lastCurrentSample = 0;
const unsigned long currentSampleInterval = 20;   // 50 Hz sampling
bool inaLogging = false;   // set true to stream "INA,millis,mA" for tuning

// ---------------- PUCK WAKE DETECTION ----------------
// Powers the console on based on the Steam Controller puck's current draw,
// measured by an INA219 in the puck's 5V standby feed.
//
// Measured states (mA):
//   puck idle (controller asleep/absent): flat ~5.6
//   controller docked & topped off:       flat ~14
//   Steam button press / controller wake: ALTERNATING 5.6 <-> 10-17 spikes
//
// TRIGGER MODES:
//   PUCK_MODE_CONTROLLER_ONLY    - Steam button press only. The wake chatter
//       must persist PUCK_SUSTAIN_MS; dock/undock transitions self-clear in
//       ~1 s and never fire. Most conservative (toddler-proof-ish).
//   PUCK_MODE_CONTROLLER_AND_PUCK - Steam button press OR lifting the
//       controller off the charging puck. Lift-off is detected as a
//       transition out of the stable flat-high "docked" state.
#define PUCK_MODE_CONTROLLER_ONLY     1
#define PUCK_MODE_CONTROLLER_AND_PUCK 2
const int PUCK_MODE = PUCK_MODE_CONTROLLER_ONLY;

const float PUCK_SPIKE_mA = 9.0;    // sample above this counts as a radio spike
const float PUCK_QUIET_mA = 8.0;    // sample below this counts as idle floor
const int   PUCK_WINDOW   = 50;     // 1 second of samples at 50 Hz
// Spike density separates the signals (measured):
//   idle controller pinging the puck sporadically:  5-8 spikes/window
//   genuine Steam-button wake chatter:             12-20 spikes/window
const int   PUCK_MIN_SPIKES = 12;   // spikes required in window (wake pattern)
const int   PUCK_MIN_LOWS   = 20;   // lows required in window (proves alternation)
// Dock/undock transitions produce a mixed window for at most ~1 s (until old
// samples flush out). Real Steam-button chatter runs continuously for many
// seconds. Requiring the pattern to PERSIST filters out all handling events.
const unsigned long PUCK_SUSTAIN_MS = 2500;     // pattern must persist this long
// Real chatter density fluctuates window to window; allow brief dips below
// threshold without resetting the persistence timer.
const unsigned long PUCK_PATTERN_GRACE_MS = 600;
const unsigned long PUCK_ARM_QUIET_MS = 5000;   // pattern-free time before arming
const unsigned long PUCK_BOOT_LOCKOUT_MS = 15000; // ignore boot-time noise
// Docked-state recognition (used by CONTROLLER_AND_PUCK lift-off detection):
const int PUCK_DOCKED_MIN_SPIKES = 45;  // window nearly all high = docked
const int PUCK_DOCKED_MAX_LOWS   = 2;
const int PUCK_LIFT_MIN_LOWS     = 10;  // this many lows after docked = lifted

uint8_t puckWindow[PUCK_WINDOW];    // 0 = low, 1 = spike, 2 = mid/ignore
int  puckIdx = 0;
bool puckWindowFull = false;
bool puckArmed = false;
bool puckWasDocked = false;           // last confirmed stable state was docked
unsigned long puckQuietSince = 0;
unsigned long puckPatternSince = 0;   // 0 = pattern not currently present
unsigned long puckPatternLastSeen = 0;
unsigned long lastPuckTrigger = 0;

// OPTIMIZED INTERVALS
unsigned long lastPinRead = 0;
const unsigned long pinReadInterval = 50;

unsigned long lastServerHandle = 0;
const unsigned long serverHandleInterval = 20;

unsigned long lastPcStateHandle = 0;
const unsigned long pcStateHandleInterval = 50;

unsigned long lastButtonDebounce = 0;
const unsigned long debounceDelay = 50;

// Cached pin states
bool cachedButtonState = HIGH;
bool lastStableButtonState = HIGH;
bool buttonPressed = false;

// Filtered PC state
bool filteredPcState = false;
unsigned long lastPcChangeTime = 0;
const unsigned long pcStableDelay = 100;

// Power state machine variables
PowerState powerState = POWER_IDLE;
unsigned long powerStateStartTime = 0;

// PS5 class
PS5Simple ps5Simple;

// ================ PROTOTYPES ================
bool getStablePcState();
void startPowerOn();
void startForceShutdown();
void startNormalShutdown();
void savePS5Config(bool enabled, String mac, bool autoConnect);

// ================ CALLBACK FUNCTIONS ================
void onConnectedGamepad(GamepadPtr gp) {
    Serial.println("=== NEW CONTROLLER DETECTED ===");
    if (gp != nullptr) {
        ps5Simple.onControllerConnected(gp);
    }
}

void onDisconnectedGamepad(GamepadPtr gp) {
    Serial.println("=== CONTROLLER DISCONNECTED ===");
    ps5Simple.onControllerDisconnected(gp);
}

#include "web_server.h"

// ================ WiFi configuration ================

void saveWiFiConfig(String ssid, String pass) {
    File file = LittleFS.open("/wifi_config.json", "w");
    if (!file) return;
    
    StaticJsonDocument<200> doc;
    doc["ssid"] = ssid;
    doc["password"] = pass;
    
    serializeJson(doc, file);
    file.close();
    
    wifiSSID = ssid;
    wifiPassword = pass;
    wifiConfigured = true;
}

void loadWiFiConfig() {
    if (!LittleFS.begin(true)) {
        wifiConfigured = false;
        apMode = true;
        return;
    }
    
    if (!LittleFS.exists("/wifi_config.json")) {
        wifiConfigured = false;
        apMode = true;
        return;
    }
    
    File file = LittleFS.open("/wifi_config.json", "r");
    if (!file) return;
    
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        wifiConfigured = false;
        apMode = true;
        return;
    }
    
    wifiSSID = doc["ssid"] | "";
    wifiPassword = doc["password"] | "";
    
    wifiConfigured = (wifiSSID.length() > 0);
    apMode = !wifiConfigured;
}

bool connectToWiFi() {
    loadWiFiConfig();
    
    if (!wifiConfigured || wifiSSID.length() == 0) {
        apMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("BC-250-POWER-CONTROL", "");
        return true;
    }
    
    WiFi.mode(WIFI_STA);
    String hostname = "bc250-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    WiFi.setHostname(hostname.c_str());
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(100);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        apMode = false;
        return true;
    } else {
        apMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("BC-250-POWER-CONTROL", "");
        return false;
    }
}

// ================ PC state filtering ================



// ================ PS5 functions ================

void savePS5Config(bool enabled, String mac, bool autoConnect) {
    File file = LittleFS.open("/ps5_config.json", "w");
    if (!file) return;
    
    StaticJsonDocument<300> doc;
    doc["enabled"] = enabled;
    
    if (mac.length() == 0 || mac == "00:00:00:00:00:00") {
        doc["macAddress"] = "";
    } else {
        doc["macAddress"] = mac;
    }
    
    doc["autoConnect"] = autoConnect;
    
    serializeJson(doc, file);
    file.close();
    
    ps5Enabled = enabled;
    ps5MacAddress = (mac.length() == 0 || mac == "00:00:00:00:00:00") ? "" : mac;
    ps5AutoConnect = autoConnect;
    
    ps5Simple.setAllowedMac(ps5MacAddress);
}

void loadPS5Config() {
    if (!LittleFS.exists("/ps5_config.json")) {
        ps5Enabled = false;
        ps5MacAddress = "";
        ps5AutoConnect = false;
        ps5Simple.setAllowedMac("");
        Serial.println("PS5: No configuration - all controllers allowed");
        return;
    }
    
    File file = LittleFS.open("/ps5_config.json", "r");
    if (!file) return;
    
    StaticJsonDocument<300> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        ps5Enabled = false;
        ps5MacAddress = "";
        ps5AutoConnect = false;
        ps5Simple.setAllowedMac("");
        Serial.println("PS5: Configuration invalid - all controllers allowed");
        return;
    }
    
    ps5Enabled = doc["enabled"] | false;
    ps5MacAddress = doc["macAddress"] | "";
    ps5AutoConnect = doc["autoConnect"] | false;
    
    ps5Simple.setAllowedMac(ps5MacAddress);
    
    Serial.print("PS5: Load complete - MAC: '");
    Serial.print(ps5MacAddress);
    Serial.print("', enabled: ");
    Serial.println(ps5Enabled);
}

// ================ SETUP ================

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== BC-250 STARTING ===");
    
    initPins();
    Serial.println("Pins initialized");

    // INA219 current sensor - I2C diagnostic on GPIO 25/26
    const int pinA = 25, pinB = 26;
    int foundSDA = -1, foundSCL = -1;
    for (int o = 0; o < 2; o++) {
        int sda = (o == 0) ? pinA : pinB;
        int scl = (o == 0) ? pinB : pinA;
        Wire.end();
        Wire.begin(sda, scl);
        Wire.setClock(50000);
        delay(50);
        Serial.printf("Scanning SDA=%d SCL=%d...\n", sda, scl);
        int found = 0;
        for (byte addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("  DEVICE at 0x%02X\n", addr);
                foundSDA = sda; foundSCL = scl;
                found++;
            }
        }
        if (!found) Serial.println("  nothing");
    }

    if (foundSDA >= 0) {
        Wire.end();
        Wire.begin(foundSDA, foundSCL);
        Wire.setClock(100000);
        if (ina219.begin()) {
            ina219.setCalibration_16V_400mA();
            ina219Present = true;
            Serial.printf("INA219 initialized (SDA=%d SCL=%d)\n", foundSDA, foundSCL);
        } else {
            Serial.println("INA219 device seen but init failed");
        }
    } else {
        Serial.println("INA219 not found - check wiring");
    }

    puckQuietSince = millis();

    initNeopixel();
    Serial.println("NeoPixel initialized");
    
    Serial.print("PC_MONITOR_PIN (4): ");
    Serial.println(digitalRead(PC_MONITOR_PIN) ? "HIGH" : "LOW");
    Serial.print("OPTO_PIN (16): ");
    Serial.println(digitalRead(OPTO_PIN) ? "HIGH" : "LOW");
    Serial.print("EXTRA_PIN (17): ");
    Serial.println(digitalRead(EXTRA_PIN) ? "HIGH" : "LOW");
    
    filteredPcState = digitalRead(PC_MONITOR_PIN);
    pcIsOn = filteredPcState;
    
    Serial.println("Loading WiFi config...");
    loadWiFiConfig();
    
    Serial.println("Connecting to WiFi...");
    connectToWiFi();
    
    Serial.print("WiFi mode: ");
    Serial.println(apMode ? "AP" : "STA");
    if (!apMode) {
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.print("AP IP address: ");
        Serial.println(WiFi.softAPIP());
    }
    
    Serial.println("Mounting LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed!");
    } else {
        Serial.println("LittleFS mounted");
    }

    Serial.println("Setting up Bluepad32...");
    
    // ONLY THESE TWO LINES ARE NEEDED
    BP32.setup(&onConnectedGamepad, &onDisconnectedGamepad);
    BP32.enableVirtualDevice(false);
    
    Serial.println("Loading PS5 config...");
    loadPS5Config();
    
    Serial.println("Bluepad32 ready - waiting for controller pairing");
    
    Serial.println("Setting up web server...");
    setupWebServer();
    
    Serial.println("=== BC-250 READY ===\n");
}

void loop() {
    unsigned long now = millis();
    
    // ================ SIMPLE SOLUTION: ESP32 RESTART ================
    static unsigned long pcOffStartTime = 0;

    // Track how long the PC has been off
    if (!pcIsOn && powerState == POWER_IDLE) {
        if (pcOffStartTime == 0) {
            pcOffStartTime = now;
            Serial.println("PC turned off - restart in 2 hours");
        }
        
        // IF PC HAS BEEN OFF FOR OVER 2 HOURS, RESTART ESP32
        if (now - pcOffStartTime >= 7200000) { // 2 hours = 7200000 ms
            Serial.println("=== PC has been off for 2 hours - ESP32 RESTART ===");
            delay(1000);
            ESP.restart();
        }
    } else {
        // PC is on or starting up - reset the timer
        pcOffStartTime = 0;
    }
    
    // ================ POWER STATE DEBUG ================
    static unsigned long lastStatePrint = 0;
    static PowerState lastPowerState = POWER_IDLE;
    
    if (powerState != lastPowerState) {
        // State has changed, print the new state
        Serial.print("STATE: ");
        switch(powerState) {
            case POWER_IDLE: Serial.print("IDLE"); break;
            case POWER_ON_START: Serial.print("ON_START"); break;
            case POWER_ON_WAITING_RELAY2: Serial.print("ON_WAITING_RELAY2"); break;
            case POWER_ON_COMPLETE: Serial.print("ON_COMPLETE"); break;
            case POWER_OFF_START: Serial.print("OFF_START"); break;
            case POWER_OFF_WAITING: Serial.print("OFF_WAITING"); break;
            case POWER_OFF_WAITING_POWEROFF: Serial.print("OFF_WAITING_POWEROFF"); break;
            case POWER_FORCE_START: Serial.print("FORCE_START"); break;
            case POWER_FORCE_WAITING: Serial.print("FORCE_WAITING"); break;
            default: Serial.print("UNKNOWN"); break;
        }
        Serial.print(" (pcIsOn=");
        Serial.print(pcIsOn ? "ON" : "OFF");
        Serial.print(", monitor=");
        Serial.print(digitalRead(PC_MONITOR_PIN) ? "HIGH" : "LOW");
        Serial.println(")");
        lastPowerState = powerState;
        lastStatePrint = now;
    }
    
    // Print state every 60 seconds
    if (now - lastStatePrint >= 60000) {
        Serial.print("HEARTBEAT: ");
        Serial.print(millis() / 1000);
        Serial.print("s - State: ");
        switch(powerState) {
            case POWER_IDLE: 
                Serial.print("IDLE");
                if (!pcIsOn) {
                    Serial.print(" (restart in ");
                    Serial.print((7200000 - (now - pcOffStartTime)) / 1000);
                    Serial.print("s)");
                }
                break;
            case POWER_ON_START: Serial.print("ON_START"); break;
            case POWER_ON_WAITING_RELAY2: Serial.print("ON_WAITING_RELAY2"); break;
            case POWER_ON_COMPLETE: Serial.print("ON_COMPLETE"); break;
            case POWER_OFF_START: Serial.print("OFF_START"); break;
            case POWER_OFF_WAITING: Serial.print("OFF_WAITING"); break;
            case POWER_OFF_WAITING_POWEROFF: Serial.print("OFF_WAITING_POWEROFF"); break;
            case POWER_FORCE_START: Serial.print("FORCE_START"); break;
            case POWER_FORCE_WAITING: Serial.print("FORCE_WAITING"); break;
            default: Serial.print("UNKNOWN"); break;
        }
        Serial.print(", PC: ");
        Serial.print(pcIsOn ? "ON" : "OFF");
        Serial.println();
        lastStatePrint = now;
    }

    // ================ PIN READ ================
    if (now - lastPinRead >= pinReadInterval) {
        cachedButtonState = digitalRead(BUTTON_PIN);
        lastPinRead = now;
    }

    // ================ PUCK CURRENT SENSING & WAKE DETECTION ================
    if (ina219Present && now - lastCurrentSample >= currentSampleInterval) {
        lastCurrentSample = now;
        float mA = ina219.getCurrent_mA();

        if (inaLogging) {
            Serial.printf("INA,%lu,%.2f\n", now, mA);
        }

        // Classify sample and push into rolling 1-second window
        uint8_t c = (mA >= PUCK_SPIKE_mA) ? 1 : ((mA <= PUCK_QUIET_mA) ? 0 : 2);
        puckWindow[puckIdx] = c;
        puckIdx = (puckIdx + 1) % PUCK_WINDOW;
        if (puckIdx == 0) puckWindowFull = true;

        if (puckWindowFull) {
            int spikes = 0, lows = 0;
            for (int i = 0; i < PUCK_WINDOW; i++) {
                if (puckWindow[i] == 1) spikes++;
                else if (puckWindow[i] == 0) lows++;
            }
            bool wakePattern = (spikes >= PUCK_MIN_SPIKES && lows >= PUCK_MIN_LOWS);
            bool dockedNow   = (spikes >= PUCK_DOCKED_MIN_SPIKES && lows <= PUCK_DOCKED_MAX_LOWS);

            // Common guard for any trigger path
            bool canTrigger = puckArmed && powerState == POWER_IDLE &&
                              now > PUCK_BOOT_LOCKOUT_MS &&
                              now - lastPuckTrigger > 10000;

            if (pcIsOn) {
                // PC running: detection irrelevant, stay disarmed
                puckArmed = false;
                puckQuietSince = now;
                puckPatternSince = 0;
                puckWasDocked = false;
            } else {
                // ---- Lift-off trigger (PUCK_MODE_CONTROLLER_AND_PUCK only) ----
                // Fires on the transition out of a confirmed stable docked
                // state: window was flat-high, now shows a burst of lows.
                if (PUCK_MODE == PUCK_MODE_CONTROLLER_AND_PUCK &&
                    puckWasDocked && !dockedNow && lows >= PUCK_LIFT_MIN_LOWS &&
                    canTrigger) {
                    Serial.printf("PUCK: Controller lifted off puck (spikes=%d lows=%d) - POWER ON\n",
                                  spikes, lows);
                    lastPuckTrigger = now;
                    puckArmed = false;
                    puckWasDocked = false;
                    puckQuietSince = now;
                    puckPatternSince = 0;
                    startPowerOn();
                }
                // Remember docked state only when the window is unambiguous
                else if (dockedNow) {
                    puckWasDocked = true;
                } else if (lows >= PUCK_LIFT_MIN_LOWS && !wakePattern) {
                    // settled back to idle without triggering (mode 1, or
                    // trigger guards not met) - clear the docked memory
                    puckWasDocked = false;
                }

                // ---- Steam button trigger (both modes) ----
                if (wakePattern) {
                    // Track how long the pattern has been continuously present.
                    // Dock/undock transitions self-clear within ~1 s; only a
                    // real Steam-button wake sustains past PUCK_SUSTAIN_MS.
                    if (puckPatternSince == 0) puckPatternSince = now;
                    puckPatternLastSeen = now;

                    if (canTrigger && now - puckPatternSince >= PUCK_SUSTAIN_MS) {
                        Serial.printf("PUCK: Sustained wake signature (%lums, spikes=%d lows=%d) - POWER ON\n",
                                      now - puckPatternSince, spikes, lows);
                        lastPuckTrigger = now;
                        puckArmed = false;
                        puckQuietSince = now;
                        puckPatternSince = 0;
                        startPowerOn();
                    }
                    // Pattern present: keep resetting the quiet timer so we
                    // don't re-arm while the controller is actively talking
                    puckQuietSince = now;
                } else {
                    // Pattern below threshold. Real wake chatter fluctuates,
                    // so tolerate brief dips before resetting the persistence
                    // timer; only a sustained absence counts as "broken".
                    if (puckPatternSince != 0 &&
                        now - puckPatternLastSeen > PUCK_PATTERN_GRACE_MS) {
                        puckPatternSince = 0;
                    }
                    // No wake pattern (flat idle or flat docked). Arm after a
                    // sustained pattern-free period so a controller that's
                    // still awake right after shutdown can't re-trigger.
                    if (!puckArmed && now - puckQuietSince >= PUCK_ARM_QUIET_MS) {
                        puckArmed = true;
                        Serial.println("PUCK: Armed");
                    }
                }
            }
        }
    }

    // ================ PC STATE HANDLING ================
    if (now - lastPcStateHandle >= pcStateHandleInterval) {
        handlePcStates();
        lastPcStateHandle = now;
    }

    // ================ POWER STATE HANDLING ================
    handlePowerStates();

    // ================ WEB SERVER ================
    if (now - lastServerHandle >= serverHandleInterval) {
        server.handleClient();
        lastServerHandle = now;
    }
    
    // ================ PS5 CONTROLLER HANDLING ================
    // Only if the PC is off and in idle state
    if (!pcIsOn && powerState == POWER_IDLE) {
        ps5Simple.handle();
    } else {
        // PC is on - don't handle PS5, but update Bluepad32
        static unsigned long lastBP32Update = 0;
        if (now - lastBP32Update >= 100) {
            BP32.update();
            lastBP32Update = now;
        }
    }
    
    // ================ BUTTON HANDLING ================
    static unsigned long buttonPressStartTime = 0;
    static bool buttonPressDetected = false;
    static bool lastStableButtonState = HIGH;
    
    // Check button state with debounce
    if (cachedButtonState != lastStableButtonState) {
        lastButtonDebounce = now;
        lastStableButtonState = cachedButtonState;
    }
    
    // If state is stable (debounce passed)
    if ((now - lastButtonDebounce) > debounceDelay) {
        
        // Button was pressed down (LOW)
        if (cachedButtonState == LOW && !buttonPressDetected) {
            buttonPressDetected = true;
            buttonPressStartTime = now;
            Serial.println("BUTTON: Button pressed down");
        }
        
        // Button was released (HIGH)
        if (cachedButtonState == HIGH && buttonPressDetected) {
            unsigned long pressDuration = now - buttonPressStartTime;
            buttonPressDetected = false;
            
            Serial.print("BUTTON: Button released - duration: ");
            Serial.print(pressDuration);
            Serial.println(" ms");
            
            // Check PC state (only in IDLE state)
            if (powerState == POWER_IDLE) {
                bool pcOn = getStablePcState();
                
                if (pcOn) {
                    // PC IS ON
                    if (pressDuration >= 5000) {
                        // Long press (over 5s) = FORCE SHUTDOWN
                        Serial.println("BUTTON: Long press (>5s) - FORCE SHUTDOWN");
                        startForceShutdown();
                    } else {
                        // Short press (under 5s) = NORMAL SHUTDOWN
                        Serial.println("BUTTON: Short press (<5s) - NORMAL SHUTDOWN");
                        startNormalShutdown();
                    }
                } else {
                    // PC IS OFF
                    Serial.println("BUTTON: PC is off - POWER ON");
                    startPowerOn();
                }
            } else {
                Serial.print("BUTTON: Power state not IDLE - command rejected. Current state: ");
                Serial.println(powerState);
            }
        }
    }
    
    // ================ NEOPIXEL ================
    // The strip is powered from 5V standby (always powered), so the firmware
    // must control whether it lights up: animate while the machine is on, and
    // actively blank the strip when the machine is off (otherwise it would run
    // 24/7). pcIsOn tracks the machine's running state. updateNeopixel() is
    // non-blocking and caps its own frame rate.
    static bool neoPrevPcOn = false;
    if (pcIsOn) {
        if (!neoPrevPcOn) neopixelResetTiming();  // fresh frame timer on power-on
        updateNeopixel();
    } else if (neoPrevPcOn) {
        neopixelClear();                          // machine just turned off -> blank
    }
    neoPrevPcOn = pcIsOn;

    // ================ SMALL DELAY ================
    delay(1);
}
