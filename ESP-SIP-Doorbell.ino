/* ====================================================================
   ESP8266 SIP Doorbell for FritzBox.
   see: https://github.com/miguelitoelgrande/ESP-SIP-Doorbell
   Features:
   - Immediate SIP call on power-on/reset (doorbell priority)
   - Serial and WebSerial debugging options
   - Time synchronization (NTP/FritzBox)
   - Persistent event logging (last 100 doorbell events)
   - Web-based configuration portal with AP fallback
   - Deep sleep with wake on hardware reset
 * ====================================================================*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <time.h>
#include <WiFiUdp.h>
#include "Sip.h"

// ====================================================================
// DEBUG CONFIGURATION
// ====================================================================
#define DEBUG_SERIAL_BAUD 115200

// Forward declarations for debug functions
void debugPrint(const char* msg);
void debugPrintln(const char* msg);
void debugPrintf(const char* format, ...);

// Debug macros - will respect config settings
#define DEBUG_PRINT(x) debugPrint(x)
#define DEBUG_PRINTLN(x) debugPrintln(x)
#define DEBUG_PRINTF(...) debugPrintf(__VA_ARGS__)

// ====================================================================
// CONFIGURATION STRUCTURE
// ====================================================================
#define CONFIG_VERSION "CFG3"
#define CONFIG_START 0

struct Config {
  char version[5];
  
  // WiFi settings
  char ssid[32];
  char password[64];
  char hostname[32];
  
  // Network settings
  bool useDHCP;
  char ip[16];
  char router[16];
  char subnet[16];
  
  // SIP settings
  int sipPort;
  char sipUser[32];
  char sipPassword[64];
  char dialNumber[16];
  char dialText[64];
  int ringDuration;
  
  // Deep sleep settings
  int sleepTimeout;
  int lightSleepEnabled;  // 0=disabled, 1=enabled
  int inactivitySleepTimeout;  // Seconds of inactivity before light sleep
  
  // AP mode settings
  char apSSID[32];
  char apPassword[64];
  
  // Debug settings
  bool debugSerial;
  bool debugWebSerial;
  
  // Time settings
  char ntpServer[64];
  int timezoneOffset;  // Offset in seconds from UTC
};

Config config;

// ====================================================================
// EVENT LOG STRUCTURE
// ====================================================================
#define MAX_EVENTS 100  // Increased from 50 since we removed timeStr
#define EVENTLOG_START 1024  // Leave space for config expansion

struct DoorbellEvent {
  unsigned long timestamp;  // Unix timestamp
  bool sipSuccess;         // Was SIP call successful
//  uint8_t wakeReason;      // Reset reason (using uint8_t to save space)
};

struct EventLog {
  int count;               // Total events logged
  int writeIndex;          // Next write position (ring buffer)
  DoorbellEvent events[MAX_EVENTS];
};

EventLog eventLog;

// ====================================================================
// GLOBAL VARIABLES
// ====================================================================
ESP8266WebServer server(80);
WiFiUDP ntpUDP;

// ====================================================================
// HARDWARE CONFIGURATION
// ====================================================================
#define LED_PIN 2
#define DOORBELL_PIN 14  // D5 on NodeMCU, change as needed. Connect doorbell button here (active LOW with pullup)

// Debounce settings
#define DEBOUNCE_MS 500  // Minimum time between doorbell presses
#define BUTTON_HOLD_MS 100  // Button must be pressed this long to be valid

unsigned long lastActivityTime = 0;
unsigned long lastDoorbellPress = 0;
bool sipCallAttempted = false;
bool sipCallSuccess = false;
bool doorbellPressed = false;
volatile bool doorbellInterruptFlag = false;
Sip* aSip = nullptr;

char caSipIn[2048];
char caSipOut[2048];

// WebSerial buffer
#define WEBSERIAL_BUFFER_SIZE 8192
char webSerialBuffer[WEBSERIAL_BUFFER_SIZE];
int webSerialIndex = 0;

// ====================================================================
// DEBUG FUNCTIONS (respect config settings)
// ====================================================================

void addToWebSerialBuffer(const char* msg) {
  if (!config.debugWebSerial) return;
  
  int len = strlen(msg);
  if (webSerialIndex + len >= WEBSERIAL_BUFFER_SIZE - 1) {
    // Buffer full, shift content
    int shift = WEBSERIAL_BUFFER_SIZE / 2;
    memmove(webSerialBuffer, webSerialBuffer + shift, WEBSERIAL_BUFFER_SIZE - shift);
    webSerialIndex -= shift;
  }
  
  strcpy(webSerialBuffer + webSerialIndex, msg);
  webSerialIndex += len;
}

void debugPrint(const char* msg) {
  if (config.debugSerial) {
    Serial.print(msg);
  }
  if (config.debugWebSerial) {
    addToWebSerialBuffer(msg);
  }
}

void debugPrintln(const char* msg) {
  if (config.debugSerial) {
    Serial.println(msg);
  }
  if (config.debugWebSerial) {
    addToWebSerialBuffer(msg);
    addToWebSerialBuffer("\n");
  }
}

void debugPrintf(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  if (config.debugSerial) {
    Serial.print(buffer);
  }
  if (config.debugWebSerial) {
    addToWebSerialBuffer(buffer);
  }
}

// ====================================================================
// FUNCTION DECLARATIONS
// ====================================================================
void loadConfig();
void saveConfig();
void setDefaultConfig();
void loadEventLog();
void saveEventLog();
void logDoorbellEvent(bool success);
void setupAP();
void setupWebServer();
void handleRoot();
void handleSave();
void handleStatus();
void handleEvents();
void handleWebSerial();
void connectWiFi();
void syncTime();
void makeEmergencySIPCall();
void checkLightSleep();
void enterLightSleep();
void IRAM_ATTR doorbellISR();
void handleDoorbellPress();
void blinkLED(int times, int delayMs);
String formatTime(unsigned long timestamp);

// ====================================================================
// SETUP - PRIORITY: RING FIRST!
// ====================================================================
void setup() {
  // Initialize Serial immediately for emergency debug
  Serial.begin(DEBUG_SERIAL_BAUD);
  delay(50);
  
  // Initialize hardware pins
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // LED on - we're starting!
  
  pinMode(DOORBELL_PIN, INPUT_PULLUP);  // Doorbell button with internal pullup
  
  // Load config first (need it for everything)
  // Calculate required EEPROM size: EventLog start + EventLog size + safety margin
  int eepromSize = EVENTLOG_START + sizeof(EventLog) + 64; // Add 64 byte safety margin
  DEBUG_PRINTF("[EEPROM] Initializing EEPROM emulation\n");
  DEBUG_PRINTF("[EEPROM]   Required size: %d bytes\n", eepromSize);
  DEBUG_PRINTF("[EEPROM]   Config: %d bytes at offset %d\n", sizeof(Config), CONFIG_START);
  DEBUG_PRINTF("[EEPROM]   EventLog: %d bytes at offset %d\n", sizeof(EventLog), EVENTLOG_START);
  DEBUG_PRINTF("[EEPROM]   Max address used: %d\n", EVENTLOG_START + sizeof(EventLog));
  
  EEPROM.begin(eepromSize);

  loadConfig();
  loadEventLog();
  
  // Get wake reason
  rst_info* resetInfo = ESP.getResetInfoPtr();
  int wakeReason = resetInfo->reason;
  
  DEBUG_PRINTLN("\n====================================");
  DEBUG_PRINTLN("ESP8266 SIP DOORBELL for FritzBox");
  DEBUG_PRINTLN("====================================");
  DEBUG_PRINTLN("[INFO] Documentation and Codebase: https://github.com/miguelitoelgrande/ESP-SIP-Doorbell");
  DEBUG_PRINTF("[WAKE] Reset reason: %d\n", wakeReason);
  
  // Check if doorbell button is pressed (active LOW)
  bool doorbellPressedOnBoot = (digitalRead(DOORBELL_PIN) == LOW);
  
  if (doorbellPressedOnBoot) {
    DEBUG_PRINTLN("[DOORBELL] Button pressed on boot!");
    
    // Wait for stable press (debounce)
    delay(BUTTON_HOLD_MS);
    if (digitalRead(DOORBELL_PIN) == LOW) {
      DEBUG_PRINTLN("[DOORBELL] Valid button press detected");
      doorbellPressed = true;
    } else {
      DEBUG_PRINTLN("[DOORBELL] Button press too short, ignoring");
    }
  }
  
  // ====================================================================
  // PRIORITY 1: ESTABLISH SIP CALL IF DOORBELL PRESSED
  // ====================================================================
  if (doorbellPressed) {
    DEBUG_PRINTLN("[PRIORITY] Doorbell activated - attempting SIP call...");
    DEBUG_PRINTF("[PRIORITY] WiFi SSID: %s\n", config.ssid);
    blinkLED(1, 50);
    
    // Disconnect any previous connections
    WiFi.disconnect(true);
    delay(100);
    
    // Quick WiFi connect
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.hostname(config.hostname);
    
    if (!config.useDHCP) {
      DEBUG_PRINTLN("[PRIORITY] Using static IP configuration");
      IPAddress ip, gw, subnet, dns;
      if (ip.fromString(config.ip) && gw.fromString(config.router) && 
          subnet.fromString(config.subnet) && dns.fromString(config.router)) {
        WiFi.config(ip, gw, subnet, dns);
        DEBUG_PRINTF("[PRIORITY] Static IP: %s, Gateway: %s\n", config.ip, config.router);
      } else {
        DEBUG_PRINTLN("[PRIORITY] ERROR: Invalid static IP config, using DHCP");
      }
    } else {
      DEBUG_PRINTLN("[PRIORITY] Using DHCP");
    }
    
    DEBUG_PRINTLN("[PRIORITY] Starting WiFi connection...");
    WiFi.begin(config.ssid, config.password);
    
    // Fast connection attempt (10 seconds max)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 50) {
      delay(200);
      attempts++;
      if (attempts % 10 == 0) {
        DEBUG_PRINTF("[PRIORITY] Connection attempt %d/50, Status: %d\n", attempts, WiFi.status());
        blinkLED(1, 20);
      }
    }
    
    DEBUG_PRINTF("[PRIORITY] WiFi Status after attempts: %d\n", WiFi.status());
    
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("[PRIORITY] WiFi connected!");
      DEBUG_PRINTF("[PRIORITY] IP address: %s\n", WiFi.localIP().toString().c_str());
      DEBUG_PRINTF("[PRIORITY] Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
      DEBUG_PRINTF("[PRIORITY] RSSI: %d dBm\n", WiFi.RSSI());
      
      // Initialize SIP immediately
      aSip = new Sip(caSipOut, sizeof(caSipOut));
      aSip->Init(config.router, config.sipPort, 
                 config.useDHCP ? WiFi.localIP().toString().c_str() : config.ip, 
                 config.sipPort, config.sipUser, config.sipPassword, config.ringDuration);
      
      delay(100); // Brief moment for SIP to initialize
      
      // MAKE THE CALL!
      DEBUG_PRINTF("[PRIORITY] Dialing: %s (%s)\n", config.dialNumber, config.dialText);
      aSip->Dial(config.dialNumber, config.dialText);
      sipCallAttempted = true;
      sipCallSuccess = true;
      
      blinkLED(3, 100); // Success indication
      
      // Keep SIP active for ring duration
      unsigned long callStart = millis();
      while (millis() - callStart < (config.ringDuration * 1000)) {
        int packetSize = aSip->Udp.parsePacket();
        if (packetSize > 0) {
          caSipIn[0] = 0;
          packetSize = aSip->Udp.read(caSipIn, sizeof(caSipIn));
          if (packetSize > 0) {
            caSipIn[packetSize] = 0;
          }
        }
        aSip->HandleUdpPacket((packetSize > 0) ? caSipIn : 0);
        delay(10);
      }
      
      DEBUG_PRINTLN("[PRIORITY] Call completed successfully!");
      lastDoorbellPress = millis();
      
    } else {
      DEBUG_PRINTLN("[PRIORITY] WiFi connection FAILED!");
      DEBUG_PRINTF("[PRIORITY] Final Status: %d\n", WiFi.status());
      DEBUG_PRINTF("[PRIORITY] Configured SSID: '%s'\n", config.ssid);
      
      if (strlen(config.ssid) < 2 || strcmp(config.ssid, "Your-WiFi-SSID") == 0) {
        DEBUG_PRINTLN("[PRIORITY] ERROR: WiFi SSID appears to be unconfigured!");
      }
      
      sipCallAttempted = true;
      sipCallSuccess = false;
      blinkLED(10, 100); // Error indication
    }
    
    digitalWrite(LED_PIN, HIGH); // LED off
  } else {
    DEBUG_PRINTLN("[BOOT] Normal boot - no doorbell press detected");
    
    // Regular WiFi connection for management
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.hostname(config.hostname);
    
    if (!config.useDHCP) {
      IPAddress ip, gw, subnet, dns;
      if (ip.fromString(config.ip) && gw.fromString(config.router) && 
          subnet.fromString(config.subnet) && dns.fromString(config.router)) {
        WiFi.config(ip, gw, subnet, dns);
      }
    }
    
    WiFi.begin(config.ssid, config.password);
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(200);
      attempts++;
      if (attempts % 5 == 0) {
        DEBUG_PRINTF("[WIFI] Connecting... %d/30\n", attempts);
      }
    }
  }
  
  // ====================================================================
  // PRIORITY 2: INITIALIZE MANAGEMENT SYSTEMS
  // ====================================================================
  DEBUG_PRINTLN("\n[INIT] Initializing management systems...");
  
  bool needAPMode = false;
  
  if (WiFi.status() != WL_CONNECTED) {
    if (strlen(config.ssid) < 2 || strcmp(config.ssid, "Your-WiFi-SSID") == 0) {
      DEBUG_PRINTLN("[INIT] WiFi appears unconfigured, starting AP for setup");
      needAPMode = true;
    } else {
      DEBUG_PRINTLN("[INIT] WiFi connection failed, starting AP for troubleshooting");
      needAPMode = true;
    }
  } else {
    DEBUG_PRINTLN("[INIT] WiFi connected, syncing time...");
    syncTime();
    delay(500);
  }
  
  // Log event if doorbell was pressed
  if (doorbellPressed) {
    logDoorbellEvent(sipCallSuccess);
  }
  
  // Set up AP if needed
  if (needAPMode) {
    setupAP();
  }
  
  // Setup web server
  setupWebServer();
  server.begin();
  DEBUG_PRINTLN("[WEB] Configuration server started");
  
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTF("[WEB] Access at: http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    DEBUG_PRINTLN("[WEB] Access via AP at: http://192.168.4.1");
  }
  
  // Attach doorbell interrupt
  attachInterrupt(digitalPinToInterrupt(DOORBELL_PIN), doorbellISR, FALLING);
  DEBUG_PRINTF("[DOORBELL] Interrupt attached to GPIO %d\n", DOORBELL_PIN);
  
  lastActivityTime = millis();
  DEBUG_PRINTLN("[INIT] Setup complete, entering main loop");
  
  if (config.lightSleepEnabled) {
    DEBUG_PRINTF("[SLEEP] Light sleep enabled - will sleep after %d seconds inactivity\n", 
                 config.inactivitySleepTimeout);
  } else {
    DEBUG_PRINTLN("[SLEEP] Light sleep disabled");
  }
  
  DEBUG_PRINTLN("====================================\n");
}

// ====================================================================
// MAIN LOOP
// ====================================================================
void loop() {
  static unsigned long lastDebugTime = 0;
  
  // Check for doorbell interrupt
  if (doorbellInterruptFlag) {
    doorbellInterruptFlag = false;
    handleDoorbellPress();
  }
  
  server.handleClient();
  
  // Periodic status output (every 30 seconds)
  if (millis() - lastDebugTime > 30000) {
    //DEBUG_PRINTLN("\n[STATUS] System Status:");
    DEBUG_PRINTF("[STATUS] WiFi: %s", WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTF(" (%d dBm)\n", WiFi.RSSI());
    } else {
      DEBUG_PRINTLN("");
    }
    // DEBUG_PRINTF("  Free heap: %d bytes\n", ESP.getFreeHeap());
    DEBUG_PRINTF("[STATUS] Uptime: %lu seconds", millis() / 1000);
    if (config.lightSleepEnabled && config.inactivitySleepTimeout > 0) {
      int timeLeft = config.inactivitySleepTimeout - ((millis() - lastActivityTime) / 1000);
      DEBUG_PRINTF(" / Light sleep in: %d seconds\n", timeLeft > 0 ? timeLeft : 0);
    }
    // DEBUG_PRINTF("  Last call: %s\n", sipCallSuccess ? "SUCCESS" : (sipCallAttempted ? "FAILED" : "NONE"));
    // DEBUG_PRINTF("  Total doorbell events: %d\n", eventLog.count);
    lastDebugTime = millis();
  }
  
  // Handle SIP packets if active
  if (aSip != nullptr) {
    int packetSize = aSip->Udp.parsePacket();
    if (packetSize > 0) {
      caSipIn[0] = 0;
      packetSize = aSip->Udp.read(caSipIn, sizeof(caSipIn));
      if (packetSize > 0) {
        caSipIn[packetSize] = 0;
      }
      lastActivityTime = millis();
    }
    aSip->HandleUdpPacket((packetSize > 0) ? caSipIn : 0);
  }
  
  checkLightSleep();
  delay(10);
}

// ====================================================================
// CONFIGURATION FUNCTIONS
// ====================================================================

void loadConfig() {
  EEPROM.get(CONFIG_START, config);
  

  if (strcmp(config.version, CONFIG_VERSION) != 0) {
    Serial.println("[CONFIG] No valid config, loading defaults");
    setDefaultConfig();
    saveConfig();
  } else {
    // Debug: dump loaded configuration
    Serial.println("[CONFIG] Configuration loaded:");
    Serial.printf("[CONFIG]   Version: %s\n", config.version);
    Serial.printf("[CONFIG]   SSID: '%s' (len=%d)\n", config.ssid, strlen(config.ssid));
    Serial.printf("[CONFIG]   Hostname: '%s'\n", config.hostname);
    Serial.printf("[CONFIG]   DHCP: %s\n", config.useDHCP ? "true" : "false");
    Serial.printf("[CONFIG]   Static IP: %s\n", config.ip);
    Serial.printf("[CONFIG]   Router: %s\n", config.router);
    Serial.printf("[CONFIG]   Subnet: %s\n", config.subnet);
    Serial.printf("[CONFIG]   SIP Port: %d\n", config.sipPort);
    Serial.printf("[CONFIG]   SIP User: '%s'\n", config.sipUser);
  }
}

void saveConfig() {
  strcpy(config.version, CONFIG_VERSION);
  EEPROM.put(CONFIG_START, config);
  EEPROM.commit();
}

void setDefaultConfig() {
  strcpy(config.version, CONFIG_VERSION);
  
  strcpy(config.ssid, "Your-WiFi-SSID");
  strcpy(config.password, "Your-WiFi-Password");
  strcpy(config.hostname, "ESP-Doorbell");
  
  config.useDHCP = false;
  strcpy(config.ip, "192.168.178.123");
  strcpy(config.router, "192.168.178.1");
  strcpy(config.subnet, "255.255.255.0");
  
  config.sipPort = 5060;
  strcpy(config.sipUser, "tuerklingel");
  strcpy(config.sipPassword, "xxxxxxx");
  strcpy(config.dialNumber, "**9");
  strcpy(config.dialText, "Tuerklingel 1");
  config.ringDuration = 30;
  
  config.sleepTimeout = 0;
  config.lightSleepEnabled = 1;
  config.inactivitySleepTimeout = 300;  // 5 minutes
  
  strcpy(config.apSSID, "ESP-Doorbell-Config");
  strcpy(config.apPassword, "12345678");
  
  config.debugSerial = true;
  config.debugWebSerial = true;
  
  strcpy(config.ntpServer, "pool.ntp.org");
  config.timezoneOffset = 3600; // UTC+1
}

// ====================================================================
// EVENT LOG FUNCTIONS
// ====================================================================

void loadEventLog() {
  DEBUG_PRINTF("[EVENT] Loading event log from EEPROM address %d\n", EVENTLOG_START);
  DEBUG_PRINTF("[EVENT] EventLog structure size: %d bytes\n", sizeof(EventLog));
  
  EEPROM.get(EVENTLOG_START, eventLog);
  
  /*
    // MM: quick and dirty flush old log..
    eventLog.count = -1;
    eventLog.writeIndex = -1;
  */
  /*
  DEBUG_PRINTF("[EVENT] Raw loaded values - count=%d, writeIndex=%d\n", 
               eventLog.count, eventLog.writeIndex);
  */

  // Validate
  if (eventLog.count < 0 || eventLog.count > 100000 || 
      eventLog.writeIndex < 0 || eventLog.writeIndex >= MAX_EVENTS) {
    DEBUG_PRINTLN("[EVENT] Invalid event log detected, resetting");
    DEBUG_PRINTF("[EVENT]   Invalid count: %d (expected 0-100000)\n", eventLog.count);
    DEBUG_PRINTF("[EVENT]   Invalid writeIndex: %d (expected 0-%d)\n", eventLog.writeIndex, MAX_EVENTS-1);
    
    eventLog.count = 0;
    eventLog.writeIndex = 0;
    memset(eventLog.events, 0, sizeof(eventLog.events));
    saveEventLog();
  } else {
    DEBUG_PRINTF("[EVENT] ✓ Loaded valid event log: %d total events, writeIndex=%d\n", 
                 eventLog.count, eventLog.writeIndex);
    
    // Debug: show last few events
    if (eventLog.count > 0) {
      int toShow = min(3, min(eventLog.count, MAX_EVENTS));
      DEBUG_PRINTF("[EVENT] Last %d events:\n", toShow);
      for (int i = 0; i < toShow; i++) {
        int index = (eventLog.writeIndex - 1 - i + MAX_EVENTS) % MAX_EVENTS;
        DoorbellEvent& evt = eventLog.events[index];
        DEBUG_PRINTF("[EVENT]   #%d: %s - %s\n", 
                     eventLog.count - i, 
                     formatTime(evt.timestamp).c_str(),
                     evt.sipSuccess ? "SUCCESS" : "FAILED");
      }
    }
  }
}

