# Project State — LanTestLogger v2.0

> Atualizado em: 2026-05-17  
> Firmware: v2.0 com diagnóstico de fases, OUI profiles, testes de conectividade, dual-core

---

## Arquivos do Projeto

| Arquivo | Linhas | Descrição |
|---|---|---|
| `LanTestLogger.ino` | ~2500 | Core: setup, loop, comandos, testarMAC, AP/debug modes, core1 |
| `WebDashboard.h` | ~910 | Servidor web, captive portal, API REST, live log, dashboard UI |
| `ConfigManager.h` | ~230 | Persistência LittleFS, SavedConfig v5, parseMAC/IP |
| `TCPMQTT.h` | ~160 | Servidor TCP (2323), cliente MQTT minimal |
| `README.md` | 203 | Documentação do projeto |
| `LICENSE` | - | MIT |
| `build.sh` | 30 | Script de compilação |
| `build/` | - | Firmwares compilados (.uf2) |

---

## Funcionalidades Implementadas

### Core
- [x] Testa MACs aleatórios com 10 perfis OUI + MAC alvo configurável
- [x] Ciclo de 24h (86400s), trava ao final preservando relatório CSV
- [x] Log em LittleFS com timestamp, MAC, OUI, tipo, fase, reason code, IP
- [x] Define MAC via IOCTL `cur_etheraddr` + atualiza netif lwIP (`hwaddr`)
- [x] LED pisca indicando fase da conexão (2=scan fail, 7=connected)
- [x] Comandos respondem rapidamente (`processCommands()` em todos os loops)
- [x] **Dual-core**: Core 0 = testes WiFi, Core 1 = web dashboard + DNS

### Diagnóstico de Conexão (9 fases)
- [x] `SCAN_OK` / `SCAN_FAIL` — SSID visível?
- [x] `AUTH_FAIL` — autenticação rejeitada (BADAUTH)
- [x] `ASSOC_FAIL` — associação falhou (LINK_DOWN)
- [x] `HANDSHAKE_FAIL` — 4-way handshake falhou
- [x] `DHCP_FAIL` — WiFi conectou mas sem IP (LINK_JOIN sem DHCP)
- [x] `CONNECTED` — IP obtido, conexão completa
- [x] `MAC_SET_FAIL` — falha ao definir MAC
- [x] Reason codes mapeados (UNSPECIFIED, AUTH_EXPIRE, 4WAY_HANDSHAKE_TIMEOUT...)
- [x] `cmdSummary()` mostra distribuição percentual por fase

### Perfis OUI (10 fabricantes)
- [x] Generico, Apple, Samsung, Intel, Broadcom, Qualcomm, Xiaomi, Huawei, Nvidia, Realtek
- [x] `oui list` — lista perfis
- [x] `oui <nome>` — seleciona perfil
- [x] `oui all` — modo rotativo (alterna OUI a cada ciclo)
- [x] Estatísticas por OUI no summary (% bloqueio por fabricante)
- [x] Dashboard mostra barras de bloqueio por OUI

### Testes de Conectividade Pós-Conexão
- [x] Ping ao gateway (WiFi.ping)
- [x] Resolução DNS (WiFi.hostByName → neverssl.com)
- [x] HTTP GET (WiFiClient → neverssl.com:80)
- [x] Resultados no CSV (ping_ms, dns_ok, http_ok)
- [x] Summary mostra taxas de ping/DNS/HTTP

### Testes de Política de Bloqueio
- [x] `blacklist-policy` — timeout test: retesta MACs bloqueados após 5/15/30/60min
- [x] `ratelimit-test` — compara taxa de bloqueio em rajada (2s) vs espaçado (30s)
- [x] Resultados salvos em `/policy_test.csv`
- [x] Diagnóstico automático: "Rate limiting DETECTADO" / "Bloqueio PERMANENTE"

### Modo AP
- [x] `ap on/off/status` — Access Point com SSID/senha próprios
- [x] `ap ip` / `ap mac` / `ap ssid` / `ap pass` — config independente do STA
- [x] `stations` — lista dispositivos conectados (via ARP table polling)
- [x] Senha dedicada do AP (`ap pass`) — separada da senha STA
- [x] Notificação serial/BT quando dispositivo conecta/desconecta

