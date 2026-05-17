# LanTestLogger

**MAC address spoofer and network debug tool for the Raspberry Pi Pico W.**

Tests random and targeted MAC addresses against a WiFi network to detect MAC filtering and blocking. Features a dual-mode AP for real-time device fingerprinting (DHCP options, HTTP User-Agent), dual-channel logging (USB + Bluetooth), and a web dashboard for remote control.

---

## Features

- **MAC spoofing** — Tests random MACs (prefix `C8:A6:EF`) plus a user-defined target MAC
- **24-hour test cycle** — Configurable duration, saves CSV report to LittleFS when complete
- **Device fingerprinting** — DebugNet AP captures hostnames, DHCP vendor class, client ID, and HTTP User-Agent
- **Dual logging** — USB Serial + Bluetooth Classic SPP simultaneously
- **Web dashboard** — Full remote control with captive portal (DNS redirects all domains)
- **Command interface** — Control everything via USB or Bluetooth serial commands
- **Persistent configuration** — SSID, password, target MAC, AP settings saved on LittleFS
- **MAC blocking detection** — Tracks blocked MACs with automatic retest for confirmation
- **AP mode** — Standalone Access Point with real-time station monitoring
- **ARP-based station scanning** — Detects connected devices via lwIP ARP table iteration

## Hardware Requirements

| Component | Description |
|-----------|-------------|
| Raspberry Pi Pico W | RP2040 + CYW43439 WiFi/BT |
| USB Micro cable | Power and serial communication |

No external wiring required — the Pico W's built-in CYW43439 handles all wireless communication.

## Board Configuration (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | `Raspberry Pi Pico W` |
| Core | Earle Philhower `rp2040:rp2040` v5.6.0+ |
| Flash Size | 2MB (Sketch: 1MB, FS: 1MB) |
| IP/Bluetooth Stack | IPv4 + Bluetooth |

### arduino-cli

```bash
arduino-cli compile -b rp2040:rp2040:rpipicow:flash=2097152_1048576,ipbtstack=ipv4btcble /path/to/LanTestLogger
```

### Upload

1. Hold the **BOOTSEL** button on the Pico W
2. Connect USB while holding BOOTSEL
3. Release BOOTSEL — the Pico mounts as a mass storage device
4. Copy the `.uf2` file:

```bash
cp build/LanTestLogger-*.uf2 /media/$USER/RPI-RP2/
sync
```

### Build Script

```bash
./build.sh
```

Compiles the firmware and saves a dated `.uf2` to `build/`.

## Configuration

Edit the credentials at the top of `LanTestLogger.ino` before compiling:

```cpp
char config_ssid[64]    = "Your_Network_SSID";
char config_pass[64]    = "your_network_password";
char config_ap_ssid[64] = "PicoTester";
uint8_t config_mac_alvo[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
```

All settings can also be changed at runtime via serial commands and are persisted to LittleFS.

## Quick Start

1. Connect the Pico W via USB
2. Open a serial monitor at **115200 baud**
3. Configure your network credentials (see above)
4. The device automatically starts the 24-hour test cycle
5. Type `help` to list all commands

## Commands

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `summary` | Full statistics and blocked MAC report |
| `dump` | Export CSV test report from LittleFS |
| `debugdump` | Export AP debug capture CSV |
| `reset` | Reset statistics, counters, and queues |
| `ssid <name>` | Change target WiFi SSID (persistent) |
| `pass <pass>` | Change WiFi password (persistent) |
| `target <mac>` | Change target MAC (persistent) |
| `log on/off` | Enable/disable verbose logging |
| `ap on/off` | Enable/disable AP mode |
| `ap status` | Show AP status, IP, connected stations |
| `ap ip <ip> <gw> <mask>` | Configure AP IP address (persistent) |
| `ap ip default` | Restore default AP IP (192.168.4.1) |
| `ap mac <mac>` | Configure AP MAC address (persistent) |
| `ap mac default` | Restore default hardware MAC |
| `stations` | List connected devices with MAC, status, uptime |
| `debug` | Enter DebugNet AP for device fingerprinting |

