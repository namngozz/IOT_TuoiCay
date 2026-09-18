#include <DHT.h>
#include <Wire.h>
#include <RTClib.h>
#include <SoftwareSerial.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <ArduinoJson.h>

//=================== CẤU HÌNH PHẦN CỨNG ===================
#define SOIL_PIN        A0
#define FLOW_PIN        A2
#define DHT_PIN         10
#define DHT_TYPE        DHT11
#define PUMP_EN_PIN     6
#define PUMP_IN1_PIN    7
#define PUMP_IN2_PIN    8
#define BUTTON_BOM      11
#define BUTTON_MODE     2 // INT0 cho Sleep
#define led     13 // INT0 cho Sleep
SoftwareSerial esp(3, 4); // TX=3, RX=4

DHT dht(DHT_PIN, DHT_TYPE);
RTC_DS1307 rtc;

//=================== HẰNG SỐ ===================
const int DoAm_Kho  = 1000;
const int DoAm_Uot  = 100;
int Nguong_min = 30;
int Nguong_max = 70;
const uint8_t chong_rung_nut = 50;
uint8_t pumpPowerPercent = 40;
uint8_t pumpPWM = 0;

unsigned long lastFlowCheck = 0;
unsigned long lastSensorRead = 0;
unsigned long pumpStartTime = 0;

//=================== TRẠNG THÁI ===================
enum Mode { MODE_AUTO, MODE_MANUAL, MODE_SCHEDULE, MODE_SLEEP };
Mode currentMode = MODE_MANUAL;
bool pumpOn = false;
bool pumpPressed = false;
bool modePressed = false;
unsigned long lastDebouncePump = 0;
unsigned long lastDebounceMode = 0;

//=================== SETUP ===================
void setup() {
  Serial.begin(9600);
  esp.begin(9600);
  Wire.begin();
  dht.begin();
  rtc.begin();

  pinMode(SOIL_PIN,INPUT);
  pinMode(FLOW_PIN,INPUT);
  pinMode(PUMP_EN_PIN,OUTPUT);
  pinMode(PUMP_IN1_PIN,OUTPUT);
  pinMode(PUMP_IN2_PIN,OUTPUT);
  pinMode(BUTTON_BOM,INPUT_PULLUP);
  pinMode(BUTTON_MODE,INPUT_PULLUP);
  pinMode(led, OUTPUT);
  pumpStop();
  updatePumpPower();
  if(!rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__),F(__TIME__)));
  Serial.println(F("=== HE THONG TUOI NUOC UNO ==="));
}

//=================== LỊCH TƯỚI ===================
#define MAX_SCHEDULE 2

struct Schedule {
  uint8_t hour;
  uint8_t minute;
};

Schedule schedules[MAX_SCHEDULE];
uint8_t scheduleCount = 0;

bool scheduleRunning = false;

//=================== SLEEP MODE ===================
void blinkLed9() {
  static uint8_t fade = 0;
  static int8_t dir = 5;

  fade += dir;
  if (fade <= 0 || fade >= 255) dir = -dir;

  analogWrite(led, fade);
}

void wakeUp() {}
void goToSleep() {

  Serial.println(F("[SLEEP] Đang vào chế độ ngủ..."));

  pumpStop();
  delay(50);

  ADCSRA &= ~(1 << ADEN);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  noInterrupts();
  attachInterrupt(digitalPinToInterrupt(BUTTON_MODE), wakeUp, FALLING);
  interrupts();
  delay(100); // ổn định tín hiệu
  sleep_cpu();

  sleep_disable();
  detachInterrupt(digitalPinToInterrupt(BUTTON_MODE));
  ADCSRA |= (1 << ADEN);

  Serial.println(F("[SLEEP] → Wake OK!"));

  // Đợi nhả nút thật sự
  while (digitalRead(BUTTON_MODE) == LOW);

  delay(120);

  modePressed = false;
  lastDebounceMode = millis();

  // MUỐN AUTO SAU KHI WAKE?
  currentMode = MODE_AUTO;
}



//=================== HÀM TIỆN ÍCH ===================
inline void updatePumpPower() {
  pumpPWM = map(pumpPowerPercent,0,100,0,255);
  if(pumpOn) analogWrite(PUMP_EN_PIN,pumpPWM);
}

inline void pumpStart() {
  digitalWrite(PUMP_IN1_PIN,HIGH);
  digitalWrite(PUMP_IN2_PIN,LOW);
  analogWrite(PUMP_EN_PIN,pumpPWM);
  pumpOn = true;
  pumpStartTime = millis();
  lastFlowCheck = millis();
  Serial.println(F("[PUMP] ON"));

}

inline void pumpStop() {
  analogWrite(PUMP_EN_PIN,0);
  digitalWrite(PUMP_IN1_PIN,LOW);
  digitalWrite(PUMP_IN2_PIN,LOW);
  pumpOn = false;
  scheduleRunning = false;
  Serial.println(F("[PUMP] OFF"));
}

