#include <WiFi.h>
#include <LittleFS.h>
#include <pico/cyw43_arch.h>
#include <SerialBT.h>
#include "lwip/etharp.h"

// =============================================
// CONFIGURAÇÕES INICIAIS (editáveis via comando)
// =============================================
char config_ssid[64]    = "Your_Network_SSID";
char config_pass[64]    = "your_network_password";
char config_ap_ssid[64] = "PicoTester";
uint8_t config_mac_alvo[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

const unsigned long DURACAO_TESTE_SEG = 86400;
const unsigned long INTERVALO_ENTRE_TENTATIVAS_SEG = 30;
const char* CSV_FILENAME = "/relatorio.csv";
const char* CONFIG_FILENAME = "/config.dat";

// Configuração persistente salva no LittleFS
#define SAVED_CONFIG_VERSION 3

struct SavedConfig {
  uint32_t version;
  char ssid[64];
  char pass[64];
  uint8_t target_mac[6];
  uint8_t ap_mac[6];        // AP MAC (zeros = usar padrão do hardware)
  uint8_t ap_ip[4];          // AP IP / gateway / subnet
  uint8_t ap_gateway[4];
  uint8_t ap_subnet[4];
  char ap_ssid[64];          // SSID separado para o AP (v3+)
};

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
void logln(const char *s = "") {
  if (!log_enabled) return;
  Serial.println(s);
  if (SerialBT) SerialBT.println(s);
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

// =============================================
// PROTÓTIPOS
// =============================================
bool macIgual(const uint8_t *a, const uint8_t *b);
int buscarBlockedMAC(const uint8_t *mac);
void addBlockedMAC(const uint8_t *mac, const char *tipo, bool conectado);
int buscarRetest(const uint8_t *mac);
void addRetest(const uint8_t *mac);
void processarRetestes();

void setupLittleFS();
void loadConfig();
void saveConfig();
void appendCSV(unsigned long timestampSeg, const String &macStr,
               const String &tipo, bool conectado, const String &ip);
bool setStationMAC(const uint8_t* newMAC);
void testarMAC(const uint8_t* mac, const String &tipo);
String macToString(const uint8_t* mac);
void processCommands();
void executeCommand(char *cmd);
bool parseMAC(const char *str, uint8_t mac[6]);
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
  while (!Serial) delay(10);

  SerialBT.setName("PicoTester");
  SerialBT.begin();

  reply("\n--- Testador de Bloqueio de MAC (24h) ---\n");
  replyf("Rede: %s\n", config_ssid);
  replyf("MAC alvo: %s\n", macToString(config_mac_alvo).c_str());
  replyf("Duracao: %lu segundos\n", DURACAO_TESTE_SEG);
  replyf("Intervalo: %lu s\n", INTERVALO_ENTRE_TENTATIVAS_SEG);
  reply("Digite 'help' para comandos\n");
  reply("-----------------------------------------\n\n");

  setupLittleFS();
  loadConfig();

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

  // AP mode: maintain AP, web dashboard, and poll for connected stations
  if (ap_mode_active) {
    processCommands();
    handleWebDashboard();
    updateStationList();
    delay(100);
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
  for (int i = 0; i < numVariacoes; i++) {
    if ((millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) break;
    processCommands();
    if (ap_mode_active) return;

    uint8_t macAleatorio[6];
    macAleatorio[0] = 0xC8;
    macAleatorio[1] = 0xA6;
    macAleatorio[2] = 0xEF;
    macAleatorio[3] = random(0, 256);
    macAleatorio[4] = random(0, 256);
    macAleatorio[5] = random(0, 256);

    testarMAC(macAleatorio, "random");
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

      testarMAC(retest_queue[i].mac, "retest");
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
    testarMAC(config_mac_alvo, "exact");
    for (int w = 0; w < INTERVALO_ENTRE_TENTATIVAS_SEG; w++) {
      processCommands();
      delay(1000);
      if (ap_mode_active || (millis() - startMillis) / 1000 >= DURACAO_TESTE_SEG) return;
    }
  }
}

// =============================================
// TESTA UM ÚNICO ENDEREÇO MAC
// =============================================
void testarMAC(const uint8_t* mac, const String &tipo) {
  String macStr = macToString(mac);
  logf("Testando [%s] %s ... ", tipo.c_str(), macStr.c_str());

  digitalWrite(LED_BUILTIN, HIGH);
  delay(50);
  digitalWrite(LED_BUILTIN, LOW);

  WiFi.disconnect(true);
  delay(500);

  if (!setStationMAC(mac)) {
    logln("FALHA ao definir MAC");
    unsigned long nowSeg = (millis() - startMillis) / 1000;
    appendCSV(nowSeg, macStr, tipo, false, "ERRO_SET_MAC");
    if (tipo == "random") stats.random_errors++;
    else if (tipo == "exact") stats.exact_errors++;
    return;
  }

  WiFi.begin(config_ssid, config_pass);

  int timeout = 20;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    timeout--;
    processCommands();
    if (ap_mode_active) {
      WiFi.disconnect(true);
      return;
    }
  }

  bool conectado = (WiFi.status() == WL_CONNECTED);
  String ip = conectado ? WiFi.localIP().toString() : "";

  logf("%s (%s)\n", conectado ? "CONECTADO" : "BLOQUEADO", ip.c_str());

  // Atualiza estatísticas e fila de bloqueio
  if (tipo == "random") {
    if (conectado) stats.random_connected++;
    else stats.random_blocked++;
  } else if (tipo == "exact") {
    if (conectado) stats.exact_connected++;
    else stats.exact_blocked++;
  }

  // Gerencia lista de bloqueados e retestes
  addBlockedMAC(mac, tipo.c_str(), conectado);

  unsigned long nowSeg = (millis() - startMillis) / 1000;
  appendCSV(nowSeg, macStr, tipo, conectado, ip);

  WiFi.disconnect(true);
  delay(200);
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

  if (WiFi.softAP(config_ap_ssid, config_pass)) {
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
// LITTLEFS
// =============================================
void setupLittleFS() {
  if (!LittleFS.begin()) {
    reply("Erro ao montar LittleFS! Verifique particao.\n");
    while (1) { delay(1000); }
  }
  reply("LittleFS montado.\n");

  if (!LittleFS.exists(CSV_FILENAME)) {
    File f = LittleFS.open(CSV_FILENAME, "w");
    if (f) {
      f.println("timestamp_s,mac_address,type,result,ip");
      f.close();
      reply("Arquivo CSV criado com cabecalho.\n");
    } else {
      reply("Erro ao criar arquivo CSV!\n");
    }
  }
}

void appendCSV(unsigned long timestampSeg, const String &macStr,
               const String &tipo, bool conectado, const String &ip) {
  File f = LittleFS.open(CSV_FILENAME, "a");
  if (!f) {
    reply("ERRO: nao foi possivel abrir o CSV para escrita!\n");
    return;
  }
  f.printf("%lu,%s,%s,%s,%s\n",
           timestampSeg, macStr.c_str(), tipo.c_str(),
           conectado ? "connected" : "blocked", ip.c_str());
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
    reply("  ssid <nome>           Altera SSID da rede\n");
    reply("  pass <senha>          Altera senha da rede\n");
    reply("  target XX:XX:XX:XX:XX:XX  Altera MAC alvo\n");
    reply("  log on                Ativa logs detalhados\n");
    reply("  log off               Desativa logs (erros e summary aparecem)\n");
    reply("  ap on                 Ativa modo AP (Pico vira roteador)\n");
    reply("  ap off                Desativa modo AP e volta ao teste\n");
    reply("  ap status             Mostra status do AP\n");
    reply("  ap ip [endereco] [gw] [mask]  Configura IP do AP\n");
    reply("  ap ip default         Restaura IP padrao (192.168.4.1)\n");
    reply("  ap mac [XX:XX:XX:XX:XX:XX]    Configura MAC do AP\n");
    reply("  ap mac default        Restaura MAC padrao do hardware\n");
    reply("  ap ssid <nome>        Altera SSID do AP (separado do STA)\n");
    reply("  stations              Lista dispositivos conectados ao AP\n");
    reply("  debug                 Modo AP Debug (captura fingerprint de dispositivos)\n");
    reply("  debugdump             Exibe dados capturados no AP Debug\n");

  } else if (strcmp(cmd, "summary") == 0) {
    cmdSummary();

  } else if (strcmp(cmd, "dump") == 0) {
    cmdDump();

  } else if (strcmp(cmd, "reset") == 0) {
    cmdReset();

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

    } else {
      reply("Uso: ap on | ap off | ap status | ap ip | ap mac | ap ssid\n");
    }

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
  blocked_count = 0;
  retest_count = 0;
  startMillis = millis();
  reply("Estatisticas e contadores resetados.\n");
}

// =============================================
// PERSISTÊNCIA DE CONFIG NO LITTLEFS
// =============================================
void saveConfig() {
  SavedConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.version = SAVED_CONFIG_VERSION;
  snprintf(cfg.ssid, sizeof(cfg.ssid), "%s", config_ssid);
  snprintf(cfg.pass, sizeof(cfg.pass), "%s", config_pass);
  memcpy(cfg.target_mac, config_mac_alvo, 6);
  if (ap_config_mac_set) {
    memcpy(cfg.ap_mac, ap_config_mac, 6);
  }
  cfg.ap_ip[0] = ap_config_ip[0];
  cfg.ap_ip[1] = ap_config_ip[1];
  cfg.ap_ip[2] = ap_config_ip[2];
  cfg.ap_ip[3] = ap_config_ip[3];
  cfg.ap_gateway[0] = ap_config_gateway[0];
  cfg.ap_gateway[1] = ap_config_gateway[1];
  cfg.ap_gateway[2] = ap_config_gateway[2];
  cfg.ap_gateway[3] = ap_config_gateway[3];
  cfg.ap_subnet[0] = ap_config_subnet[0];
  cfg.ap_subnet[1] = ap_config_subnet[1];
  cfg.ap_subnet[2] = ap_config_subnet[2];
  cfg.ap_subnet[3] = ap_config_subnet[3];
  snprintf(cfg.ap_ssid, sizeof(cfg.ap_ssid), "%s", config_ap_ssid);

  File f = LittleFS.open(CONFIG_FILENAME, "w");
  if (!f) {
    reply("Erro ao salvar config!\n");
    return;
  }
  f.write((uint8_t*)&cfg, sizeof(cfg));
  f.close();
  logf("Configuracao salva.\n");
}

void loadConfig() {
  // Reset AP config to defaults
  ap_config_ip = IPAddress(192, 168, 4, 1);
  ap_config_gateway = IPAddress(192, 168, 4, 1);
  ap_config_subnet = IPAddress(255, 255, 255, 0);
  memset(ap_config_mac, 0, 6);
  ap_config_mac_set = false;
  snprintf(config_ap_ssid, sizeof(config_ap_ssid), "%s", "PicoTester");

  if (!LittleFS.exists(CONFIG_FILENAME)) {
    logln("Nenhuma config salva, usando padroes.");
    return;
  }
  File f = LittleFS.open(CONFIG_FILENAME, "r");
  if (!f) {
    reply("Erro ao ler config salva, usando padroes.\n");
    return;
  }

  uint8_t raw[sizeof(SavedConfig)];
  int n = f.read(raw, sizeof(raw));
  f.close();

  // Try to read version field (first 4 bytes)
  if (n >= 4) {
    uint32_t ver;
    memcpy(&ver, raw, 4);

    // v3: version==3, full struct
    if (ver == 3 && n >= (int)sizeof(SavedConfig)) {
      SavedConfig *cfg = (SavedConfig *)raw;
      snprintf(config_ssid, sizeof(config_ssid), "%s", cfg->ssid);
      snprintf(config_pass, sizeof(config_pass), "%s", cfg->pass);
      memcpy(config_mac_alvo, cfg->target_mac, 6);
      if (memcmp(cfg->ap_mac, "\0\0\0\0\0\0", 6) != 0) {
        memcpy(ap_config_mac, cfg->ap_mac, 6);
        ap_config_mac_set = true;
      }
      ap_config_ip = IPAddress(cfg->ap_ip[0], cfg->ap_ip[1], cfg->ap_ip[2], cfg->ap_ip[3]);
      ap_config_gateway = IPAddress(cfg->ap_gateway[0], cfg->ap_gateway[1], cfg->ap_gateway[2], cfg->ap_gateway[3]);
      ap_config_subnet = IPAddress(cfg->ap_subnet[0], cfg->ap_subnet[1], cfg->ap_subnet[2], cfg->ap_subnet[3]);
      snprintf(config_ap_ssid, sizeof(config_ap_ssid), "%s", cfg->ap_ssid);
      replyf("Config v3 carregada: STA=%s, AP=%s, AP IP=%s\n",
             config_ssid, config_ap_ssid, ap_config_ip.toString().c_str());
      return;
    }

    // v2: version==2, expect 156 bytes
    if (ver == 2 && n >= 156) {
      char *s = (char *)raw;
      memcpy(config_ssid, s + 4, 64); config_ssid[63] = '\0';
      memcpy(config_pass, s + 68, 64); config_pass[63] = '\0';
      memcpy(config_mac_alvo, s + 132, 6);
      if (memcmp(s + 138, "\0\0\0\0\0\0", 6) != 0) {
        memcpy(ap_config_mac, s + 138, 6);
        ap_config_mac_set = true;
      }
      ap_config_ip = IPAddress((uint8_t)(s[144]), (uint8_t)(s[145]), (uint8_t)(s[146]), (uint8_t)(s[147]));
      ap_config_gateway = IPAddress((uint8_t)(s[148]), (uint8_t)(s[149]), (uint8_t)(s[150]), (uint8_t)(s[151]));
      ap_config_subnet = IPAddress((uint8_t)(s[152]), (uint8_t)(s[153]), (uint8_t)(s[154]), (uint8_t)(s[155]));
      // ap_ssid stays at default "PicoTester"
      replyf("Config v2 carregada: SSID=%s, AP IP=%s\n",
             config_ssid, ap_config_ip.toString().c_str());
      return;
    }
  }

  // Try v1 format (no version field: ssid[64] + pass[64] + target_mac[6])
  if (n >= 64 + 64 + 6) {
    char old_ssid[64], old_pass[64];
    memcpy(old_ssid, raw, 64);
    memcpy(old_pass, raw + 64, 64);
    memcpy(config_mac_alvo, raw + 128, 6);
    old_ssid[63] = '\0';
    old_pass[63] = '\0';
    snprintf(config_ssid, sizeof(config_ssid), "%s", old_ssid);
    snprintf(config_pass, sizeof(config_pass), "%s", old_pass);

    replyf("Config v1 carregada: SSID=%s (AP IP=default)\n", config_ssid);
    return;
  }

  reply("Config corrompida, usando padroes.\n");
}

// =============================================
// CONVERSÃO DE STRING MAC PARA ARRAY
// =============================================
bool parseMAC(const char *str, uint8_t mac[6]) {
  int vals[6];
  if (sscanf(str, "%x:%x:%x:%x:%x:%x",
             &vals[0], &vals[1], &vals[2],
             &vals[3], &vals[4], &vals[5]) == 6) {
    for (int i = 0; i < 6; i++) {
      if (vals[i] < 0 || vals[i] > 255) return false;
      mac[i] = (uint8_t)vals[i];
    }
    return true;
  }
  return false;
}

// =============================================
// CONVERSÃO DE STRING IP PARA IPAddress
// =============================================
bool parseIP(const char *str, IPAddress &ip) {
  unsigned int a, b, c, d;
  if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
    if (a <= 255 && b <= 255 && c <= 255 && d <= 255) {
      ip = IPAddress(a, b, c, d);
      return true;
    }
  }
  return false;
}

// =============================================
// CONVERSÃO DE ARRAY MAC PARA STRING
// =============================================
String macToString(const uint8_t* mac) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
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
