// =============================================
// DASHBOARD WEB + CAPTIVE PORTAL
// =============================================

#include <WebServer.h>
#include <DNSServer.h>

// ---------- HTML PAGE (PROGMEM) ----------

static const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no">
<title>PicoTester</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:#0a0a14;color:#c8c8d4;font-size:14px;line-height:1.5;-webkit-font-smoothing:antialiased}
.container{max-width:480px;margin:0 auto;padding:14px}
header{text-align:center;padding:16px 0 12px}
header h1{font-size:22px;font-weight:700;background:linear-gradient(135deg,#6c5ce7,#a29bfe);-webkit-background-clip:text;-webkit-text-fill-color:transparent;background-clip:text}
.badge{display:inline-block;font-size:11px;padding:3px 10px;border-radius:10px;margin-top:4px;font-weight:600}
.badge.on{background:#1a3a2a;color:#4ade80}
.badge.off{background:#3a1a1a;color:#f87171}
.card{background:#14141f;border-radius:12px;padding:14px;margin-bottom:10px;border:1px solid #1e1e30}
.card h2{font-size:13px;font-weight:600;color:#8888aa;text-transform:uppercase;letter-spacing:.5px;margin-bottom:10px}
.row{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid #1a1a28;font-size:13px}
.row:last-child{border:none}
.row .lbl{color:#666}
.row .val{color:#e0e0f0;font-weight:500;text-align:right;word-break:break-all}
.stat-grid{display:grid;grid-template-columns:1fr 1fr 1fr;gap:6px;margin-bottom:6px}
.stat-box{background:#0f0f1c;border-radius:8px;padding:10px;text-align:center}
.stat-box .num{font-size:20px;font-weight:700;color:#a29bfe}
.stat-box .lbl{font-size:10px;color:#666;margin-top:2px}
table{width:100%;border-collapse:collapse;font-size:12px}
th{text-align:left;color:#666;font-weight:600;padding:5px 3px;border-bottom:1px solid #1e1e30;font-size:10px;text-transform:uppercase}
td{padding:5px 3px;border-bottom:1px solid #1a1a28}
.status-dot{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:4px}
.status-dot.on{background:#4ade80;box-shadow:0 0 6px #4ade8033}
.status-dot.off{background:#f87171;box-shadow:0 0 6px #f8717133}
label{display:block;font-size:12px;color:#888;margin:8px 0 3px}
input,select{width:100%;padding:9px 11px;border-radius:8px;border:1px solid #1e1e30;background:#0f0f1c;color:#e0e0f0;font-size:14px;outline:none;transition:border .2s}
input:focus{border-color:#6c5ce7}
.btn-group{display:flex;gap:6px;margin-top:8px;flex-wrap:wrap}
.btn{flex:1;padding:9px;border:none;border-radius:8px;font-size:13px;font-weight:600;cursor:pointer;transition:opacity .2s;text-align:center;text-decoration:none;display:inline-block}
.btn:active{opacity:.7}
.btn.primary{background:linear-gradient(135deg,#6c5ce7,#a29bfe);color:#fff}
.btn.danger{background:#3a1a1a;color:#f87171}
.btn.outline{background:transparent;border:1px solid #1e1e30;color:#888}
.btn.success{background:#1a3a2a;color:#4ade80}
.btn.info{background:#1a2a3a;color:#60a5fa}
.toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%);background:#1e1e30;color:#e0e0f0;padding:10px 20px;border-radius:10px;font-size:13px;z-index:100;opacity:0;transition:opacity .3s;pointer-events:none;max-width:90%}
.toast.show{opacity:1}
.hidden{display:none}
.tab-bar{display:flex;gap:4px;margin-bottom:10px;background:#0f0f1c;border-radius:10px;padding:3px}
.tab{flex:1;text-align:center;padding:7px;border-radius:8px;font-size:12px;font-weight:600;color:#666;cursor:pointer;transition:all .2s}
.tab.active{background:#1e1e30;color:#e0e0f0}
.mono{font-family:'SF Mono',Monaco,'Cascadia Code',monospace;font-size:11px;color:#888;word-break:break-all}
pre{background:#0a0a14;border-radius:6px;padding:8px;font-size:11px;overflow-x:auto;color:#a29bfe;max-height:200px}
</style>
</head>
<body>
<div class="container" id="app">
  <header>
    <h1>PicoTester</h1>
    <span class="badge" id="modeBadge">AP</span>
  </header>

  <!-- Tab Navigation -->
  <div class="tab-bar">
    <div class="tab active" data-tab="dashboard">Dashboard</div>
    <div class="tab" data-tab="stations">Dispositivos</div>
    <div class="tab" data-tab="config">Config</div>
    <div class="tab" data-tab="debug">AP Debug</div>
  </div>

  <!-- Tab: Dashboard -->
  <div id="tab-dashboard" class="tab-content">
    <div class="card">
      <h2>AP Status</h2>
      <div class="row"><span class="lbl">SSID</span><span class="val" id="dSsid">--</span></div>
      <div class="row"><span class="lbl">IP</span><span class="val" id="dIp">--</span></div>
      <div class="row"><span class="lbl">MAC</span><span class="val mono" id="dMac">--</span></div>
      <div class="row"><span class="lbl">Uptime</span><span class="val" id="dUptime">--</span></div>
    </div>
    <div class="card">
      <h2>Test Summary</h2>
      <div class="stat-grid" id="testStats">
        <div class="stat-box"><div class="num" id="sConnected">0</div><div class="lbl">Conectados</div></div>
        <div class="stat-box"><div class="num" id="sBlocked">0</div><div class="lbl">Bloqueados</div></div>
        <div class="stat-box"><div class="num" id="sCycles">0</div><div class="lbl">Ciclos</div></div>
      </div>
    </div>
    <div class="btn-group">
      <button class="btn primary" onclick="runCmd('summary')">Summary</button>
      <button class="btn info" onclick="runCmd('dump')">CSV Dump</button>
      <a class="btn success" href="/api/dump" download>Download CSV</a>
      <button class="btn danger" onclick="runCmd('reset')">Reset</button>
    </div>
    <div id="cmdOutput" class="card hidden">
      <h2>Output</h2>
      <pre id="cmdOutputText"></pre>
    </div>
  </div>

  <!-- Tab: Stations -->
  <div id="tab-stations" class="tab-content hidden">
    <div class="card">
      <h2>Dispositivos Conectados</h2>
      <div id="stationsList"><p style="color:#666;text-align:center;padding:10px">Carregando...</p></div>
    </div>
    <div class="card">
      <h2>MACs Bloqueados</h2>
      <div id="blockedList"><p style="color:#666;text-align:center;padding:10px">Carregando...</p></div>
    </div>
  </div>

  <!-- Tab: Config -->
  <div id="tab-config" class="tab-content hidden">
    <div class="card">
      <h2>Rede WiFi (STA)</h2>
      <label>SSID</label><input id="cfgSsid" placeholder="SSID da rede">
      <label>Senha</label><input id="cfgPass" type="password" placeholder="Senha">
    </div>
    <div class="card">
      <h2>AP</h2>
      <label>IP do AP</label><input id="cfgApIp" placeholder="192.168.4.1">
      <label>Gateway</label><input id="cfgApGw" placeholder="192.168.4.1">
      <label>Máscara</label><input id="cfgApMask" placeholder="255.255.255.0">
      <label>SSID do AP</label><input id="cfgApSsid" placeholder="PicoTester">
      <label>MAC do AP (vazio = padrão)</label><input id="cfgApMac" placeholder="AA:BB:CC:DD:EE:FF">
    </div>
    <div class="card">
      <h2>Alvo</h2>
      <label>MAC Alvo</label><input id="cfgTarget" placeholder="AA:BB:CC:DD:EE:FF">
    </div>
    <div class="card">
      <h2>Log</h2>
      <label style="display:flex;align-items:center;gap:8px;margin:0">
        <input type="checkbox" id="cfgLog" style="width:auto;accent-color:#6c5ce7"> Log detalhado ativo
      </label>
    </div>
    <button class="btn primary" onclick="saveConfig()" style="width:100%;margin-top:4px">Salvar Configurações</button>
  </div>

  <!-- Tab: AP Debug -->
  <div id="tab-debug" class="tab-content hidden">
    <div class="card">
      <h2>Dados Capturados (AP Debug)</h2>
      <div class="btn-group" style="margin-bottom:8px">
        <button class="btn primary" onclick="loadDebugData()">Atualizar</button>
        <a class="btn success" href="/api/debugdump?dl=1" download>Download CSV</a>
      </div>
      <div id="debugData"><p style="color:#666;text-align:center;padding:10px">Clique em "Atualizar" para carregar.</p></div>
    </div>
  </div>

  <div class="toast" id="toast"></div>
</div>

<script>
// API Base
const API = '/api';

// Tabs
document.querySelectorAll('.tab').forEach(t => {
  t.addEventListener('click', () => {
    document.querySelectorAll('.tab').forEach(x => x.classList.remove('active'));
    document.querySelectorAll('.tab-content').forEach(x => x.classList.add('hidden'));
    t.classList.add('active');
    document.getElementById('tab-' + t.dataset.tab).classList.remove('hidden');
  });
});

// Toast
let toastTimer;
function showToast(msg, ok) {
  const t = document.getElementById('toast');
  t.textContent = msg;
  t.className = 'toast show';
  t.style.background = ok ? '#1a3a2a' : '#3a1a1a';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.className = 'toast', 3000);
}

// Fetch JSON
async function api(url, opts) {
  try {
    const r = await fetch(url, opts);
    if (url.endsWith('/command')) return { output: await r.text() };
    if (!r.ok) return { error: 'HTTP ' + r.status };
    return await r.json();
  } catch(e) { return { error: e.message }; }
}

// Format time
function fmtTime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60;
  return `${h}h${String(m).padStart(2,'0')}m${String(s).padStart(2,'0')}s`;
}

// Format MAC
function fmtMac(m) {
  if (!m || m.length !== 6) return '--';
  return m.map(x => x.toString(16).padStart(2,'0').toUpperCase()).join(':');
}

// Run command via serial
async function runCmd(cmd) {
  const out = document.getElementById('cmdOutput');
  const txt = document.getElementById('cmdOutputText');
  out.classList.remove('hidden');
  txt.textContent = 'Executando ' + cmd + '...';
  const res = await api(API + '/command', {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify({ cmd })
  });
  txt.textContent = res.output || 'Sem resposta';
}

// Save config
async function saveConfig() {
  const body = {
    ssid: document.getElementById('cfgSsid').value,
    pass: document.getElementById('cfgPass').value,
    ap_ssid: document.getElementById('cfgApSsid').value,
    target_mac: document.getElementById('cfgTarget').value,
    ap_ip: document.getElementById('cfgApIp').value,
    ap_gateway: document.getElementById('cfgApGw').value,
    ap_subnet: document.getElementById('cfgApMask').value,
    ap_mac: document.getElementById('cfgApMac').value,
    log_enabled: document.getElementById('cfgLog').checked
  };
  const res = await api(API + '/config', {
    method: 'POST', headers: {'Content-Type':'application/json'},
    body: JSON.stringify(body)
  });
  showToast('Configurações salvas!', true);
  loadConfig();
}

// Load config into form
async function loadConfig() {
  const d = await api(API + '/config');
  if (d.error) { showToast('Config: ' + d.error, false); return; }
  document.getElementById('cfgSsid').value = d.ssid || '';
  document.getElementById('cfgPass').value = d.pass || '';
  document.getElementById('cfgApSsid').value = d.ap_ssid || '';
  document.getElementById('cfgTarget').value = d.target_mac || '';
  document.getElementById('cfgApIp').value = d.ap_ip || '';
  document.getElementById('cfgApGw').value = d.ap_gateway || '';
  document.getElementById('cfgApMask').value = d.ap_subnet || '';
  document.getElementById('cfgApMac').value = d.ap_mac || '';
  document.getElementById('cfgLog').checked = !!d.log_enabled;
}

// Render stations
function renderStations(data) {
  const el = document.getElementById('stationsList');
  if (!data.stations || data.stations.length === 0) {
    el.innerHTML = '<p style="color:#666;text-align:center;padding:10px">Nenhum dispositivo conectado</p>';
    return;
  }
  let h = '<table><tr><th>#</th><th>MAC</th><th>Status</th><th>Tempo</th></tr>';
  data.stations.forEach((s, i) => {
    const a = s.active;
    h += `<tr><td>${i+1}</td><td class="mono">${s.mac}</td><td><span class="status-dot ${a?'on':'off'}"></span>${a?'Online':'Offline'}</td><td>${fmtTime(s.uptime)}</td></tr>`;
  });
  h += '</table>';
  el.innerHTML = h;
}

// Render blocked MACs
function renderBlocked(data) {
  const el = document.getElementById('blockedList');
  if (!data.blocked || data.blocked.length === 0) {
    el.innerHTML = '<p style="color:#666;text-align:center;padding:10px">Nenhum MAC bloqueado</p>';
    return;
  }
  let h = '<table><tr><th>MAC</th><th>Tipo</th><th>Status</th></tr>';
  data.blocked.forEach(b => {
    h += `<tr><td class="mono">${b.mac}</td><td>${b.tipo}</td><td>${b.confirmado?'CONFIRMADO':'Pendente'}</td></tr>`;
  });
  h += '</table>';
  el.innerHTML = h;
}

// Render dashboard
function renderDashboard(data) {
  const ap = data.ap || {};
  document.getElementById('dSsid').textContent = ap.ssid || '--';
  document.getElementById('dIp').textContent = ap.ip || '--';
  document.getElementById('dMac').textContent = ap.mac || '--';
  document.getElementById('dUptime').textContent = fmtTime(data.uptime || 0);
  document.getElementById('modeBadge').textContent = data.mode || 'AP';
  document.getElementById('modeBadge').className = 'badge ' + (data.mode === 'AP' ? 'on' : 'off');

  if (data.stats) {
    document.getElementById('sConnected').textContent = data.stats.connected || 0;
    document.getElementById('sBlocked').textContent = data.stats.blocked || 0;
    document.getElementById('sCycles').textContent = data.stats.cycles || 0;
  }
}

// Fetch all
async function fetchAll() {
  const status = await api(API + '/status');
  if (status.error) { showToast('API: ' + status.error, false); return; }
  renderDashboard(status);
  renderStations(status);
  if (status.blocked) renderBlocked(status);
}
function doFetch() { fetchAll(); }

async function loadDebugData() {
  const el = document.getElementById('debugData');
  el.innerHTML = '<p style="color:#666;text-align:center;padding:10px">Carregando...</p>';
  const data = await api(API + '/debugdump');
  if (data.error || !Array.isArray(data)) {
    el.innerHTML = '<p style="color:#f87171;text-align:center;padding:10px">Nenhum dado encontrado.</p>';
    return;
  }
  if (data.length === 0) {
    el.innerHTML = '<p style="color:#666;text-align:center;padding:10px">Nenhum dispositivo capturado ainda.</p>';
    return;
  }
  let html = '<table><thead><tr><th>MAC</th><th>IP</th><th>Hostname</th><th>Vendor Class</th><th>User-Agent</th><th>RSSI</th></tr></thead><tbody>';
  data.forEach(r => {
    html += '<tr>';
    html += '<td class="mono">' + (r.mac || '--') + '</td>';
    html += '<td>' + (r.ip || '--') + '</td>';
    html += '<td>' + (r.hostname || '--') + '</td>';
    html += '<td>' + (r.vendor_class || '--') + '</td>';
    html += '<td style="max-width:150px;overflow:hidden;text-overflow:ellipsis">' + (r.user_agent || '--') + '</td>';
    html += '<td>' + (r.rssi || '--') + '</td>';
    html += '</tr>';
  });
  html += '</tbody></table>';
  el.innerHTML = html;
}

// Init
fetchAll();
loadConfig();
loadDebugData();
setInterval(doFetch, 5000);
</script>
</body>
</html>
)rawliteral";

// ---------- GLOBALS ----------
static WebServer webServer(80);
static DNSServer dnsServer;
static bool webActive = false;
static bool apAPIMode = false;  // tracks if we're in AP mode for API responses

// Command output capture (defined in main .ino)
extern char cmd_output_buf[1024];
extern bool cmd_output_capture;

// ---------- HELPERS ----------
static String jsonStr(const char *s) {
  if (!s) return "\"\"";
  String out = "\"";
  while (*s) {
    char c = *s++;
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) { out += ' '; }  // strip other control chars
        else { out += c; }
        break;
    }
  }
  return out + "\"";
}

static String jsonStr(const String &s) {
  return jsonStr(s.c_str());
}

static String macToStr(const uint8_t *mac) {
  if (!mac) return "\"\"";
  char buf[24];
  snprintf(buf, sizeof(buf), "\"%02X:%02X:%02X:%02X:%02X:%02X\"",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

static String macOrBlank(const uint8_t *mac) {
  if (!mac) return "\"\"";
  bool allZero = true;
  for (int i = 0; i < 6; i++) { if (mac[i]) { allZero = false; break; } }
  if (allZero) return "\"\"";
  return macToStr(mac);
}

// ---------- API HANDLERS ----------
static void handleAPIStatus() {
  String json = "{\n";
  json += "\"mode\":" + jsonStr(ap_mode_active ? "AP" : "STA") + ",\n";
  json += "\"uptime\":" + String(millis() / 1000) + ",\n";
  json += "\"ap\":{\n";
  json += "  \"ssid\":" + jsonStr(ap_mode_active ? config_ap_ssid : config_ssid) + ",\n";
  json += "  \"ip\":" + jsonStr(ap_mode_active ? WiFi.softAPIP().toString() : ap_config_ip.toString()) + ",\n";
  json += "  \"mac\":" + jsonStr(ap_mode_active ? WiFi.softAPmacAddress() : "") + ",\n";
  json += "  \"stations\":" + String(ap_mode_active ? WiFi.softAPgetStationNum() : 0) + "\n";
  json += "},\n";

  // Stats
  json += "\"stats\":{\n";
  json += "  \"connected\":" + String(stats.random_connected + stats.exact_connected) + ",\n";
  json += "  \"blocked\":" + String(stats.random_blocked + stats.exact_blocked) + ",\n";
  json += "  \"cycles\":" + String(stats.total_cycles) + ",\n";
  json += "  \"errors\":" + String(stats.random_errors + stats.exact_errors) + ",\n";
  json += "  \"exact_connected\":" + String(stats.exact_connected) + ",\n";
  json += "  \"exact_blocked\":" + String(stats.exact_blocked) + "\n";
  json += "},\n";

  // Stations
  json += "\"stations\":[\n";
  for (int i = 0; i < connected_station_count; i++) {
    if (i > 0) json += ",\n";
    unsigned long elapsed = (millis() - connected_stations[i].first_seen_ms) / 1000;
    json += "  {\"mac\":" + macToStr(connected_stations[i].mac) + ",";
    json += "\"active\":" + String(connected_stations[i].active ? "true" : "false") + ",";
    json += "\"uptime\":" + String(elapsed) + "}";
  }
  json += "\n],\n";

  // Blocked MACs
  json += "\"blocked\":[\n";
  for (int i = 0; i < blocked_count; i++) {
    if (i > 0) json += ",\n";
    json += "  {\"mac\":" + macToStr(blocked_macs[i].mac) + ",";
    json += "\"tipo\":" + jsonStr(blocked_macs[i].tipo) + ",";
    json += "\"confirmado\":" + String(blocked_macs[i].confirmado ? "true" : "false") + "}";
  }
  json += "\n]\n";

  json += "}\n";
  webServer.send(200, "application/json", json);
}

static void handleAPIConfig() {
  if (webServer.method() == HTTP_POST) {
    // Parse incoming JSON
    String body = webServer.arg("plain");
    if (body.length()) {
      // Simple key-value parsing from JSON
      auto setStr = [&](const char *key, char *buf, int bufSize) {
        String k = String("\"") + key + "\"";
        int idx = body.indexOf(k);
        if (idx < 0) return;
        int start = body.indexOf('"', idx + k.length() + 2);
        if (start < 0) return;
        int end = body.indexOf('"', start + 1);
        if (end < 0) return;
        String val = body.substring(start + 1, end);
        snprintf(buf, bufSize, "%s", val.c_str());
      };

      auto setBool = [&](const char *key, bool &ref) {
        String k = "\"" + String(key) + "\"";
        int idx = body.indexOf(k);
        if (idx < 0) return;
        ref = (body.indexOf("true", idx) >= 0);
      };

      auto setMAC = [&](const char *key, uint8_t *mac, bool *setFlag) {
        String k = "\"" + String(key) + "\"";
        int idx = body.indexOf(k);
        if (idx < 0) return;
        int start = body.indexOf('"', idx + k.length() + 2);
        if (start < 0) return;
        int end = body.indexOf('"', start + 1);
        if (end < 0) return;
        String val = body.substring(start + 1, end);
        if (val.length() == 0) {
          memset(mac, 0, 6);
          if (setFlag) *setFlag = false;
        } else {
          uint8_t tmp[6];
          if (parseMAC(val.c_str(), tmp)) {
            memcpy(mac, tmp, 6);
            if (setFlag) *setFlag = true;
          }
        }
      };

      auto setIP = [&](const char *key, IPAddress &ip) {
        String k = "\"" + String(key) + "\"";
        int idx = body.indexOf(k);
        if (idx < 0) return;
        int start = body.indexOf('"', idx + k.length() + 2);
        if (start < 0) return;
        int end = body.indexOf('"', start + 1);
        if (end < 0) return;
        String val = body.substring(start + 1, end);
        if (val.length()) {
          IPAddress tmp;
          if (parseIP(val.c_str(), tmp)) ip = tmp;
        }
      };

      setStr("ssid", config_ssid, sizeof(config_ssid));
      setStr("pass", config_pass, sizeof(config_pass));
      setStr("ap_ssid", config_ap_ssid, sizeof(config_ap_ssid));
      setMAC("target_mac", config_mac_alvo, nullptr);
      // Override: handle target_mac specially
      {
        String k = "\"target_mac\"";
        int idx = body.indexOf(k);
        if (idx >= 0) {
          int start = body.indexOf('"', idx + k.length() + 2);
          if (start >= 0) {
            int end = body.indexOf('"', start + 1);
            if (end >= 0) {
              String val = body.substring(start + 1, end);
              if (val.length()) {
                uint8_t tmp[6];
                if (parseMAC(val.c_str(), tmp)) memcpy(config_mac_alvo, tmp, 6);
              }
            }
          }
        }
      }
      setIP("ap_ip", ap_config_ip);
      setIP("ap_gateway", ap_config_gateway);
      setIP("ap_subnet", ap_config_subnet);
      setMAC("ap_mac", ap_config_mac, &ap_config_mac_set);
      setBool("log_enabled", log_enabled);

      saveConfig();
      webServer.send(200, "application/json", "{\"ok\":true}");
      return;
    }
    webServer.send(400, "application/json", "{\"error\":\"empty body\"}");
    return;
  }

  // GET: return config
  String json = "{\n";
  json += "  \"ssid\":" + jsonStr(config_ssid) + ",\n";
  json += "  \"pass\":" + jsonStr("********") + ",\n";
  json += "  \"target_mac\":" + macToStr(config_mac_alvo) + ",\n";
  json += "  \"ap_ssid\":" + jsonStr(config_ap_ssid) + ",\n";
  json += "  \"ap_ip\":" + jsonStr(ap_config_ip.toString()) + ",\n";
  json += "  \"ap_gateway\":" + jsonStr(ap_config_gateway.toString()) + ",\n";
  json += "  \"ap_subnet\":" + jsonStr(ap_config_subnet.toString()) + ",\n";
  json += "  \"ap_mac\":" + macOrBlank(ap_config_mac_set ? ap_config_mac : nullptr) + ",\n";
  json += "  \"log_enabled\":" + String(log_enabled ? "true" : "false") + "\n";
  json += "}\n";
  webServer.send(200, "application/json", json);
}

static void handleAPICommand() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "text/plain", "POST required");
    return;
  }

  String body = webServer.arg("plain");
  String cmd;

  // Parse cmd from JSON
  {
    int idx = body.indexOf("\"cmd\"");
    if (idx < 0) { webServer.send(400, "text/plain", "missing cmd"); return; }
    int start = body.indexOf('"', idx + 6);
    if (start < 0) { webServer.send(400, "text/plain", "bad format"); return; }
    int end = body.indexOf('"', start + 1);
    if (end < 0) { webServer.send(400, "text/plain", "bad format"); return; }
    cmd = body.substring(start + 1, end);
  }

  String output;
  // Capture command output to buffer
  {
    cmd_output_buf[0] = '\0';
    cmd_output_capture = true;
    char cmdbuf[64];
    snprintf(cmdbuf, sizeof(cmdbuf), "%s", cmd.c_str());
    executeCommand(cmdbuf);
    cmd_output_capture = false;
    output = String(cmd_output_buf);
  }

  webServer.send(200, "text/plain", output);
}

// ---------- API: DUMP CSV ----------
static void handleAPIDump() {
  File f = LittleFS.open(CSV_FILENAME, "r");
  if (!f) {
    webServer.send(404, "text/plain", "CSV not found");
    return;
  }

  String content;
  char buf[128];
  while (f.available()) {
    int n = f.readBytesUntil('\n', buf, sizeof(buf) - 1);
    buf[n] = '\0';
    content += buf;
    content += "\n";
  }
  f.close();

  webServer.sendHeader("Content-Type", "text/csv");
  webServer.sendHeader("Content-Disposition", "attachment; filename=relatorio.csv");
  webServer.send(200, "text/csv", content);
}

// ---------- API: DEBUG DUMP ----------
static void handleAPIDebugDump() {
  bool download = webServer.hasArg("dl");

  if (download) {
    // Serve raw CSV for download
    File f = LittleFS.open("/ap_debug.csv", "r");
    if (!f) { webServer.send(404, "text/plain", "No data"); return; }
    String content;
    char buf[256];
    while (f.available()) {
      int n = f.readBytesUntil('\n', buf, sizeof(buf)-1);
      buf[n] = 0; content += buf; content += "\n";
    }
    f.close();
    webServer.sendHeader("Content-Type", "text/csv");
    webServer.sendHeader("Content-Disposition", "attachment; filename=ap_debug.csv");
    webServer.send(200, "text/csv", content);
    return;
  }

  // Serve JSON for web UI
  File f = LittleFS.open("/ap_debug.csv", "r");
  if (!f) { webServer.send(200, "application/json", "[]"); return; }

  String json = "[";
  char buf[256];
  bool first = true;
  bool needsComma = false;
  while (f.available()) {
    int n = f.readBytesUntil('\n', buf, sizeof(buf)-1);
    buf[n] = '\0';
    if (n == 0) continue;
    // Skip header
    if (first && strncmp(buf, "timestamp", 9) == 0) { first = false; continue; }
    if (needsComma) json += ",";
    needsComma = true;
    // Parse CSV: timestamp,mac,ip,hostname,client_id,vendor_class,user_agent,rssi
    char *fields[8];
    int fi = 0;
    char *p = buf;
    while (fi < 8 && p) {
      char *comma = strchr(p, ',');
      if (comma) { *comma = '\0'; fields[fi++] = p; p = comma + 1; }
      else { fields[fi++] = p; p = NULL; }
    }
    json += "{\"ts\":" + jsonStr(fi > 0 && fields[0] ? fields[0] : "");
    json += ",\"mac\":" + jsonStr(fi > 1 && fields[1] ? fields[1] : "");
    json += ",\"ip\":" + jsonStr(fi > 2 && fields[2] ? fields[2] : "");
    json += ",\"hostname\":" + jsonStr(fi > 3 && fields[3] ? fields[3] : "");
    json += ",\"client_id\":" + jsonStr(fi > 4 && fields[4] ? fields[4] : "");
    json += ",\"vendor_class\":" + jsonStr(fi > 5 && fields[5] ? fields[5] : "");
    json += ",\"user_agent\":" + jsonStr(fi > 6 && fields[6] ? fields[6] : "");
    json += ",\"rssi\":" + jsonStr(fi > 7 && fields[7] ? fields[7] : "");
    json += "}";
  }
  f.close();
  json += "]";
  webServer.send(200, "application/json", json);
}

// ---------- CATCH-ALL / CAPTIVE PORTAL ----------
static void handleRoot() {
  webServer.send_P(200, PSTR("text/html"), DASHBOARD_HTML);
}

// ---------- INIT / HANDLE / STOP ----------
void initWebDashboard() {
  // Start DNS server (captive portal: resolve all domains to our IP)
  IPAddress apIP = WiFi.softAPIP();
  dnsServer.start(53, "*", apIP);

  // Start web server
  webServer.on("/", handleRoot);
  webServer.on("/api/status", handleAPIStatus);
  webServer.on("/api/config", handleAPIConfig);
  webServer.on("/api/command", handleAPICommand);
  webServer.on("/api/dump", handleAPIDump);
  webServer.on("/api/debugdump", handleAPIDebugDump);
  webServer.onNotFound(handleRoot); // captive portal: everything serves the dashboard
  webServer.begin();

  webActive = true;
  replyf("Web dashboard: http://%s\n", apIP.toString().c_str());
}

void handleWebDashboard() {
  if (!webActive) return;
  dnsServer.processNextRequest();
  webServer.handleClient();
}

void stopWebDashboard() {
  if (!webActive) return;
  webServer.stop();
  dnsServer.stop();
  webActive = false;
}
