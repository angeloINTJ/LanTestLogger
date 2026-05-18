#include <WiFi.h>
#include <LittleFS.h>
#include <pico/cyw43_arch.h>
#include <SerialBT.h>
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "OUI.h"
#include "ConfigManager.h"

// =============================================
// CONFIGURAÇÕES INICIAIS (editáveis via comando)
// =============================================
char config_ssid[64]    = "Your_Network_SSID";
char config_pass[64]    = "your_network_password";
char config_ap_ssid[64] = "PicoTester";
char config_ap_pass[64] = "12345678";
uint8_t config_mac_alvo[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// =============================================
// PERFIS OUI (Organizational Unique Identifier)
// =============================================
struct OUIProfile {
  const char *nome;
  uint8_t prefixo[3];
};

OUIProfile perfis_oui[] = {
  {"Generico",   {0xC8, 0xA6, 0xEF}},
  {"Apple",      {0x00, 0x1E, 0xC2}},
  {"Samsung",    {0xCC, 0x05, 0x77}},
  {"Intel",      {0x00, 0x1C, 0xBF}},
  {"Broadcom",   {0x00, 0x10, 0x18}},
  {"Qualcomm",   {0x00, 0x0A, 0xF7}},
  {"Xiaomi",     {0x8C, 0x85, 0x90}},
  {"Huawei",     {0x28, 0x6E, 0xD4}},
  {"Nvidia",     {0x00, 0x04, 0x4B}},
  {"Realtek",    {0x00, 0xE0, 0x4C}},
};
const int OUI_COUNT = sizeof(perfis_oui) / sizeof(perfis_oui[0]);

int oui_ativo = 0;           // indice do perfil ativo
bool oui_rotativo = false;   // true = alterna entre todos os perfis
int oui_ciclo_atual = 0;     // contador para modo rotativo

// Estatisticas por OUI
#define MAX_OUI_STATS 10
struct OUIStat {
  char nome[16];
  unsigned long testados;
  unsigned long bloqueados;
};
OUIStat oui_stats[MAX_OUI_STATS];
int oui_stats_count = 0;

String ouiNomeAtivo() {
  return String(perfis_oui[oui_ativo].nome);
}

String ouiDoMAC(const uint8_t *mac) {
  for (int i = 0; i < OUI_COUNT; i++) {
    if (mac[0] == perfis_oui[i].prefixo[0] &&
        mac[1] == perfis_oui[i].prefixo[1] &&
        mac[2] == perfis_oui[i].prefixo[2]) {
      return String(perfis_oui[i].nome);
    }
  }
  return "Desconhecido";
}

void registrarOUIStat(const String &oui, bool bloqueado) {
  for (int i = 0; i < oui_stats_count; i++) {
    if (strcmp(oui_stats[i].nome, oui.c_str()) == 0) {
      oui_stats[i].testados++;
      if (bloqueado) oui_stats[i].bloqueados++;
      return;
    }
  }
  if (oui_stats_count < MAX_OUI_STATS) {
    snprintf(oui_stats[oui_stats_count].nome, sizeof(oui_stats[oui_stats_count].nome),
             "%s", oui.c_str());
    oui_stats[oui_stats_count].testados = 1;
    oui_stats[oui_stats_count].bloqueados = bloqueado ? 1 : 0;
    oui_stats_count++;
  }
}

const unsigned long DURACAO_TESTE_SEG = 86400;
const unsigned long INTERVALO_ENTRE_TENTATIVAS_SEG = 30;
const char* CSV_FILENAME = "/relatorio.csv";
const char* CONFIG_FILENAME = "/config.dat";

// =============================================
// LISTA DE MACs BLOQUEADOS
// =============================================
#define MAX_BLOCKED 30
#define MAX_RETEST 10
#define CONFIRMAR_CICLOS 2   // ciclos extras para confirmar bloqueio

struct BlockedEntry {
  uint8_t mac[6];
  uint8_t test_count;      // vezes que foi testado e ficou bloqueado
  char tipo[7];            // "random" ou "exact"
  bool confirmado;         // true se passou pelos ciclos de confirmacao
};

BlockedEntry blocked_macs[MAX_BLOCKED];
int blocked_count = 0;

// Fila de reteste: MACs bloqueados que precisam ser testados de novo
struct RetestEntry {
  uint8_t mac[6];
  int ciclos_restantes;    // começa em CONFIRMAR_CICLOS, decrementa até 0
};

RetestEntry retest_queue[MAX_RETEST];
int retest_count = 0;

// =============================================
// VARIÁVEIS GLOBAIS
// =============================================
unsigned long startMillis;
bool log_enabled = true;

struct {
  unsigned long random_connected = 0;
  unsigned long random_blocked = 0;
  unsigned long exact_connected = 0;
  unsigned long exact_blocked = 0;
  unsigned long random_errors = 0;
  unsigned long exact_errors = 0;
  unsigned long total_cycles = 0;
} stats;

// =============================================
// FASES DE CONEXAO
// =============================================
enum ConnPhase {
  PHASE_NONE = 0,
  PHASE_SCAN_OK,        // SSID encontrado no scan
  PHASE_SCAN_FAIL,      // SSID nao encontrado
  PHASE_AUTH_FAIL,      // falha na autenticacao
  PHASE_ASSOC_FAIL,     // falha na associacao
  PHASE_HANDSHAKE_FAIL, // falha no 4-way handshake
  PHASE_DHCP_FAIL,      // conectou mas sem DHCP
  PHASE_CONNECTED,      // conexao completa com IP
  PHASE_MAC_SET_FAIL    // falha ao definir MAC
};

const char* phaseName(uint8_t p) {
  switch (p) {
    case PHASE_NONE:            return "NONE";
    case PHASE_SCAN_OK:         return "SCAN_OK";
    case PHASE_SCAN_FAIL:       return "SCAN_FAIL";
    case PHASE_AUTH_FAIL:       return "AUTH_FAIL";
    case PHASE_ASSOC_FAIL:      return "ASSOC_FAIL";
    case PHASE_HANDSHAKE_FAIL:  return "HANDSHAKE_FAIL";
    case PHASE_DHCP_FAIL:       return "DHCP_FAIL";
    case PHASE_CONNECTED:       return "CONNECTED";
    case PHASE_MAC_SET_FAIL:    return "MAC_SET_FAIL";
    default:                    return "UNKNOWN";
  }
}

// Mapeamento de reason codes WiFi para texto
const char* reasonCodeName(int code) {
  switch (code) {
    case 0:  return "NONE";
    case 1:  return "UNSPECIFIED";
    case 2:  return "AUTH_EXPIRE";
    case 3:  return "AUTH_LEAVE";
    case 4:  return "ASSOC_EXPIRE";
    case 5:  return "ASSOC_TOOMANY";
    case 6:  return "NOT_AUTHED";
    case 7:  return "NOT_ASSOCED";
    case 8:  return "ASSOC_LEAVE";
    case 9:  return "ASSOC_NOT_AUTHED";
    case 13: return "INVALID_IE";
    case 14: return "MIC_FAILURE";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 16: return "GROUP_KEY_TIMEOUT";
    case 23: return "8021X_AUTH_FAIL";
    case 24: return "CIPHER_REJECTED";
    default: return "CODE_UNKNOWN";
  }
}

struct PhaseCounters {
  unsigned long phase_scan_ok         = 0;
  unsigned long phase_scan_fail       = 0;
  unsigned long phase_auth_fail       = 0;
  unsigned long phase_assoc_fail      = 0;
  unsigned long phase_handshake_fail  = 0;
  unsigned long phase_dhcp_fail       = 0;
  unsigned long phase_connected       = 0;
  unsigned long phase_mac_set_fail    = 0;
} phaseStats;

// =============================================
// TESTES DE CONECTIVIDADE POS-CONEXAO
// =============================================
struct ConnTestResult {
  int ping_ms;       // -1 = nao testado, -2 = timeout, >=0 = latencia em ms
  bool dns_ok;       // true = resolveu DNS
  bool http_ok;      // true = HTTP GET funcionou
};

struct ConnTestStats {
  unsigned long total_tested;       // quantos MACs conectados foram testados
  unsigned long ping_ok;
  unsigned long ping_fail;
  unsigned long dns_ok;
  unsigned long dns_fail;
  unsigned long http_ok;
  unsigned long http_fail;
} connStats;

// Funcoes implementadas apos as definicoes de log/reply
ConnTestResult testarConectividadeReal();
void registrarConnStats(const ConnTestResult &r);

// =============================================
// TESTES DE POLITICA DE BLOQUEIO (Timeout + Rate Limiting)
// =============================================
#define MAX_TIMEOUT_MACS 5
#define TIMEOUT_STAGES 4

struct TimeoutTestEntry {
  uint8_t mac[6];
  unsigned long blocked_at_ms;       // quando foi confirmado como bloqueado
  unsigned long next_retest_ms;      // quando re-testar
  int stage;                         // 0=5min, 1=15min, 2=30min, 3=60min
  bool done;
  bool was_blocked[TIMEOUT_STAGES];  // resultado de cada estagio
};

TimeoutTestEntry timeout_tests[MAX_TIMEOUT_MACS];
int timeout_test_count = 0;
bool timeout_test_active = false;
bool timeout_test_running = false;   // true while actively testing a specific MAC
int timeout_test_idx = 0;            // which entry is being tested now
const unsigned long TIMEOUT_MINUTES[TIMEOUT_STAGES] = {5, 15, 30, 60};

// Rate limiting test
bool ratelimit_test_active = false;
int ratelimit_phase = 0;             // 0=fast, 1=slow, 2=done
unsigned long ratelimit_fast_blocked = 0;
unsigned long ratelimit_fast_total = 0;
unsigned long ratelimit_slow_blocked = 0;
unsigned long ratelimit_slow_total = 0;
unsigned long ratelimit_start_ms = 0;
#define RATELIMIT_MACS_PER_PHASE 10   // testa 10 MACs em cada fase

void processarTimeoutTests();
void processarRatelimitTest();
void iniciarBlacklistPolicyTest();
void pararBlacklistPolicyTest();
void iniciarRatelimitTest();
void pushEvent(const char *type, const char *mac, const char *detail);
void pollMonitorFrames();
// =============================================
// SERVIDOR TCP + MQTT (globals — usadas por TCPMQTT.h)
// =============================================
WiFiServer tcpServer(2323);
WiFiClient tcpClients[3];
bool tcp_server_active = false;
bool captive_enabled = true;  // captive portal ativo por padrao

IPAddress mqtt_broker_ip;
uint16_t mqtt_broker_port = 1883;
WiFiClient mqttClient;
unsigned long lastMqttPublish = 0;
bool mqtt_connected = false;

// =============================================
// AP MODE - CONNECTED STATIONS TRACKING
// =============================================
#define MAX_CONNECTED_STATIONS 10

struct ConnectedStation {
  uint8_t mac[6];
  unsigned long first_seen_ms;
  unsigned long last_seen_ms;
  bool active;
};

ConnectedStation connected_stations[MAX_CONNECTED_STATIONS];
int connected_station_count = 0;
bool ap_mode_active = false;
unsigned long last_station_poll_ms = 0;

// Configuração do AP (IP e MAC)
IPAddress ap_config_ip = IPAddress(192, 168, 4, 1);
IPAddress ap_config_gateway = IPAddress(192, 168, 4, 1);
IPAddress ap_config_subnet = IPAddress(255, 255, 255, 0);
uint8_t ap_config_mac[6] = {0};
bool ap_config_mac_set = false;

#define CMD_BUF_SIZE 128
char cmd_buf[CMD_BUF_SIZE];
int cmd_idx = 0;

// Command output capture for web API
char cmd_output_buf[1024] = {0};
bool cmd_output_capture = false;

// =============================================
// LOGGER DUAL (USB + Bluetooth)
// =============================================
void reply(const char *s) {
  Serial.print(s);
  if (SerialBT) SerialBT.print(s);
  if (cmd_output_capture) {
    size_t slen = strlen(s);
    size_t blen = strlen(cmd_output_buf);
    size_t room = sizeof(cmd_output_buf) - blen - 1;
    if (room > 0) {
      size_t n = (slen < room) ? slen : room;
      memcpy(cmd_output_buf + blen, s, n);
      cmd_output_buf[blen + n] = '\0';
    }
  }
}
void replyf(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  if (SerialBT) SerialBT.print(buf);
  if (cmd_output_capture) {
    size_t slen = strlen(buf);
    size_t blen = strlen(cmd_output_buf);
    size_t room = sizeof(cmd_output_buf) - blen - 1;
    if (room > 0) {
      size_t n = (slen < room) ? slen : room;
      memcpy(cmd_output_buf + blen, buf, n);
      cmd_output_buf[blen + n] = '\0';
    }
  }
}

void log(const char *s) {
  if (!log_enabled) return;
  Serial.print(s);
  if (SerialBT) SerialBT.print(s);
}
void logln(const char *s) {
  if (!log_enabled) return;
  if (s) Serial.println(s);
  else Serial.println();
  if (SerialBT) {
    if (s) SerialBT.println(s);
    else SerialBT.println();
  }
}
void logf(const char *fmt, ...) {
  if (!log_enabled) return;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);
  if (SerialBT) SerialBT.print(buf);
}