void saveEventLog() {
  DEBUG_PRINTF("[EVENT] saveEventLog() called - saving count=%d, writeIndex=%d\n", 
               eventLog.count, eventLog.writeIndex);
  EEPROM.put(EVENTLOG_START, eventLog);
  bool result = EEPROM.commit();
  DEBUG_PRINTF("[EVENT] saveEventLog() commit result: %s\n", result ? "SUCCESS" : "FAILED");
}

void logDoorbellEvent(bool success) {
  time_t now = time(nullptr);
  
  /*
  DEBUG_PRINTF("[EVENT] Logging event - timestamp=%lu, success=%d\n", 
               (unsigned long)now, success ? 1 : 0);
  DEBUG_PRINTF("[EVENT] Current state BEFORE - count=%d, writeIndex=%d\n", 
               eventLog.count, eventLog.writeIndex);
  */

  DoorbellEvent event;
  event.timestamp = now;
  event.sipSuccess = success;
  
  // Store in ring buffer at current write position
  eventLog.events[eventLog.writeIndex] = event;
  // DEBUG_PRINTF("[EVENT] Stored event at index %d (timestamp=%lu, success=%d)\n", eventLog.writeIndex, (unsigned long)event.timestamp, event.sipSuccess ? 1 : 0);
  
  // Update counters BEFORE saving
  int oldWriteIndex = eventLog.writeIndex;
  int oldCount = eventLog.count;
  
  eventLog.writeIndex = (eventLog.writeIndex + 1) % MAX_EVENTS;
  eventLog.count++;
  
  // DEBUG_PRINTF("[EVENT] Updated state - count=%d->%d, writeIndex=%d->%d\n", oldCount, eventLog.count, oldWriteIndex, eventLog.writeIndex);
  
  // Force flush any pending EEPROM operations
  EEPROM.commit();
  delay(10);
  
  // Now save everything to EEPROM
  EEPROM.put(EVENTLOG_START, eventLog);
  bool commitResult = EEPROM.commit();
  // DEBUG_PRINTF("[EVENT] EEPROM.commit() result: %s\n", commitResult ? "SUCCESS" : "FAILED");
  delay(50); // Give EEPROM time to settle
  
  // Verify it was saved correctly
  EventLog verify;
  EEPROM.get(EVENTLOG_START, verify);
  // DEBUG_PRINTF("[EVENT] Verification AFTER save - count=%d, writeIndex=%d\n", verify.count, verify.writeIndex);
  
  if (verify.count != eventLog.count || verify.writeIndex != eventLog.writeIndex) {
    DEBUG_PRINTLN("[EVENT] ⚠ WARNING: EEPROM verification FAILED!");
    DEBUG_PRINTF("[EVENT]   Expected: count=%d, writeIndex=%d\n", eventLog.count, eventLog.writeIndex);
    DEBUG_PRINTF("[EVENT]   Got:      count=%d, writeIndex=%d\n", verify.count, verify.writeIndex);
  } else {
    // DEBUG_PRINTLN("[EVENT] ✓ EEPROM verification PASSED");
  }
  
  DEBUG_PRINTF("[EVENT] Event #%d logged: %s - %s\n", 
               eventLog.count, formatTime(event.timestamp).c_str(), 
               success ? "SUCCESS" : "FAILED");
}