int readSoil() {
  long sum=0; 
  for(int i=0;i<10;i++)
  {sum+=analogRead(SOIL_PIN);delay(2);}
  int avg=sum/10; 
  return constrain(map(avg,DoAm_Kho,DoAm_Uot,0,100),0,100);
}

bool checkWaterFlow() {
  long sum=0; 
  for(int i=0;i<10;i++)
  {sum+=analogRead(FLOW_PIN);delay(2);}
  int avg=sum/10; 
  Serial.println(avg);
  return avg<=550;
}

//=================== THÊM CÁC HÀM TÍNH TOÁN ===================
float tocDoKhoDat(float nhietDo) {
  if (nhietDo < 20) return 2.0;
  if (nhietDo < 28) return 4.0;
  if (nhietDo < 33) return 6.0;
  if (nhietDo < 38) return 9.0;
  return 12.0;
}

int tinhThoiGianTuoiTiep(int doAmDat, int nguongMin, float nhietDo) {
  if (doAmDat <= nguongMin) return 0;

  int doAmCanGiam = doAmDat - nguongMin;
  float giamMoiGio = tocDoKhoDat(nhietDo);
  float soGio = doAmCanGiam / giamMoiGio;
  return (int)(soGio * 60);
}

int calcMinThreshold(float t, float h) {
  int baseMin = 35;
  float deltaT = (t - 25) * 0.5;
  float deltaH = (60 - h) * 0.1;
  int result = baseMin + deltaT + deltaH;
  return constrain(result, 25, 55);
}

int calcMaxThreshold(float t, float h) {
  int baseMax = 70;
  float deltaT = (t - 25) * 0.3;
  float deltaH = (60 - h) * 0.1;
  int result = baseMax + deltaT + deltaH;
  return constrain(result, 50, 85);
}


//=================== (MÔI TRƯỜNG)  ===================
bool Kiemtramoitruong(float t, float h) {
  DateTime now = rtc.now();
  // 1. Kiểm tra giờ cấm (12:00 - 15:00)
  if (now.hour() >= 12 && now.hour() <= 14) {
    Serial.println(F("Cam tuoi: Gio nang gat!"));
    return false;
  }
  // 2. Kiểm tra nhiệt độ
  if (t > 38) {
    Serial.println(F("Cam tuoi: Qua nong!"));
    return false;
  }
  return true; 
}

//=================== LỊCH TƯỚI ===================
void scheduleWater() {
  DateTime now = rtc.now();
  DateTime time = rtc.now();
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.println(now.second(), DEC);

  for(uint8_t i=0; i<scheduleCount; i++){
    if(now.hour() == schedules[i].hour && now.minute() == schedules[i].minute){
      if(!scheduleRunning && !pumpOn) { pumpStart(); scheduleRunning=true; }
    }
  }
  if(scheduleRunning && (millis()-pumpStartTime>=60000)) pumpStop();
}

//=================== NÚT NHẤN ===================
void checkPumpButton() {
  bool reading=digitalRead(BUTTON_BOM);
  if(reading!=pumpPressed) lastDebouncePump=millis();
  if(millis()-lastDebouncePump>chong_rung_nut){
    if(reading==LOW && !pumpPressed){
      pumpPressed=true;
      if(currentMode==MODE_MANUAL) pumpOn?pumpStop():pumpStart();
    }else if(reading==HIGH) pumpPressed=false;
  }
}

void checkModeButton() {
  bool reading=digitalRead(BUTTON_MODE);
  if(reading!=modePressed) lastDebounceMode=millis();
  if(millis()-lastDebounceMode>chong_rung_nut){
    if(reading==LOW && !modePressed){
      modePressed=true;
      currentMode=(Mode)(((int)currentMode+1)%4);
      switch(currentMode){
        case MODE_AUTO: Serial.println(F("[MODE] AUTO")); break;
        case MODE_MANUAL: Serial.println(F("[MODE] MANUAL")); break;
        case MODE_SCHEDULE: Serial.println(F("[MODE] SCHEDULE")); break;
        case MODE_SLEEP: Serial.println(F("[MODE] SLEEP")); break;
      }
    } else if(reading==HIGH) modePressed=false;
  }
}

const char* modeToString(Mode m) {
  switch(m) {
    case MODE_AUTO:     return "AUTO";
    case MODE_MANUAL:   return "MANUAL";
    case MODE_SCHEDULE: return "SCHEDULE";
    case MODE_SLEEP:    return "SLEEP";
  }
  return "UNKNOWN";
}