#include <DNSServer.h>
extern DNSServer dnsServer;

#include "TCPMQTT.h"

// =============================================
// PROTÓTIPOS
// =============================================
bool macIgual(const uint8_t *a, const uint8_t *b);
int buscarBlockedMAC(const uint8_t *mac);
void addBlockedMAC(const uint8_t *mac, const char *tipo, bool conectado);
int buscarRetest(const uint8_t *mac);
void addRetest(const uint8_t *mac);
void processarRetestes();
void piscarLED(int vezes, int intervaloMs);

void setupLittleFS();
void appendCSV(unsigned long timestampSeg, const String &macStr,
               const String &oui, const String &tipo, uint8_t phase,
               int reasonCode, const String &reasonName, const String &ip,
               int pingMs = -1, int dnsOk = -1, int httpOk = -1);
bool setStationMAC(const uint8_t* newMAC);
void testarMAC(const uint8_t* mac, const String &tipo, const String &ouiNome);
void processCommands();
void executeCommand(char *cmd);
void cmdSummary();
void cmdDump();
void cmdReset();
void startAPMode();
void stopAPMode();
void updateStationList();
void cmdStations();
bool parseIP(const char *str, IPAddress &ip);
bool setAPMAC(const uint8_t *mac);
void initWebDashboard();
void handleWebDashboard();
void stopWebDashboard();
void startAPDebugMode();
void cmdDebugDump();

// =============================================
// SETUP
// =============================================
void setup() {
  Serial.begin(115200);
  delay(1000);  // aguardar USB estabilizar, sem bloquear

  SerialBT.setName("PicoTester");
  SerialBT.begin();

  reply("\n--- Testador de Bloqueio de MAC (24h) ---\n");
  replyf("Rede: %s\n", config_ssid);
  replyf("MAC alvo: %s\n", macToString(config_mac_alvo).c_str());
  replyf("OUI: %s (%02X:%02X:%02X) %s\n",
         perfis_oui[oui_ativo].nome,
         perfis_oui[oui_ativo].prefixo[0],
         perfis_oui[oui_ativo].prefixo[1],
         perfis_oui[oui_ativo].prefixo[2],
         oui_rotativo ? "[ROTATIVO]" : "");
  replyf("Duracao: %lu segundos\n", DURACAO_TESTE_SEG);
  replyf("Intervalo: %lu s\n", INTERVALO_ENTRE_TENTATIVAS_SEG);
  reply("Digite 'help' para comandos\n");
  reply("-----------------------------------------\n\n");

  setupLittleFS();
  loadConfig();
  ouiInit(false);  // false = busca em arquivo (sem custo de RAM)

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(500);

  randomSeed(analogRead(A0) ^ millis());
  startMillis = millis();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
}

