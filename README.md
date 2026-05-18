# LanTestLogger v2.0

**MAC address spoofer and WiFi diagnostic tool for Raspberry Pi Pico W.**

Tests random and targeted MAC addresses against a WiFi network to detect MAC filtering and blocking at every layer. Features 9-phase connection diagnostics, multi-vendor OUI profiles, post-connection connectivity tests, blacklist policy detection, dual-core web dashboard with live event log, captive portal with internet simulation, TCP/MQTT remote access, and Bluetooth SPP.

---

## What's New in v2.0

- **9-phase connection diagnostics** — Know exactly WHERE a block occurs (SCAN → AUTH → ASSOC → HANDSHAKE → DHCP → CONNECTED) with reason codes
- **10 OUI profiles** — Test blocking by vendor (Apple, Samsung, Intel, Broadcom, Qualcomm, Xiaomi, Huawei, Nvidia, Realtek)
- **Post-connection tests** — Ping gateway, DNS resolution, HTTP GET after successful WiFi association
- **Blacklist policy detection** — Timeout-based retesting (5/15/30/60min) + rate limiting detection
- **Dual-core architecture** — Core 1 dedicated to web dashboard + DNS for maximum responsiveness
- **Live event log** — Real-time dashboard with color-coded connection events
- **Internet simulation** — Proper captive portal probe responses (Android HTTP 204, Apple "Success", Windows NCSI)
- **TCP remote control** — Command interface on port 2323
- **MQTT publisher** — Status published to broker every 30s
- **Separate AP credentials** — Independent SSID and password for Access Point mode

---

## Features

- **MAC spoofing** — Tests random MACs (10 selectable OUI prefixes) plus a user-defined target MAC
- **9-phase diagnostics** — Tracks every WiFi connection state: SCAN, AUTH, ASSOC, HANDSHAKE, DHCP
- **24-hour test cycle** — Configurable duration, saves CSV report to LittleFS when complete
- **Vendor profiling** — Detects vendor-specific blocking patterns with per-OUI statistics
- **Post-connection tests** — Ping, DNS, and HTTP verification of real internet access
- **Policy tests** — `blacklist-policy` (timeout detection) and `ratelimit-test` (rate limiting)
- **Device fingerprinting** — DebugNet AP captures hostnames, DHCP vendor class, client ID, and HTTP User-Agent
- **Dual-core web dashboard** — Full remote control via captive portal with live event log
- **Internet simulation** — Responds to Android/iOS/Windows captive portal probes so devices stay connected
- **Dual logging** — USB Serial + Bluetooth Classic SPP simultaneously
- **TCP command server** — Remote control via telnet on port 2323 (up to 3 clients)
- **MQTT publisher** — Publishes status JSON to MQTT broker every 30s
- **Persistent configuration** — All settings saved on LittleFS (SavedConfig v5, backward compatible)

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| Raspberry Pi Pico W | RP2040 + CYW43439 WiFi/BT |
| USB Micro cable | Power and serial communication |

No external wiring required.

## Board Configuration (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | `Raspberry Pi Pico W` |
| Core | Earle Philhower `rp2040:rp2040` v5.6.0+ |
| Flash Size | 2MB (Sketch: 1MB, FS: 1MB) |
| IP/Bluetooth Stack | IPv4 + Bluetooth |

### arduino-cli

```bash
arduino-cli compile -b rp2040:rp2040:rpipicow:flash=2097152_1048576,ipbtstack=ipv4btcble .
```

### Build Script

```bash
./build.sh
```

Compiles and saves a dated `.uf2` to `build/`.

### Upload

1. Hold **BOOTSEL** button on the Pico W
2. Connect USB while holding BOOTSEL
3. Release BOOTSEL — Pico mounts as RPI-RP2
4. Copy the `.uf2` file:

```bash
cp build/LanTestLogger-*.uf2 /media/$USER/RPI-RP2/
sync
```

## Quick Start