## Web Dashboard

When AP mode is active (`ap on`), connect to the **PicoTester** WiFi network and open any URL in a browser — the captive portal redirects to:

```
http://192.168.4.1
```

### Tabs

| Tab | Description |
|-----|-------------|
| **Dashboard** | Real-time stats — connected count, blocked count, cycles, uptime |
| **Dispositivos** | Connected station list + blocked MACs with confirmation status |
| **Config** | Edit SSID, password, AP IP/MAC, target MAC via web forms |
| **AP Debug** | View captured device fingerprints from DebugNet mode |

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Full status JSON (stats, stations, blocked) |
| `/api/config` | GET/POST | Read or write configuration |
| `/api/command` | POST | Execute a serial command (JSON: `{"cmd":"summary"}`) |
| `/api/dump` | GET | Download test report as CSV |
| `/api/debugdump` | GET | Download AP debug data as CSV (`?dl=1`) or JSON |

All domains resolve to the dashboard via DNS (captive portal).

## Bluetooth

The device broadcasts a Bluetooth Classic SPP connection named **PicoTester**. Connect with any SPP-compatible app to receive real-time serial output and send commands — identical to the USB serial interface.

---

## How It Works

### MAC Testing

1. Generates random MAC addresses with the `C8:A6:EF` OUI prefix
2. Sets the STA interface MAC via CYW43439 IOCTL (`cur_etheraddr`)
3. Attempts to connect to the configured WiFi network
4. Logs the result (timestamp, MAC, type, success/fail, IP) to LittleFS CSV
5. MACs that fail consistently are flagged as potentially blocked
6. Blocked MACs enter a retest queue for confirmation (2 additional cycles)

### AP Debug Mode

The `debug` command creates a dedicated **DebugNet** AP (WPA2, channel 1) for passive device fingerprinting:

- **ARP scanning** — Iterates the lwIP ARP table via `etharp_get_entry()` to detect connected stations (the `cyw43_wifi_ap_get_stas()` API is non-functional on Pico W core v5.6.0)
- **DHCP passive capture** — Listens on port 67 for DHCP Discover/Request packets to extract hostname (option 12), vendor class (option 60), and client identifier (option 61). Note: the built-in lwIP DHCP server consumes DHCPACK, so option capture is one-way
- **HTTP capture** — Serves a minimal HTTP endpoint that records User-Agent headers from captive portal probes
- **DNS** — Raw UDP DNS server on port 53 responds to all queries with the AP IP
- All captured data is saved to `/ap_debug.csv` on LittleFS
- Press `q` to exit and return to normal STA mode

### Station Detection

Station detection in AP mode uses `etharp_get_entry()` from the lwIP stack to iterate the ARP table — the only reliable method on the Pico W core v5.6.0, since the native `cyw43_wifi_ap_get_stas()` driver call always returns zero entries.

## Project Structure

```
├── LanTestLogger.ino      # Main firmware (setup, loop, commands, debug mode)
├── WebDashboard.h         # Web server, captive portal DNS, JSON API, dashboard UI
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
| Flash (program) | ~604 KB | 1,044,480 | ~58% |
| RAM (globals) | ~95 KB | 262,144 | ~36% |

## Limitations

- **DHCP option capture is one-way** — the built-in lwIP DHCP server on port 67 consumes ACK packets before the raw UDP listener can read them. Discover and Request options are captured; ACK options are not.
- **`cyw43_wifi_ap_get_stas()` is non-functional** on Pico W core v5.6.0 — station detection relies on ARP table scanning as a workaround.
- **Coexistence** — STA + AP simultaneous mode is not supported by the CYW43439 firmware on this core. Use `ap on` to switch to AP mode or `debug` for the DebugNet.

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE) for details.

## See Also

- [DHT22PIO_RP2040](https://github.com/angeloINTJ/DHT22PIO_RP2040) — PIO-accelerated DHT22 library by the same author
- [OneWirePIO_RP2040](https://github.com/angeloINTJ/OneWirePIO_RP2040) — PIO-accelerated DS18B20 library by the same author