// =============================================
// LOOP PRINCIPAL
// =============================================
void loop() {
  processCommands();

  // Processar testes de politica e servicos de rede (background)
  processarTimeoutTests();
  processarRatelimitTest();
  processarTCPServer();
  processarMQTT();
  pollMonitorFrames();

  // AP mode: web dashboard now runs on core 1 (loop1)
  if (ap_mode_active) {
    processCommands();
    delay(10);
    return;
  }

  unsigned long segDecorridos = (millis() - startMillis) / 1000;

  if (segDecorridos >= DURACAO_TESTE_SEG) {
    reply("\n24 horas completadas. Teste encerrado.\n");
    reply("Relatorio salvo em LittleFS.\n");
    while (1) { processCommands(); delay(10000); }
  }

  stats.total_cycles++;
  int numVariacoes = random(3, 16);

  logf("\n=== Ciclo %lu: %d variacoes + retestes + MAC exato (%lu s) ===\n",
       stats.total_cycles, numVariacoes, segDecorridos);

  // --- MACs aleatórios ---
  // Seleciona OUI para este ciclo
  if (oui_rotativo) {
    oui_ativo = oui_ciclo_atual % OUI_COUNT;
    oui_ciclo_atual++;
  }
  uint8_t *ouiPrefixo = perfis_oui[oui_ativo].prefixo;
  String ouiNome = perfis_oui[oui_ativo].nome;

  for (int i = 0; i < numVariacoes; i++) {
    if ((millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) break;
    processCommands();
    if (ap_mode_active) return;

    uint8_t macAleatorio[6];
    macAleatorio[0] = ouiPrefixo[0];
    macAleatorio[1] = ouiPrefixo[1];
    macAleatorio[2] = ouiPrefixo[2];
    macAleatorio[3] = random(0, 256);
    macAleatorio[4] = random(0, 256);
    macAleatorio[5] = random(0, 256);

    testarMAC(macAleatorio, "random", ouiNome);
    for (int w = 0; w < INTERVALO_ENTRE_TENTATIVAS_SEG; w++) {
      processCommands();
      delay(1000);
      if (ap_mode_active || (millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) return;
    }
  }

  // --- Retestes: re-testar MACs bloqueados para confirmar ---
  if ((millis() - startMillis) / 1000 < DURACAO_TESTE_SEG && retest_count > 0) {
    processCommands();
    if (ap_mode_active) return;
    logf("--- Retestes pendentes: %d ---\n", retest_count);
    // Percorre a fila ao contrário para poder remover itens
    for (int i = retest_count - 1; i >= 0; i--) {
      if ((millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) break;
      processCommands();
      if (ap_mode_active) return;

      String retestOUI = ouiDoMAC(retest_queue[i].mac);
      testarMAC(retest_queue[i].mac, "retest", retestOUI);
      for (int w = 0; w < INTERVALO_ENTRE_TENTATIVAS_SEG; w++) {
        processCommands();
        delay(1000);
        if (ap_mode_active || (millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) return;
      }
    }
  }

  // --- MAC exato ---
  if ((millis() - startMillis) / 1000 < DURACAO_TESTE_SEG) {
    processCommands();
    if (ap_mode_active) return;
    testarMAC(config_mac_alvo, "exact", "target");
    for (int w = 0; w < INTERVALO_ENTRE_TENTATIVAS_SEG; w++) {
      processCommands();
      delay(1000);
      if (ap_mode_active || (millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) return;
    }
  }
}

// =============================================
// TESTA UM ÚNICO ENDEREÇO MAC (com fases)
// =============================================
void testarMAC(const uint8_t* mac, const String &tipo, const String &ouiNome) {
  String macStr = macToString(mac);
  String vendor = ouiLookupName(mac);  // fabricante real da IEEE
  logf("Testando [%s|%s] %s (%s) ... ", tipo.c_str(), ouiNome.c_str(),
       macStr.c_str(), vendor.c_str());
  pushEvent("test_start", macStr.c_str(), vendor.c_str());

  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);

  WiFi.disconnect(true);
  delay(500);

  if (!setStationMAC(mac)) {
    logln("FALHA ao definir MAC");
    pushEvent("blocked", macStr.c_str(), "MAC_SET_FAIL");
    unsigned long nowSeg = (millis() - startMillis) / 1000;
    appendCSV(nowSeg, macStr, vendor, tipo, PHASE_MAC_SET_FAIL, -99, "MAC_SET_FAIL", "");
    phaseStats.phase_mac_set_fail++;
    if (tipo == "random") { stats.random_errors++; registrarOUIStat(ouiNome, true); }
    else if (tipo == "exact") stats.exact_errors++;
    piscarLED(1, 150);
    return;
  }

  unsigned long t0 = millis();
  WiFi.begin(config_ssid, config_pass);

  // Aguarda conexao com timeout de 20s, monitorando fases
  int timeout = 20;
  bool conectado = false;
  int linkStatus = 0;

  while (timeout > 0) {
    delay(1000);
    timeout--;
    processCommands();
    if (ap_mode_active) {
      WiFi.disconnect(true);
      return;
    }

    // Verificar WiFi.status() E o link status raw
    // WiFi.status() so retorna WL_CONNECTED quando tem IP,
    // mas o chip pode estar conectado (JOIN) sem IP ainda
    if (WiFi.status() == WL_CONNECTED) {
      conectado = true;
      break;
    }
    int ls = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    if (ls == CYW43_LINK_JOIN || ls == CYW43_LINK_NOIP || ls == CYW43_LINK_UP) {
      conectado = true;
      break;
    }
  }

  unsigned long elapsedMs = millis() - t0;
  uint8_t phase;
  int reasonCode = 0;
  String reasonName = "NONE";
  String ip = "";

  if (conectado) {
    // Forcar reinicio do DHCP no netif STA apos troca de MAC
    for (struct netif *n = netif_list; n != NULL; n = n->next) {
      if (n->name[0] == 'e') {
        dhcp_start(n);
        break;
      }
    }

    // Conexao WiFi estabelecida. Verificar DHCP (IP)
    unsigned long tDhcp0 = millis();
    bool temIP = false;
    while (millis() - tDhcp0 < 15000) {
      processCommands();
      if (ap_mode_active) { WiFi.disconnect(true); return; }
      IPAddress lip = WiFi.localIP();
      if (lip[0] != 0 || lip[1] != 0 || lip[2] != 0 || lip[3] != 0) {
        temIP = true;
        ip = lip.toString();
        break;
      }
      delay(500);
    }

    if (temIP) {
      phase = PHASE_CONNECTED;
      reasonCode = 0;
      reasonName = "OK";
      phaseStats.phase_connected++;
    } else {
      phase = PHASE_DHCP_FAIL;
      reasonCode = CYW43_LINK_NOIP;
      reasonName = "DHCP_TIMEOUT";
      phaseStats.phase_dhcp_fail++;
      ip = "(sem DHCP)";
    }
  } else {
    // Conexao falhou. Usar cyw43_wifi_link_status para determinar a fase
    linkStatus = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);

    if (linkStatus == CYW43_LINK_BADAUTH) {
      phase = PHASE_AUTH_FAIL;
      reasonCode = linkStatus;
      reasonName = "BADAUTH";
      phaseStats.phase_auth_fail++;
    } else if (linkStatus == CYW43_LINK_NONET) {
      phase = PHASE_SCAN_FAIL;
      reasonCode = linkStatus;
      reasonName = "SSID_NOT_FOUND";
      phaseStats.phase_scan_fail++;
    } else if (linkStatus == CYW43_LINK_FAIL) {
      phase = PHASE_HANDSHAKE_FAIL;
      reasonCode = linkStatus;
      reasonName = "HANDSHAKE_OR_ASSOC_FAIL";
      phaseStats.phase_handshake_fail++;
    } else if (linkStatus == CYW43_LINK_DOWN) {
      phase = PHASE_ASSOC_FAIL;
      reasonCode = linkStatus;
      reasonName = "LINK_DOWN";
      phaseStats.phase_assoc_fail++;
    } else {
      // JOIN, NOIP ou outro estado inesperado
      phase = PHASE_ASSOC_FAIL;
      reasonCode = linkStatus;
      if (linkStatus == CYW43_LINK_JOIN) reasonName = "JOIN_NOIP";
      else if (linkStatus == CYW43_LINK_NOIP) reasonName = "NOIP";
      else if (linkStatus == CYW43_LINK_UP) reasonName = "UP_UNEXPECTED";
      else reasonName = "UNKNOWN";
      phaseStats.phase_assoc_fail++;
    }
  }

  // Testes de conectividade real (ping/DNS/HTTP) apenas com IP valido
  int connPingMs = -1, connDnsOk = -1, connHttpOk = -1;
  if (phase == PHASE_CONNECTED && ip.length() > 0 && ip != "(sem DHCP)") {
    ConnTestResult cr = testarConectividadeReal();
    connPingMs = cr.ping_ms;
    connDnsOk = cr.dns_ok ? 1 : 0;
    connHttpOk = cr.http_ok ? 1 : 0;
    registrarConnStats(cr);
  }

  // LED feedback: numero de piscadas = fase
  // 2 = scan fail, 3 = auth fail, 4 = assoc fail, 5 = handshake fail, 6 = dhcp fail, 7 = connected
  int ledPulses = phase;  // PHASE_SCAN_FAIL=2, AUTH=3, ASSOC=4, HANDSHAKE=5, DHCP=6, CONNECTED=7
  piscarLED(ledPulses, 120);

  // Emitir evento live
  {
    char detail[64];
    snprintf(detail, sizeof(detail), "%s %s",
             vendor.c_str(), phaseName(phase));
    if (conectado) {
      pushEvent("result", macStr.c_str(), detail);
    } else {
      pushEvent("blocked", macStr.c_str(), detail);
    }
  }

  logf("%s [fase=%s, rc=%d/%s, %lums] (%s)\n",
       conectado ? "CONECTADO" : "BLOQUEADO",
       phaseName(phase), reasonCode, reasonName.c_str(),
       elapsedMs, ip.c_str());

  // Atualiza estatisticas e fila de bloqueio
  if (tipo == "random") {
    if (conectado) stats.random_connected++;
    else stats.random_blocked++;
  } else if (tipo == "exact") {
    if (conectado) stats.exact_connected++;
    else stats.exact_blocked++;
  }

  // Gerencia lista de bloqueados e retestes + OUI stats
  addBlockedMAC(mac, tipo.c_str(), conectado);
  if (tipo == "random" || tipo == "retest") {
    registrarOUIStat(ouiNome, !conectado);
  }

  unsigned long nowSeg = (millis() - startMillis) / 1000;
  appendCSV(nowSeg, macStr, vendor, tipo, phase, reasonCode, reasonName, ip,
            connPingMs, connDnsOk, connHttpOk);

  WiFi.disconnect(true);
  delay(200);
}

// Pisca o LED n vezes com intervalo definido
void piscarLED(int vezes, int intervaloMs) {
  if (vezes <= 0) return;
  for (int i = 0; i < vezes; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(intervaloMs);
    digitalWrite(LED_BUILTIN, LOW);
    if (i < vezes - 1) delay(intervaloMs);
  }
}

// =============================================
// TESTES DE CONECTIVIDADE REAL (implementacao)
// =============================================
ConnTestResult testarConectividadeReal() {
  ConnTestResult r;
  r.ping_ms = -1;
  r.dns_ok = false;
  r.http_ok = false;

  IPAddress gw = WiFi.gatewayIP();

  // 1. Ping ao gateway
  if (gw[0] != 0 || gw[1] != 0 || gw[2] != 0 || gw[3] != 0) {
    int ret = WiFi.ping(gw, 3);
    if (ret >= 0) {
      r.ping_ms = ret;
      if (log_enabled) { replyf("  [PING] gateway %s: %d ms\n", gw.toString().c_str(), ret); }
    } else {
      r.ping_ms = -2;
      if (log_enabled) { replyf("  [PING] gateway %s: TIMEOUT\n", gw.toString().c_str()); }
    }
  } else {
    r.ping_ms = -3;
  }

  // 2. Resolucao DNS
  IPAddress resolved;
  if (WiFi.hostByName("neverssl.com", resolved)) {
    r.dns_ok = true;
    if (log_enabled) { replyf("  [DNS] neverssl.com -> %s OK\n", resolved.toString().c_str()); }
  } else {
    if (log_enabled) { reply("  [DNS] neverssl.com: FALHA\n"); }
  }

  // 3. HTTP GET
  WiFiClient client;
  client.setTimeout(5);
  if (client.connect("neverssl.com", 80)) {
    client.print("GET / HTTP/1.0\r\nHost: neverssl.com\r\nConnection: close\r\n\r\n");
    unsigned long t0 = millis();
    bool gotResponse = false;
    while (millis() - t0 < 5000) {
      if (client.available()) {
        String line = client.readStringUntil('\n');
        if (line.startsWith("HTTP/")) {
          r.http_ok = true;
          if (log_enabled) { replyf("  [HTTP] neverssl.com: %s", line.c_str()); }
          break;
        }
        gotResponse = true;
      }
      if (!client.connected() && gotResponse) break;
      delay(10);
    }
    if (!r.http_ok && log_enabled) { reply("  [HTTP] neverssl.com: sem resposta HTTP\n"); }
    client.stop();
  } else {
    if (log_enabled) { reply("  [HTTP] neverssl.com: FALHA ao conectar\n"); }
  }

  // Emitir evento de conectividade
  {
    char detail[64];
    snprintf(detail, sizeof(detail), "ping=%d dns=%d http=%d", r.ping_ms, r.dns_ok, r.http_ok);
    pushEvent("conn_test", "", detail);
  }

  return r;
}

void registrarConnStats(const ConnTestResult &r) {
  connStats.total_tested++;
  if (r.ping_ms >= 0) connStats.ping_ok++;
  else if (r.ping_ms == -2 || r.ping_ms == -3) connStats.ping_fail++;
  if (r.dns_ok) connStats.dns_ok++; else connStats.dns_fail++;
  if (r.http_ok) connStats.http_ok++; else connStats.http_fail++;
}

// =============================================
// TESTES DE POLITICA DE BLOQUEIO (implementacao)
// =============================================

void iniciarBlacklistPolicyTest() {
  if (timeout_test_active) {
    reply("Teste de politica ja esta em andamento.\n");
    reply("Use 'blacklist-policy stop' para parar.\n");
    return;
  }

  reply("\n=== TESTE DE POLITICA DE BLACKLIST ===\n");
  reply("Serao gerados 3 MACs aleatorios, testados ate serem\n");
  reply("confirmados como bloqueados, e depois re-testados apos:\n");
  reply("  5 min, 15 min, 30 min, 60 min\n\n");
  reply("O teste inicia agora. Aguarde os MACs serem bloqueados...\n");

  timeout_test_count = 0;
  timeout_test_active = true;
  timeout_test_running = false;
  timeout_test_idx = 0;

  // Gera 3 MACs com o OUI ativo
  for (int i = 0; i < 3; i++) {
    memcpy(timeout_tests[i].mac, perfis_oui[oui_ativo].prefixo, 3);
    timeout_tests[i].mac[3] = random(0, 256);
    timeout_tests[i].mac[4] = random(0, 256);
    timeout_tests[i].mac[5] = random(0, 256);
    timeout_tests[i].blocked_at_ms = 0;
    timeout_tests[i].next_retest_ms = 0;
    timeout_tests[i].stage = 0;
    timeout_tests[i].done = false;
    memset(timeout_tests[i].was_blocked, 0, sizeof(timeout_tests[i].was_blocked));
    timeout_test_count++;
    replyf("  MAC #%d: %s\n", i + 1, macToString(timeout_tests[i].mac).c_str());
  }
}

void pararBlacklistPolicyTest() {
  if (!timeout_test_active) {
    reply("Nenhum teste de politica ativo.\n");
    return;
  }
  timeout_test_active = false;
  timeout_test_running = false;
  reply("Teste de politica de blacklist interrompido.\n");
}

// Encontra o indice de um MAC nos timeout_tests
int findTimeoutTestMAC(const uint8_t *mac) {
  for (int i = 0; i < timeout_test_count; i++) {
    if (macIgual(timeout_tests[i].mac, mac)) return i;
  }
  return -1;
}

// Chamada apos cada testarMAC quando o teste de politica esta ativo
void processarTimeoutTests() {
  if (!timeout_test_active) return;

  unsigned long now = millis();

  // Fase 1: Verificar se os MACs iniciais ja foram confirmados como bloqueados
  if (!timeout_test_running) {
    for (int i = 0; i < timeout_test_count; i++) {
      if (timeout_tests[i].blocked_at_ms > 0) continue; // ja foi bloqueado

      int bi = buscarBlockedMAC(timeout_tests[i].mac);
      if (bi >= 0 && blocked_macs[bi].confirmado) {
        // MAC confirmado como bloqueado! Registrar timestamp
        timeout_tests[i].blocked_at_ms = now;
        timeout_tests[i].stage = 0;
        timeout_tests[i].next_retest_ms = now + TIMEOUT_MINUTES[0] * 60000UL;
        replyf("\n[POLICY] MAC %s confirmado bloqueado. Reteste em %lu min.\n",
               macToString(timeout_tests[i].mac).c_str(), TIMEOUT_MINUTES[0]);
      }
    }

    // Verificar se todos ja foram bloqueados ou se expirou o tempo maximo (5 ciclos)
    bool allBlocked = true;
    for (int i = 0; i < timeout_test_count; i++) {
      if (timeout_tests[i].blocked_at_ms == 0) allBlocked = false;
    }
    if (!allBlocked && stats.total_cycles < 10) return; // continua esperando
    if (!allBlocked) {
      reply("[POLICY] Nem todos os MACs foram bloqueados apos 10 ciclos.\n");
      reply("Continuando apenas com os que foram confirmados.\n");
    }

    // Iniciar fase de retestes
    timeout_test_running = true;
    timeout_test_idx = 0;
    reply("\n[POLICY] Iniciando fase de retestes periodicos...\n");
  }

  // Fase 2: Verificar se algum MAC precisa ser re-testado agora
  if (timeout_test_running && !ap_mode_active) {
    for (int i = 0; i < timeout_test_count; i++) {
      if (timeout_tests[i].done) continue;
      if (timeout_tests[i].blocked_at_ms == 0) continue;
      if (now < timeout_tests[i].next_retest_ms) continue;

      timeout_test_idx = i;
      String ouiNome = ouiDoMAC(timeout_tests[i].mac);

      replyf("\n[POLICY] Retestando MAC %s (estagio %d: %lu min)...\n",
             macToString(timeout_tests[i].mac).c_str(),
             timeout_tests[i].stage + 1,
             TIMEOUT_MINUTES[timeout_tests[i].stage]);

      testarMAC(timeout_tests[i].mac, "policy", ouiNome);

      // Verificar resultado: olhar se o MAC continua na lista de bloqueados
      int bi = buscarBlockedMAC(timeout_tests[i].mac);
      bool stillBlocked = (bi >= 0);

      timeout_tests[i].was_blocked[timeout_tests[i].stage] = stillBlocked;

      if (stillBlocked) {
        replyf("  -> AINDA BLOQUEADO apos %lu min\n",
               TIMEOUT_MINUTES[timeout_tests[i].stage]);
      } else {
        replyf("  -> DESBLOQUEADO apos %lu min! Bloqueio temporario confirmado!\n",
               TIMEOUT_MINUTES[timeout_tests[i].stage]);
      }

      // Avancar para o proximo estagio
      timeout_tests[i].stage++;
      if (timeout_tests[i].stage >= TIMEOUT_STAGES) {
        timeout_tests[i].done = true;
        replyf("[POLICY] MAC %s completou todos os estagios.\n",
               macToString(timeout_tests[i].mac).c_str());
      } else {
        timeout_tests[i].next_retest_ms = now + TIMEOUT_MINUTES[timeout_tests[i].stage] * 60000UL;
        replyf("[POLICY] Proximo reteste em %lu min.\n",
               TIMEOUT_MINUTES[timeout_tests[i].stage]);
      }

      break; // testa apenas um MAC por ciclo para nao atrasar o loop
    }

    // Verificar se todos terminaram
    bool allDone = true;
    for (int i = 0; i < timeout_test_count; i++) {
      if (!timeout_tests[i].done && timeout_tests[i].blocked_at_ms > 0) allDone = false;
    }
    if (allDone && timeout_test_running) {
      reply("\n=== RESULTADO FINAL: POLITICA DE BLACKLIST ===\n");
      for (int i = 0; i < timeout_test_count; i++) {
        if (timeout_tests[i].blocked_at_ms == 0) {
          replyf("  %s: nao foi bloqueado (nao elegivel)\n",
                 macToString(timeout_tests[i].mac).c_str());
          continue;
        }
        replyf("  %s:\n", macToString(timeout_tests[i].mac).c_str());
        for (int s = 0; s < TIMEOUT_STAGES; s++) {
          if (timeout_tests[i].was_blocked[s]) {
            replyf("    %lu min: BLOQUEADO\n", TIMEOUT_MINUTES[s]);
          } else {
            replyf("    %lu min: DESBLOQUEADO <- timeout maximo\n", TIMEOUT_MINUTES[s]);
            break;
          }
          if (s == TIMEOUT_STAGES - 1) {
            reply("    -> Bloqueio PERMANENTE (resistiu a 60 min)\n");
          }
        }
      }
      reply("=============================================\n\n");
      timeout_test_active = false;
      timeout_test_running = false;

      // Salvar em CSV
      File f = LittleFS.open("/policy_test.csv", "a");
      if (f) {
        f.println("mac,5min,15min,30min,60min,tipo");
        for (int i = 0; i < timeout_test_count; i++) {
          if (timeout_tests[i].blocked_at_ms == 0) continue;
          f.printf("%s,%d,%d,%d,%d,timeout\n",
                   macToString(timeout_tests[i].mac).c_str(),
                   timeout_tests[i].was_blocked[0],
                   timeout_tests[i].was_blocked[1],
                   timeout_tests[i].was_blocked[2],
                   timeout_tests[i].was_blocked[3]);
        }
        f.close();
        reply("Resultados salvos em /policy_test.csv\n");
      }
    }
  }
}

// Rate Limiting Test
void iniciarRatelimitTest() {
  if (ratelimit_test_active) {
    reply("Teste de rate limiting ja esta em andamento.\n");
    return;
  }
  if (timeout_test_active) {
    reply("Termine o teste de blacklist-policy primeiro.\n");
    return;
  }

  reply("\n=== TESTE DE RATE LIMITING ===\n");
  replyf("Fase 1: %d MACs com intervalo de 2s (rajada)\n", RATELIMIT_MACS_PER_PHASE);
  replyf("Fase 2: %d MACs com intervalo de 30s (espacado)\n", RATELIMIT_MACS_PER_PHASE);
  reply("O teste usara o OUI ativo e rodara entre os ciclos normais.\n\n");

  ratelimit_test_active = true;
  ratelimit_phase = 0;
  ratelimit_fast_blocked = 0;
  ratelimit_fast_total = 0;
  ratelimit_slow_blocked = 0;
  ratelimit_slow_total = 0;
  ratelimit_start_ms = millis();
}

void processarRatelimitTest() {
  if (!ratelimit_test_active) return;
  if (ap_mode_active) return;

  unsigned long totalTestadas = ratelimit_fast_total + ratelimit_slow_total;

  // Fase 0: rajada (fast)
  if (ratelimit_phase == 0) {
    if (ratelimit_fast_total >= RATELIMIT_MACS_PER_PHASE) {
      ratelimit_phase = 1;
      replyf("\n[RATELIMIT] Fase 1 concluida: %lu/%lu bloqueados (%.0f%%)\n",
             ratelimit_fast_blocked, ratelimit_fast_total,
             ratelimit_fast_total > 0 ? 100.0 * ratelimit_fast_blocked / ratelimit_fast_total : 0);
      reply("Iniciando Fase 2 (intervalo de 30s)...\n");
      return;
    }

    // Gera e testa um MAC com intervalo curto
    uint8_t mac[6];
    memcpy(mac, perfis_oui[oui_ativo].prefixo, 3);
    mac[3] = random(0, 256);
    mac[4] = random(0, 256);
    mac[5] = random(0, 256);

    replyf("[RATELIMIT-FAST] #%lu: %s\n", ratelimit_fast_total + 1, macToString(mac).c_str());
    testarMAC(mac, "ratelimit", ouiNomeAtivo());

    ratelimit_fast_total++;
    if (buscarBlockedMAC(mac) >= 0) ratelimit_fast_blocked++;

    // Intervalo curto (2s)
    for (int w = 0; w < 2; w++) {
      delay(1000);
      processCommands();
      if (ap_mode_active) return;
    }
    return;
  }

  // Fase 1: espacado (slow)
  if (ratelimit_phase == 1) {
    if (ratelimit_slow_total >= RATELIMIT_MACS_PER_PHASE) {
      ratelimit_phase = 2;
      replyf("\n[RATELIMIT] Fase 2 concluida: %lu/%lu bloqueados (%.0f%%)\n",
             ratelimit_slow_blocked, ratelimit_slow_total,
             ratelimit_slow_total > 0 ? 100.0 * ratelimit_slow_blocked / ratelimit_slow_total : 0);

      // Analise final
      float taxaFast = ratelimit_fast_total > 0 ? 100.0 * ratelimit_fast_blocked / ratelimit_fast_total : 0;
      float taxaSlow = ratelimit_slow_total > 0 ? 100.0 * ratelimit_slow_blocked / ratelimit_slow_total : 0;

      reply("\n=== RESULTADO: RATE LIMITING ===\n");
      replyf("Rajada (2s):   %.0f%% bloqueados (%lu/%lu)\n",
             taxaFast, ratelimit_fast_blocked, ratelimit_fast_total);
      replyf("Espacado (30s): %.0f%% bloqueados (%lu/%lu)\n",
             taxaSlow, ratelimit_slow_blocked, ratelimit_slow_total);

      if (taxaFast > taxaSlow * 1.5) {
        reply("DIAGNOSTICO: Rate limiting DETECTADO!\n");
        reply("O AP bloqueia mais MACs quando as tentativas sao rapidas.\n");
        reply("Intervalo seguro sugerido: >= 30s entre tentativas.\n");
      } else {
        reply("DIAGNOSTICO: Rate limiting NAO detectado.\n");
        reply("As taxas de bloqueio sao similares independente do intervalo.\n");
      }
      reply("==================================\n\n");

      // Salvar em CSV
      File f = LittleFS.open("/policy_test.csv", "a");
      if (f) {
        f.println("phase,macs_tested,macs_blocked,rate_pct");
        f.printf("fast,%lu,%lu,%.1f\n", ratelimit_fast_total, ratelimit_fast_blocked, taxaFast);
        f.printf("slow,%lu,%lu,%.1f\n", ratelimit_slow_total, ratelimit_slow_blocked, taxaSlow);
        f.close();
        reply("Resultados salvos em /policy_test.csv\n");
      }

      ratelimit_test_active = false;
      return;
    }

    // Aguardar 30s entre tentativas (fora do loop principal de teste)
    // Usamos um contador de segundos acumulado
    static unsigned long lastSlowTest = 0;
    if (millis() - lastSlowTest < 30000) return;
    lastSlowTest = millis();

    uint8_t mac[6];
    memcpy(mac, perfis_oui[oui_ativo].prefixo, 3);
    mac[3] = random(0, 256);
    mac[4] = random(0, 256);
    mac[5] = random(0, 256);

    replyf("[RATELIMIT-SLOW] #%lu: %s\n", ratelimit_slow_total + 1, macToString(mac).c_str());
    testarMAC(mac, "ratelimit", ouiNomeAtivo());

    ratelimit_slow_total++;
    if (buscarBlockedMAC(mac) >= 0) ratelimit_slow_blocked++;
  }
}

// =============================================
// GERENCIAMENTO DE MACs BLOQUEADOS
// =============================================

bool macIgual(const uint8_t *a, const uint8_t *b) {
  for (int i = 0; i < 6; i++) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

int buscarBlockedMAC(const uint8_t *mac) {
  for (int i = 0; i < blocked_count; i++) {
    if (macIgual(blocked_macs[i].mac, mac)) return i;
  }
  return -1;
}

void addBlockedMAC(const uint8_t *mac, const char *tipo, bool conectado) {
  int idx = buscarBlockedMAC(mac);

  if (conectado) {
    // MAC conectou: se estava na lista de bloqueados, remover
    if (idx >= 0) {
      replyf("  -> MAC %s saiu da lista de bloqueados (conectou)\n",
             macToString(mac).c_str());
      // Remove deslocando os itens
      for (int i = idx; i < blocked_count - 1; i++) {
        blocked_macs[i] = blocked_macs[i + 1];
      }
      blocked_count--;
    }
    // Remove de retestes pendentes se estiver
    int r = buscarRetest(mac);
    if (r >= 0) {
      for (int i = r; i < retest_count - 1; i++) {
        retest_queue[i] = retest_queue[i + 1];
      }
      retest_count--;
    }
    return;
  }

  // MAC não conectou (bloqueado)
  if (idx >= 0) {
    // Já estava na lista: incrementa contagem
    blocked_macs[idx].test_count++;
    if (blocked_macs[idx].test_count > 3) blocked_macs[idx].test_count = 3;
  } else {
    // Novo bloqueado: adiciona na lista
    if (blocked_count < MAX_BLOCKED) {
      memcpy(blocked_macs[blocked_count].mac, mac, 6);
      blocked_macs[blocked_count].test_count = 1;
      snprintf(blocked_macs[blocked_count].tipo, sizeof(blocked_macs[blocked_count].tipo), "%s", tipo);
      blocked_macs[blocked_count].confirmado = false;
      blocked_count++;
    }
  }

  // Tipos "exact" ou "retest" já estão sendo confirmados
  if (strcmp(tipo, "retest") == 0) {
    // Reteste: decrementa ciclos
    int r = buscarRetest(mac);
    if (r >= 0) {
      retest_queue[r].ciclos_restantes--;
      if (retest_queue[r].ciclos_restantes <= 0) {
        // Confirmado! Marca na lista
        int bi = buscarBlockedMAC(mac);
        if (bi >= 0) {
          blocked_macs[bi].confirmado = true;
          replyf("\n!!! MAC CONFIRMADO COMO BLOQUEADO: %s !!!\n", macToString(mac).c_str());
          replyf("!!! Tipo: %s, Testes: %d\n\n", blocked_macs[bi].tipo, blocked_macs[bi].test_count);
        }
        // Remove da fila de reteste
        for (int i = r; i < retest_count - 1; i++) {
          retest_queue[i] = retest_queue[i + 1];
        }
        retest_count--;
      }
    }
  } else if (blocked_count > 0 && strcmp(tipo, "exact") != 0) {
    // MACs aleatórios que foram bloqueados: enfileirar para reteste
    // (exceto o MAC exato, que já é testado em todo ciclo)
    addRetest(mac);
  }
}

// =============================================
// FILA DE RETESTE
// =============================================
int buscarRetest(const uint8_t *mac) {
  for (int i = 0; i < retest_count; i++) {
    if (macIgual(retest_queue[i].mac, mac)) return i;
  }
  return -1;
}

void addRetest(const uint8_t *mac) {
  if (buscarRetest(mac) >= 0) return;  // já está na fila
  if (retest_count >= MAX_RETEST) return;  // fila cheia

  memcpy(retest_queue[retest_count].mac, mac, 6);
  retest_queue[retest_count].ciclos_restantes = CONFIRMAR_CICLOS;
  retest_count++;
  logf("  -> MAC %s enfileirado para reteste (%d ciclos)\n",
       macToString(mac).c_str(), CONFIRMAR_CICLOS);
}

// =============================================
// AP MODE
// =============================================

void startAPMode() {
  if (ap_mode_active) {
    reply("AP ja esta ativo.\n");
    return;
  }

  replyf("Iniciando AP com SSID: %s\n", config_ap_ssid);

  WiFi.disconnect(true);
  delay(500);

  WiFi.mode(WIFI_AP);
  delay(100);

  // Apply custom AP MAC before starting the AP (if configured)
  if (ap_config_mac_set) {
    if (setAPMAC(ap_config_mac)) {
      replyf("MAC do AP definido para: %s\n", macToString(ap_config_mac).c_str());
    } else {
      reply("Aviso: falha ao definir MAC do AP, usando MAC padrao.\n");
    }
  }

  // Apply custom IP configuration
  WiFi.softAPConfig(ap_config_ip, ap_config_gateway, ap_config_subnet);

  if (WiFi.softAP(config_ap_ssid, config_ap_pass)) {
    ap_mode_active = true;
    connected_station_count = 0;
    memset(connected_stations, 0, sizeof(connected_stations));
    last_station_poll_ms = 0;

    replyf("AP iniciado. IP: %s/%s\n",
           WiFi.softAPIP().toString().c_str(),
           ap_config_subnet.toString().c_str());
    replyf("SSID: %s\n", config_ap_ssid);
    replyf("MAC do AP: %s\n", WiFi.softAPmacAddress().c_str());
    reply("Conecte dispositivos e use 'stations' para listar.\n");
    replyf("Dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());
    initWebDashboard();
  } else {
    reply("Erro ao iniciar AP!\n");
  }
}

void stopAPMode() {
  if (!ap_mode_active) {
    reply("AP nao esta ativo.\n");
    return;
  }

  reply("Desligando AP...\n");
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(500);

  stopWebDashboard();
  ap_mode_active = false;
  connected_station_count = 0;
  memset(connected_stations, 0, sizeof(connected_stations));

  reply("Modo STA (teste) reativado. Use 'summary' para status.\n");
}

void updateStationList() {
  if (!ap_mode_active) return;

  unsigned long now = millis();
  if (now - last_station_poll_ms < 3000) return;
  last_station_poll_ms = now;

  int max_stas;
  cyw43_wifi_ap_get_max_stas(&cyw43_state, &max_stas);
  if (max_stas <= 0) return;

  uint8_t *macs = (uint8_t *)malloc(max_stas * 6);
  if (!macs) return;

  int count = max_stas;
  cyw43_wifi_ap_get_stas(&cyw43_state, &count, macs);

  for (int i = 0; i < connected_station_count; i++) {
    connected_stations[i].active = false;
  }

  for (int i = 0; i < count; i++) {
    uint8_t *mac = macs + (i * 6);
    bool found = false;

    for (int j = 0; j < connected_station_count; j++) {
      if (memcmp(connected_stations[j].mac, mac, 6) == 0) {
        connected_stations[j].active = true;
        connected_stations[j].last_seen_ms = now;
        found = true;
        break;
      }
    }

    if (!found && connected_station_count < MAX_CONNECTED_STATIONS) {
      memcpy(connected_stations[connected_station_count].mac, mac, 6);
      connected_stations[connected_station_count].first_seen_ms = now;
      connected_stations[connected_station_count].last_seen_ms = now;
      connected_stations[connected_station_count].active = true;
      connected_station_count++;

      replyf("\n!!! NOVO DISPOSITIVO CONECTADO: %s !!!\n",
             macToString(mac).c_str());
    }
  }

  for (int i = 0; i < connected_station_count; i++) {
    if (!connected_stations[i].active && connected_stations[i].last_seen_ms > 0) {
      replyf("--- Dispositivo desconectado: %s ---\n",
             macToString(connected_stations[i].mac).c_str());
      connected_stations[i].last_seen_ms = 0;
    }
  }

  free(macs);
}

void cmdStations() {
  if (!ap_mode_active) {
    reply("AP nao esta ativo. Use 'ap on' primeiro.\n");
    return;
  }

  unsigned long now = millis();

  replyf("\n=== DISPOSITIVOS CONECTADOS AO AP ===\n");
  replyf("SSID: %s\n", config_ap_ssid);
  replyf("IP do AP: %s\n", WiFi.softAPIP().toString().c_str());
  replyf("MAC do AP: %s\n", WiFi.softAPmacAddress().c_str());
  replyf("Conectados agora: %d\n", WiFi.softAPgetStationNum());
  replyf("Total registrados: %d\n\n", connected_station_count);

  if (connected_station_count == 0) {
    reply("  (nenhum dispositivo registrado)\n");
  } else {
    replyf("%-3s %-20s %-10s %-15s\n",
           "#", "MAC", "Status", "Tempo conectado");
    reply("------------------------------------------------\n");

    for (int i = 0; i < connected_station_count; i++) {
      unsigned long elapsed = (now - connected_stations[i].first_seen_ms) / 1000;
      char tempo[32];
      snprintf(tempo, sizeof(tempo), "%02luh%02lum%02lus",
               elapsed / 3600, (elapsed % 3600) / 60, elapsed % 60);

      replyf("%-3d %-20s %-10s %-15s\n",
             i + 1,
             macToString(connected_stations[i].mac).c_str(),
             connected_stations[i].active ? "Online" : "Offline",
             tempo);
    }
  }
  reply("============================================\n\n");
}

// =============================================
// ALTERA O MAC DA INTERFACE STA
// =============================================
bool setStationMAC(const uint8_t* newMAC) {
  extern cyw43_t cyw43_state;

  memcpy(cyw43_state.mac, newMAC, 6);

  uint8_t buf[32];
  const char *var = "cur_etheraddr";
  size_t varlen = strlen(var) + 1;
  memcpy(buf, var, varlen);
  memcpy(buf + varlen, newMAC, 6);

  int ret = cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, varlen + 6, buf, CYW43_ITF_STA);

  // Atualizar tambem o netif STA (w0) para DHCP/ARP usarem o novo MAC
  for (struct netif *n = netif_list; n != NULL; n = n->next) {
    if (n->name[0] == 'w' && n->name[1] == '0') {
      memcpy(n->hwaddr, newMAC, 6);
      break;
    }
  }

  return (ret == 0);
}

bool setAPMAC(const uint8_t *newMAC) {
  if (!newMAC) return false;
  extern cyw43_t cyw43_state;

  uint8_t buf[32];
  const char *var = "cur_etheraddr";
  size_t varlen = strlen(var) + 1;
  memcpy(buf, var, varlen);
  memcpy(buf + varlen, newMAC, 6);

  int ret = cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, varlen + 6, buf, CYW43_ITF_AP);
  return (ret == 0);
}

// =============================================
// MONITOR MODE EXPERIMENTAL
// =============================================
bool monitor_mode_active = false;

bool setMonitorMode(bool enable) {
  extern cyw43_t cyw43_state;

  // WLC_SET_MONITOR = 108
  // Envia como SET_VAR com o comando nos primeiros 4 bytes e valor nos 4 seguintes
  uint8_t buf[8];
  uint32_t cmd = 108;  // WLC_SET_MONITOR
  uint32_t val = enable ? 1 : 0;
  memcpy(buf, &cmd, 4);
  memcpy(buf + 4, &val, 4);

  int ret = cyw43_ioctl(&cyw43_state, CYW43_IOCTL_SET_VAR, 8, buf, CYW43_ITF_STA);
  if (ret == 0) {
    replyf("[MONITOR] Modo monitor %s via IOCTL.\n", enable ? "ATIVADO" : "desativado");
    monitor_mode_active = enable;
  } else {
    replyf("[MONITOR] Falha ao %s modo monitor (ret=%d).\n",
           enable ? "ativar" : "desativar", ret);
  }
  return (ret == 0);
}

// Tenta capturar frames raw em modo monitor (experimental)
void pollMonitorFrames() {
  if (!monitor_mode_active) return;

  // Tentar ler dados diretamente do buffer da interface
  // Em modo monitor, o chip pode entregar frames 802.11 raw
  // Usamos a callback de ethernet para verificar

  // O CYW43 entrega frames via cyw43_cb_process_ethernet -> lwIP
  // Em modo monitor, os frames 802.11 podem ser descartados pelo driver
  // como "nao-ethernet". Tentamos verificar se ha dados pendentes.

  // Por enquanto, apenas verificamos periodicamente se o modo
  // continua ativo e reportamos
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck < 10000) return;
  lastCheck = millis();

  // Verifica se ainda estamos em modo monitor
  int linkStatus = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
  logf("[MONITOR] Status: link=%d, monitor=%s\n",
       linkStatus, monitor_mode_active ? "ON" : "OFF");
}

void startMonitorMode() {
  reply("\n=== MODO MONITOR EXPERIMENTAL ===\n");
  reply("Tentando ativar WLC_SET_MONITOR (IOCTL 108)...\n");
  reply("Este recurso e experimental e depende do firmware CYW43439.\n\n");

  WiFi.disconnect(true);
  delay(500);

  if (setMonitorMode(true)) {
    reply("Modo monitor ativado com sucesso!\n");
    reply("O chip CYW43439 agora opera em modo promiscuo.\n");
    reply("Nota: A captura de frames raw 802.11 depende do suporte\n");
    reply("do driver. O firmware pode nao entregar frames ao host.\n\n");

    reply("Use 'monitor off' para desativar.\n");
    reply("Use 'monitor status' para verificar o estado.\n");

    // Criar CSV para captura
    if (!LittleFS.exists("/monitor.csv")) {
      File f = LittleFS.open("/monitor.csv", "w");
      if (f) {
        f.println("timestamp_ms,frame_type,src_mac,dst_mac,rssi,channel,data_len");
        f.close();
      }
    }
  } else {
    reply("FALHA ao ativar modo monitor.\n");
    reply("Possiveis causas:\n");
    reply("  - Firmware CYW43439 nao suporta WLC_SET_MONITOR\n");
    reply("  - Driver nao implementa o IOCTL necessario\n");
    reply("  - Hardware precisa de configuracao adicional\n");
  }
}

void stopMonitorMode() {
  if (!monitor_mode_active) {
    reply("Modo monitor nao esta ativo.\n");
    return;
  }

  setMonitorMode(false);
  monitor_mode_active = false;

  reply("Modo monitor desativado. Voltando ao modo normal...\n");
  WiFi.mode(WIFI_STA);
  delay(200);
  reply("Pronto.\n");
}

void cmdMonitorStatus() {
  extern cyw43_t cyw43_state;
  int linkStatus = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);

  replyf("=== STATUS DO MODO MONITOR ===\n");
  replyf("Ativo: %s\n", monitor_mode_active ? "SIM" : "NAO");
  replyf("Link status: %d\n", linkStatus);
  replyf("WiFi mode: %d\n", WiFi.getMode());
  replyf("================================\n");
}
void setupLittleFS() {
  if (!LittleFS.begin()) {
    reply("Erro ao montar LittleFS! Verifique particao.\n");
    while (1) { delay(1000); }
  }
  reply("LittleFS montado.\n");

  if (!LittleFS.exists(CSV_FILENAME)) {
    File f = LittleFS.open(CSV_FILENAME, "w");
    if (f) {
      f.println("timestamp_s,mac_address,oui,type,result,phase,reason_code,reason_name,ip,ping_ms,dns_ok,http_ok");
      f.close();
      reply("Arquivo CSV criado com cabecalho.\n");
    } else {
      reply("Erro ao criar arquivo CSV!\n");
    }
  }
}

void appendCSV(unsigned long timestampSeg, const String &macStr,
               const String &oui, const String &tipo, uint8_t phase,
               int reasonCode, const String &reasonName, const String &ip,
               int pingMs, int dnsOk, int httpOk) {
  File f = LittleFS.open(CSV_FILENAME, "a");
  if (!f) {
    reply("ERRO: nao foi possivel abrir o CSV para escrita!\n");
    return;
  }
  f.printf("%lu,%s,%s,%s,%s,%s,%d,%s,%s,%d,%d,%d\n",
           timestampSeg, macStr.c_str(), oui.c_str(), tipo.c_str(),
           (phase == PHASE_CONNECTED) ? "connected" : "blocked",
           phaseName(phase), reasonCode, reasonName.c_str(), ip.c_str(),
           pingMs, dnsOk, httpOk);
  f.close();
}

// =============================================
// PROCESSADOR DE COMANDOS
// =============================================
void processCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (cmd_idx > 0) {
        cmd_buf[cmd_idx] = '\0';
        executeCommand(cmd_buf);
        cmd_idx = 0;
      }
    } else if (cmd_idx < CMD_BUF_SIZE - 1) {
      cmd_buf[cmd_idx++] = c;
    }
  }
  while (SerialBT.available()) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r') {
      if (cmd_idx > 0) {
        cmd_buf[cmd_idx] = '\0';
        executeCommand(cmd_buf);
        cmd_idx = 0;
      }
    } else if (cmd_idx < CMD_BUF_SIZE - 1) {
      cmd_buf[cmd_idx++] = c;
    }
  }
}

