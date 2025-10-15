# ESP SIP Doorbell - Getting Started Guide
Attach an ESP8266 or ESP32 board to a classic doorbell and use SIP protocol to make the phones connected to a FritzBox ring.

This ESP8266-based doorbell connects to your FritzBox (or other SIP server) and makes instant phone calls when triggered. The device prioritizes the doorbell function above all else - when powered on or reset, it immediately attempts to connect and ring your phones.

- Hardware setup can be found here: https://www.reichelt.de/magazin/projekte/smarte-tuerklingel/
- or: https://www.heise.de/select/ct/2017/17/1502995489716437
- or: https://www.heise.de/select/ct/2018/17/1534215254552977
- or: http://www.roehrenkramladen.de/Tuerklingel/TK-FB-V1a-1.html
There are probably even board schematics around, but breadboard-style wiring should be fine.

And again: This ESP-Software is **not** TR064 based, but uses *SIP* for simplicity and security.

---

## ⚡ Key Features
- Immediate SIP call on wake/reset: SIP initialization and call within seconds
- All configuration and management happens AFTER the call. Also the first configuration
- Starts an Access Point for the initial configuration (see below)
- **Serial Debug**: Traditional UART debugging at 115200 baud
- **WebSerial**: Browser-based debug console accessible via web interface (especially, if you cannot physically connect to the board)
- Both can be enabled/disabled independently via configuration
- Persistent storage of last 100 doorbell events
- Automatic time sync after initial call; configurable NTP server (default: pool.ntp.org) and timezone offset
- Configurable timezone offset
- **Smart Power Management**: Configurable deep sleep timeout (default: 180 seconds)
- Set timeout to 0 to disable deep sleep
- Wake on hardware reset (doorbell button)
- **Flexible Network Configuration**: DHCP or static IP, WiFi station mode for normal operation
- **Automatic AP fallback** when WiFi connection fails
- Web-based configuration portal, status, past events, WebSerial always available
- **Complete SIP Integration**: Compatible with FritzBox and standard SIP servers
- Configurable ring duration (5-120 seconds) and dial text

---

## 🚀 First-Time Setup

### Hardware and Software Requirements
- ESP8266 board (ESP-01, NodeMCU, Wemos D1, etc.)
- or ESP32 board (e.g. a tiny ESP32-C3 SuperMini will do)
- USB power or battery power supply
- Adapter to connect to existing, physical door bell circuit (with AC tansformer)
- Connect the Doorbell Button circuit between GPIO14 and GND. Will bring the ESP out of Light Sleep
- OBSOLETE: Doorbell button connected between RST (Reset) and GND - TODO: Might change to better solution
- OBSOLETE: For deep sleep wake: RST pin connected to doorbell button circuit
- Compile with **Arduino IDE** (.ino is the Sketch File, add the SIP.h and SIP.cpp)

### Initial Configuration

0. **Register Settings at FritzBox**
   - Ensure, you have your WIFI SSID and password at hand.
   - !! Needs to be a static IP in main Fritz network, not guest !!
   - You will also need the IP of your FritzBox (192.168.xx.xx).
   - If you decide to do the Fritzbox-sided registration of the IP-Doorbell first (see below), you should have the SIP user and SIP password as well. Otherwise, enter later.

2. **Power on the ESP8266**
   - On first boot, default WiFi credentials won't work
   - Device automatically starts AP mode for configuration

3. **Connect to the Configuration AP**
   - SSID: **`ESP-Doorbell-Config`**
   - Password: **`12345678`**
   - _Note: If you connect with your mobile phone, you might have to confirm, you want to stay connected to a netwok without internet access!_
   - Device IP: `192.168.4.1` (use Chrome), e.g. on a mobile phone in **same** network

4. **Open the Configuration Portal**
   - Open browser and navigate to: **`http://192.168.4.1`**
   - You'll see the main configuration page

5. **Configure WiFi Settings**
   - **SSID**: Your home WiFi network name
   - **Password**: Your WiFi password
   - **Hostname**: Device hostname (default: ESP-Doorbell)

6. **Configure Network Settings**
   - **Use DHCP**: Check for automatic IP (static IP recommended for most users - speeds up the startup)
   - **Static IP**: the static IP of the doorbell (e.g., 192.168.178.123), if DHCP is unchecked.. TODO: could be removed at all?
   - **Router/Gateway**: Your FritzBox IP (e.g., 192.168.178.1)
   - **Subnet Mask**: Usually 255.255.255.0

7. **Configure SIP Settings** (Critical!)
   - **SIP Port**: Usually 5060 (FritzBox default)
   - **SIP User**: SIP username as configured at FritzBox (e.g., "tuerklingel")
   - **SIP Password**: SIP password from FritzBox
   - **Dial Number**: Number to dial (e.g., **9 to ring at all phones, or specific extension, e.g. "1" - as configured at FritzBox)
   - **Dial Text**: Caller ID text (e.g., "Front Door")
   - **Ring Duration**: How long phones should ring (default: 30 seconds)

8. **Configure Debug Options**
   - **Serial Debug**: Enable for USB/UART debugging
   - **WebSerial Debug**: Enable for browser-based debug console
   - Both can be enabled simultaneously

9. **Configure Time Settings**
   - **NTP Server**: Time server (default: pool.ntp.org)
   - **Timezone Offset**: Seconds from UTC
     - UTC+1 (CET): 3600
     - UTC+2 (CEST): **7200**

10. **Configure Power Management**
   - **Sleep Timeout**: Seconds before deep sleep (default: 180)
   - Set to 0 to disable deep sleep
   - Device stays awake longer when accessed via web interface

