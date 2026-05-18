// =============================================
// ConfigManager.h — Persistencia e utilidades
// =============================================

#include <LittleFS.h>

// SavedConfig (definido aqui, usado pelo .ino e por saveConfig/loadConfig)
#define SAVED_CONFIG_VERSION 5

struct SavedConfig {
  uint32_t version;
  char ssid[64];
  char pass[64];
  uint8_t target_mac[6];
  uint8_t ap_mac[6];
  uint8_t ap_ip[4];
  uint8_t ap_gateway[4];
  uint8_t ap_subnet[4];
  char ap_ssid[64];
  uint8_t mqtt_broker[4];
  uint16_t mqtt_port;
  uint8_t tcp_enabled;
  char ap_pass[64];          // senha separada para o AP (v5+)
};

// Externs — definidos no .ino
extern char config_ssid[64];
extern char config_pass[64];
extern char config_ap_ssid[64];
extern char config_ap_pass[64];
extern uint8_t config_mac_alvo[6];
extern const char* CSV_FILENAME;
extern const char* CONFIG_FILENAME;
extern IPAddress ap_config_ip;
extern IPAddress ap_config_gateway;
extern IPAddress ap_config_subnet;
extern uint8_t ap_config_mac[6];
extern bool ap_config_mac_set;
extern IPAddress mqtt_broker_ip;
extern uint16_t mqtt_broker_port;
extern bool tcp_server_active;
extern bool log_enabled;

// Logger functions (defined later in .ino)
void reply(const char *s);
void replyf(const char *fmt, ...);
void logln(const char *s);
void logf(const char *fmt, ...);

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
  snprintf(cfg.ap_pass, sizeof(cfg.ap_pass), "%s", config_ap_pass);
  cfg.mqtt_broker[0] = mqtt_broker_ip[0];
  cfg.mqtt_broker[1] = mqtt_broker_ip[1];
  cfg.mqtt_broker[2] = mqtt_broker_ip[2];
  cfg.mqtt_broker[3] = mqtt_broker_ip[3];
  cfg.mqtt_port = mqtt_broker_port;
  cfg.tcp_enabled = tcp_server_active ? 1 : 0;

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
  ap_config_ip = IPAddress(192, 168, 4, 1);
  ap_config_gateway = IPAddress(192, 168, 4, 1);
  ap_config_subnet = IPAddress(255, 255, 255, 0);
  memset(ap_config_mac, 0, 6);
  ap_config_mac_set = false;
  snprintf(config_ap_ssid, sizeof(config_ap_ssid), "%s", "PicoTester");
  snprintf(config_ap_pass, sizeof(config_ap_pass), "%s", "12345678");

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

  if (n >= 4) {
    uint32_t ver;
    memcpy(&ver, raw, 4);

    if (ver == 5 && n >= (int)sizeof(SavedConfig)) {
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
      snprintf(config_ap_pass, sizeof(config_ap_pass), "%s", cfg->ap_pass);
      mqtt_broker_ip = IPAddress(cfg->mqtt_broker[0], cfg->mqtt_broker[1], cfg->mqtt_broker[2], cfg->mqtt_broker[3]);
      mqtt_broker_port = cfg->mqtt_port ? cfg->mqtt_port : 1883;
      replyf("Config v5 carregada: STA=%s, AP=%s, AP IP=%s, MQTT=%s:%d\n",
             config_ssid, config_ap_ssid, ap_config_ip.toString().c_str(),
             mqtt_broker_ip.toString().c_str(), mqtt_broker_port);
      return;
    }

    if (ver == 4 && n >= (int)sizeof(SavedConfig) - 64) {
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
      // v4 nao tem ap_pass — usar padrao
      snprintf(config_ap_pass, sizeof(config_ap_pass), "%s", "12345678");
      mqtt_broker_ip = IPAddress(cfg->mqtt_broker[0], cfg->mqtt_broker[1], cfg->mqtt_broker[2], cfg->mqtt_broker[3]);
      mqtt_broker_port = cfg->mqtt_port ? cfg->mqtt_port : 1883;
      replyf("Config v4 carregada: STA=%s, AP=%s, AP IP=%s, MQTT=%s:%d\n",
             config_ssid, config_ap_ssid, ap_config_ip.toString().c_str(),
             mqtt_broker_ip.toString().c_str(), mqtt_broker_port);
      return;
    }

    if (ver == 3 && n >= 192) {
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
      replyf("Config v2 carregada: SSID=%s, AP IP=%s\n",
             config_ssid, ap_config_ip.toString().c_str());
      return;
    }
  }

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
