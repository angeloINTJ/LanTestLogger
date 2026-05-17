# Project State — LanTestLogger

> Gerado em: 2026-05-17 13:30  
> v1.0.0 publicada em https://github.com/angeloINTJ/LanTestLogger
> Último firmware: 2026-05-17 13:17 — v1.0.0 com web dashboard, ARP scanning, JSON fix

---

## Arquivos do Projeto

| Arquivo | Descrição |
|---|---|
| `LanTestLogger.ino` | Firmware completo para Raspberry Pi Pico W |
| `WebDashboard.h` | Servidor web, captive portal DNS, API JSON, dashboard UI |
| `README.md` | Documentação do projeto (GitHub) |
| `LICENSE` | Licença MIT |
| `PROJECT_STATE.md` | Este arquivo — estado atual do projeto |
| `build.sh` | Script de compilação que salva `.uf2` em `build/` |
| `build/` | Pasta com firmwares compilados (datados) |
| `.gitignore` | Artefatos de build ignorados |

---

## Funcionalidades Implementadas

### Core
- [x] Testa MACs aleatórios (prefixo `C8:A6:EF`) + MAC alvo configurável
- [x] Ciclo de 24h (86400s), trava ao final preservando relatório CSV
- [x] Log em LittleFS (`/relatorio.csv`) com timestamp, MAC, tipo, resultado, IP
- [x] Define MAC da interface STA via IOCTL `cur_etheraddr` no driver CYW43439
- [x] LED pisca a cada tentativa
- [x] Comandos respondem rapidamente mesmo durante delays (`processCommands()` em todos os loops)

### Modo AP
- [x] `ap on` — Pico vira Access Point com SSID/senha configurados
- [x] `ap off` — desliga AP e volta ao modo STA (teste)
- [x] `ap status` — mostra status do AP (SSID, IP, número de estações)
- [x] `ap ip [endereco] [gateway] [mascara]` — configura IP do AP (persistente)
- [x] `ap ip default` — restaura IP padrão (192.168.4.1)
- [x] `ap mac XX:XX:XX:XX:XX:XX` — configura MAC do AP via IOCTL `cur_etheraddr` (persistente)
- [x] `ap mac default` — restaura MAC padrão do hardware
- [x] `stations` — lista detalhada de dispositivos conectados (MAC, status, tempo)
- [x] Polling via tabela ARP (`etharp_get_entry`) como workaround para `cyw43_wifi_ap_get_stas` (não funcional no core v5.6.0)
- [x] Notificação na serial/BT quando dispositivo conecta ou desconecta
- [x] `processCommands()` ativo no loop do AP mode

### Web Dashboard
- [x] Servidor web + captive portal (DNS redireciona todos os domínios)
- [x] Aba Dashboard: estatísticas em tempo real
- [x] Aba Dispositivos: lista de estações e MACs bloqueados
- [x] Aba Config: formulário web para configurações
- [x] Aba AP Debug: visualização de dados de fingerprint capturados
- [x] API REST: `/api/status`, `/api/config`, `/api/command`, `/api/dump`, `/api/debugdump`
- [x] Download CSV dos relatórios
- [x] JSON válido corrigido (objetos com `{` faltante + vírgula extra entre itens)

### Modo Debug (Fingerprinting)
- [x] `debug` — ativa AP DebugNet (WPA2, canal 1) para captura passiva
- [x] Varredura ARP via `etharp_get_entry()` para detectar dispositivos conectados
- [x] Captura DHCP: hostname (option 12), vendor class (option 60), client ID (option 61)
- [x] Captura HTTP User-Agent de requisições de captive portal
- [x] Servidor DNS raw na porta 53
- [x] Salvamento em `/ap_debug.csv` no LittleFS
- [x] Saída com `q` retorna ao modo STA normal
- [x] `debugdump` — exporta CSV de debug via serial
- [x] Visualização no web dashboard (aba AP Debug)

### Bluetooth
- [x] `SerialBT` — Bluetooth Classic SPP, nome `PicoTester`
- [x] Log duplicado USB + Bluetooth simultaneamente
- [x] Notificação destacada quando MAC é confirmado como bloqueado

### Comandos via Serial/BT
- [x] `help` — lista todos os comandos
- [x] `summary` — resumo completo com estatísticas e MACs bloqueados
- [x] `dump` — exporta o conteúdo do CSV gravado no LittleFS
- [x] `reset` — reseta estatísticas, contadores e filas
- [x] `ssid <nome>` — altera SSID em runtime (salvo no LittleFS)
- [x] `pass <senha>` — altera senha em runtime (salvo no LittleFS)
- [x] `target XX:XX:XX:XX:XX:XX` — altera MAC alvo em runtime (salvo no LittleFS)
- [x] `log on` / `log off` — ativa/desativa logs detalhados