//=================== GỬI DỮ LIỆU QUA ESP ===================
void sendDataToESP(int soil, float t, float h, bool water) {

  int min = calcMinThreshold(t,h);
  int max = calcMaxThreshold(t,h);
  int next = tinhThoiGianTuoiTiep(soil, min, t);

  // Tạo danh sách lịch tưới dạng "06:30,17:00"
  String sch = "";
  for(int i=0;i<scheduleCount;i++){
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", schedules[i].hour, schedules[i].minute);
    sch += buf;
    if(i < scheduleCount-1) sch += ",";
  }

  // ======= GỬI 1 DÒNG DUY NHẤT =======
  String packet = "";
  packet += "S=" + String(soil) + ";";
  packet += "T=" + String(t) + ";";
  packet += "H="  + String(h) + ";";
  packet += "F=" + String(water ? 1 : 0) + ";";
  packet += "MODE=" + String(modeToString(currentMode)) + ";";
  packet += "MIN="  + String(min) + ";";
  packet += "MAX="  + String(max) + ";";
  packet += "NT=" + String(next) + ";";
  packet += "L="  + sch + ";";

  esp.println(packet);

}




//=================== NHẬN LỆNH TỪ ESP32 ===================
void checkESPCommand() {
  if(esp.available()) {
    String cmd = esp.readStringUntil('\n');
    cmd.trim();
    if(cmd.length()==0) return;

    if(cmd.startsWith("MODE:")) {
      String m = cmd.substring(5);
      if(m=="AUTO") currentMode = MODE_AUTO;
      else if(m=="MANUAL") currentMode = MODE_MANUAL;
      else if(m=="SCHEDULE") currentMode = MODE_SCHEDULE;
      else if(m=="SLEEP") currentMode = MODE_SLEEP;
    }

    else if (cmd.startsWith("PUMP:")) {
      String p = cmd.substring(5);

      if (currentMode == MODE_MANUAL) {
        if (p == "ON") {
          pumpStart();
        } 
        else if (p == "OFF") {
          pumpStop();
        }
      } else {
     
      }
    }


    else if(cmd.startsWith("MIN:")) {
      Nguong_min = cmd.substring(4).toInt();
      Serial.print("[MIN] set to "); Serial.println(Nguong_min);
    }

    else if(cmd.startsWith("MAX:")) {
      Nguong_max = cmd.substring(4).toInt();
      Serial.print("[MAX] set to "); Serial.println(Nguong_max);
    }

    else if(cmd.startsWith("POWER:")) {
    int val = cmd.substring(6).toInt();
    pumpPowerPercent = constrain(val,0,100);
    updatePumpPower();
}
    else if(cmd.startsWith("SCHEDULES:")) {
      String json = cmd.substring(10);
      StaticJsonDocument<256> doc;
      DeserializationError error = deserializeJson(doc, json);
      if(!error){
          scheduleCount = min(doc.size(), MAX_SCHEDULE);
          for(int i=0; i<scheduleCount; i++){
              schedules[i].hour = doc[i]["hour"];
              schedules[i].minute = doc[i]["minute"];
          }
          for(int i=0; i<scheduleCount; i++){
          }
      } else {
          Serial.println("[SCHEDULE] JSON parse error");
      }
    }
  }
}

//=================== LOOP ===================
void loop() {
  if(currentMode != MODE_SLEEP) {
    blinkLed9();
    delay(20);
  } 
  else {
    goToSleep();   // chỉ gọi 1 lần
  }

  checkPumpButton();
  checkModeButton();
  checkESPCommand();

  unsigned long now = millis();
  if(now - lastSensorRead >= 3000){
    lastSensorRead = now;
    int soil = readSoil();
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    bool water = checkWaterFlow();

    // ======= TÍNH NGƯỠNG ĐỘ ẨM THEO NHIỆT ĐỘ & ĐỘ ẨM KHÔNG KHÍ =======
    Nguong_min = calcMinThreshold(t, h);
    Nguong_max = calcMaxThreshold(t, h);

    int ThoiGianTuoiTiepTheo = tinhThoiGianTuoiTiep(soil, Nguong_min, t);

    Serial.print("[AUTO CALC] Min="); Serial.print(Nguong_min);
    Serial.print("  Max="); Serial.print(Nguong_max);
    Serial.print("  NextWater="); Serial.print(ThoiGianTuoiTiepTheo);
    Serial.println(" phút");

    sendDataToESP(soil,t,h,water);

    switch(currentMode){
      case MODE_AUTO:
        if(soil<Nguong_min && !pumpOn){
          if(Kiemtramoitruong(t, h)){
            pumpStart();
          }
        } 
        else if(soil>=Nguong_max && pumpOn) pumpStop();
        break;
      case MODE_SCHEDULE: scheduleWater(); break;
      case MODE_MANUAL: break;
      case MODE_SLEEP: break;
    }
  }

  if(pumpOn && (millis()-lastFlowCheck >= 10000)){
    if(!checkWaterFlow()){
      Serial.println(F("[ALERT] Bơm bật nhưng không có nước!"));
      pumpStop();
      currentMode=MODE_MANUAL;
    }
    lastFlowCheck=millis();
  }

  if(pumpOn && (millis()-pumpStartTime > 20000)){
    Serial.println(F("[SAFETY] Bơm chạy quá lâu → tắt!"));
    pumpStop();
  }
}