void executeCommand(char *cmd) {
  while (*cmd == ' ') cmd++;
  if (*cmd == '\0') return;

  replyf("\n> %s\n", cmd);

  if (strcmp(cmd, "help") == 0) {
    reply("Comandos disponiveis:\n");
    reply("  help                  Mostra esta ajuda\n");
    reply("  summary               Resumo completo do teste\n");
    reply("  dump                  Exporta o relatorio CSV gravado\n");
    reply("  reset                 Reseta estatisticas e contadores\n");
    reply("  clearlog              Apaga arquivos de log do LittleFS\n");
    reply("  ssid <nome>           Altera SSID da rede\n");
    reply("  pass <senha>          Altera senha da rede\n");
    reply("  target XX:XX:XX:XX:XX:XX  Altera MAC alvo\n");
    reply("  log on                Ativa logs detalhados\n");
    reply("  log off               Desativa logs (erros e summary aparecem)\n");
    reply("  oui list              Lista perfis OUI disponiveis\n");
    reply("  oui <perfil>          Seleciona perfil OUI (ex: oui Apple)\n");
    reply("  oui all               Modo rotativo: alterna entre todos os OUIs\n");
    reply("  oui info              Mostra status da base OUI IEEE\n");
    reply("  oui ram on/off        Ativa/desativa cache RAM (115 KB)\n");
    reply("  ap on                 Ativa modo AP (Pico vira roteador)\n");
    reply("  ap off                Desativa modo AP e volta ao teste\n");
    reply("  ap status             Mostra status do AP\n");
    reply("  ap ip [endereco] [gw] [mask]  Configura IP do AP\n");
    reply("  ap ip default         Restaura IP padrao (192.168.4.1)\n");
    reply("  ap mac [XX:XX:XX:XX:XX:XX]    Configura MAC do AP\n");
    reply("  ap mac default        Restaura MAC padrao do hardware\n");
    reply("  ap ssid <nome>        Altera SSID do AP (separado do STA)\n");
    reply("  ap pass <senha>       Altera senha do AP (separada do STA)\n");
    reply("  stations              Lista dispositivos conectados ao AP\n");
    reply("  captive on/off        Ativa/desativa captive portal no AP\n");
    reply("  tcp on/off            Servidor TCP na porta 2323 (controle remoto)\n");
    reply("  mqtt <ip> [porta]     Configura broker MQTT para publicacao\n");
    reply("  mqtt off              Desativa publicacao MQTT\n");
    reply("  monitor on/off/status Modo monitor experimental (WLC_SET_MONITOR)\n");
    reply("  blacklist-policy      Testa politica de blacklist (timeout)\n");
    reply("  ratelimit-test        Testa se ha rate limiting na rede\n");
    reply("  debug                 Modo AP Debug (captura fingerprint de dispositivos)\n");
    reply("  debugdump             Exibe dados capturados no AP Debug\n");

  } else if (strcmp(cmd, "summary") == 0) {
    cmdSummary();

  } else if (strcmp(cmd, "dump") == 0) {
    cmdDump();

  } else if (strcmp(cmd, "reset") == 0) {
    cmdReset();

  } else if (strncmp(cmd, "uploadoui ", 10) == 0) {
    // uploadoui <filename> <size>
    char fname[32]; unsigned long fsize;
    if (sscanf(cmd + 10, "%31s %lu", fname, &fsize) == 2) {
      replyf("READY %s %lu\n", fname, fsize);
      // Ler fsize bytes raw da serial
      File f = LittleFS.open(fname, "w");
      if (!f) {
        reply("ERROR: nao foi possivel criar arquivo\n");
        return;
      }
      unsigned long received = 0;
      unsigned long t0 = millis();
      while (received < fsize && millis() - t0 < 60000) {
        if (Serial.available()) {
          uint8_t buf[128];
          int n = Serial.readBytes(buf, min((unsigned long)sizeof(buf), fsize - received));
          if (n > 0) {
            f.write(buf, n);
            received += n;
            t0 = millis(); // reset timeout
          }
        }
        delay(1);
      }
      f.close();
      if (received == fsize) {
        replyf("OK %lu bytes written to %s\n", received, fname);
        // Reinicializar OUI se for oui.dat
        if (strstr(fname, "oui.dat")) {
          ouiInit(false);
          replyf("OUI reinitialized: %lu entries\n", ouiGetCount());
        }
      } else {
        replyf("ERROR: received %lu/%lu bytes\n", received, fsize);
        LittleFS.remove(fname);
      }
    } else {
      reply("Uso: uploadoui <filename> <size>\n");
    }

  } else if (strcmp(cmd, "clearlog") == 0) {
    reply("Apagando arquivos de log...\n");
    if (LittleFS.exists(CSV_FILENAME)) {
      LittleFS.remove(CSV_FILENAME);
      replyf("  %s removido\n", CSV_FILENAME);
    }
    if (LittleFS.exists("/ap_debug.csv")) {
      LittleFS.remove("/ap_debug.csv");
      reply("  /ap_debug.csv removido\n");
    }
    if (LittleFS.exists("/policy_test.csv")) {
      LittleFS.remove("/policy_test.csv");
      reply("  /policy_test.csv removido\n");
    }
    // Recriar CSV principal com cabecalho
    File f = LittleFS.open(CSV_FILENAME, "w");
    if (f) {
      f.println("timestamp_s,mac_address,oui,type,result,phase,reason_code,reason_name,ip,ping_ms,dns_ok,http_ok");
      f.close();
      reply("  Novo arquivo de log criado.\n");
    }
    // Resetar tambem as estatisticas
    cmdReset();
    reply("Logs apagados e estatisticas resetadas.\n");

  } else if (strncmp(cmd, "ssid ", 5) == 0) {
    const char *val = cmd + 5;
    while (*val == ' ') val++;
    if (*val == '\0') {
      replyf("SSID: %s\n", config_ssid);
    } else {
      snprintf(config_ssid, sizeof(config_ssid), "%s", val);
      replyf("SSID alterado para: %s\n", config_ssid);
      saveConfig();
    }

  } else if (strncmp(cmd, "pass ", 5) == 0) {
    const char *val = cmd + 5;
    while (*val == ' ') val++;
    if (*val == '\0') {
      reply("Senha: ********\n");
    } else {
      snprintf(config_pass, sizeof(config_pass), "%s", val);
      reply("Senha alterada.\n");
      saveConfig();
    }

  } else if (strncmp(cmd, "target ", 7) == 0) {
    const char *val = cmd + 7;
    while (*val == ' ') val++;
    uint8_t mac[6];
    if (parseMAC(val, mac)) {
      memcpy(config_mac_alvo, mac, 6);
      replyf("MAC alvo alterado para: %s\n", macToString(config_mac_alvo).c_str());
      saveConfig();
    } else {
      replyf("Formato invalido. Use: target AA:BB:CC:DD:EE:FF\n");
    }

  } else if (strcmp(cmd, "log on") == 0) {
    log_enabled = true;
    reply("Logs ativados.\n");

  } else if (strcmp(cmd, "log off") == 0) {
    log_enabled = false;
    reply("Logs desativados.\n");

  } else if (strncmp(cmd, "oui", 3) == 0) {
    const char *val = cmd + 3;
    while (*val == ' ') val++;
    if (*val == '\0' || strcmp(val, "list") == 0) {
      replyf("\n--- Perfis OUI (%d disponiveis) ---\n", OUI_COUNT);
      for (int i = 0; i < OUI_COUNT; i++) {
        char prefix[18];
        snprintf(prefix, sizeof(prefix), "%02X:%02X:%02X",
                 perfis_oui[i].prefixo[0], perfis_oui[i].prefixo[1], perfis_oui[i].prefixo[2]);
        replyf("  %-12s  %s%s\n",
               perfis_oui[i].nome, prefix,
               (i == oui_ativo && !oui_rotativo) ? "  [ATIVO]" : "");
      }
      replyf("\nModo: %s\n", oui_rotativo ? "ROTATIVO (todos)" : "FIXO");
      replyf("Perfil ativo: %s\n", perfis_oui[oui_ativo].nome);
      reply("Use 'oui <nome>' para selecionar ou 'oui all' para modo rotativo.\n");
    } else if (strcmp(val, "all") == 0) {
      oui_rotativo = true;
      oui_ciclo_atual = 0;
      reply("Modo OUI rotativo ativado. Cada ciclo usara um perfil diferente.\n");
    } else if (strcmp(val, "info") == 0) {
      replyf("Base OUI IEEE: %s\n", ouiIsReady() ? "CARREGADA" : "NAO DISPONIVEL");
      replyf("Entradas: %lu\n", ouiGetCount());
      replyf("Cache RAM: %s\n", ouiRAM ? "ATIVA (115 KB)" : "desativada");
      reply("Arquivos no LittleFS:\n");
      replyf("  /oui.dat:       %s\n", LittleFS.exists("/oui.dat") ? "SIM" : "NAO");
      replyf("  /ouinames.dat:  %s\n", LittleFS.exists("/ouinames.dat") ? "SIM" : "NAO");
      // Teste rapido
      if (ouiIsReady()) {
        uint8_t testMAC[6] = {0x00, 0x1E, 0xC2, 0x00, 0x00, 0x01};
        String name = ouiLookupName(testMAC);
        replyf("Teste 00:1E:C2 -> %s\n", name.c_str());
      }
    } else if (strcmp(val, "ram on") == 0) {
      if (!ouiIsReady()) {
        reply("Base OUI nao carregada. Verifique /oui.dat no LittleFS.\n");
      } else if (ouiRAM) {
        reply("Cache RAM ja esta ativa.\n");
      } else {
        reply("Carregando 115 KB na RAM...\n");
        ouiInit(true);
        replyf("Cache RAM: %s (%lu entradas)\n", ouiRAM ? "OK" : "FALHA", ouiGetCount());
      }
    } else if (strcmp(val, "ram off") == 0) {
      if (ouiRAM) {
        free(ouiRAM);
        ouiRAM = nullptr;
        reply("Cache RAM liberada. Usando busca em arquivo.\n");
      } else {
        reply("Cache RAM ja esta desativada.\n");
      }
    } else {
      // Buscar perfil por nome
      bool found = false;
      for (int i = 0; i < OUI_COUNT; i++) {
        if (strcasecmp(val, perfis_oui[i].nome) == 0) {
          oui_ativo = i;
          oui_rotativo = false;
          char prefix[18];
          snprintf(prefix, sizeof(prefix), "%02X:%02X:%02X",
                   perfis_oui[i].prefixo[0], perfis_oui[i].prefixo[1], perfis_oui[i].prefixo[2]);
          replyf("Perfil OUI alterado para: %s (%s)\n", perfis_oui[i].nome, prefix);
          found = true;
          break;
        }
      }
      if (!found) {
        replyf("Perfil '%s' nao encontrado. Use 'oui list' para ver os disponiveis.\n", val);
      }
    }

  } else if (strcmp(cmd, "stations") == 0) {
    cmdStations();

  } else if (cmd[0] == 'a' && cmd[1] == 'p' && (cmd[2] == ' ' || cmd[2] == '\0')) {
    const char *val = cmd + 2;
    while (*val == ' ') val++;
    if (strcmp(val, "on") == 0) {
      startAPMode();
    } else if (strcmp(val, "off") == 0) {
      stopAPMode();
    } else if (strcmp(val, "status") == 0 || *val == '\0') {
      if (ap_mode_active) {
        replyf("AP ativo - SSID: %s, IP: %s/%s, Stations: %d\n",
               config_ap_ssid, WiFi.softAPIP().toString().c_str(),
               ap_config_subnet.toString().c_str(),
               WiFi.softAPgetStationNum());
      } else {
        replyf("AP inativo. Use 'ap on' para ativar.\n");
        replyf("Config: SSID=%s, IP=%s/%s GW=%s\n",
               config_ap_ssid,
               ap_config_ip.toString().c_str(),
               ap_config_subnet.toString().c_str(),
               ap_config_gateway.toString().c_str());
        if (ap_config_mac_set) {
          replyf("MAC custom: %s\n", macToString(ap_config_mac).c_str());
        } else {
          reply("MAC: padrao do hardware\n");
        }
      }
    } else if (strncmp(val, "ip", 2) == 0) {
      const char *ipstr = val + 2;
      while (*ipstr == ' ') ipstr++;
      if (*ipstr == '\0' || strcmp(ipstr, "show") == 0) {
        replyf("IP AP: %s/%s GW: %s\n",
               ap_config_ip.toString().c_str(),
               ap_config_subnet.toString().c_str(),
               ap_config_gateway.toString().c_str());
      } else if (strcmp(ipstr, "default") == 0) {
        ap_config_ip = IPAddress(192, 168, 4, 1);
        ap_config_gateway = IPAddress(192, 168, 4, 1);
        ap_config_subnet = IPAddress(255, 255, 255, 0);
        reply("IP do AP restaurado para 192.168.4.1/24\n");
        saveConfig();
      } else {
        IPAddress ip, gw, mask;
        char buf[3][16];
        int parsed = sscanf(ipstr, "%15s %15s %15s", buf[0], buf[1], buf[2]);
        if (parsed >= 1 && parseIP(buf[0], ip)) {
          ap_config_ip = ip;
          if (parsed >= 3 && parseIP(buf[2], mask)) {
            ap_config_gateway = ip;
            ap_config_subnet = mask;
            if (parsed >= 2 && parseIP(buf[1], gw)) {
              ap_config_gateway = gw;
            }
          } else if (parsed >= 2 && parseIP(buf[1], mask)) {
            ap_config_gateway = ip;
            ap_config_subnet = mask;
          } else {
            ap_config_gateway = ip;
            ap_config_subnet = IPAddress(255, 255, 255, 0);
          }
          replyf("IP do AP configurado: %s/%s GW: %s\n",
                 ap_config_ip.toString().c_str(),
                 ap_config_subnet.toString().c_str(),
                 ap_config_gateway.toString().c_str());
          saveConfig();
        } else {
          reply("Formato invalido. Uso: ap ip <endereco> [gateway] [mask]\n");
        }
      }

    } else if (strncmp(val, "mac", 3) == 0) {
      const char *macstr = val + 3;
      while (*macstr == ' ') macstr++;
      if (*macstr == '\0' || strcmp(macstr, "show") == 0) {
        if (ap_config_mac_set) {
          replyf("MAC custom do AP: %s\n", macToString(ap_config_mac).c_str());
        } else {
          reply("MAC do AP: padrao do hardware\n");
        }
      } else if (strcmp(macstr, "default") == 0) {
        memset(ap_config_mac, 0, 6);
        ap_config_mac_set = false;
        reply("MAC do AP restaurado para o padrao do hardware.\n");
        saveConfig();
      } else {
        uint8_t mac[6];
        if (parseMAC(macstr, mac)) {
          memcpy(ap_config_mac, mac, 6);
          ap_config_mac_set = true;
          replyf("MAC do AP configurado para: %s\n", macToString(ap_config_mac).c_str());
          if (ap_mode_active) {
            reply("NOTA: o MAC sera aplicado na proxima vez que o AP for iniciado (ap off; ap on).\n");
          }
          saveConfig();
        } else {
          reply("Formato invalido. Uso: ap mac XX:XX:XX:XX:XX:XX\n");
        }
      }

    } else if (strncmp(val, "ssid", 4) == 0) {
      const char *ssidstr = val + 4;
      while (*ssidstr == ' ') ssidstr++;
      if (*ssidstr == '\0' || strcmp(ssidstr, "show") == 0) {
        replyf("SSID do AP: %s\n", config_ap_ssid);
      } else {
        snprintf(config_ap_ssid, sizeof(config_ap_ssid), "%s", ssidstr);
        replyf("SSID do AP alterado para: %s\n", config_ap_ssid);
        if (ap_mode_active) {
          reply("NOTA: o novo SSID sera usado na proxima vez que o AP for reiniciado (ap off; ap on).\n");
        }
        saveConfig();
      }

    } else if (strncmp(val, "pass", 4) == 0) {
      const char *passstr = val + 4;
      while (*passstr == ' ') passstr++;
      if (*passstr == '\0' || strcmp(passstr, "show") == 0) {
        reply("Senha do AP: ********\n");
      } else {
        snprintf(config_ap_pass, sizeof(config_ap_pass), "%s", passstr);
        replyf("Senha do AP alterada (%d caracteres).\n", strlen(config_ap_pass));
        if (ap_mode_active) {
          reply("NOTA: a nova senha sera usada na proxima vez que o AP for reiniciado (ap off; ap on).\n");
        }
        saveConfig();
      }

    } else {
      reply("Uso: ap on | ap off | ap status | ap ip | ap mac | ap ssid | ap pass\n");
    }

  } else if (strcmp(cmd, "captive on") == 0) {
    captive_enabled = true;
    if (ap_mode_active) {
      dnsServer.start(53, "*", WiFi.softAPIP());
    }
    reply("Captive portal ATIVADO. DNS redireciona para o dashboard.\n");
  } else if (strcmp(cmd, "captive off") == 0) {
    captive_enabled = false;
    if (ap_mode_active) {
      dnsServer.stop();
    }
    reply("Captive portal DESATIVADO. Apenas acesso direto ao IP.\n");
  } else if (strcmp(cmd, "tcp on") == 0) {
    if (WiFi.status() != WL_CONNECTED && !ap_mode_active) {
      reply("WiFi nao conectado. Conecte primeiro ou use 'ap on'.\n");
    } else {
      iniciarTCPServer();
      saveConfig();
    }
  } else if (strcmp(cmd, "tcp off") == 0) {
    pararTCPServer();
    saveConfig();
  } else if (strcmp(cmd, "tcp status") == 0) {
    if (tcp_server_active) {
      replyf("TCP: ativo na porta 2323 (%d clientes)\n",
             (tcpClients[0] ? 1 : 0) + (tcpClients[1] ? 1 : 0) + (tcpClients[2] ? 1 : 0));
    } else {
      reply("TCP: inativo. Use 'tcp on' para ativar.\n");
    }
  } else if (strcmp(cmd, "mqtt off") == 0) {
    mqtt_broker_ip = IPAddress(0, 0, 0, 0);
    if (mqttClient.connected()) mqttClient.stop();
    mqtt_connected = false;
    reply("MQTT desativado.\n");
    saveConfig();
  } else if (strncmp(cmd, "mqtt ", 5) == 0) {
    const char *val = cmd + 5;
    while (*val == ' ') val++;
    if (*val == '\0') {
      if (mqtt_broker_ip.toString() != "0.0.0.0") {
        replyf("MQTT broker: %s:%d %s\n",
               mqtt_broker_ip.toString().c_str(), mqtt_broker_port,
               mqtt_connected ? "[CONECTADO]" : "[desconectado]");
      } else {
        reply("MQTT: desativado. Use 'mqtt <ip> [porta]' para configurar.\n");
      }
    } else {
      char ipStr[16]; int port = 1883;
      int parsed = sscanf(val, "%15s %d", ipStr, &port);
      if (parsed >= 1 && parseIP(ipStr, mqtt_broker_ip)) {
        if (parsed >= 2) mqtt_broker_port = port;
        replyf("MQTT configurado: broker %s:%d\n",
               mqtt_broker_ip.toString().c_str(), mqtt_broker_port);
        if (mqttClient.connected()) mqttClient.stop();
        mqtt_connected = false;
        saveConfig();
      } else {
        reply("Formato invalido. Use: mqtt <ip> [porta]\n");
      }
    }
  } else if (strncmp(cmd, "staticip ", 9) == 0) {
    // Teste manual com IP estatico: staticip <ip> <gw> <mask>
    const char *val = cmd + 9;
    while (*val == ' ') val++;
    IPAddress sip, sgw, smask;
    char b1[16], b2[16], b3[16];
    if (sscanf(val, "%15s %15s %15s", b1, b2, b3) >= 1) {
      if (parseIP(b1, sip)) {
        if (!parseIP(b2, sgw)) sgw = sip;
        if (!parseIP(b3, smask)) smask = IPAddress(255, 255, 255, 0);
        WiFi.config(sip, sgw, smask);
        replyf("IP estatico configurado: %s/%s GW:%s\n",
               sip.toString().c_str(), smask.toString().c_str(), sgw.toString().c_str());
        reply("Testando ping...\n");
        int ret = WiFi.ping(sgw, 3);
        replyf("Ping para GW %s: %s\n", sgw.toString().c_str(),
               ret >= 0 ? (String(ret) + " ms").c_str() : "TIMEOUT");
      } else {
        reply("IP invalido. Uso: staticip <ip> <gateway> <mask>\n");
      }
    }
  } else if (strcmp(cmd, "monitor on") == 0) {
    startMonitorMode();
  } else if (strcmp(cmd, "monitor off") == 0) {
    stopMonitorMode();
  } else if (strcmp(cmd, "monitor status") == 0) {
    cmdMonitorStatus();
  } else if (strcmp(cmd, "blacklist-policy") == 0) {
    iniciarBlacklistPolicyTest();
  } else if (strcmp(cmd, "blacklist-policy stop") == 0) {
    pararBlacklistPolicyTest();
  } else if (strcmp(cmd, "ratelimit-test") == 0) {
    iniciarRatelimitTest();
  } else if (strcmp(cmd, "debug") == 0) {
    startAPDebugMode();

  } else if (strcmp(cmd, "debugdump") == 0) {
    cmdDebugDump();

  } else {
    replyf("Comando desconhecido: %s\n", cmd);
    reply("Digite 'help' para comandos disponiveis.\n");
  }
}