### Bloqueio e Confirmação
- [x] Lista de até 30 MACs bloqueados com contagem de testes
- [x] MACs bloqueados são re-testados por mais 2 ciclos para confirmação
- [x] Fila de reteste com até 10 MACs pendentes simultâneos
- [x] `summary` mostra MACs bloqueados: CONFIRMADOS e PENDENTES com status

### Persistência
- [x] Config (SSID, senha, MAC alvo, AP IP/MAC) salva em `/config.dat` no LittleFS
- [x] Auto-save ao alterar SSID/senha/target via comando
- [x] Load automático no boot
- [x] Formato v3 do SavedConfig com AP SSID separado

---

## Configuração da Arduino IDE / arduino-cli

**Board:** `rp2040:rp2040:rpipicow` (Earle Philhower core v5.6.0)  
**Flash Size:** `2MB (Sketch: 1MB, FS: 1MB)` → `flash=2097152_1048576`  
**IP/Bluetooth Stack:** `IPv4 + Bluetooth` → `ipbtstack=ipv4btcble`

Comando de compilação:
```
arduino-cli compile -b rp2040:rp2040:rpipicow:flash=2097152_1048576,ipbtstack=ipv4btcble /home/angelo/Documentos/LanTestLogger
```

---

## Última Compilação

| Métrica | Valor | Limite |
|---|---|---|
| Flash (programa) | 603.668 bytes | 1.044.480 (58%) |
| RAM (globais) | 95.028 bytes | 262.144 (36%) |

---

## Credenciais da Rede (substituir antes de usar)

- **SSID:** `Your_Network_SSID`
- **Senha:** `your_network_password`
- **MAC Alvo:** `AA:BB:CC:DD:EE:FF`

---

## GitHub

- **Repositório:** https://github.com/angeloINTJ/LanTestLogger
- **Licença:** MIT
- **Release:** v1.0.0

---

## Histórico de Alterações

### 2026-05-16
1. **20:50** — Criação do `PLAN.md` com especificação completa
2. **20:50** — Criação do `LanTestLogger.ino` conforme PLAN.md
3. **20:58** — Correção: `cyw43_wifi_set_mac_address` não existe no core v5.6.0. Substituído por `cyw43_ioctl` com IOVAR `cur_etheraddr`
4. **21:08** — Adicionado `SerialBT` (Bluetooth) com log dual USB+BT
5. **21:20** — Adicionado sistema de comandos: `help`, `summary`, `ssid`, `pass`, `target`, `log on/off`
6. **22:51** — Adicionado rastreio de MACs bloqueados + reteste em 2 ciclos para confirmação

### 2026-05-17
1. **00:22** — Compilação e flash bem-sucedidos (mesmo firmware, sem alterações)
2. **00:24** — Criada pasta `build/` com primeiro firmware compilado arquivado
3. **00:25** — Criado `build.sh` para compilar e salvar .uf2 automaticamente
4. **00:25** — Teste ao vivo via serial: Pico bootou, LittleFS montado, ciclos rodando
5. **01:00** — Implementados comandos `dump`, `reset` e persistência de config no LittleFS
6. **01:00** — Notificação BT aprimorada para MACs confirmados como bloqueados
7. **01:00** — Firmware v2 compilado e flashado com sucesso
8. **08:57** — Adicionado modo AP: comandos `ap on/off/status` e `stations` para rastrear dispositivos conectados via `cyw43_wifi_ap_get_stas`
9. **09:15** — Adicionados comandos `ap ip` e `ap mac` com persistência no SavedConfig v2; compat retroativa com config v1
10. **11:14** — Adicionado modo Debug AP com captura de fingerprint (DHCP, HTTP, ARP)
11. **11:14** — Criado `WebDashboard.h` com servidor web, captive portal e API REST
12. **11:14** — Workaround ARP: `cyw43_wifi_ap_get_stas` não funciona no core v5.6.0, substituído por `etharp_get_entry()`
13. **12:48** — Fix: JSON do `/api/debugdump` sem `{` de abertura nos objetos
14. **12:48** — Fix: vírgula extra antes do primeiro item no JSON array
15. **13:17** — Adicionado `processCommands()` em todos os loops de delay e no loop do AP mode
16. **13:25** — Projeto organizado para GitHub: README, LICENSE, .gitignore, credenciais sanitizadas
17. **13:30** — v1.0.0 publicada no GitHub

---

## Pendentes / Ideias

- Verificar se o MAC alvo realmente está bloqueado na rede (até agora conectou)
- Modo deep sleep entre ciclos para economia de energia
- Salvar/Carregar `log_enabled` na config persistente
- Exibir IP dos dispositivos conectados ao AP (via leases DHCP)
