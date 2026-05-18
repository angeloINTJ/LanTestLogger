// =============================================
// TCPMQTT.h — Servidor TCP + MQTT
// Incluir apos todos os globais e loggers definidos no .ino
// =============================================

// Forward declarations (definidas depois no .ino)
void executeCommand(char *cmd);

// =============================================
// SERVIDOR TCP
// =============================================
void iniciarTCPServer() {
  if (tcp_server_active) return;
  tcpServer.begin();
  tcp_server_active = true;
  reply("Servidor TCP iniciado na porta 2323.\n");
  replyf("Conecte via: telnet %s 2323\n", WiFi.localIP().toString().c_str());
}

void pararTCPServer() {
  if (!tcp_server_active) return;
  for (int i = 0; i < 3; i++) {
    if (tcpClients[i]) tcpClients[i].stop();
  }
  tcpServer.stop();
  tcp_server_active = false;
  reply("Servidor TCP parado.\n");
}

void processarTCPServer() {
  if (!tcp_server_active) return;

  WiFiClient newClient = tcpServer.accept();
  if (newClient) {
    bool accepted = false;
    for (int i = 0; i < 3; i++) {
      if (!tcpClients[i] || !tcpClients[i].connected()) {
        tcpClients[i] = newClient;
        tcpClients[i].print("--- PicoTester TCP ---\nDigite 'help' para comandos.\n> ");
        replyf("[TCP] Cliente conectado #%d\n", i + 1);
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      newClient.print("Too many clients.\n");
      newClient.stop();
    }
  }

  for (int i = 0; i < 3; i++) {
    if (!tcpClients[i] || !tcpClients[i].connected()) continue;
    while (tcpClients[i].available()) {
      char c = tcpClients[i].read();
      if (c == '\n' || c == '\r') {
        if (cmd_idx > 0) {
          cmd_buf[cmd_idx] = '\0';
          cmd_output_buf[0] = '\0';
          cmd_output_capture = true;
          executeCommand(cmd_buf);
          cmd_output_capture = false;
          tcpClients[i].print(cmd_output_buf);
          tcpClients[i].print("> ");
          cmd_idx = 0;
        }
      } else if (cmd_idx < CMD_BUF_SIZE - 1) {
        cmd_buf[cmd_idx++] = c;
      }
    }
  }
}

// =============================================
// MQTT MINIMAL (publish-only, QoS 0)
// =============================================
bool mqttConnect() {
  if (!mqtt_broker_ip || mqtt_broker_ip.toString() == "0.0.0.0") return false;
  if (mqttClient.connected()) return true;

  if (!mqttClient.connect(mqtt_broker_ip, mqtt_broker_port)) {
    return false;
  }

  uint8_t pkt[64];
  int pos = 0;
  pkt[pos++] = 0x10;
  uint8_t varHeader[] = {
    0x00, 0x04, 'M', 'Q', 'T', 'T',
    0x04,
    0x02,
    0x00, 0x3C,
  };
  const char *clientId = "PicoTester";
  uint8_t cidLen = strlen(clientId);

  int remaining = sizeof(varHeader) + 2 + cidLen;
  pkt[pos++] = remaining;
  memcpy(pkt + pos, varHeader, sizeof(varHeader)); pos += sizeof(varHeader);
  pkt[pos++] = 0x00;
  pkt[pos++] = cidLen;
  memcpy(pkt + pos, clientId, cidLen); pos += cidLen;

  mqttClient.write(pkt, pos);
  mqttClient.flush();

  unsigned long t0 = millis();
  while (mqttClient.available() < 4 && millis() - t0 < 5000) { delay(10); }
  if (mqttClient.available() >= 4) {
    uint8_t resp[4];
    mqttClient.read(resp, 4);
    if (resp[0] == 0x20 && resp[1] == 0x02 && resp[3] == 0x00) {
      replyf("[MQTT] Conectado ao broker %s:%d\n",
             mqtt_broker_ip.toString().c_str(), mqtt_broker_port);
      return true;
    }
  }
  mqttClient.stop();
  return false;
}

bool mqttPublish(const char *topic, const char *payload) {
  if (!mqttClient.connected()) return false;

  uint8_t pkt[256];
  int pos = 0;
  int topicLen = strlen(topic);
  int payloadLen = strlen(payload);

  pkt[pos++] = 0x30;
  int remaining = 2 + topicLen + payloadLen;
  pkt[pos++] = remaining;
  pkt[pos++] = 0x00;
  pkt[pos++] = topicLen;
  memcpy(pkt + pos, topic, topicLen); pos += topicLen;
  memcpy(pkt + pos, payload, payloadLen); pos += payloadLen;

  mqttClient.write(pkt, pos);
  mqttClient.flush();
  return true;
}

void processarMQTT() {
  if (!mqtt_broker_ip || mqtt_broker_ip.toString() == "0.0.0.0") return;

  if (!mqtt_connected || !mqttClient.connected()) {
    mqtt_connected = mqttConnect();
    if (!mqtt_connected) return;
  }

  unsigned long now = millis();
  if (now - lastMqttPublish >= 30000) {
    lastMqttPublish = now;

    char payload[256];
    snprintf(payload, sizeof(payload),
      "{\"uptime\":%lu,\"cycles\":%lu,\"blocked\":%d,\"connected\":%lu,\"auth_fail\":%lu,\"hs_fail\":%lu}",
      now / 1000UL, stats.total_cycles, blocked_count,
      stats.random_connected + stats.exact_connected,
      phaseStats.phase_auth_fail, phaseStats.phase_handshake_fail);
    mqttPublish("pico/status", payload);
  }
}