// =============================================
// SUMÁRIO DO TESTE
// =============================================
void cmdSummary() {
  unsigned long seg = (millis() - startMillis) / 1000;
  unsigned long h = seg / 3600;
  unsigned long m = (seg % 3600) / 60;
  unsigned long s = seg % 60;

  replyf("\n=== RESUMO DO TESTE (%luh %lum %lus) ===\n", h, m, s);
  replyf("Ciclos completados: %lu\n", stats.total_cycles);
  replyf("\nMACs aleatorios:\n");
  replyf("  Conectados: %lu\n", stats.random_connected);
  replyf("  Bloqueados: %lu\n", stats.random_blocked);
  replyf("  Erros:      %lu\n", stats.random_errors);
  replyf("\nMAC alvo (%s):\n", macToString(config_mac_alvo).c_str());
  replyf("  Conectados: %lu\n", stats.exact_connected);
  replyf("  Bloqueados: %lu\n", stats.exact_blocked);
  replyf("  Erros:      %lu\n", stats.exact_errors);

  // Lista de MACs bloqueados
  replyf("\nMACs bloqueados detectados (%d):\n", blocked_count);
  if (blocked_count == 0) {
    reply("  (nenhum)\n");
  } else {
    // Primeiro os confirmados, depois os pendentes
    int confirmados = 0, pendentes = 0;
    for (int i = 0; i < blocked_count; i++) {
      if (blocked_macs[i].confirmado) confirmados++;
      else pendentes++;
    }
    if (confirmados > 0) {
      replyf("  CONFIRMADOS (%d):\n", confirmados);
      for (int i = 0; i < blocked_count; i++) {
        if (blocked_macs[i].confirmado) {
          replyf("    %s (%s, %d testes)\n",
                 macToString(blocked_macs[i].mac).c_str(),
                 blocked_macs[i].tipo,
                 blocked_macs[i].test_count);
        }
      }
    }
    if (pendentes > 0) {
      replyf("  PENDENTES (%d):\n", pendentes);
      for (int i = 0; i < blocked_count; i++) {
        if (!blocked_macs[i].confirmado) {
          int r = buscarRetest(blocked_macs[i].mac);
          int faltam = (r >= 0) ? retest_queue[r].ciclos_restantes : 0;
          replyf("    %s (%s, %d/%d testes, aguardando %d ciclos)\n",
                 macToString(blocked_macs[i].mac).c_str(),
                 blocked_macs[i].tipo,
                 blocked_macs[i].test_count,
                 CONFIRMAR_CICLOS + 1,
                 faltam);
        }
      }
    }
  }

  // Distribuicao de fases de falha
  unsigned long totalFalhas = phaseStats.phase_scan_fail + phaseStats.phase_auth_fail +
      phaseStats.phase_assoc_fail + phaseStats.phase_handshake_fail +
      phaseStats.phase_dhcp_fail + phaseStats.phase_mac_set_fail;
  if (totalFalhas > 0) {
    replyf("\n--- Fases de falha (%lu total) ---\n", totalFalhas);
    replyf("  SCAN_FAIL:       %lu (%.1f%%)\n",
           phaseStats.phase_scan_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_scan_fail / totalFalhas : 0);
    replyf("  AUTH_FAIL:       %lu (%.1f%%)\n",
           phaseStats.phase_auth_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_auth_fail / totalFalhas : 0);
    replyf("  ASSOC_FAIL:      %lu (%.1f%%)\n",
           phaseStats.phase_assoc_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_assoc_fail / totalFalhas : 0);
    replyf("  HANDSHAKE_FAIL:  %lu (%.1f%%)\n",
           phaseStats.phase_handshake_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_handshake_fail / totalFalhas : 0);
    replyf("  DHCP_FAIL:       %lu (%.1f%%)\n",
           phaseStats.phase_dhcp_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_dhcp_fail / totalFalhas : 0);
    replyf("  MAC_SET_FAIL:    %lu (%.1f%%)\n",
           phaseStats.phase_mac_set_fail,
           totalFalhas > 0 ? 100.0 * phaseStats.phase_mac_set_fail / totalFalhas : 0);
    replyf("  CONNECTED:       %lu\n", phaseStats.phase_connected);
  }

  // Testes de conectividade pos-conexao
  if (connStats.total_tested > 0) {
    replyf("\n--- Conectividade pos-conexao (%lu testados) ---\n", connStats.total_tested);
    replyf("  Ping (gateway):  %lu OK / %lu falha\n", connStats.ping_ok, connStats.ping_fail);
    replyf("  DNS (resolucao): %lu OK / %lu falha\n", connStats.dns_ok, connStats.dns_fail);
    replyf("  HTTP (GET):      %lu OK / %lu falha\n", connStats.http_ok, connStats.http_fail);
  }

  // Estatisticas por OUI
  if (oui_stats_count > 0) {
    replyf("\n--- Estatisticas por OUI ---\n");
    replyf("%-12s  %8s  %8s  %7s\n", "Perfil", "Testados", "Bloqueados", "Taxa");
    reply("------------------------------------------\n");
    for (int i = 0; i < oui_stats_count; i++) {
      float taxa = oui_stats[i].testados > 0 ?
        100.0 * oui_stats[i].bloqueados / oui_stats[i].testados : 0.0;
      replyf("%-12s  %8lu  %8lu  %6.1f%%\n",
             oui_stats[i].nome, oui_stats[i].testados,
             oui_stats[i].bloqueados, taxa);
    }
  }

  // Testes de politica ativos
  if (timeout_test_active || ratelimit_test_active) {
    reply("\n--- Testes de politica ativos ---\n");
    if (timeout_test_active) {
      replyf("  Blacklist-policy: em andamento (%d MACs)\n", timeout_test_count);
    }
    if (ratelimit_test_active) {
      replyf("  Ratelimit-test: fase %d (%lu/%lu)\n",
             ratelimit_phase + 1,
             ratelimit_fast_total + ratelimit_slow_total,
             (unsigned long)(RATELIMIT_MACS_PER_PHASE * 2));
    }
  }

  replyf("\nConfiguracao atual:\n");
  replyf("  SSID:   %s\n", config_ssid);
  replyf("  Senha:  ********\n");
  replyf("  Alvo:   %s\n", macToString(config_mac_alvo).c_str());
  replyf("  Log:    %s\n", log_enabled ? "ligado" : "desligado");
  reply("================================\n\n");
}