11. **Save Configuration**
    - Click "Save & Reboot"
    - Device will reboot in 10 seconds
    - If WiFi connection succeeds, AP mode will be disabled
    - If WiFi fails, AP mode will remain active for reconfiguration
---

## 📞 FritzBox SIP Configuration for the doorbell

Other VoIP capable routers potentially similar (or worldwide VoIP services?)

### Important: The Doorbell needs a fixed IP address. Register at FritzBox after first connect to WIFI and use above.

### Creating a Doorbell Extension

 ### **Log into FritzBox**
   - Navigate to `http://fritz.box`
   - Login to your **FritzBox** admin portal.
   - Go to **"Telephony"** -> **"Telephony Devices"** -> **"Configure New Device"**.
   - Choose **"Door intercom system"** and **Next**.

   ### **Register new Device/SIP credentials**
      + Port: "LAN / Wi-Fi (IP door intercom system)".
      + Name: Tuerklingel.
   
   ![image info](./images/Fritz_Doorbell_Wizard-page1.png)
   Register a new "Intercom" IP Device

   ### SIP user and Password (also note the IP of your FritzBox)
   ![image info](./images/Fritz_Doorbell_Wizard-page2.png)
   
   
   ### We need this telephone number for the configuration on the ESP.
   ![image info](./images/Fritz_Doorbell_Wizard-page3.png)
   
   
   ### You could route this to all phones or a subset of phones.
   ![image info](./images/Fritz_Doorbell_Wizard-page3-combo.png)
   
   
   ### Check the Settings and apply
   ![image info](./images/Fritz_Doorbell_Wizard-page4.png)
   
   
   ### List of all registered devices. You can edit anytime with the pen-symbol:
   ![image info](./images/Fritz_Doorbell_Overview-page1.png)
   
   
   ### Leave the lower part empty (..for the next fancy project with ESP32-CAM?)
   ![image info](./images/Fritz_Doorbell_Overview-page2.png)
   
   
   ### If you forgot to note your SIP-passord/credentials, change here:
   ![image info](./images/Fritz_Doorbell_Overview-page2b.png)
   
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

## 🎯 Quick Start Summary

1. Power on ESP8266
2. Connect to `ESP-Doorbell-Config` WiFi (password: `12345678`)
3. Open `http://192.168.4.1` in browser
4. Configure WiFi and SIP settings
5. Save and reboot
6. Press doorbell button to test
7. Check event log and status page
8. Adjust settings as needed

---

---

## 🌐 Web Interface

#### Main Configuration Page (`/`)
- Complete device configuration, including WiFi, SIP, debug, and power settings
- Links to other management pages

#### Status Page (`/status`)
- Real-time information, like WiFi connection details, last doorbell call status

#### Event Log (`/events`)
- Shows the history of the doorbell events

#### WebSerial Console (`/webserial`)
- Live debug output in browser, if WebSerial enabled in config
- All serial debug messages
- Auto-refreshes every 5 seconds

---

## 🐛 Troubleshooting

- Enable Debug Output, Serial and WebSerial on config page.
- Check WiFi Connection, needs to be with static IP in main Fritz network, not guest
- Double-check SIP credentials, username and password

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

---

## 🔌 TODO: Connecting to the doorbell

### Last minute change...

Now, the Doorbell button (circuit) needs to connect to a GPIO pin found in the code (DOORBELL_PIN). This allows for WIFI wakeup and also unwanted Reset action during EEPROM ops , etc. (to be on the safe side)

GPIO14 (== D5 on NodeMCU), Connect doorbell button here (active LOW with pullup)

### Basic Wiring (Power-on Reset)
```
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


Schematics:
  ![Schematics](./schematics/Klingeltrafo_ESP32-C3-Mini-Breadboard_schem.svg)

... could look like this (with ESP32-C3 Supermini):
![Sample Breadboard](./schematics/Klingeltrafo_ESP32-C3-Mini-Breadboard_bb.png)

```







> Hauptbestandteil ist ein Optokoppler (PC817), der zwei Stromkreise über eine Lichtbrücke trennt. Damit die LED des PC817 die korrekte Betriebsspannung von 1,2 Volt erhält, ist ein passender Vorwiderstand von 330 Ohm (8 Volt), 560 Ohm (12 Volt) oder 1,2 Kiloohm (24 Volt) erforderlich. Bei 24 V müsste der Widerstand eigentlich 0,5 W aushalten, da in der Regel aber nur wenige Sekunden geklingelt wird, hält auch die 0,25-W-Variante stand. Hat man jedoch Dauerdrücker im Freundeskreis, sollte man vorsichtshalber die 0,5-W-Variante wählen.

> Der Vorwiderstand senkt die Spannung in Durchlassrichtung der Optokoppler-LED zwar ab, jedoch ist bei Wechselspannung auch die Gegenrichtung zu berücksichtigen. Die Durchbruchspannung der LED im PC817 beträgt lediglich 6 Volt. Eine antiparallele Diode führt die gegenläufige Wechselspannung über den Widerstand ab und verhindert so, dass der Optokoppler beschädigt wird. Erfahrene Bastler werden hier schnell bemerken, dass der Ausgang des Optokopplers beim Betätigen des Klingetasters mit der Netzfrequenz pulsiert. Für den Raspberry Pi ist dies jedoch kein Problem. Mittels Polling-Schleife erkennt er die Impulse zuverlässig.

_Source: https://www.heise.de/select/ct/2017/17/1502995489716437_



obsolete (new version has one universal set of two 1/4W resistors):
| DoorBell Transformer<br>(AC current)    |  R1     |
|-----------------|----------|
| 8 V            | 330 Ohm  |
| 12 V           | 560 Ohm  |
| 24 V           | 1,2 kOhm |







       
