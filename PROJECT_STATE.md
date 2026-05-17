# Project State — LanTestLogger

> Gerado em: 2026-05-17 01:05  
> Último firmware flashado: 2026-05-17 00:59 — build v2 com dump, reset e persistência de config.

---

## Arquivos do Projeto

| Arquivo | Descrição |
|---|---|
| `PLAN.md` | Especificação original do projeto |
| `LanTestLogger.ino` | Firmware completo para Raspberry Pi Pico W |
| `PROJECT_STATE.md` | Este arquivo — estado atual do projeto |
| `build.sh` | Script de compilação que salva `.uf2` em `build/` |
| `build/` | Pasta com firmwares compilados (datados) |

---

## Funcionalidades Implementadas

### Core
- [x] Testa MACs aleatórios (prefixo `C8:A6:EF`) + MAC alvo configurável
- [x] Ciclo de 24h (86400s), trava ao final preservando relatório CSV
- [x] Log em LittleFS (`/relatorio.csv`) com timestamp, MAC, tipo, resultado, IP
- [x] Define MAC da interface STA via IOCTL `cur_etheraddr` no driver CYW43439
- [x] LED pisca a cada tentativa

### Modo AP
- [x] `ap on` — Pico vira Access Point com SSID/senha configurados
- [x] `ap off` — desliga AP e volta ao modo STA (teste)
- [x] `ap status` — mostra status do AP (SSID, IP, número de estações)
- [x] `ap ip [endereco] [gateway] [mascara]` — configura IP do AP (persistente)
- [x] `ap ip default` — restaura IP padrão (192.168.4.1)
- [x] `ap mac XX:XX:XX:XX:XX:XX` — configura MAC do AP via IOCTL `cur_etheraddr` (persistente)
- [x] `ap mac default` — restaura MAC padrão do hardware
- [x] `stations` — lista detalhada de dispositivos conectados (MAC, status, tempo)
- [x] Polling automático via `cyw43_wifi_ap_get_stas` para capturar MACs em tempo real
- [x] Notificação na serial/BT quando dispositivo conecta ou desconecta

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
- [x] Config (SSID, senha, MAC alvo) salva em `/config.dat` no LittleFS
- [x] Auto-save ao alterar SSID/senha/target via comando
- [x] Load automático no boot

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
| Flash (programa) | 552.796 bytes | 1.044.480 (52%) |
| RAM (globais) | 92.700 bytes | 262.144 (35%) |

---

## Credenciais da Rede (substituir antes de usar)

- **SSID:** `Your_Network_SSID`
- **Senha:** `your_network_password`
- **MAC Alvo:** `AA:BB:CC:DD:EE:FF`

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
9. **Observação:** Todos os MACs aleatórios testados conectaram na rede configurada — um MAC com bloqueio detectado no CSV

---

## Para Continuar Amanhã

### Pendentes / Ideias
- ~~Exportar relatório CSV do LittleFS via comando~~ → `dump` implementado
- ~~Reset das estatísticas via comando~~ → `reset` implementado
- ~~Salvar config (SSID, PASS, target) no LittleFS~~ → implementado (auto-save + load no boot)
- ~~Notificação Bluetooth quando MAC confirmado~~ → implementado
- ~~Modo AP para debug de dispositivos~~ → implementado (`ap on/off/status`, `stations`)
- Verificar se o MAC alvo realmente está bloqueado na rede (até agora conectou)
- Modo deep sleep entre ciclos para economia de energia
- Salvar/Carregar `log_enabled` na config persistente
- Exibir IP dos dispositivos conectados ao AP (via leases DHCP)

### Como testar
1. Conectar USB e monitorar:
   ```bash
   cat /dev/ttyACM0
   ```
2. Enviar comandos em outro terminal:
   ```bash
   printf "help\n" > /dev/ttyACM0
   printf "summary\n" > /dev/ttyACM0
   ```
3. Ou conectar via Bluetooth ao dispositivo `PicoTester`

### Se precisar recompilar e flashar
```bash
# Compilar
./build.sh

# Colocar Pico em BOOTSEL, depois:
cp build/LanTestLogger-$(date +%Y-%m-%d).uf2 /media/angelo/RPI-RP2/
sync
```