// =============================================
// COMANDO: DUMP — exporta o CSV gravado
// =============================================
void cmdDump() {
  File f = LittleFS.open(CSV_FILENAME, "r");
  if (!f) {
    reply("Erro: nao foi possivel abrir o CSV.\n");
    return;
  }
  reply("--- CONTEUDO DO CSV ---\n");
  char buf[128];
  while (f.available()) {
    int n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[n] = '\0';
    reply(buf);
    reply("\n");
  }
  f.close();
  reply("--- FIM DO CSV ---\n");
}

// =============================================
// COMANDO: DEBUGDUMP — exibe dados do AP Debug
// =============================================
void cmdDebugDump() {
  File f = LittleFS.open("/ap_debug.csv", "r");
  if (!f) {
    reply("Nenhum dado de AP Debug encontrado.\n");
    return;
  }
  reply("--- DADOS DO AP DEBUG ---\n");
  char buf[256];
  while (f.available()) {
    int n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[n] = '\0';
    reply(buf);
    reply("\n");
  }
  f.close();
  reply("--- FIM ---\n");
}

// =============================================
// COMANDO: RESET — reseta estatísticas
// =============================================
void cmdReset() {
  memset(&stats, 0, sizeof(stats));
  memset(&phaseStats, 0, sizeof(phaseStats));
  memset(oui_stats, 0, sizeof(oui_stats));
  oui_stats_count = 0;
  memset(&connStats, 0, sizeof(connStats));
  blocked_count = 0;
  retest_count = 0;
  startMillis = millis();
  reply("Estatisticas e contadores resetados.\n");
}