String formatTime(unsigned long timestamp) {
  if (timestamp < 1577836800) {  // Before Jan 1, 2020
    return "No time sync (ts: " + String(timestamp) + ")";
  }
  
  time_t t = (time_t)timestamp;
  struct tm* timeinfo = localtime(&t);
  
  if (timeinfo == nullptr) {
    return "Invalid time (ts: " + String(timestamp) + ")";
  }
  
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}

// ====================================================================
// TIME SYNC FUNCTION
// ====================================================================

void syncTime() {
  DEBUG_PRINTLN("[TIME] Synchronizing time...");
  DEBUG_PRINTF("[TIME] NTP Server: %s\n", config.ntpServer);
  DEBUG_PRINTF("[TIME] Timezone Offset: %d seconds (%+.1f hours)\n", 
               config.timezoneOffset, config.timezoneOffset / 3600.0);
  
  // Set timezone first
  setenv("TZ", "UTC", 1);
  tzset();
  
  // Configure time with timezone offset and DST offset (0 for no DST adjustment)
  configTime(config.timezoneOffset, 0, config.ntpServer, "time.google.com", "pool.ntp.org");
  
  DEBUG_PRINTLN("[TIME] Waiting for NTP sync...");
  int attempts = 0;
  time_t now = time(nullptr);
  
  // Wait for valid time (unix timestamp should be > year 2020)
  while (now < 1577836800 && attempts < 30) {  // 1577836800 = Jan 1, 2020
    delay(500);
    now = time(nullptr);
    attempts++;
    if (attempts % 5 == 0) {
      DEBUG_PRINTF("[TIME] Waiting... attempt %d/30 (current: %lu)\n", attempts, now);
    }
  }
  
  if (now > 1577836800) {
    DEBUG_PRINTLN("[TIME] ✓ Time synchronized successfully!");
    // DEBUG_PRINTF("[TIME] Unix timestamp: %lu\n", now);
    DEBUG_PRINTF("[TIME] Current time: %s\n", formatTime(now).c_str());
    
    /*
    // Verify the time makes sense
    struct tm* timeinfo = localtime(&now);
    if (timeinfo) {
      DEBUG_PRINTF("[TIME] Verification: Year=%d, Month=%d, Day=%d, Hour=%d, Min=%d, Sec=%d\n",
                   timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
                   timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    }
    */
  } else {
    DEBUG_PRINTLN("[TIME] ✗ Time sync FAILED - using default");
    DEBUG_PRINTF("[TIME] Last timestamp received: %lu\n", now);
  }
}

