# ESP-SIP-Doorbell
Attach an ESP8266 board to a classic doorbell and use SIP protocol to make the phones connected to a FritzBox ring.

Hardware setup can be found here: https://www.reichelt.de/magazin/projekte/smarte-tuerklingel/
Again: This version is not TR064 based, but uses SIP for simplicity.


# ESP8266 SIP Doorbell - Getting Started Guide

## 🔔 Overview

This ESP8266-based doorbell connects to your FritzBox (or other SIP server) and makes instant phone calls when triggered. The device prioritizes the doorbell function above all else - when powered on or reset, it immediately attempts to connect and ring your phones.

---

## ⚡ Key Features

### 1. **Priority Ring Architecture**
- **Immediate SIP call on wake/reset** - no delays, rings first
- Fast WiFi connection (10 second timeout)
- SIP initialization and call within seconds
- All configuration and management happens AFTER the call

### 2. **Dual Debug System**
- **Serial Debug**: Traditional UART debugging at 115200 baud
- **WebSerial**: Browser-based debug console accessible via web interface
- Both can be enabled/disabled independently via configuration

### 3. **Event Logging**
- Persistent storage of last 50 doorbell events
- Each event records:
  - Timestamp (human-readable)
  - Success/failure status
  - Wake reason (power-on, deep sleep, reset, etc.)
- Survives reboots and deep sleep cycles

### 4. **Time Synchronization**
- Automatic NTP time sync after initial call
- Configurable NTP server (default: pool.ntp.org)
- Configurable timezone offset
- Falls back to FritzBox/router as time source

### 5. **Smart Power Management**
- Configurable deep sleep timeout (default: 180 seconds)
- Automatic sleep after inactivity
- Wake on hardware reset (doorbell button)
- Set timeout to 0 to disable deep sleep

### 6. **Flexible Network Configuration**
- DHCP or static IP
- WiFi station mode for normal operation
- **Automatic AP fallback** when WiFi connection fails
- Web-based configuration portal always available

### 7. **Complete SIP Integration**
- Compatible with FritzBox and standard SIP servers
- Configurable ring duration (5-120 seconds)
- Custom dial text/caller ID
- Support for special dial codes (e.g., **9 for all phones)

---

## 🚀 First-Time Setup

### Hardware Requirements
- ESP8266 board (ESP-01, NodeMCU, Wemos D1, etc.)
- Doorbell button connected between GPIO0 (D3) and GND
- For deep sleep wake: RST pin connected to doorbell button circuit
- USB power or battery power supply

### Initial Configuration

1. **Power on the ESP8266**
   - On first boot, default WiFi credentials won't work
   - Device automatically starts AP mode for configuration

2. **Connect to the Configuration AP**
   - SSID: `ESP-Doorbell-Config`
   - Password: `12345678`
   - Device IP: `192.168.4.1`

3. **Open the Configuration Portal**
   - Open browser and navigate to: `http://192.168.4.1`
   - You'll see the main configuration page

4. **Configure WiFi Settings**
   - **SSID**: Your home WiFi network name
   - **Password**: Your WiFi password
   - **Hostname**: Device hostname (default: ESP-Doorbell)

5. **Configure Network Settings**
   - **Use DHCP**: Check for automatic IP (recommended for most users)
   - **Static IP**: Only if DHCP is unchecked (e.g., 192.168.178.123)
   - **Router/Gateway**: Your FritzBox IP (e.g., 192.168.178.1)
   - **Subnet Mask**: Usually 255.255.255.0

6. **Configure SIP Settings** (Critical!)
   - **SIP Port**: Usually 5060 (FritzBox default)
   - **SIP User**: Phone extension username (e.g., "tuerklingel")
   - **SIP Password**: Extension password from FritzBox
   - **Dial Number**: Number to dial (e.g., **9 for all phones, or specific extension)
   - **Dial Text**: Caller ID text (e.g., "Front Door")
   - **Ring Duration**: How long phones ring (default: 30 seconds)

7. **Configure Debug Options**
   - **Serial Debug**: Enable for USB/UART debugging
   - **WebSerial Debug**: Enable for browser-based debug console
   - Both can be enabled simultaneously