### Web Dashboard (Core 1 dedicado)
- [x] Servidor web + captive portal (DNS redireciona todos os domínios)
- [x] **Dual-core**: loop1() no core 1 processa DNS + HTTP sem bloquear testes
- [x] Aba Dashboard: estatísticas em tempo real + gráfico OUI
- [x] Aba Dispositivos: estações conectadas + MACs bloqueados
- [x] Aba Config: formulário web para SSID, senha, AP, target MAC
- [x] Aba AP Debug: dados de fingerprint capturados
- [x] **Live Log**: buffer circular de 60 eventos, polling incremental (2s)
- [x] API REST: `/api/status`, `/api/config`, `/api/command`, `/api/dump`, `/api/debugdump`, `/api/events`
- [x] **Simulação de internet**: responde probes Android (HTTP 204), iOS ("Success"), Windows ("Microsoft NCSI")
- [x] `captive on/off` — ativa/desativa captive portal via comando
- [x] Cache-Control headers para performance
- [x] Download CSV dos relatórios

### Modo Debug (Fingerprinting)
- [x] `debug` — AP DebugNet (WPA2, canal 1) para captura passiva
- [x] Varredura ARP via `etharp_get_entry()`
- [x] Captura DHCP: hostname, vendor class, client ID
- [x] Captura HTTP User-Agent
- [x] Servidor DNS raw na porta 53

### Bluetooth
- [x] SerialBT — Bluetooth Classic SPP, nome `PicoTester`
- [x] Log dual USB + Bluetooth simultaneamente

### Rede e Acesso Remoto
- [x] `tcp on/off` — servidor TCP porta 2323 (até 3 clientes simultâneos)
- [x] `mqtt <ip> [porta]` — publica status em tópico MQTT a cada 30s
- [x] `mqtt off` — desativa MQTT

### Persistência
- [x] SavedConfig v5 no LittleFS (`/config.dat`)
- [x] Compatibilidade retroativa com v1, v2, v3, v4
- [x] `clearlog` — apaga CSVs de log e reseta estatísticas

### Comandos Serial/BT
`help`, `summary`, `dump`, `debugdump`, `reset`, `clearlog`, `ssid`, `pass`, `target`, `log on/off`, `oui list/<nome>/all`, `ap on/off/status/ip/mac/ssid/pass`, `stations`, `tcp on/off/status`, `mqtt <ip>/off`, `blacklist-policy`, `ratelimit-test`, `monitor on/off/status`, `captive on/off`, `debug`

---

## Bugs Corrigidos (v2.0)

1. **WiFi não conectava**: `WiFi.status()` retorna `WL_DISCONNECTED` em estado `CYW43_LINK_JOIN` (sem IP). Correção: loop de conexão verifica `cyw43_wifi_link_status()` diretamente.
2. **DHCP não funcionava**: `WiFi.mode(WIFI_STA)` extra bloqueava recriação do netif. Removido. Netif STA usa prefixo `e` (não `w`). `dhcp_start()` forçado no netif correto.
3. **MAC do netif desincronizado**: `setStationMAC()` agora atualiza `netif->hwaddr` além do chip.
4. **Pico travava sem serial**: `while(!Serial)` substituído por `delay(1000)`.
5. **Dashboard lento**: Core 1 dedicado ao web+DNS. Captive portal probes respondem com respostas mínimas.
6. **Senha do AP**: Agora separada da senha STA (`config_ap_pass`, padrão `12345678`).

---

## Configuração de Build

**Board:** `rp2040:rp2040:rpipicow` (Earle Philhower core v5.6.0)  
**Flash:** 2MB (Sketch: 1MB, FS: 1MB) → `flash=2097152_1048576`  
**Stack:** IPv4 + Bluetooth → `ipbtstack=ipv4btcble`

```bash
arduino-cli compile -b rp2040:rp2040:rpipicow:flash=2097152_1048576,ipbtstack=ipv4btcble .
```

---

## Uso de Recursos

| Métrica | Valor | Limite | Uso |
|---|---|---|---|
| Flash (programa) | ~630 KB | 1.044.480 | ~60% |
| RAM (globais) | ~102 KB | 262.144 | ~39% |

---

## Credenciais Padrão

- **STA SSID:** `Your_Network_SSID` / `your_network_password`
- **AP SSID:** `PicoTester` / `12345678`
- **MAC Alvo:** `AA:BB:CC:DD:EE:FF`

---

## Histórico de Versões

### v2.0 (2026-05-17)
- 9 fases de diagnóstico de conexão com reason codes
- 10 perfis OUI com modo rotativo
- Testes pós-conexão: ping, DNS, HTTP
- Testes de política: timeout blacklist + rate limiting
- Live log no dashboard web com buffer de eventos
- Servidor TCP porta 2323 + MQTT publish
- Dual-core: core 1 dedicado ao dashboard
- Captive portal com simulação de internet
- Senha AP separada, comando clearlog
- Refatoração: ConfigManager.h, TCPMQTT.h

### v1.0.0 (2026-05-17)
- Lançamento inicial no GitHub