// =============================================
// AP DEBUG MODE — DEVICE FINGERPRINT CAPTURE
// =============================================
#define MAX_DEBUG_CLIENTS 10

struct DebugClientInfo {
  String mac;
  String ip;
  String hostname;
  String clientId;
  String vendorClass;
  String userAgent;
  int rssi;
  unsigned long timestamp;
  bool dhcpDone;
  bool httpDone;
};

static DebugClientInfo s_dbgClients[MAX_DEBUG_CLIENTS];
static int s_dbgCount = 0;
static IPAddress s_dbgIPs[MAX_DEBUG_CLIENTS];

static void saveDebugCSV(int idx) {
  File f = LittleFS.open("/ap_debug.csv", "a");
  if (!f) { reply("Erro ao salvar debug CSV\n"); return; }
  f.printf("%lu,%s,%s,%s,%s,%s,%s,%d\n",
    s_dbgClients[idx].timestamp,
    s_dbgClients[idx].mac.c_str(),
    s_dbgClients[idx].ip.length() ? s_dbgClients[idx].ip.c_str() : "N/A",
    s_dbgClients[idx].hostname.length() ? s_dbgClients[idx].hostname.c_str() : "N/A",
    s_dbgClients[idx].clientId.length() ? s_dbgClients[idx].clientId.c_str() : "N/A",
    s_dbgClients[idx].vendorClass.length() ? s_dbgClients[idx].vendorClass.c_str() : "N/A",
    s_dbgClients[idx].userAgent.length() ? s_dbgClients[idx].userAgent.c_str() : "N/A",
    s_dbgClients[idx].rssi);
  f.close();
  replyf("[CSV] Dados salvos: %s\n", s_dbgClients[idx].mac.c_str());
}