8. **Configure Time Settings**
   - **NTP Server**: Time server (default: pool.ntp.org)
   - **Timezone Offset**: Seconds from UTC
     - UTC+1 (CET): 3600
     - UTC+2 (CEST): 7200
     - UTC-5 (EST): -18000

9. **Configure Power Management**
   - **Sleep Timeout**: Seconds before deep sleep (default: 180)
   - Set to 0 to disable deep sleep
   - Device stays awake longer when accessed via web interface

10. **Save Configuration**
    - Click "Save & Reboot"
    - Device will reboot in 10 seconds
    - If WiFi connection succeeds, AP mode will be disabled
    - If WiFi fails, AP mode will remain active for reconfiguration

---

## 📞 FritzBox SIP Configuration

### Creating a Doorbell Extension

1. **Log into FritzBox**
   - Navigate to `http://fritz.box`
   - Go to "Telephony" → "Telephone Numbers"

2. **Create New Phone Extension**
   - Click "New Device"
   - Select "Telephone (with or without answering machine)"
   - Give it a name (e.g., "Tuerklingel" or "Doorbell")
   - Assign a username and password (use these in ESP config)

3. **Configure Call Routing**
   - For **9 dialing: Go to "Telephony" → "Call Handling"
   - Set up internal call rules if needed
   - **9 typically rings all registered phones

4. **Test the Extension**
   - Note the username, password, and dial number
   - Enter these in the ESP8266 configuration

---

## 🔌 Hardware Wiring

### Basic Wiring (Power-on Reset)
```
Doorbell Button → GPIO0 (D3) and GND
Power Supply → 3.3V/5V and GND
```

### Deep Sleep Wiring (Recommended)
```
Doorbell Button → RST pin and GND (via 10kΩ pull-up)
Additional: Button → GPIO0 (for programming mode)
Power Supply → 3.3V/5V and GND
```

### LED Indicator
- Built-in LED on GPIO2 (D4) - inverted logic
- ON = SIP call in progress
- Blinks = various status indicators

---

## 🌐 Web Interface Features

### Main Configuration Page (`/`)
- Complete device configuration
- WiFi, SIP, debug, and power settings
- Shows current connection status
- Links to other management pages

### Status Page (`/status`)
- Real-time system information
- WiFi connection details
- Last doorbell call status
- Uptime and memory usage
- Auto-refreshes every 5 seconds

### Event Log (`/events`)
- Last 50 doorbell events
- Timestamp for each event
- Success/failure status
- Wake reason for each trigger
- Sorted newest to oldest

### WebSerial Console (`/webserial`)
- Live debug output in browser
- All serial debug messages
- Auto-refreshes every 5 seconds
- Only works if WebSerial debug is enabled

---

## 🐛 Troubleshooting

### Doorbell Not Ringing

1. **Check WiFi Connection**
   - Access status page
   - Verify "Connected" status
   - Check signal strength (should be > -70 dBm)

2. **Verify SIP Credentials**
   - Double-check username and password
   - Ensure FritzBox extension is active
   - Test dial number works from another phone

3. **Check Event Log**
   - Look for "FAILED" entries
   - Check timestamps to verify button presses are detected

4. **Enable Debug Output**
   - Turn on Serial and/or WebSerial debugging
   - Watch for error messages during call attempts

### Can't Access Configuration

1. **WiFi Connection Failed**
   - Device will automatically start AP mode
   - Connect to `ESP-Doorbell-Config` network
   - Access `http://192.168.4.1`

2. **Forgot IP Address**
   - Check your router's DHCP client list
   - Look for hostname (default: ESP-Doorbell)
   - Or power cycle to trigger AP mode

3. **AP Mode Not Working**
   - Reset device completely
   - Check AP settings in configuration
   - Default AP: `ESP-Doorbell-Config` / `12345678`

### Deep Sleep Issues

1. **Device Won't Wake**
   - Verify RST pin is connected to doorbell button
   - Check for proper pull-up resistor (10kΩ)
   - Try setting sleep timeout to 0 (disabled)