1. Connect Pico W via USB
2. Open serial monitor at **115200 baud**
3. Configure your network credentials:
   ```
   ssid YourNetworkName
   pass YourPassword
   ```
4. The device auto-starts the 24-hour test cycle
5. Type `help` to list all commands

## Configuration

Edit `LanTestLogger.ino` before compiling, or change at runtime via serial commands:

```cpp
char config_ssid[64]    = "Your_Network_SSID";
char config_pass[64]    = "your_network_password";
char config_ap_ssid[64] = "PicoTester";
char config_ap_pass[64] = "12345678";
```

### Default AP Credentials
- **SSID:** `PicoTester`
- **Password:** `12345678`

## Commands

### Core
| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `summary` | Full statistics, phase distribution, per-OUI rates, blocked MACs |
| `dump` | Export CSV test report from LittleFS |
| `debugdump` | Export AP debug capture CSV |
| `reset` | Reset statistics, counters, and queues |
| `clearlog` | Delete all log files from LittleFS |

### Network Config
| Command | Description |
|---------|-------------|
| `ssid <name>` | Change target WiFi SSID (persistent) |
| `pass <pass>` | Change WiFi password (persistent) |
| `target <mac>` | Change target MAC (persistent) |
| `log on/off` | Enable/disable verbose logging |

### OUI Profiles
| Command | Description |
|---------|-------------|
| `oui list` | List all 10 OUI profiles (Generico, Apple, Samsung, Intel, Broadcom, Qualcomm, Xiaomi, Huawei, Nvidia, Realtek) |
| `oui <name>` | Select vendor profile (e.g., `oui Apple`) |
| `oui all` | Rotate mode: cycles through all OUIs |

### AP Mode
| Command | Description |
|---------|-------------|
| `ap on/off` | Enable/disable AP mode |
| `ap status` | Show AP status, IP, connected stations |
| `ap ip <ip> <gw> <mask>` | Configure AP IP address (persistent) |
| `ap ip default` | Restore default AP IP (192.168.4.1) |
| `ap mac <mac>` | Configure AP MAC address (persistent) |
| `ap mac default` | Restore hardware MAC |
| `ap ssid <name>` | Change AP SSID (persistent) |
| `ap pass <pass>` | Change AP password (persistent) |
| `stations` | List connected devices with MAC, status, uptime |
| `captive on/off` | Enable/disable captive portal DNS redirection |

### Remote Access
| Command | Description |
|---------|-------------|
| `tcp on/off` | TCP command server on port 2323 |
| `tcp status` | Show connected TCP clients |
| `mqtt <ip> [port]` | Configure MQTT broker |
| `mqtt off` | Disable MQTT |

### Diagnostics
| Command | Description |
|---------|-------------|
| `blacklist-policy` | Test blacklist timeout (retests at 5/15/30/60min) |
| `ratelimit-test` | Detect rate limiting (fast burst vs slow) |
| `debug` | Enter DebugNet AP for device fingerprinting |
| `monitor on/off/status` | Experimental monitor mode (WLC_SET_MONITOR) |

## Web Dashboard

When AP mode is active (`ap on`), connect to **PicoTester** WiFi (password: `12345678`) and open any URL — the captive portal redirects to:

```
http://192.168.4.1
```

### Tabs

| Tab | Description |
|-----|-------------|
| **Dashboard** | Real-time stats, OUI blocking bars, live event log |
| **Dispositivos** | Connected station list + blocked MACs with confirmation status |
| **Config** | Edit SSID, passwords, AP IP/MAC, target MAC via web forms |
| **AP Debug** | View captured device fingerprints from DebugNet mode |

### Internet Simulation

The captive portal responds correctly to OS connectivity probes:
- **Android:** `GET /generate_204` → HTTP 204
- **iOS/Apple:** `GET /hotspot-detect.html` → "Success"
- **Windows:** `GET /ncsi.txt` → "Microsoft NCSI"

