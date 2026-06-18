#ifndef PS5_SIMPLE_H
#define PS5_SIMPLE_H

#include <Bluepad32.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "LittleFS.h"

// IMPORTANT: Include pc_control.h FIRST so PowerState is known
#include "pc_control.h"

// IMPORTANT: These are external variables/functions defined elsewhere
extern bool ps5Enabled;
extern String ps5MacAddress;
extern bool getStablePcState();
extern void startPowerOn();
extern PowerState powerState;
extern void savePS5Config(bool enabled, String mac, bool autoConnect);

class PS5Simple {
private:
    GamepadPtr myController = nullptr;
    String allowedMac = "";           // Saved MAC (empty = all allowed)
    String lastSeenMac = "";          // Last seen MAC
    unsigned long lastSeenTime = 0;   // When last seen
    bool macAutoSaved = false;        // Whether the MAC has already been auto-saved
    
public:
    // Set the allowed MAC (empty = all allowed)
    void setAllowedMac(String mac) {
        mac.trim();
        mac.replace(":", "");
        mac.replace("-", "");
        mac.toUpperCase();
        
        if (mac.length() == 0 || mac == "000000000000") {
            allowedMac = "";
            Serial.println("PS5: All controllers allowed");
        } else {
            allowedMac = mac;
            Serial.println("PS5: Only MAC " + allowedMac + " allowed");
        }
        macAutoSaved = false;
    }
    
    // Return the allowed MAC formatted
    String getAllowedMac() {
        if (allowedMac.length() != 12) return "";
        
        String formatted = "";
        for (int i = 0; i < 12; i += 2) {
            if (i > 0) formatted += ":";
            formatted += allowedMac.substring(i, i+2);
        }
        return formatted;
    }
    
    // Connected controller's MAC
    String getConnectedMac() {
        return lastSeenMac;
    }
    
    // Is a controller connected? (Check if last seen less than 5 seconds ago)
    bool isConnected() {
        if (lastSeenMac.length() == 0) return false;
        if (millis() - lastSeenTime < 5000) return true;
        return false;
    }
    
    // Disconnect
    void disconnect() {
        // Do nothing - the controller disconnects itself
    }

    // RESET CONTROLLER DATA (called when the PC powers off)
    void resetControllerData() {
        if (lastSeenMac.length() > 0) {
            Serial.print("PS5: Resetting controller data - MAC: ");
            Serial.println(lastSeenMac);
            lastSeenMac = "";
            lastSeenTime = 0;
            macAutoSaved = false;
        }
    }
    
    // NEW CONTROLLER DETECTED
    void onControllerConnected(GamepadPtr gp) {
        if (gp == nullptr) return;

        // IMPORTANT: Check whether the PC is on
        bool pcOn = getStablePcState();
        
        if (pcOn || powerState != POWER_IDLE) {
            Serial.println("PS5: PC ON - CONTROLLER NOT ACCEPTED");
            gp->disconnect();
            return;
        }
        
        // Get the MAC address
        GamepadProperties prop = gp->getProperties();
        char macStr[18];
        sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
                prop.btaddr[0], prop.btaddr[1], prop.btaddr[2],
                prop.btaddr[3], prop.btaddr[4], prop.btaddr[5]);
        
        String mac = String(macStr);
        Serial.print("PS5: Controller detected - MAC: ");
        Serial.println(mac);
        
        // Remove separators for comparison
        String macClean = mac;
        macClean.replace(":", "");
        macClean.replace("-", "");
        macClean.toUpperCase();
        
        // CHECK THE MAC
        bool allowed = false;

        if (allowedMac.length() == 0) {
            allowed = true;
        }
        else if (macClean == allowedMac) {
            allowed = true;
        }

        if (allowed) {
            // SAVE THE MAC
            lastSeenMac = mac;
            lastSeenTime = millis();
            Serial.println("✅ PS5: Allowed MAC detected!");

            // AUTOMATIC SAVE: If MAC lock is not on, save this MAC
            if (allowedMac.length() == 0 && !macAutoSaved) {
                Serial.println("PS5: No MAC lock - saving this MAC automatically!");

                // Save the MAC to configuration
                ps5MacAddress = mac;
                savePS5Config(true, mac, false);
                
                // Also update our own allowedMac
                setAllowedMac(mac);

                macAutoSaved = true;
                Serial.println("✅ PS5: MAC saved automatically!");
            }

            // INTENTIONALLY DROP THE CONNECTION
            if (gp != nullptr) {
                gp->disconnect();
                Serial.println("PS5: Disconnecting - using MAC info only");
            }
        } else {
            Serial.println("❌ PS5: Rejected MAC - not allowed");
            if (gp != nullptr) {
                gp->disconnect();
            }
        }
    }
    
    // CONTROLLER DISCONNECTED
    void onControllerDisconnected(GamepadPtr gp) {
        Serial.println("PS5: Controller disconnected");
    }

    // CHECK WHETHER AN ALLOWED CONTROLLER IS NEARBY
    bool isAuthorizedControllerNearby() {
        return isConnected();
    }
    
    // "Read PS button" - returns true if an allowed controller is nearby
    bool psButtonPressed() {
        return isAuthorizedControllerNearby();
    }
    
    // MAIN HANDLER
    void handle() {
        // Check PC state
        bool pcOn = getStablePcState();
        
        // IF PC IS ON OR POWERING OFF, RESET CONTROLLER DATA
        if (pcOn || powerState != POWER_IDLE) {
            if (lastSeenMac.length() > 0) {
                Serial.println("PS5: PC is on or powering off - resetting controller data");
                lastSeenMac = "";
                lastSeenTime = 0;
                macAutoSaved = false;
            }
            // Update Bluepad32 ONLY if the PC is on (so the connection drops)
            BP32.update();
            return;
        }
        
        // PC IS OFF - update Bluepad32 normally
        BP32.update();

        // If PS5 is not enabled, do nothing else
        if (!ps5Enabled) {
            return;
        }
        
        unsigned long now = millis();
        
        // If an allowed controller is nearby
        if (isAuthorizedControllerNearby()) {
            static unsigned long lastTriggerTime = 0;

            // If last seen less than 2 seconds ago and over 5s since the last trigger
            if (now - lastSeenTime < 2000 && now - lastTriggerTime > 5000) {
                Serial.println("PS5: Allowed controller nearby - powering on PC!");
                lastTriggerTime = now;
                startPowerOn();
            }
        }
        
        // Print only occasionally
        static unsigned long lastPrint = 0;
        if (now - lastPrint > 10000 && ps5Enabled) {
            if (isAuthorizedControllerNearby()) {
                Serial.println("PS5: Allowed controller nearby...");
            } else {
                Serial.println("PS5: Waiting for controller...");
            }
            lastPrint = now;
        }
    }
};

extern PS5Simple ps5Simple;

#endif