// ====================================================================
// WIFI FUNCTIONS
// ====================================================================

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(config.hostname);
  
  if (!config.useDHCP) {
    IPAddress ip, gw, subnet, dns;
    ip.fromString(config.ip);
    gw.fromString(config.router);
    subnet.fromString(config.subnet);
    dns.fromString(config.router);
    WiFi.config(ip, gw, subnet, dns);
  }
  
  WiFi.begin(config.ssid, config.password);
}

void setupAP() {
  WiFi.mode(WIFI_AP);
  
  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);
  
  WiFi.softAP(config.apSSID, config.apPassword);
  
  DEBUG_PRINTF("[AP] Started: %s\n", WiFi.softAPIP().toString().c_str());
}

// ====================================================================
// WEB SERVER FUNCTIONS
// ====================================================================

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/status", handleStatus);
  server.on("/events", handleEvents);
  server.on("/webserial", handleWebSerial);
}

void handleRoot() {
  lastActivityTime = millis();  // Reset sleep timer on web access
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='utf-8'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:20px;background:#f0f0f0}";
  html += ".container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}";
  html += "h1{color:#333;border-bottom:2px solid #4CAF50;padding-bottom:10px}";
  html += "h2{color:#555;margin-top:20px;font-size:18px}";
  html += "label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}";
  html += "input,select{width:100%;padding:8px;margin-bottom:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box}";
  html += "input[type='checkbox']{width:auto;margin-right:10px}";
  html += ".button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;font-size:16px;margin:5px}";
  html += ".button:hover{background:#45a049}";
  html += ".button-secondary{background:#2196F3}";
  html += ".button-secondary:hover{background:#0b7dda}";
  html += ".info{background:#e7f3fe;border-left:4px solid #2196F3;padding:10px;margin:10px 0}";
  html += ".success{background:#d4edda;border-left:4px solid #28a745;padding:10px;margin:10px 0}";
  html += ".warning{background:#fff3cd;border-left:4px solid #ffc107;padding:10px;margin:10px 0}";
  html += "small{color:#666;font-size:12px}";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🔔 ESP Doorbell Configuration</h1>";
  
  // main page navigation
  html += "<div class='navigation'>";
  html += "<a href='/events'>View Events</a> | ";
  html += "<a href='/status'>System Status</a> | ";
  html += "<a href='/webserial'>WebSerial</a> <br>";
  html += "</div>";

  // Info box
  html += "<div class='info'>";
  html += "<strong>WiFi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "<br>";
  if (WiFi.status() == WL_CONNECTED) {
    html += "<strong>IP:</strong> " + WiFi.localIP().toString() + "<br>";
  }
  html += "<strong>Events Logged:</strong> " + String(eventLog.count);
  html += "</div>";
  
  // Status box
  html += "<div class='";
  if (sipCallSuccess) {
    html += "success'>✓ Last doorbell call: SUCCESS";
  } else {
    html += "warning'>⚠ Last doorbell call: FAILED";
  }
  html += "</div>";
  
 
  
  html += "<form method='POST' action='/save'>";
  
  // WiFi Settings
  html += "<h2>📶 WiFi Settings</h2>";
  html += "<label>SSID:</label><input type='text' name='ssid' value='" + String(config.ssid) + "' required>";
  html += "<label>Password:</label><input type='password' name='password' value='" + String(config.password) + "'>";
  html += "<small>Leave blank to keep current</small>";
  html += "<label>Hostname:</label><input type='text' name='hostname' value='" + String(config.hostname) + "'>";
  
  // Network Settings
  html += "<h2>🌐 Network Settings</h2>";
  html += "<label><input type='checkbox' name='useDHCP' value='1' " + String(config.useDHCP ? "checked" : "") + "> Use DHCP</label>";
  html += "<label>Static IP:</label><input type='text' name='ip' value='" + String(config.ip) + "'>";
  html += "<small>prefer static IP for faster connects. Assign a static IP for this Doorbell at the FritzBox first</small><br>";
  html += "<label>Router/Gateway:</label><input type='text' name='router' value='" + String(config.router) + "'>";
  html += "<label>Subnet Mask:</label><input type='text' name='subnet' value='" + String(config.subnet) + "'>";
  
  // SIP Settings
  html += "<h2>📞 SIP Settings</h2>";
  html += "<small>register a phone device of type 'IP Door Intercom System' at your FritzBox to obtain the data</small><br>";
  
  html += "<label>SIP Port:</label><input type='number' name='sipPort' value='" + String(config.sipPort) + "' required>";
  html += "<label>SIP User:</label><input type='text' name='sipUser' value='" + String(config.sipUser) + "' required>";
  html += "<label>SIP Password:</label><input type='password' name='sipPassword' value='" + String(config.sipPassword) + "'>";
  html += "<label>Dial Number:</label><input type='text' name='dialNumber' value='" + String(config.dialNumber) + "' required>";
  html += "<small>Examples: use '1', so you can configure via FritzBox 'doorbell button 1' or '**9' which should ring on all phones registered at FritzBox</small><br>";
  html += "<label>Dial Text:</label><input type='text' name='dialText' value='" + String(config.dialText) + "'>";
  html += "<small>Example: 'Tuerklingel' - not shown in all cases</small><br>";
  html += "<label>Ring Duration (sec):</label><input type='number' name='ringDuration' value='" + String(config.ringDuration) + "' min='5' max='120' required>";

  // Time Settings
  html += "<h2>🕐 Time Settings</h2>";
  html += "<label>NTP Server:</label><input type='text' name='ntpServer' value='" + String(config.ntpServer) + "'>";
  html += "<small>Default: pool.ntp.org</small>";
  html += "<label>Timezone Offset (seconds from UTC):</label><input type='number' name='timezoneOffset' value='" + String(config.timezoneOffset) + "'>";
  html += "<small>Examples: UTC+1/CET=3600, UTC+2/CEST=7200</small><br>";
  html += "<small><b>Current system time: " + formatTime(time(nullptr)) + "</b></small>";
  
  // Power Settings
  html += "<h2>⚡ Power Management</h2>";
  html += "<label><input type='checkbox' name='lightSleepEnabled' value='1' " + String(config.lightSleepEnabled ? "checked" : "") + "> Enable Light Sleep</label>";
  html += "<small>Light sleep saves power while keeping WiFi connection active</small>";
  html += "<label>Inactivity Sleep Timeout (sec):</label><input type='number' name='inactivitySleepTimeout' value='" + String(config.inactivitySleepTimeout) + "' min='0' required>";
  html += "<small>Enter light sleep after this many seconds of inactivity (0 to disable)</small>";
  html += "<label>Legacy Deep Sleep Timeout (sec):</label><input type='number' name='sleepTimeout' value='" + String(config.sleepTimeout) + "' min='0' required>";
  html += "<small>Deep sleep timeout - only used if explicitly triggered (0 to disable)</small>";
  
  // Debug Settings
  html += "<h2>🐛 Debug Settings</h2>";
  html += "<label><input type='checkbox' name='debugSerial' value='1' " + String(config.debugSerial ? "checked" : "") + "> Serial Debug</label>";
  html += "<label><input type='checkbox' name='debugWebSerial' value='1' " + String(config.debugWebSerial ? "checked" : "") + "> WebSerial Debug</label>";
  
  html += "<br><br>";
  html += "<button type='submit' class='button'>💾 Save & Reboot</button>";
  html += "</form>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  lastActivityTime = millis();
  
  DEBUG_PRINTLN("[SAVE] Saving configuration...");
  
  if (server.hasArg("ssid")) {
    strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid) - 1);
    config.ssid[sizeof(config.ssid) - 1] = '\0';
    DEBUG_PRINTF("[SAVE] SSID: '%s'\n", config.ssid);
  }
  
  if (server.hasArg("password") && server.arg("password").length() > 0) {
    strncpy(config.password, server.arg("password").c_str(), sizeof(config.password) - 1);
    config.password[sizeof(config.password) - 1] = '\0';
    DEBUG_PRINTLN("[SAVE] Password updated");
  }
  
  if (server.hasArg("hostname")) {
    strncpy(config.hostname, server.arg("hostname").c_str(), sizeof(config.hostname) - 1);
    config.hostname[sizeof(config.hostname) - 1] = '\0';
    DEBUG_PRINTF("[SAVE] Hostname: %s\n", config.hostname);
  }
  
  config.useDHCP = server.hasArg("useDHCP");
  DEBUG_PRINTF("[SAVE] DHCP: %s\n", config.useDHCP ? "true" : "false");
  
  if (server.hasArg("ip")) {
    strncpy(config.ip, server.arg("ip").c_str(), sizeof(config.ip) - 1);
    config.ip[sizeof(config.ip) - 1] = '\0';
    DEBUG_PRINTF("[SAVE] IP: %s\n", config.ip);
  }
  
  if (server.hasArg("router")) {
    strncpy(config.router, server.arg("router").c_str(), sizeof(config.router) - 1);
    config.router[sizeof(config.router) - 1] = '\0';
    DEBUG_PRINTF("[SAVE] Router: %s\n", config.router);
  }
  
  if (server.hasArg("subnet")) {
    strncpy(config.subnet, server.arg("subnet").c_str(), sizeof(config.subnet) - 1);
    config.subnet[sizeof(config.subnet) - 1] = '\0';
    DEBUG_PRINTF("[SAVE] Subnet: %s\n", config.subnet);
  }
  
  if (server.hasArg("sipPort")) {
    config.sipPort = server.arg("sipPort").toInt();
    DEBUG_PRINTF("[SAVE] SIP Port: %d\n", config.sipPort);
  }
  
  if (server.hasArg("sipUser")) {
    strncpy(config.sipUser, server.arg("sipUser").c_str(), sizeof(config.sipUser) - 1);
    config.sipUser[sizeof(config.sipUser) - 1] = '\0';
  }
  
  if (server.hasArg("sipPassword") && server.arg("sipPassword").length() > 0) {
    strncpy(config.sipPassword, server.arg("sipPassword").c_str(), sizeof(config.sipPassword) - 1);
    config.sipPassword[sizeof(config.sipPassword) - 1] = '\0';
    DEBUG_PRINTLN("[SAVE] SIP password updated");
  }
  
  if (server.hasArg("dialNumber")) {
    strncpy(config.dialNumber, server.arg("dialNumber").c_str(), sizeof(config.dialNumber) - 1);
    config.dialNumber[sizeof(config.dialNumber) - 1] = '\0';
  }
  
  if (server.hasArg("dialText")) {
    strncpy(config.dialText, server.arg("dialText").c_str(), sizeof(config.dialText) - 1);
    config.dialText[sizeof(config.dialText) - 1] = '\0';
  }
  
  if (server.hasArg("ringDuration")) {
    config.ringDuration = server.arg("ringDuration").toInt();
    DEBUG_PRINTF("[SAVE] Ring duration: %d\n", config.ringDuration);
  }
  
  config.debugSerial = server.hasArg("debugSerial");
  config.debugWebSerial = server.hasArg("debugWebSerial");
  DEBUG_PRINTF("[SAVE] Debug - Serial: %s, WebSerial: %s\n", 
               config.debugSerial ? "ON" : "OFF",
               config.debugWebSerial ? "ON" : "OFF");
  
  if (server.hasArg("ntpServer")) {
    strncpy(config.ntpServer, server.arg("ntpServer").c_str(), sizeof(config.ntpServer) - 1);
    config.ntpServer[sizeof(config.ntpServer) - 1] = '\0';
  }
  
  if (server.hasArg("timezoneOffset")) {
    config.timezoneOffset = server.arg("timezoneOffset").toInt();
  }
  
  if (server.hasArg("sleepTimeout")) {
    config.sleepTimeout = server.arg("sleepTimeout").toInt();
    DEBUG_PRINTF("[SAVE] Sleep timeout: %d\n", config.sleepTimeout);
  }
  
  config.lightSleepEnabled = server.hasArg("lightSleepEnabled");
  DEBUG_PRINTF("[SAVE] Light sleep: %s\n", config.lightSleepEnabled ? "ENABLED" : "DISABLED");
  
  if (server.hasArg("inactivitySleepTimeout")) {
    config.inactivitySleepTimeout = server.arg("inactivitySleepTimeout").toInt();
    DEBUG_PRINTF("[SAVE] Inactivity sleep timeout: %d\n", config.inactivitySleepTimeout);
  }
 
  saveConfig();
  DEBUG_PRINTLN("[SAVE] Configuration saved to EEPROM");
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='10;url=/'>";
  html += "<style>body{font-family:Arial;text-align:center;padding:50px}</style>";
  html += "<meta charset='utf-8'></head><body>";
  html += "<h1>✓ Configuration Saved!</h1>";
  html += "<p>Device will reboot in 10 seconds...</p>";
  html += "<p>After reboot, ";
  if (strlen(config.ssid) > 1 && strcmp(config.ssid, "Your-WiFi-SSID") != 0) {
    html += "it will try to connect to: <strong>" + String(config.ssid) + "</strong></p>";
    html += "<p>If successful, access at: <strong>http://";
    html += config.useDHCP ? "assigned-by-dhcp" : String(config.ip);
    html += "</strong></p>";
  } else {
    html += "AP mode will be available at: <strong>http://192.168.4.1</strong></p>";
  }
  html += "</body></html>";
  
  server.send(200, "text/html", html);
  
  DEBUG_PRINTLN("[SAVE] Rebooting in 10 seconds...");
  delay(10000);
  ESP.restart();
}

