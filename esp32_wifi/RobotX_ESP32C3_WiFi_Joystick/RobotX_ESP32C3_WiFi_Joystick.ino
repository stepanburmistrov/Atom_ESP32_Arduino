/*
  RobotX ESP32-C3 Wi-Fi UART Joystick
  -----------------------------------
  Создаёт веб-пульт управления и пересылает команды на Arduino Nano по UART.
  USB Serial на 115200 бод используется для настройки AP/STA и параметров сети.

  Важно для Arduino IDE / ESP32-C3:
  Tools -> USB CDC On Boot -> Enabled

  Подробнее: docs/wifi_module.md и docs/uart_protocol.md
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// =====================
// UART К ARDUINO NANO
// =====================
const int UART_TX_PIN = 4;   // ESP32-C3 TX -> Arduino Nano RX
const int UART_RX_PIN = 5;   // можно не подключать
const int UART_BAUD   = 9600;

// =====================
// WIFI / WEB
// =====================
Preferences prefs;
WebServer server(80);

String wifiMode = "AP";  // AP или STA

String apName = "Kod_atoma_01";
String apPass = "12345678";

String staSsid = "";
String staPass = "";

bool useStaticIp = false;
String staticIp   = "192.168.1.77";
String gatewayIp  = "192.168.1.1";
String subnetMask = "255.255.255.0";
String dnsIp      = "8.8.8.8";

// =====================
// HTML СТРАНИЦА
// =====================
const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<title>RobotX Wi-Fi Joystick</title>
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<style>
* {
  box-sizing: border-box;
  -webkit-tap-highlight-color: transparent;
}

html, body {
  margin: 0;
  padding: 0;
  width: 100%;
  height: 100%;
  overflow: hidden;
  background: #101010;
  color: white;
  font-family: Arial, sans-serif;
  touch-action: none;
  user-select: none;
}

body {
  display: flex;
  flex-direction: column;
}

.header {
  height: 72px;
  min-height: 72px;
  text-align: center;
  border-bottom: 1px solid #222;
}

h1 {
  margin: 12px 0 4px 0;
  font-size: 28px;
}

.status {
  font-size: 20px;
  color: #aaa;
}

.status span {
  color: #f39c12;
  font-weight: bold;
}

.main {
  flex: 1;
  display: flex;
  width: 100%;
  min-height: 0;
}

.panel {
  width: 50%;
  height: 100%;
  padding: 10px 32px 14px 32px;
  display: flex;
  flex-direction: column;
  justify-content: center;
}

.leftPanel {
  border-right: 1px solid #252525;
}

.panelTitle {
  text-align: center;
  font-size: 24px;
  font-weight: bold;
  margin-bottom: 14px;
}

.servoGrid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 22px 28px;
}

.driveGrid {
  width: 100%;
  max-width: 520px;
  margin: 0 auto;
  display: grid;
  grid-template-columns: 1fr 1fr 1fr;
  gap: 16px;
  align-items: center;
}

.btn {
  height: 92px;
  border: none;
  border-radius: 20px;
  color: white;
  font-weight: bold;
  font-size: 30px;
  background: #2d89ef;
  box-shadow: 0 7px #14559b;
}

.btn:active {
  transform: translateY(5px);
  box-shadow: 0 2px #14559b;
  filter: brightness(0.9);
}

.servo {
  background: #f39c12;
  box-shadow: 0 7px #9a5f00;
}

.stop {
  background: #e74c3c;
  box-shadow: 0 7px #922b21;
}

.empty {
  visibility: hidden;
}

.smallText {
  display: block;
  margin-top: 6px;
  font-size: 14px;
  font-weight: normal;
  opacity: 0.9;
}

.footer {
  height: 42px;
  min-height: 42px;
  text-align: center;
  color: #999;
  font-size: 15px;
  border-top: 1px solid #222;
  padding-top: 4px;
}

@media (orientation: portrait) {
  .main {
    flex-direction: column;
  }

  .panel {
    width: 100%;
    height: 50%;
    padding: 8px 18px;
  }

  .leftPanel {
    border-right: none;
    border-bottom: 1px solid #252525;
  }

  .header {
    height: 66px;
    min-height: 66px;
  }

  h1 {
    font-size: 22px;
    margin-top: 8px;
  }

  .panelTitle {
    font-size: 19px;
    margin-bottom: 8px;
  }

  .btn {
    height: 62px;
    font-size: 24px;
    border-radius: 14px;
  }

  .smallText {
    display: none;
  }

  .footer {
    display: none;
  }
}

@media (max-height: 430px) and (orientation: landscape) {
  .header {
    height: 56px;
    min-height: 56px;
  }

  h1 {
    font-size: 24px;
    margin: 6px 0 2px 0;
  }

  .status {
    font-size: 17px;
  }

  .panel {
    padding: 6px 36px 8px 36px;
  }

  .panelTitle {
    font-size: 21px;
    margin-bottom: 8px;
  }

  .btn {
    height: 70px;
    font-size: 25px;
    border-radius: 17px;
    box-shadow: 0 5px #14559b;
  }

  .servo {
    box-shadow: 0 5px #9a5f00;
  }

  .stop {
    box-shadow: 0 5px #922b21;
  }

  .servoGrid {
    gap: 18px 26px;
  }

  .driveGrid {
    max-width: 470px;
    gap: 12px;
  }

  .smallText {
    font-size: 12px;
    margin-top: 3px;
  }

  .footer {
    height: 28px;
    min-height: 28px;
    font-size: 13px;
    padding-top: 2px;
  }
}

@media (max-height: 360px) and (orientation: landscape) {
  .header {
    height: 48px;
    min-height: 48px;
  }

  h1 {
    font-size: 20px;
    margin: 4px 0 0 0;
  }

  .status {
    font-size: 15px;
  }

  .panelTitle {
    display: none;
  }

  .btn {
    height: 58px;
    font-size: 22px;
    border-radius: 14px;
  }

  .smallText {
    display: none;
  }

  .footer {
    display: none;
  }
}
</style>
</head>

<body>

<div class="header">
  <h1>RobotX Wi-Fi Joystick</h1>
  <div class="status">Команда: <span id="status">S</span></div>
</div>

<div class="main">

  <div class="panel leftPanel">
    <div class="panelTitle">Сервоприводы</div>
    <div class="servoGrid">
      <button class="btn servo" data-cmd="Q">
        Q
        <span class="smallText">Левый вверх</span>
      </button>

      <button class="btn servo" data-cmd="W">
        W
        <span class="smallText">Правый вверх</span>
      </button>

      <button class="btn servo" data-cmd="A">
        A
        <span class="smallText">Левый вниз</span>
      </button>

      <button class="btn servo" data-cmd="D">
        D
        <span class="smallText">Правый вниз</span>
      </button>
    </div>
  </div>

  <div class="panel rightPanel">
    <div class="panelTitle">Движение</div>
    <div class="driveGrid">
      <div class="empty"></div>

      <button class="btn" data-cmd="F">
        ▲
        <span class="smallText">ВПЕРЕД</span>
      </button>

      <div class="empty"></div>

      <button class="btn" data-cmd="L">
        ◀
        <span class="smallText">ВЛЕВО</span>
      </button>

      <button class="btn stop" data-cmd="S">
        STOP
        <span class="smallText">СТОП</span>
      </button>

      <button class="btn" data-cmd="R">
        ▶
        <span class="smallText">ВПРАВО</span>
      </button>

      <div class="empty"></div>

      <button class="btn" data-cmd="B">
        ▼
        <span class="smallText">НАЗАД</span>
      </button>

      <div class="empty"></div>
    </div>
  </div>

</div>

<div class="footer">
  Движение: F/B/L/R, стоп: S. Сервы: Q/q, A/a, W/w, D/d
</div>

<script>
let lastCmd = "";

function sendCmd(cmd) {
  lastCmd = cmd;
  document.getElementById("status").innerText = cmd;

  fetch("/cmd?c=" + encodeURIComponent(cmd)).catch(() => {
    document.getElementById("status").innerText = "ERR";
  });
}

function releaseCmd(cmd) {
  let release = "S";

  if (cmd === "Q") release = "q";
  else if (cmd === "W") release = "w";
  else if (cmd === "A") release = "a";
  else if (cmd === "D") release = "d";
  else release = "S";

  sendCmd(release);
}

document.querySelectorAll(".btn").forEach(btn => {
  const cmd = btn.dataset.cmd;

  btn.addEventListener("touchstart", e => {
    e.preventDefault();
    sendCmd(cmd);
  }, {passive: false});

  btn.addEventListener("touchend", e => {
    e.preventDefault();
    releaseCmd(cmd);
  }, {passive: false});

  btn.addEventListener("touchcancel", e => {
    e.preventDefault();
    releaseCmd(cmd);
  }, {passive: false});

  btn.addEventListener("mousedown", e => {
    e.preventDefault();
    sendCmd(cmd);
  });

  btn.addEventListener("mouseup", e => {
    e.preventDefault();
    releaseCmd(cmd);
  });

  btn.addEventListener("mouseleave", e => {
    releaseCmd(cmd);
  });
});

document.addEventListener("contextmenu", e => e.preventDefault());
</script>

</body>
</html>
)rawliteral";

// =====================
// NVS: ЗАГРУЗКА
// =====================
void loadSettings() {
  prefs.begin("robotx", true);

  wifiMode = prefs.getString("wifi_mode", "AP");

  apName = prefs.getString("ap_name", "KodAtoma_C3");
  apPass = prefs.getString("ap_pass", "12345678");

  staSsid = prefs.getString("sta_ssid", "");
  staPass = prefs.getString("sta_pass", "");

  useStaticIp = prefs.getBool("use_static", false);
  staticIp   = prefs.getString("static_ip", "192.168.1.77");
  gatewayIp  = prefs.getString("gateway_ip", "192.168.1.1");
  subnetMask = prefs.getString("subnet", "255.255.255.0");
  dnsIp      = prefs.getString("dns", "8.8.8.8");

  prefs.end();

  wifiMode.toUpperCase();

  if (wifiMode != "AP" && wifiMode != "STA") {
    wifiMode = "AP";
  }

  if (apPass.length() < 8) {
    apPass = "12345678";
  }
}

// =====================
// NVS: СОХРАНЕНИЕ
// =====================
void saveStringSetting(const char* key, String value) {
  value.trim();
  prefs.begin("robotx", false);
  prefs.putString(key, value);
  prefs.end();
}

void saveBoolSetting(const char* key, bool value) {
  prefs.begin("robotx", false);
  prefs.putBool(key, value);
  prefs.end();
}

// =====================
// ОТПРАВКА КОМАНДЫ В NANO
// =====================
void sendToNano(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  Serial.print("[UART TX] ");
  Serial.println(cmd);

  Serial1.print(cmd);
  Serial1.print('\n');
}

// =====================
// WIFI START
// =====================
void startWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(1000);

  if (wifiMode == "AP") {
    WiFi.mode(WIFI_AP);
    delay(100);

    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    Serial.print("TX Power = ");
    Serial.println((int)WiFi.getTxPower());

    bool ok = WiFi.softAP(apName.c_str(), apPass.c_str());

    Serial.println();
    Serial.println("========== WIFI AP ==========");
    Serial.print("AP status: ");
    Serial.println(ok ? "OK" : "ERROR");
    Serial.print("SSID: ");
    Serial.println(apName);
    Serial.print("PASS: ");
    Serial.println(apPass);
    Serial.print("IP: ");
    Serial.println(WiFi.softAPIP());
    Serial.println("=============================");

    return;
  }

  if (wifiMode == "STA") {
    WiFi.mode(WIFI_STA);
    delay(100);

    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    Serial.print("TX Power = ");
    Serial.println((int)WiFi.getTxPower());

    if (staSsid.length() == 0) {
      Serial.println("STA SSID is empty!");
      Serial.println("Fallback to AP mode.");

      WiFi.mode(WIFI_AP);
      delay(100);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);

      WiFi.softAP(apName.c_str(), apPass.c_str());

      Serial.print("Fallback AP SSID: ");
      Serial.println(apName);
      Serial.print("Fallback AP IP: ");
      Serial.println(WiFi.softAPIP());
      return;
    }

    Serial.println();
    Serial.println("========== WIFI STA ==========");
    Serial.print("Connecting to: ");
    Serial.println(staSsid);

    // Если включён статический IP, применяем его ДО WiFi.begin().
    // В исходной версии параметры SETSTATIC сохранялись в NVS, но фактически не применялись.
    if (useStaticIp) {
      IPAddress ip;
      IPAddress gateway;
      IPAddress subnet;
      IPAddress dns;

      bool ipOk = ip.fromString(staticIp);
      bool gatewayOk = gateway.fromString(gatewayIp);
      bool subnetOk = subnet.fromString(subnetMask);
      bool dnsOk = dns.fromString(dnsIp);

      if (ipOk && gatewayOk && subnetOk && dnsOk) {
        bool configOk = WiFi.config(ip, gateway, subnet, dns);
        Serial.print("Static IP config: ");
        Serial.println(configOk ? "OK" : "ERROR");
      } else {
        Serial.println("Static IP config skipped: bad saved IP settings");
      }
    }

    WiFi.begin(staSsid.c_str(), staPass.c_str());

    unsigned long startTime = millis();

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
      delay(500);
      Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Connected!");
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("STA connection failed!");
      Serial.println("Fallback to AP mode.");

      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(500);

      WiFi.mode(WIFI_AP);
      delay(100);
      WiFi.setTxPower(WIFI_POWER_8_5dBm);

      WiFi.softAP(apName.c_str(), apPass.c_str());

      Serial.print("Fallback AP SSID: ");
      Serial.println(apName);
      Serial.print("Fallback AP IP: ");
      Serial.println(WiFi.softAPIP());
    }
  }
}

// =====================
// WEB ОБРАБОТЧИКИ
// =====================
void handleRoot() {
  server.send_P(200, "text/html", MAIN_page);
}

void handleCommand() {
  if (!server.hasArg("c")) {
    server.send(400, "text/plain", "No command");
    return;
  }

  String cmd = server.arg("c");
  sendToNano(cmd);

  server.send(200, "text/plain", "OK");
}

void handleInfo() {
  String currentIp = "";

  if (WiFi.getMode() == WIFI_AP) {
    currentIp = WiFi.softAPIP().toString();
  } else {
    currentIp = WiFi.localIP().toString();
  }

  String info = "";
  info += "RobotX ESP32-C3 WiFi UART Bridge\n";
  info += "Mode: " + wifiMode + "\n";
  info += "AP SSID: " + apName + "\n";
  info += "STA SSID: " + staSsid + "\n";
  info += "Current IP: " + currentIp + "\n";
  info += "UART TX: GPIO4\n";
  info += "UART RX: GPIO5\n";
  info += "UART baud: 9600\n";

  server.send(200, "text/plain", info);
}

// =====================
// USB SERIAL КОМАНДЫ
// =====================
void printHelp() {
  Serial.println();
  Serial.println("========== COMMANDS ==========");
  Serial.println("INFO");
  Serial.println("SETMODE AP");
  Serial.println("SETMODE STA");
  Serial.println("SETAP RobotX_C3 12345678");
  Serial.println("SETSTA MyWiFi mypassword");
  Serial.println("SETDHCP");
  Serial.println("SETSTATIC 192.168.1.77 192.168.1.1 255.255.255.0 8.8.8.8");
  Serial.println("REBOOT");
  Serial.println("==============================");
}

void handleUsbSerial() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  Serial.println();
  Serial.print("[USB RX] ");
  Serial.println(cmd);

  if (cmd == "INFO") {
    String currentIp = "";

    if (WiFi.getMode() == WIFI_AP) {
      currentIp = WiFi.softAPIP().toString();
    } else {
      currentIp = WiFi.localIP().toString();
    }

    Serial.println("========== INFO ==========");
    Serial.print("Mode: ");
    Serial.println(wifiMode);

    Serial.print("AP SSID: ");
    Serial.println(apName);
    Serial.print("AP PASS: ");
    Serial.println(apPass);

    Serial.print("STA SSID: ");
    Serial.println(staSsid);
    Serial.print("STA PASS: ");
    Serial.println(staPass);

    Serial.print("Use static IP: ");
    Serial.println(useStaticIp ? "YES" : "NO");

    Serial.print("Static IP: ");
    Serial.println(staticIp);
    Serial.print("Gateway: ");
    Serial.println(gatewayIp);
    Serial.print("Subnet: ");
    Serial.println(subnetMask);
    Serial.print("DNS: ");
    Serial.println(dnsIp);

    Serial.print("Current IP: ");
    Serial.println(currentIp);

    Serial.println("==========================");
    return;
  }

  if (cmd == "REBOOT") {
    Serial.println("Rebooting...");
    delay(500);
    ESP.restart();
    return;
  }

  if (cmd == "HELP") {
    printHelp();
    return;
  }

  if (cmd == "SETMODE AP") {
    saveStringSetting("wifi_mode", "AP");
    wifiMode = "AP";
    Serial.println("OK. Mode saved: AP. Use REBOOT.");
    return;
  }

  if (cmd == "SETMODE STA") {
    saveStringSetting("wifi_mode", "STA");
    wifiMode = "STA";
    Serial.println("OK. Mode saved: STA. Use REBOOT.");
    return;
  }

  if (cmd.startsWith("SETAP ")) {
    int p1 = cmd.indexOf(' ', 6);

    if (p1 < 0) {
      Serial.println("ERROR. Use: SETAP RobotX_C3 12345678");
      return;
    }

    String ssid = cmd.substring(6, p1);
    String pass = cmd.substring(p1 + 1);

    if (pass.length() < 8) {
      Serial.println("ERROR. AP password must be at least 8 chars.");
      return;
    }

    saveStringSetting("ap_name", ssid);
    saveStringSetting("ap_pass", pass);

    apName = ssid;
    apPass = pass;

    Serial.println("OK. AP settings saved. Use REBOOT.");
    return;
  }

  if (cmd.startsWith("SETSTA ")) {
    int p1 = cmd.indexOf(' ', 7);

    if (p1 < 0) {
      Serial.println("ERROR. Use: SETSTA MyWiFi mypassword");
      return;
    }

    String ssid = cmd.substring(7, p1);
    String pass = cmd.substring(p1 + 1);

    saveStringSetting("sta_ssid", ssid);
    saveStringSetting("sta_pass", pass);

    staSsid = ssid;
    staPass = pass;

    Serial.println("OK. STA settings saved. Use REBOOT.");
    return;
  }

  if (cmd == "SETDHCP") {
    saveBoolSetting("use_static", false);
    useStaticIp = false;
    Serial.println("OK. DHCP enabled. Use REBOOT.");
    return;
  }

  if (cmd.startsWith("SETSTATIC ")) {
    String rest = cmd.substring(10);
    rest.trim();

    int p1 = rest.indexOf(' ');
    int p2 = rest.indexOf(' ', p1 + 1);
    int p3 = rest.indexOf(' ', p2 + 1);

    if (p1 < 0 || p2 < 0 || p3 < 0) {
      Serial.println("ERROR. Use:");
      Serial.println("SETSTATIC 192.168.1.77 192.168.1.1 255.255.255.0 8.8.8.8");
      return;
    }

    String ip      = rest.substring(0, p1);
    String gateway = rest.substring(p1 + 1, p2);
    String subnet  = rest.substring(p2 + 1, p3);
    String dns     = rest.substring(p3 + 1);

    IPAddress testIp, testGateway, testSubnet, testDns;

    if (!testIp.fromString(ip) ||
        !testGateway.fromString(gateway) ||
        !testSubnet.fromString(subnet) ||
        !testDns.fromString(dns)) {
      Serial.println("ERROR. Bad IP format.");
      return;
    }

    saveStringSetting("static_ip", ip);
    saveStringSetting("gateway_ip", gateway);
    saveStringSetting("subnet", subnet);
    saveStringSetting("dns", dns);
    saveBoolSetting("use_static", true);

    staticIp = ip;
    gatewayIp = gateway;
    subnetMask = subnet;
    dnsIp = dns;
    useStaticIp = true;

    Serial.println("OK. Static IP saved. Use REBOOT.");
    return;
  }

  Serial.println("Unknown command.");
  printHelp();
}

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(20);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("RobotX ESP32-C3 WiFi Joystick");
  Serial.println("AP / STA mode + NVS settings");
  Serial.println("================================");

  loadSettings();

  Serial1.begin(UART_BAUD, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  Serial.println("[UART] Started");
  Serial.println("[UART] TX = GPIO4");
  Serial.println("[UART] RX = GPIO5");

  startWiFi();

  server.on("/", handleRoot);
  server.on("/cmd", handleCommand);
  server.on("/info", handleInfo);

  server.begin();

  Serial.println("[WEB] Server started");
  Serial.println("Open current device IP in browser.");
  printHelp();
}

// =====================
// LOOP
// =====================
void loop() {
  server.handleClient();
  handleUsbSerial();
}