static void processDebugDNS(WiFiUDP &udp) {
  int sz = udp.parsePacket();
  if (sz <= 0) return;
  uint8_t buf[512];
  udp.read(buf, sz);
  uint8_t resp[512];
  resp[0] = buf[0]; resp[1] = buf[1];
  resp[2] = 0x81; resp[3] = 0x80;
  resp[4] = 0; resp[5] = 1;
  resp[6] = 0; resp[7] = 1;
  memset(resp + 8, 0, 4);
  memcpy(resp + 12, buf + 12, sz - 12);
  int qEnd = sz;
  resp[qEnd] = 0xC0; resp[qEnd+1] = 0x0C;
  resp[qEnd+2] = 0; resp[qEnd+3] = 1;
  resp[qEnd+4] = 0; resp[qEnd+5] = 1;
  resp[qEnd+6] = 0; resp[qEnd+7] = 0; resp[qEnd+8] = 0; resp[qEnd+9] = 0x3C;
  resp[qEnd+10] = 0; resp[qEnd+11] = 4;
  resp[qEnd+12] = 192; resp[qEnd+13] = 168; resp[qEnd+14] = 4; resp[qEnd+15] = 1;
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.write(resp, qEnd + 16);
  udp.endPacket();
}

static void processDebugHTTP(WiFiClient client) {
  if (!client) return;
  String req = "";
  unsigned long start = millis();
  while (client.connected() && millis() - start < 3000) {
    if (client.available()) {
      char c = client.read();
      req += c;
      if (req.endsWith("\r\n\r\n")) break;
    }
  }
  String ua = "";
  int idx = req.indexOf("User-Agent: ");
  if (idx >= 0) {
    int end = req.indexOf("\r\n", idx);
    if (end > idx) ua = req.substring(idx + 12, end);
  }
  IPAddress rip = client.remoteIP();
  for (int i = 0; i < s_dbgCount; i++) {
    if (s_dbgIPs[i] == rip) {
      s_dbgClients[i].userAgent = ua;
      if (!s_dbgClients[i].httpDone) {
        s_dbgClients[i].httpDone = true;
        replyf("[HTTP] %s: User-Agent='%s'\n", s_dbgClients[i].mac.c_str(), ua.c_str());
      }
      break;
    }
  }
  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print("<html><body><h1>Debug AP</h1><p>Dados capturados.</p></body></html>");
  client.stop();
}

void startAPDebugMode() {
  stopWebDashboard();
  if (ap_mode_active) stopAPMode();

  reply("\n=== MODO AP DEBUG ===\n");
  reply("Cria um AP para capturar impressoes digitais de dispositivos.\n");
  reply("SSID: DebugNet | Senha: 12345678\n\n");

  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_AP);
  delay(500);  // Aguarda rádio estabilizar

  IPAddress apIP(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apIP, subnet);
  delay(200);
  bool apOk = WiFi.softAP("DebugNet", "12345678", 1, 0);  // channel 1
  delay(500);

  if (!apOk) {
    reply("ERRO: Falha ao criar AP DebugNet!\n");
    reply("Tente reiniciar o Pico fisicamente.\n");
    return;
  }

  reply("AP criado. IP: 192.168.4.1 | Channel: 1\n");
  reply("Conecte o dispositivo alvo.\n");
  reply("Digite 'q' para sair.\n\n");

  WiFiUDP dhcpUdp;
  dhcpUdp.begin(67);
  WiFiUDP dnsUdp;
  dnsUdp.begin(53);
  WiFiServer httpServer(80);
  httpServer.begin();
  httpServer.setNoDelay(true);

  if (!LittleFS.exists("/ap_debug.csv")) {
    File f = LittleFS.open("/ap_debug.csv", "w");
    if (f) {
      f.println("timestamp,mac,ip,hostname,client_id,vendor_class,user_agent,rssi");
      f.close();
    }
  }

  s_dbgCount = 0;
  unsigned long lastPoll = 0;
  uint8_t knownMACs[MAX_DEBUG_CLIENTS][6]; memset(knownMACs, 0, sizeof(knownMACs));
  int knownCount = 0;

  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'q' || c == 'Q') {
        while (Serial.available()) Serial.read();
        break;
      }
    }
    if (SerialBT.available()) {
      char c = SerialBT.read();
      if (c == 'q' || c == 'Q') break;
    }

    // Poll for new stations via ARP table
    unsigned long now = millis();
    if (now - lastPoll >= 2000) {
      lastPoll = now;
      struct netif *apNetif = NULL;
      for (struct netif *n = netif_list; n != NULL; n = n->next) {
        if (n->name[0] == 'w' && n->name[1] == '1') { apNetif = n; break; }
      }
      // Fallback: use any non-loopback netif
      if (!apNetif) {
        for (struct netif *n = netif_list; n != NULL; n = n->next) {
          if (n->name[0] != 'l') apNetif = n;
        }
      }
      for (size_t ei = 0; ei < ARP_TABLE_SIZE; ei++) {
        ip4_addr_t *ipaddr = NULL;
        struct netif *netif = NULL;
        struct eth_addr *eth_ret = NULL;
        if (etharp_get_entry(ei, &ipaddr, &netif, &eth_ret)) {
          if (netif != apNetif) continue;
          if (!ipaddr || !eth_ret) continue;
          uint32_t ip = ip4_addr_get_u32(ipaddr);
          if (ip == 0) continue;
          uint8_t *mac = eth_ret->addr;
          // Skip broadcast/multicast
          if (mac[0] & 0x01) continue;
          // Check if known
          bool found = false;
          for (int j = 0; j < knownCount; j++) {
            if (memcmp(knownMACs[j], mac, 6) == 0) { found = true; break; }
          }
          if (!found && s_dbgCount < MAX_DEBUG_CLIENTS) {
            memcpy(knownMACs[knownCount++], mac, 6);
            s_dbgClients[s_dbgCount].mac = macToString(mac);
            s_dbgClients[s_dbgCount].timestamp = now;
            s_dbgClients[s_dbgCount].ip = IPAddress(ip).toString();
            s_dbgIPs[s_dbgCount] = IPAddress(ip);
            s_dbgClients[s_dbgCount].dhcpDone = true;
            s_dbgClients[s_dbgCount].httpDone = false;
            s_dbgClients[s_dbgCount].rssi = WiFi.RSSI(0);
            s_dbgCount++;
            replyf("[ARP] Dispositivo detectado: %s / %s\n",
                   macToString(mac).c_str(), IPAddress(ip).toString().c_str());
          }
        }
      }
    }

    // Passive DHCP capture (listen on port 67)
    {
      int sz = dhcpUdp.parsePacket();
      if (sz > 0) {
        uint8_t pkt[548];
        dhcpUdp.read(pkt, sz);
        if (pkt[0] == 1) { // BOOTREQUEST
          uint8_t cmac[6];
          memcpy(cmac, pkt + 28, 6);
          String macStr = macToString(cmac);
          for (int ci = 0; ci < s_dbgCount; ci++) {
            if (s_dbgClients[ci].mac.equalsIgnoreCase(macStr)) {
              if (pkt[236] == 0x63 && pkt[237] == 0x82 && pkt[238] == 0x53 && pkt[239] == 0x63) {
                int pos = 240;
                while (pos < sz) {
                  uint8_t code = pkt[pos];
                  if (code == 255) break;
                  if (code == 0) { pos++; continue; }
                  if (pos + 1 >= sz) break;
                  uint8_t len = pkt[pos + 1];
                  if (pos + 2 + len > sz) break;
                  uint8_t *data = pkt + pos + 2;
                  if (code == 12 && len > 0) {
                    s_dbgClients[ci].hostname = String((char*)data).substring(0, len);
                  } else if (code == 60 && len > 0) {
                    s_dbgClients[ci].vendorClass = String((char*)data).substring(0, len);
                  } else if (code == 61 && len > 0) {
                    s_dbgClients[ci].clientId = "";
                    for (int bi = 0; bi < len; bi++) {
                      if (data[bi] < 0x10) s_dbgClients[ci].clientId += "0";
                      s_dbgClients[ci].clientId += String(data[bi], HEX);
                    }
                    s_dbgClients[ci].clientId.toUpperCase();
                  } else if (code == 53 && len == 1) {
                    if (data[0] == 3) { // DHCPREQUEST
                      s_dbgClients[ci].ip = IPAddress(192, 168, 4, 100 + ci).toString();
                      s_dbgIPs[ci] = IPAddress(192, 168, 4, 100 + ci);
                      if (!s_dbgClients[ci].dhcpDone) {
                        s_dbgClients[ci].dhcpDone = true;
                        replyf("[DHCP] %s: hostname='%s' vendor='%s' clientId='%s' IP=%s\n",
                          macStr.c_str(),
                          s_dbgClients[ci].hostname.c_str(),
                          s_dbgClients[ci].vendorClass.c_str(),
                          s_dbgClients[ci].clientId.c_str(),
                          s_dbgClients[ci].ip.c_str());
                      }
                    }
                  }
                  pos += 2 + len;
                }
              }
              break;
            }
          }
        }
      }
    }

    // DNS
    processDebugDNS(dnsUdp);

    // HTTP
    WiFiClient hc = httpServer.accept();
    if (hc) processDebugHTTP(hc);

    // Check for completion (DHCP + HTTP) or timeout
    for (int i = 0; i < s_dbgCount; i++) {
      if (!s_dbgClients[i].dhcpDone) continue;
      if (!s_dbgClients[i].httpDone && now - s_dbgClients[i].timestamp > 15000) {
        s_dbgClients[i].httpDone = true;
      }
      if (s_dbgClients[i].dhcpDone && s_dbgClients[i].httpDone) {
        saveDebugCSV(i);
        for (int j = i; j < s_dbgCount - 1; j++) {
          s_dbgClients[j] = s_dbgClients[j + 1];
          s_dbgIPs[j] = s_dbgIPs[j + 1];
        }
        s_dbgCount--;
        i--;
      }
    }

    yield();
    delay(10);
  }

  dhcpUdp.stop();
  dnsUdp.stop();
  httpServer.stop();
  WiFi.softAPdisconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  delay(200);
  reply("Reconectando a rede STA...\n");
  WiFi.begin(config_ssid, config_pass);
  reply("Modo AP Debug encerrado.\n");
}

#include "WebDashboard.h"

// =============================================
// CORE 1: WEB DASHBOARD + DNS (dedicado)
// =============================================
void setup1() {
  // Core 1 nao inicializa hardware — usa o que core 0 configurou
}

void loop1() {
  if (webActive) {
    dnsServer.processNextRequest();
    webServer.handleClient();
    for (int i = 0; i < 5; i++) {
      dnsServer.processNextRequest();
      webServer.handleClient();
    }
  }
  delay(1);
}