2. **Device Sleeps Too Quickly**
   - Increase sleep timeout in configuration
   - Default: 180 seconds (3 minutes)
   - Any web access resets the timeout

### Time Not Syncing

1. **Check Network Connection**
   - Verify internet access from your network
   - Try changing NTP server
   - Alternative: `time.google.com` or `time.nist.gov`

2. **Timezone Issues**
   - Verify timezone offset is correct
   - Calculate: Hours × 3600 seconds

---

## 💡 Tips & Best Practices

### Optimal Performance
- Use static IP for faster connection
- Place ESP8266 close to WiFi router
- Use quality power supply (stable 5V/3.3V)
- Keep ring duration reasonable (20-40 seconds)

### Reliability
- Enable both Serial and WebSerial debugging initially
- Monitor event log regularly
- Set conservative sleep timeout initially (300+ seconds)
- Test thoroughly before final installation

### Security
- Change default AP password immediately
- Use strong SIP password
- Keep ESP8266 firmware updated
- Consider MAC filtering on FritzBox

### Debugging
- WebSerial is invaluable for installed devices
- Check event log for patterns
- Monitor free heap (should stay > 20KB)
- Watch for WiFi signal strength drops

---

## 📝 Configuration Quick Reference

| Setting | Default | Notes |
|---------|---------|-------|
| WiFi SSID | Your-WiFi-SSID | Must configure |
| WiFi Password | Your-WiFi-Password | Must configure |
| Hostname | ESP-Doorbell | Optional |
| DHCP | Disabled | Enable for simplicity |
| Static IP | 192.168.178.123 | Only if DHCP off |
| Router/Gateway | 192.168.178.1 | FritzBox IP |
| SIP Port | 5060 | Standard |
| SIP User | tuerklingel | From FritzBox |
| SIP Password | xxxxxxx | From FritzBox |
| Dial Number | **9 | All phones |
| Ring Duration | 30 seconds | Adjust as needed |
| Serial Debug | Enabled | For USB debugging |
| WebSerial Debug | Enabled | For remote debugging |
| NTP Server | pool.ntp.org | Time server |
| Timezone | 3600 | UTC+1 (CET) |
| Sleep Timeout | 180 seconds | 3 minutes |
| AP SSID | ESP-Doorbell-Config | Change if desired |
| AP Password | 12345678 | Change immediately! |

---

## 🔄 Operation Flow

1. **Doorbell Pressed** → Hardware reset triggered
2. **ESP8266 Powers On** → Boots in ~100ms
3. **Configuration Loaded** → From EEPROM
4. **WiFi Connection** → 10 second timeout
5. **SIP Initialized** → Immediate if WiFi succeeds
6. **Call Made** → Phones start ringing!
7. **Event Logged** → Saved to persistent storage
8. **Time Synced** → NTP after call completes
9. **Web Server Started** → Configuration available
10. **Management Mode** → Waits for next event or sleep

---

## 📊 Monitoring

### Regular Checks
- **Event Log**: Review for failed calls
- **WebSerial**: Check for error messages
- **Status Page**: Monitor WiFi signal strength
- **Free Heap**: Should stay relatively stable

### Warning Signs
- Increasing failed call rate
- Decreasing free heap memory
- WiFi signal below -80 dBm
- Frequent unexpected resets

---

## 🆘 Support

For issues not covered here:
1. Check WebSerial debug output
2. Review event log for patterns
3. Verify FritzBox extension is working
4. Test with minimal configuration
5. Check power supply stability

---

**Version**: 3.0 (Priority Ring Architecture)  
**Last Updated**: 2025

---

## 🎯 Quick Start Summary

1. Power on ESP8266
2. Connect to `ESP-Doorbell-Config` WiFi (password: `12345678`)
3. Open `http://192.168.4.1` in browser
4. Configure WiFi and SIP settings
5. Save and reboot
6. Press doorbell button to test
7. Check event log and status page
8. Adjust settings as needed


For deep sleep wake functionality, you need to wire the doorbell button:
Button --[10kΩ]-- 3.3V
       |
       +---------- RST pin
       |
       +---------- GND (when pressed)


       