void handleStatus() {
  lastActivityTime = millis();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='utf-8'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:10px;background:#f0f0f0}";
  html += ".container{max-width:800px;margin:0 auto;background:white;padding:15px;border-radius:8px;box-shadow:0 2px 5px rgba(0,0,0,0.1)}";
  html += "h1{color:#333;font-size:24px;margin:10px 0;border-bottom:2px solid #4CAF50;padding-bottom:10px}";
  html += ".nav{margin:15px 0;font-size:14px}";
  html += ".nav a{color:#2196F3;text-decoration:none;margin-right:15px}";
  html += ".section{margin:15px 0;border:1px solid #ddd;border-radius:5px;overflow:hidden}";
  html += ".section-title{background:#4CAF50;color:white;padding:10px;font-weight:bold;font-size:16px}";
  html += ".row{display:flex;border-bottom:1px solid #eee;flex-wrap:wrap}";
  html += ".row:last-child{border-bottom:none}";
  html += ".label{flex:1;min-width:120px;padding:10px;background:#f9f9f9;font-weight:bold;color:#555}";
  html += ".value{flex:2;min-width:150px;padding:10px;word-break:break-word}";
  html += "@media (max-width: 480px){";
  html += "  .row{flex-direction:column}";
  html += "  .label,.value{min-width:100%;padding:8px}";
  html += "  .label{background:#f0f0f0;border-bottom:1px solid #ddd}";
  html += "  h1{font-size:20px}";
  html += "}";
  html += "</style>";
  html += "</head><body>";
  
  html += "<div class='container'>";
  html += "<h1>📊 System Status</h1>";
  html += "<div class='nav'><a href='/'>← Config</a> <a href='/events'>Events</a> <a href='/webserial'>WebSerial</a></div>";
  
  html += "<div class='section'>";
  html += "<div class='section-title'>System Information</div>";
  html += "<div class='row'><div class='label'>Uptime</div><div class='value'>" + String(millis() / 1000) + " seconds</div></div>";
  html += "<div class='row'><div class='label'>Free Heap</div><div class='value'>" + String(ESP.getFreeHeap()) + " bytes</div></div>";
  html += "<div class='row'><div class='label'>Current Time</div><div class='value'>" + formatTime(time(nullptr)) + "</div></div>";
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<div class='section-title'>WiFi Status</div>";
  html += "<div class='row'><div class='label'>Connection</div><div class='value'>" + String(WiFi.status() == WL_CONNECTED ? "✓ Connected" : "✗ Disconnected") + "</div></div>";
  if (WiFi.status() == WL_CONNECTED) {
    html += "<div class='row'><div class='label'>SSID</div><div class='value'>" + WiFi.SSID() + "</div></div>";
    html += "<div class='row'><div class='label'>IP Address</div><div class='value'>" + WiFi.localIP().toString() + "</div></div>";
    html += "<div class='row'><div class='label'>Signal</div><div class='value'>" + String(WiFi.RSSI()) + " dBm</div></div>";
  }
  html += "</div>";
  
  html += "<div class='section'>";
  html += "<div class='section-title'>Last Doorbell Call</div>";
  html += "<div class='row'><div class='label'>Status</div><div class='value'>" + String(sipCallSuccess ? "✓ SUCCESS" : "✗ FAILED") + "</div></div>";
  if (eventLog.count > 0) {
    int lastIndex = (eventLog.writeIndex - 1 + MAX_EVENTS) % MAX_EVENTS;
    html += "<div class='row'><div class='label'>Time</div><div class='value'>" + formatTime(eventLog.events[lastIndex].timestamp) + "</div></div>";
  }
  html += "</div>";
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

void handleEvents() {
  lastActivityTime = millis();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='utf-8'>";
  html += "<style>body{font-family:Arial;margin:20px}table{border-collapse:collapse;width:100%}td,th{border:1px solid #ddd;padding:8px;text-align:left}th{background:#4CAF50;color:white}.success{color:#28a745;font-weight:bold}.fail{color:#dc3545;font-weight:bold}</style>";
  html += "</head><body>";
  html += "<h1>🔔 Doorbell Event Log</h1>";
  html += "<p><a href='/'>← Config</a> <a href='/status'>Status</a>  <a href='/webserial'>WebSerial</a></p>";

  html += "<p><strong>Total Events:</strong> " + String(eventLog.count) + " | ";
  html += "<strong>Storage:</strong> " + String(MAX_EVENTS) + " events max</p>";
  
  html += "<table><tr><th>#</th><th>Date & Time</th><th>Status</th></tr>";
  
  // Show events in reverse order (newest first)
  int displayCount = min(eventLog.count, MAX_EVENTS);
  for (int i = 0; i < displayCount; i++) {
    int index = (eventLog.writeIndex - 1 - i + MAX_EVENTS) % MAX_EVENTS;
    DoorbellEvent& event = eventLog.events[index];
    
    html += "<tr>";
    html += "<td>" + String(eventLog.count - i) + "</td>";
    html += "<td>" + formatTime(event.timestamp) + "</td>";
    html += "<td class='" + String(event.sipSuccess ? "success" : "fail") + "'>";
    html += event.sipSuccess ? "✓ SUCCESS" : "✗ FAILED";
    html += "</td>";
    // html += "<td>";
    /*
    switch(event.wakeReason) {
      case REASON_DEFAULT_RST: html += "Power-on"; break;
      case REASON_DEEP_SLEEP_AWAKE: html += "Deep Sleep Wake"; break;
      case REASON_SOFT_RESTART: html += "Software Reset"; break;
      case REASON_EXT_SYS_RST: html += "External Reset"; break;
      case REASON_WDT_RST: html += "Hardware Watchdog"; break;
      case REASON_EXCEPTION_RST: html += "Exception"; break;
      case REASON_SOFT_WDT_RST: html += "Software Watchdog"; break;
      default: html += "Unknown (" + String(event.wakeReason) + ")"; break;
    }
    html += "</td></tr>";
    */
    html += "</tr>";
  }
  
  if (displayCount == 0) {
    html += "<tr><td colspan='4' style='text-align:center;padding:20px'>No events logged yet</td></tr>";
  }
  
  html += "</table>";
  html += "<br><p><small>Times are displayed in configured timezone (UTC";
  if (config.timezoneOffset >= 0) html += "+";
  html += String(config.timezoneOffset / 3600) + ")</small></p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleWebSerial() {
  lastActivityTime = millis();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta http-equiv='refresh' content='5'>";
  html += "<meta charset='utf-8'>";
  html += "<style>body{font-family:monospace;margin:20px;background:#1e1e1e;color:#d4d4d4}";
  html += ".container{background:#252526;padding:15px;border-radius:5px;max-width:1000px;margin:0 auto}";
  html += "pre{white-space:pre-wrap;word-wrap:break-word;margin:0}";
  html += "a{color:#4CAF50}</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h2 style='color:#4CAF50'>🖥️ WebSerial Debug Console</h2>";
  html += "<p><a href='/'>← Back to Config</a> <a href='/status'>Status</a> <a href='/events'>Events</a> | Auto-refresh: 5s</p>";

  
  if (config.debugWebSerial) {
    html += "<pre>" + String(webSerialBuffer) + "</pre>";
  } else {
    html += "<p style='color:#ff6b6b'>⚠ WebSerial debugging is disabled. Enable in configuration.</p>";
  }
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

// ====================================================================
// DOORBELL INTERRUPT AND HANDLER
// ====================================================================

// Interrupt service routine - must be in IRAM and very fast
void IRAM_ATTR doorbellISR() {
  doorbellInterruptFlag = true;
}

void handleDoorbellPress() {
  unsigned long now = millis();
  
  // Debounce check
  if (now - lastDoorbellPress < DEBOUNCE_MS) {
    DEBUG_PRINTF("[DOORBELL] Ignored - debounce (only %lu ms since last press)\n", 
                 now - lastDoorbellPress);
    return;
  }
  
  // Verify button is still pressed (not noise)
  if (digitalRead(DOORBELL_PIN) != LOW) {
    DEBUG_PRINTLN("[DOORBELL] Ignored - button not pressed (noise)");
    return;
  }
  
  // Wait for stable press
  delay(BUTTON_HOLD_MS);
  if (digitalRead(DOORBELL_PIN) != LOW) {
    DEBUG_PRINTLN("[DOORBELL] Ignored - button press too short");
    return;
  }
  
  // DEBUG_PRINTLN("\n====================================");
  DEBUG_PRINTLN("[DOORBELL] *** VALID PRESS DETECTED ***");
  // DEBUG_PRINTLN("====================================");
  
  lastDoorbellPress = now;
  lastActivityTime = now;
  
  digitalWrite(LED_PIN, LOW); // LED on
  
  // Check WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("[WIFI] ERROR: WiFi not connected!");
    blinkLED(10, 100);
    digitalWrite(LED_PIN, HIGH);
    logDoorbellEvent(false);
    return;
  }
  
  // Initialize SIP if not already active
  if (aSip == nullptr) {
    DEBUG_PRINTLN("[CALL] Initializing SIP...");
    aSip = new Sip(caSipOut, sizeof(caSipOut));
    aSip->Init(config.router, config.sipPort, 
               config.useDHCP ? WiFi.localIP().toString().c_str() : config.ip, 
               config.sipPort, config.sipUser, config.sipPassword, config.ringDuration);
    delay(100);
  }
  
  // Make the call
  DEBUG_PRINTF("[CALL] Dialing: %s (%s)\n", config.dialNumber, config.dialText);
  aSip->Dial(config.dialNumber, config.dialText);
  
  blinkLED(3, 100);
  
  // Keep SIP active for ring duration
  unsigned long callStart = millis();
  while (millis() - callStart < (config.ringDuration * 1000)) {
    int packetSize = aSip->Udp.parsePacket();
    if (packetSize > 0) {
      caSipIn[0] = 0;
      packetSize = aSip->Udp.read(caSipIn, sizeof(caSipIn));
      if (packetSize > 0) {
        caSipIn[packetSize] = 0;
      }
    }
    aSip->HandleUdpPacket((packetSize > 0) ? caSipIn : 0);
    
    // Also handle web server during call
    server.handleClient();
    delay(10);
  }
  
  digitalWrite(LED_PIN, HIGH); // LED off
  
  DEBUG_PRINTLN("[CALL] Call completed successfully!");
  // DEBUG_PRINTLN("====================================\n");
  
  // Log the event
  logDoorbellEvent(true);
  
  sipCallSuccess = true;
  sipCallAttempted = true;
}

// ====================================================================
// LIGHT SLEEP FUNCTIONS
// ====================================================================

void checkLightSleep() {
  if (!config.lightSleepEnabled || config.inactivitySleepTimeout == 0) {
    return;
  }
  
  unsigned long inactiveTime = (millis() - lastActivityTime) / 1000;
  
  if (inactiveTime >= config.inactivitySleepTimeout) {
    enterLightSleep();
  }
}

void enterLightSleep() {
  DEBUG_PRINTLN("\n====================================");
  DEBUG_PRINTLN("[SLEEP] Entering light sleep mode...");
  DEBUG_PRINTLN("[SLEEP] Will wake on:");
  DEBUG_PRINTLN("[SLEEP]   - Doorbell button press");
  DEBUG_PRINTLN("[SLEEP]   - WiFi activity");
  DEBUG_PRINTLN("====================================\n");
  
  // Flush debug output
  Serial.flush();
  delay(10);
  
  // Configure wake sources
  // WiFi will wake automatically on incoming packets
  // GPIO interrupt will wake on doorbell press
  
  // Enter light sleep until any interrupt
  wifi_set_sleep_type(LIGHT_SLEEP_T);
  
  // Reset activity timer when we wake
  lastActivityTime = millis();
  
  DEBUG_PRINTLN("[SLEEP] Woke from light sleep");
}

// ====================================================================
// DEEP SLEEP FUNCTION (Legacy - kept for compatibility)
// ====================================================================

void checkDeepSleep() {
  // This is now legacy - light sleep is preferred
  // Only used if explicitly needed for very low power scenarios
  if (config.sleepTimeout == 0) return;
  
  unsigned long inactiveTime = (millis() - lastActivityTime) / 1000;
  
  if (inactiveTime >= config.sleepTimeout) {
    DEBUG_PRINTLN("\n====================================");
    DEBUG_PRINTLN("[SLEEP] Entering deep sleep...");
    DEBUG_PRINTLN("[SLEEP] Press doorbell button or reset to wake");
    DEBUG_PRINTLN("====================================\n");
    
    // Cleanup
    if (aSip != nullptr) {
      delete aSip;
      aSip = nullptr;
    }
    server.stop();
    WiFi.disconnect();
    WiFi.softAPdisconnect();
    
    // Blink to indicate sleep
    for (int i = 0; i < 5; i++) {
      digitalWrite(LED_PIN, LOW);
      delay(100);
      digitalWrite(LED_PIN, HIGH);
      delay(100);
    }
    
    delay(100);
    ESP.deepSleep(0); // Sleep until hardware reset
  }
}

// ====================================================================
// UTILITY FUNCTIONS
// ====================================================================

void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
  }
}
