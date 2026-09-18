#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <ElegantOTA.h>

// ================= UART với UNO =================
#define RXD2 16   // ESP32 RX  -> UNO TX
#define TXD2 17   // ESP32 TX  -> UNO RX
String buffer = "";

// ================= SERVER =================
String serverBase = "https://iot-server-yc6r.onrender.com/api/save";
String serverCommand = "https://iot-server-yc6r.onrender.com/api/command";

// ================= WI-FI & OTA =================
WiFiManager wm;
WebServer server(80);

// ================= NHẬN LỆNH TỪ SERVER =================
unsigned long lastFetch = 0;
const int fetchInterval = 5000;   // lấy lệnh mỗi 5s

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  // ===== WIFI CONFIG PORTAL =====
  wm.setConfigPortalTimeout(30);     // Portal mở 30s sau reset
  wm.setBreakAfterConfig(true);

  Serial.println("📡 Mở WiFi Config Portal (30s nếu chưa có WiFi)");
  // MỞ CONFIG TRƯỚC
  wm.startConfigPortal("MyCar_WifiSet", "12345678");

  Serial.println("Hết thời gian config → kết nối WiFi cũ");

  if (!wm.autoConnect("MyCar_WifiSet", "12345678")) {
    Serial.println("⚠️ Không kết nối được WiFi, ESP restart...");
    ESP.restart();
  }

  Serial.println("✅ Đã kết nối WiFi!");
  Serial.print("📶 IP (DHCP): ");
  Serial.println(WiFi.localIP());

  // ===== GÁN IP TĨNH SAU KHI KẾT NỐI =====
  IPAddress local_IP(192, 168, 1, 50);     // IP cố định cho ESP32
  IPAddress gateway(192, 168, 1, 1);       // Thường là router
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);        // Google DNS
  IPAddress secondaryDNS(8, 8, 4, 4);

  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS); // gán IP tĩnh 

  Serial.print("📡 IP hiện tại: ");
  Serial.println(WiFi.localIP());

  // ===== ElegantOTA =====
  
  // ElegantOTA.setID("ESP32_Garden");
  //ElegantOTA.setAuth("admin", "1234"); // bảo mật OTA

  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server & ElegantOTA started!");
  Serial.print("➡️ Truy cập http://");
  Serial.print(WiFi.localIP());
  Serial.println("/update để cập nhật OTA");
}

// ================= GỬI DỮ LIỆU LÊN SERVER =================
void sendToServer(String data) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost");
    return;
  }

  data.trim();
  if (data.length() == 0) return;

  // Phân tách dữ liệu
  float t = 0, h = 0;
  int soil = 0, flow = 0, minv = 0, maxv = 0, next = 0, pump_power = 36;
  String mode = "";
  String sch = "";

  auto getVal = [&](String key) {
    int s = data.indexOf(key + "=");
    if (s < 0) return String("");
    int e = data.indexOf(";", s);
    if (e < 0) e = data.length();
    return data.substring(s + key.length() + 1, e);
  };

  soil = getVal("S").toInt();
  t = getVal("T").toFloat();
  h = getVal("H").toFloat();
  flow = getVal("F").toInt();
  mode = getVal("MODE");
  minv = getVal("MIN").toInt();
  maxv = getVal("MAX").toInt();
  next = getVal("NT").toInt();
  sch = getVal("L");
  pump_power = getVal("POWER").toInt();

  // Convert lịch tưới từ string "06:30,17:00" sang array JSON
  String scheduleJSON = "[";
  int last = 0;
  while (true) {
    int comma = sch.indexOf(',', last);
    int end = (comma >= 0) ? comma : sch.length();
    String t_str = sch.substring(last, end);
    int colon = t_str.indexOf(':');
    if (colon>0){
      String h_str = t_str.substring(0,colon);
      String m_str = t_str.substring(colon+1);
      scheduleJSON += "{\"hour\":"+h_str+",\"minute\":"+m_str+"}";
      if(end < sch.length()-1) scheduleJSON += ",";
    }
    if(comma<0) break;
    last = comma+1;
  }
  scheduleJSON += "]";

  String postData = "{\"soil\":" + String(soil) +
                    ",\"temp\":" + String(t,1) +
                    ",\"hum\":" + String(h,1) +
                    ",\"flow\":" + String(flow) +
                    ",\"mode\":\"" + mode + "\"" +
                    ",\"min\":" + String(minv) +
                    ",\"max\":" + String(maxv) +
                    ",\"next\":" + String(next) +
                    ",\"pump_power\":" + String(pump_power) +
                    ",\"schedule\":" + scheduleJSON + "}";

  HTTPClient http;
  http.begin(serverBase);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(postData);


  if (code > 0) {
    String resp = http.getString();
    Serial.println("🧾 Server resp: " + resp);
  } else {
    Serial.println("❌ POST failed");
  }
  http.end();
}

// ================= NHẬN LỆNH TỪ SERVER =================
void fetchCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverCommand);
  int code = http.GET();

  if (code == 200) {
    String res = http.getString();
    Serial.println("[CMD] " + res);

    if (res.indexOf("PUMP:ON") >= 0) Serial2.println("PUMP:ON");
    if (res.indexOf("PUMP:OFF") >= 0) Serial2.println("PUMP:OFF");
    if (res.indexOf("MODE:AUTO") >= 0) Serial2.println("MODE:AUTO");
    if (res.indexOf("MODE:MANUAL") >= 0) Serial2.println("MODE:MANUAL");
    if (res.indexOf("MODE:SCHEDULE") >= 0) Serial2.println("MODE:SCHEDULE");
    if (res.indexOf("MODE:SLEEP") >= 0) Serial2.println("MODE:SLEEP");
    if (res.indexOf("POWER:") >= 0) Serial2.println(res.substring(res.indexOf("POWER:")).c_str());
    if (res.indexOf("SCHEDULES:") >=0 ) Serial2.println(res.substring(res.indexOf("SCHEDULES:")).c_str());
  }
  http.end();
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();

  // 1️⃣ Đọc dữ liệu từ UNO liên tục, gửi ngay khi nhận
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n') {
      buffer.trim();
      if (buffer.length() > 0 && buffer.indexOf("S=") >= 0) {
        Serial.println("📩 RX: " + buffer);
        sendToServer(buffer);   // gửi ngay
      }
      buffer = "";
    } else buffer += c;
  }

  // 2️⃣ Lấy lệnh từ server mỗi 5s
  if (now - lastFetch >= fetchInterval) {
    fetchCommand();
    lastFetch = now;
  }

  // 3️⃣ Duy trì hoạt động OTA + HTTP
  server.handleClient();
  ElegantOTA.loop();
}