Devices will show WiFi as connected with internet access and stay associated.

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Full status JSON (stats, stations, blocked, phases, OUI, connectivity) |
| `/api/config` | GET/POST | Read or write configuration |
| `/api/command` | POST | Execute a serial command (JSON: `{"cmd":"summary"}`) |
| `/api/dump` | GET | Download test report as CSV |
| `/api/debugdump` | GET | Download AP debug data as CSV (`?dl=1`) or JSON |
| `/api/events` | GET | Live event log since sequence number (`?since=N`) |

## Bluetooth

The device broadcasts Bluetooth Classic SPP as **PicoTester**. Connect with any SPP-compatible app for real-time serial output and commands.

## How It Works

### MAC Testing (9-Phase Diagnostics)

1. Generates random MACs with the selected OUI prefix
2. Sets the STA interface MAC via CYW43439 IOCTL (`cur_etheraddr`) and updates lwIP netif
3. Attempts connection and tracks each phase:
   - `SCAN` — Is the SSID visible?
   - `AUTH` — Does the AP accept authentication? (`BADAUTH` = MAC blocked at radio level)
   - `ASSOC` — Does association succeed?
   - `HANDSHAKE` — Does the 4-way WPA handshake complete?
   - `DHCP` — Is an IP address assigned?
   - `CONNECTED` — Full connectivity confirmed
4. Post-connection tests verify real internet access (ping, DNS, HTTP)
5. Failed MACs are enqueued for retesting (2 additional cycles for confirmation)
6. Confirmed blocked MACs can be tested for timeout-based policies

### AP Debug Mode

The `debug` command creates a **DebugNet** AP (WPA2, channel 1) for passive device fingerprinting:

- **ARP scanning** — Iterates lwIP ARP table via `etharp_get_entry()`
- **DHCP capture** — Listens on port 67 for options 12 (hostname), 60 (vendor class), 61 (client ID)
- **HTTP capture** — Records User-Agent from captive portal probes
- **DNS** — Raw UDP DNS server on port 53

### Dual-Core Architecture

- **Core 0:** Main test loop, serial/BT commands, TCP server, MQTT
- **Core 1:** Web dashboard + DNS server (tight loop, no delays)

## Project Structure

```
├── LanTestLogger.ino      # Main firmware (setup, loop, commands, debug mode, core1)
├── WebDashboard.h         # Web server, captive portal DNS, JSON API, dashboard UI
├── ConfigManager.h        # LittleFS persistence, SavedConfig v5, utilities
├── TCPMQTT.h              # TCP command server + minimal MQTT client
├── build.sh               # Compile script (outputs dated .uf2 to build/)
├── build/                 # Compiled firmware binaries
├── README.md              # This file
├── LICENSE                # MIT License
├── PROJECT_STATE.md       # Development history and changelog
└── .gitignore             # Build artifacts and temporary files
```

## Resource Usage

| Metric | Value | Limit | Usage |
|--------|-------|-------|-------|
| Flash (program) | ~630 KB | 1,044,480 | ~60% |
| RAM (globals) | ~102 KB | 262,144 | ~39% |

## Limitations

- **DHCP option capture is one-way** — lwIP DHCP server consumes ACK packets before raw UDP listener
- **`cyw43_wifi_ap_get_stas()` is non-functional** on Pico W core v5.6.0 — station detection uses ARP table scanning
- **Coexistence** — STA + AP simultaneous mode not supported by CYW43439 firmware on this core
- **Monitor mode** — `WLC_SET_MONITOR` IOCTL is compiled out of the CYW43 driver (`#if 0`)

## License

MIT License — see [LICENSE](LICENSE) for details.

## See Also

- [DHT22PIO_RP2040](https://github.com/angeloINTJ/DHT22PIO_RP2040) — PIO-accelerated DHT22 library
- [OneWirePIO_RP2040](https://github.com/angeloINTJ/OneWirePIO_RP2040) — PIO-accelerated DS18B20 library
