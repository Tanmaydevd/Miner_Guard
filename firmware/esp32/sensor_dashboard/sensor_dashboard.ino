// ============================================================
//   COMBINED SENSOR DASHBOARD - ESP32
//   Sensors: MPU6050 + BME280 + MQ-2 + MAX30102
//   Features: Buzzer Alerts + WiFi Web Dashboard
// ============================================================

#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include "heartRate.h"

// ============================================================
//   ⚙️  CONFIGURE THESE
// ============================================================
#define WIFI_SSID     "Sushi"      // ← Change this
#define WIFI_PASSWORD "sushi12345"  // ← Change this
#define BUZZER_PIN    25                    // Buzzer GPIO pin

// ============================================================
//   🌐  MINE GUARD BACKEND (Flask /update endpoint)
//   Edit BACKEND_URL to point at the PC running app.py
//   (find with `ipconfig` on Windows — use the IPv4 address).
// ============================================================
#define BACKEND_URL        "http://192.168.157.153:5001/update"
#define DEVICE_ID          "EMP001"
#define DEVICE_NAME        "Rajesh Kumar"
#define DEVICE_ZONE        "Zone A - Tunnel 3"
#define DEVICE_LEVEL       "L1"
#define BACKEND_PERIOD_MS  3000  // POST every 3 seconds

// ============================================================
//   PIN DEFINITIONS
// ============================================================
#define SDA_PIN       21
#define SCL_PIN       22
#define MQ2_AO_PIN    34   // MQ-2 Analog
#define MQ2_DO_PIN    35   // MQ-2 Digital

// ============================================================
//   SENSOR ADDRESSES
// ============================================================
#define MPU6050_ADDR  0x68
uint8_t BME280_ADDR = 0x76;  // Auto-detects 0x76 or 0x77

// ============================================================
//   ALERT THRESHOLDS
// ============================================================
#define TEMP_HIGH_C       35.0   // °C — too hot
#define TEMP_LOW_C        10.0   // °C — too cold
#define HUMIDITY_HIGH     80.0   // % — too humid
#define HUMIDITY_LOW      20.0   // % — too dry
#define GAS_PPM_DANGER    600.0  // ppm — dangerous gas level
#define SPO2_LOW          90     // % — low blood oxygen
#define HR_HIGH           120    // bpm — high heart rate
#define HR_LOW            40     // bpm — low heart rate
#define PRESSURE_LOW      890.0  // hPa — storm warning (Bangalore)

// ============================================================
//   GLOBAL SENSOR DATA
// ============================================================
// MPU6050
float ax=0, ay=0, az=0, gx=0, gy=0, gz=0, mpu_temp=0;
String tiltStatus = "UNKNOWN";
float TEMP_OFFSET = -5.0; // Calibrate this!

// BME280
float bme_temp=0, bme_humidity=0, bme_pressure=0, bme_altitude=0;
String weatherStatus = "UNKNOWN";
String pressureTrend = "Collecting...";
float pressureHistory[10]; int pressureIdx=0; bool pressFull=false;

// MQ-2
float mq_lpg=0, mq_smoke=0, mq_co=0, mq_ch4=0, mq_h2=0;
float mq_R0 = 9.83, mq_RL = 10.0;
bool mq_digitalAlert = false;
String gasDetected = "Clean Air";
bool mq_calibrated = false;

// MAX30102
MAX30105 particleSensor;
bool max_found = false;
int32_t spo2=0; int8_t spo2Valid=0;
int32_t heartRate_val=0; int8_t hrValid=0;
float beatsPerMinute=0; int beatAvg=0;
bool fingerPresent = false;
#define BUFFER_LENGTH 100
uint32_t irBuffer[BUFFER_LENGTH];
uint32_t redBuffer[BUFFER_LENGTH];
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE]; byte rateSpot=0;
long lastBeat=0;

// Alerts
bool buzzerActive = false;
String activeAlerts = "";
unsigned long lastBuzzerTime = 0;

// Web Server
WebServer server(80);
unsigned long lastSensorUpdate = 0;
unsigned long lastBackendSend = 0;

// ============================================================
//   BME280 RAW I2C
// ============================================================
uint16_t dig_T1; int16_t dig_T2, dig_T3;
uint16_t dig_P1; int16_t dig_P2,dig_P3,dig_P4,dig_P5,dig_P6,dig_P7,dig_P8,dig_P9;
uint8_t  dig_H1,dig_H3; int16_t dig_H2,dig_H4,dig_H5; int8_t dig_H6;
int32_t  t_fine;

void bmeWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(BME280_ADDR); Wire.write(reg); Wire.write(val);
  Wire.endTransmission(true); delay(5);
}
uint8_t bmeRead(uint8_t reg) {
  Wire.beginTransmission(BME280_ADDR); Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)BME280_ADDR,(uint8_t)1,(uint8_t)true);
  return Wire.available() ? Wire.read() : 0xFF;
}
uint16_t bmeRead16(uint8_t r){ return (uint16_t)(bmeRead(r+1)<<8|bmeRead(r)); }
int16_t  bmeRead16S(uint8_t r){ return (int16_t)bmeRead16(r); }

void bmeCalibration() {
  dig_T1=bmeRead16(0x88); dig_T2=bmeRead16S(0x8A); dig_T3=bmeRead16S(0x8C);
  dig_P1=bmeRead16(0x8E); dig_P2=bmeRead16S(0x90); dig_P3=bmeRead16S(0x92);
  dig_P4=bmeRead16S(0x94); dig_P5=bmeRead16S(0x96); dig_P6=bmeRead16S(0x98);
  dig_P7=bmeRead16S(0x9A); dig_P8=bmeRead16S(0x9C); dig_P9=bmeRead16S(0x9E);
  dig_H1=bmeRead(0xA1); dig_H2=bmeRead16S(0xE1); dig_H3=bmeRead(0xE3);
  dig_H4=((int8_t)bmeRead(0xE4)<<4)|(bmeRead(0xE5)&0x0F);
  dig_H5=((int8_t)bmeRead(0xE6)<<4)|(bmeRead(0xE5)>>4);
  dig_H6=(int8_t)bmeRead(0xE7);
}

float bmeCompTemp(int32_t adc_T){
  int32_t v1=((((adc_T>>3)-((int32_t)dig_T1<<1)))*((int32_t)dig_T2))>>11;
  int32_t v2=(((((adc_T>>4)-((int32_t)dig_T1))*((adc_T>>4)-((int32_t)dig_T1)))>>12)*((int32_t)dig_T3))>>14;
  t_fine=v1+v2; return ((t_fine*5+128)>>8)/100.0;
}
float bmeCompPressure(int32_t adc_P){
  int64_t v1=((int64_t)t_fine)-128000, v2=v1*v1*(int64_t)dig_P6;
  v2=v2+((v1*(int64_t)dig_P5)<<17); v2=v2+(((int64_t)dig_P4)<<35);
  v1=((v1*v1*(int64_t)dig_P3)>>8)+((v1*(int64_t)dig_P2)<<12);
  v1=((((int64_t)1)<<47)+v1)*((int64_t)dig_P1)>>33;
  if(v1==0) return 0;
  int64_t p=1048576-adc_P; p=(((p<<31)-v2)*3125)/v1;
  v1=(((int64_t)dig_P9)*(p>>13)*(p>>13))>>25;
  v2=(((int64_t)dig_P8)*p)>>19;
  p=((p+v1+v2)>>8)+(((int64_t)dig_P7)<<4);
  return (float)p/25600.0;
}
float bmeCompHumidity(int32_t adc_H){
  int32_t v=t_fine-76800;
  v=(((((adc_H<<14)-(((int32_t)dig_H4)<<20)-(((int32_t)dig_H5)*v))+16384)>>15)*
     (((((((v*((int32_t)dig_H6))>>10)*(((v*((int32_t)dig_H3))>>11)+32768))>>10)+2097152)*((int32_t)dig_H2)+8192)>>14));
  v=v-(((((v>>15)*(v>>15))>>7)*((int32_t)dig_H1))>>4);
  v=v<0?0:v; v=v>419430400?419430400:v;
  return (float)(v>>12)/1024.0;
}

bool bmeFind() {
  uint8_t addrs[]={0x76,0x77};
  for(int i=0;i<2;i++){
    BME280_ADDR=addrs[i];
    Wire.beginTransmission(BME280_ADDR);
    if(Wire.endTransmission()==0){
      uint8_t id=bmeRead(0xD0);
      if(id==0x60||id==0x58) return true;
    }
  }
  return false;
}

void readBME280() {
  Wire.beginTransmission(BME280_ADDR); Wire.write(0xF7);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)BME280_ADDR,(uint8_t)8,(uint8_t)true);
  if(Wire.available()<8) return;
  uint8_t d[8]; for(int i=0;i<8;i++) d[i]=Wire.read();
  int32_t adc_P=((uint32_t)d[0]<<12)|((uint32_t)d[1]<<4)|(d[2]>>4);
  int32_t adc_T=((uint32_t)d[3]<<12)|((uint32_t)d[4]<<4)|(d[5]>>4);
  int32_t adc_H=((uint32_t)d[6]<<8)|d[7];
  bme_temp     = bmeCompTemp(adc_T) - 2.0;
  bme_pressure = bmeCompPressure(adc_P);
  bme_humidity = bmeCompHumidity(adc_H);
  bme_altitude = 44330.0*(1.0-pow(bme_pressure/1013.25,0.1903));

  // Pressure trend
  pressureHistory[pressureIdx]=bme_pressure;
  pressureIdx=(pressureIdx+1)%10;
  if(pressureIdx==0) pressFull=true;
  float oldest=pressureHistory[pressFull?pressureIdx:0];
  float diff=bme_pressure-oldest;
  if(!pressFull && pressureIdx<3) pressureTrend="Collecting...";
  else if(diff>1.5)  pressureTrend="RISING FAST";
  else if(diff>0.5)  pressureTrend="RISING";
  else if(diff<-1.5) pressureTrend="FALLING FAST";
  else if(diff<-0.5) pressureTrend="FALLING";
  else               pressureTrend="STABLE";

  // Weather
  if(pressureTrend.indexOf("FALLING")>=0 && bme_humidity>70) weatherStatus="RAINY / STORMY";
  else if(pressureTrend.indexOf("RISING")>=0 && bme_humidity<60) weatherStatus="CLEARING UP";
  else if(bme_pressure>912 && bme_humidity<55) weatherStatus="CLEAR / SUNNY";
  else if(bme_pressure>900) weatherStatus="PARTLY CLOUDY";
  else weatherStatus="RAINY / STORMY";
}

// ============================================================
//   MPU6050 RAW I2C
// ============================================================
void mpuWrite(uint8_t reg, uint8_t val){
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(reg); Wire.write(val);
  Wire.endTransmission(true);
}

float readMPUTemp(){
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x41);
  Wire.endTransmission(false); Wire.requestFrom(MPU6050_ADDR, 2, true);
  int16_t raw = Wire.read() << 8 | Wire.read();
  // First sample uses correct 36.53 offset
  float total = (raw / 340.0) + 36.53;
  // Average 9 more samples to reduce noise
  for (int i = 1; i < 10; i++) {
    Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x41);
    Wire.endTransmission(false); Wire.requestFrom(MPU6050_ADDR, 2, true);
    raw = Wire.read() << 8 | Wire.read();
    total += (raw / 340.0) + 26.53;
    delay(10);
  }
  return (total / 10.0) + TEMP_OFFSET;
}

void readMPU6050(){
  Wire.beginTransmission(MPU6050_ADDR); Wire.write(0x3B);
  Wire.endTransmission(false); Wire.requestFrom(MPU6050_ADDR, 14, true);
  int16_t rAx = Wire.read()<<8|Wire.read();
  int16_t rAy = Wire.read()<<8|Wire.read();
  int16_t rAz = Wire.read()<<8|Wire.read();
  Wire.read(); Wire.read();
  int16_t rGx = Wire.read()<<8|Wire.read();
  int16_t rGy = Wire.read()<<8|Wire.read();
  int16_t rGz = Wire.read()<<8|Wire.read();
  ax=rAx/4096.0; ay=rAy/4096.0; az=rAz/4096.0;
  gx=rGx/65.5;   gy=rGy/65.5;   gz=rGz/65.5;
  mpu_temp=readMPUTemp();
  if(az>0.9)       tiltStatus="FLAT (face up)";
  else if(az<-0.9) tiltStatus="UPSIDE DOWN";
  else if(ax>0.9)  tiltStatus="TILTED RIGHT";
  else if(ax<-0.9) tiltStatus="TILTED LEFT";
  else             tiltStatus="ANGLED";
}

// ============================================================
//   MQ-2 ANALOG
// ============================================================
float mqGetRS(){
  long total=0;
  for(int i=0;i<20;i++){ total+=analogRead(MQ2_AO_PIN); delay(5); }
  float raw=total/20.0;
  float v=(raw/4095.0)*3.3;
  if(v<=0) return 0;
  return mq_RL*(3.3-v)/v;
}
float mqPPM(float ratio,float a,float b){ return a*pow(ratio,b); }

void readMQ2(){
  float rs=mqGetRS();
  if(rs<=0||mq_R0<=0) return;
  float ratio=rs/mq_R0;
  mq_lpg   = mqPPM(ratio,574.25,  -2.222);
  mq_smoke = mqPPM(ratio,3616.1,  -2.675);
  mq_co    = mqPPM(ratio,36974.0, -3.109);
  mq_ch4   = mqPPM(ratio,158.63,  -2.285);
  mq_h2    = mqPPM(ratio,987.99,  -2.162);
  mq_digitalAlert=(digitalRead(MQ2_DO_PIN)==LOW);
  if(ratio<0.6)      gasDetected="LPG / Propane";
  else if(ratio<1.0) gasDetected="Smoke";
  else if(ratio<1.5) gasDetected="Alcohol / Hydrogen";
  else if(ratio<3.0) gasDetected="Methane / CO";
  else               gasDetected="Clean Air";
}

// ============================================================
//   MAX30102
// ============================================================
void readMAX30102(){
  if(!max_found) return;
  for(byte i=25;i<BUFFER_LENGTH;i++){
    redBuffer[i-25]=redBuffer[i];
    irBuffer[i-25]=irBuffer[i];
  }
  for(byte i=BUFFER_LENGTH-25;i<BUFFER_LENGTH;i++){
    while(!particleSensor.available()) particleSensor.check();
    uint32_t ir=particleSensor.getIR();
    uint32_t red=particleSensor.getRed();
    redBuffer[i]=red; irBuffer[i]=ir;
    if(checkForBeat(ir)){
      long delta=millis()-lastBeat; lastBeat=millis();
      beatsPerMinute=60.0/(delta/1000.0);
      if(beatsPerMinute>20&&beatsPerMinute<255){
        rates[rateSpot++]=(byte)beatsPerMinute;
        rateSpot%=RATE_SIZE;
        beatAvg=0;
        for(byte x=0;x<RATE_SIZE;x++) beatAvg+=rates[x];
        beatAvg/=RATE_SIZE;
      }
    }
    particleSensor.nextSample();
  }
  maxim_heart_rate_and_oxygen_saturation(
    irBuffer,BUFFER_LENGTH,redBuffer,
    &spo2,&spo2Valid,&heartRate_val,&hrValid);
  fingerPresent=(irBuffer[BUFFER_LENGTH-1]>50000);
}

// ============================================================
//   BUZZER ALERT SYSTEM
// ============================================================
void checkAlerts(){
  activeAlerts="";
  bool alert=false;

  // BME280 alerts
  if(bme_temp>TEMP_HIGH_C){
    activeAlerts+="HIGH TEMP("+String(bme_temp,1)+"C) | "; alert=true;
  }
  if(bme_temp<TEMP_LOW_C && bme_temp>0){
    activeAlerts+="LOW TEMP("+String(bme_temp,1)+"C) | "; alert=true;
  }
  if(bme_humidity>HUMIDITY_HIGH){
    activeAlerts+="HIGH HUMIDITY("+String(bme_humidity,0)+"%) | "; alert=true;
  }
  if(bme_pressure<PRESSURE_LOW && bme_pressure>100){
    activeAlerts+="STORM WARNING | "; alert=true;
  }

  // MQ-2 alerts
  if(mq_smoke>GAS_PPM_DANGER){
    activeAlerts+="SMOKE DANGER("+String(mq_smoke,0)+"ppm) | "; alert=true;
  }
  if(mq_lpg>GAS_PPM_DANGER){
    activeAlerts+="GAS LEAK("+String(mq_lpg,0)+"ppm) | "; alert=true;
  }
  if(mq_digitalAlert){
    activeAlerts+="GAS DIGITAL ALERT | "; alert=true;
  }

  // MAX30102 alerts
  if(fingerPresent && spo2Valid && spo2<SPO2_LOW && spo2>0){
    activeAlerts+="LOW SPO2("+String(spo2)+"%) | "; alert=true;
  }
  if(fingerPresent && beatAvg>HR_HIGH){
    activeAlerts+="HIGH HR("+String(beatAvg)+"bpm) | "; alert=true;
  }
  if(fingerPresent && beatAvg>0 && beatAvg<HR_LOW){
    activeAlerts+="LOW HR("+String(beatAvg)+"bpm) | "; alert=true;
  }

  buzzerActive=alert;

  // Buzzer pattern: beep every 500ms when alert active
  if(alert && millis()-lastBuzzerTime>500){
    digitalWrite(BUZZER_PIN,HIGH); delay(100);
    digitalWrite(BUZZER_PIN,LOW);
    lastBuzzerTime=millis();
  }
}

// ============================================================
//   POST TO MINE GUARD FLASK BACKEND  (/update)
//   Maps this hub's sensors to the schema app.py expects:
//     hr, spo2, bodytemp, co, ch4 (% LEL), smoke, airtemp,
//     humidity, pressure, fall, sos, ax/ay/az/gz
//   Sensors we don't have (h2s, o2, flame) are simply omitted —
//   app.py treats missing fields as "no reading".
// ============================================================
void postToBackend(){
  if(WiFi.status()!=WL_CONNECTED) return;

  // CH4: app.py wants % LEL (LEL of methane = 5% v/v = 50000 ppm) → ppm/500
  float ch4_lel = mq_ch4 / 500.0;

  // Crude fall detection from MPU6050 free-fall (|a| < 0.5 g)
  float aMag = sqrt(ax*ax + ay*ay + az*az);
  bool  fall = (aMag < 0.5);

  // Build JSON payload exactly the way app.py wants it
  String body = "{";
  body += "\"id\":\""    + String(DEVICE_ID)    + "\",";
  body += "\"name\":\""  + String(DEVICE_NAME)  + "\",";
  body += "\"zone\":\""  + String(DEVICE_ZONE)  + "\",";
  body += "\"level\":\"" + String(DEVICE_LEVEL) + "\",";
  body += "\"data\":{";
  // health
  body += "\"hr\":"       + String(fingerPresent && beatAvg>0 ? beatAvg : -1) + ",";
  body += "\"spo2\":"     + String(fingerPresent && spo2Valid ? (int)spo2 : -1) + ",";
  body += "\"bodytemp\":" + String(mpu_temp,1) + ",";        // MPU temp as proxy
  // gases (we only have MQ-2: CO, CH4, smoke; H2S and O2 omitted)
  body += "\"co\":"       + String(mq_co,0)    + ",";
  body += "\"ch4\":"      + String(ch4_lel,2)  + ",";
  body += "\"smoke\":"    + String(mq_smoke,0) + ",";
  // environment
  body += "\"airtemp\":"  + String(bme_temp,1)     + ",";
  body += "\"humidity\":" + String(bme_humidity,1) + ",";
  body += "\"pressure\":" + String(bme_pressure,2) + ",";
  // safety flags
  body += "\"flame\":false,";
  body += "\"fall\":"     + String(fall ? "true" : "false")  + ",";
  body += "\"sos\":false,";
  // raw motion for supervisor view
  body += "\"ax\":" + String(ax,3) + ",";
  body += "\"ay\":" + String(ay,3) + ",";
  body += "\"az\":" + String(az,3) + ",";
  body += "\"gz\":" + String(gz,3);
  body += "}}";

  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type","application/json");
  int code = http.POST(body);
  Serial.printf("→ POST %s : %d\n", BACKEND_URL, code);
  if(code > 0){
    String resp = http.getString();
    if(resp.length() < 200) Serial.println("   resp: " + resp);
  }
  http.end();
}

// ============================================================
//   JSON DATA ENDPOINT
// ============================================================
void handleData(){
  String json="{";
  // MPU6050
  json+="\"mpu\":{";
  json+="\"ax\":"+String(ax,3)+",";
  json+="\"ay\":"+String(ay,3)+",";
  json+="\"az\":"+String(az,3)+",";
  json+="\"gx\":"+String(gx,3)+",";
  json+="\"gy\":"+String(gy,3)+",";
  json+="\"gz\":"+String(gz,3)+",";
  json+="\"temp\":"+String(mpu_temp,2)+",";
  json+="\"tilt\":\""+tiltStatus+"\"";
  json+="},";
  // BME280
  json+="\"bme\":{";
  json+="\"temp\":"+String(bme_temp,2)+",";
  json+="\"humidity\":"+String(bme_humidity,2)+",";
  json+="\"pressure\":"+String(bme_pressure,2)+",";
  json+="\"altitude\":"+String(bme_altitude,2)+",";
  json+="\"weather\":\""+weatherStatus+"\",";
  json+="\"trend\":\""+pressureTrend+"\"";
  json+="},";
  // MQ-2
  json+="\"mq2\":{";
  json+="\"lpg\":"+String(mq_lpg,1)+",";
  json+="\"smoke\":"+String(mq_smoke,1)+",";
  json+="\"co\":"+String(mq_co,1)+",";
  json+="\"ch4\":"+String(mq_ch4,1)+",";
  json+="\"h2\":"+String(mq_h2,1)+",";
  json+="\"digital\":"+String(mq_digitalAlert?"true":"false")+",";
  json+="\"gas\":\""+gasDetected+"\"";
  json+="},";
  // MAX30102
  json+="\"max\":{";
  json+="\"bpm\":"+String(beatsPerMinute,0)+",";
  json+="\"bpmAvg\":"+String(beatAvg)+",";
  json+="\"spo2\":"+String(spo2)+",";
  json+="\"spo2Valid\":"+String(spo2Valid?"true":"false")+",";
  json+="\"finger\":"+String(fingerPresent?"true":"false");
  json+="},";
  // Alerts
  json+="\"alerts\":{";
  json+="\"active\":"+String(buzzerActive?"true":"false")+",";
  json+="\"message\":\""+activeAlerts+"\"";
  json+="}";
  json+="}";
  server.sendHeader("Access-Control-Allow-Origin","*");
  server.send(200,"application/json",json);
}

// ============================================================
//   HTML DASHBOARD
// ============================================================
const char INDEX_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>ESP32 Sensor Dashboard</title>
<link href="https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Orbitron:wght@400;700;900&display=swap" rel="stylesheet">
<style>
  :root{
    --bg:#090e1a;--panel:#0d1526;--border:#1a2a4a;
    --accent:#00f5ff;--accent2:#ff3c6e;--accent3:#39ff14;
    --warn:#ffb800;--text:#c8d8f0;--dim:#4a6080;
  }
  *{margin:0;padding:0;box-sizing:border-box;}
  body{background:var(--bg);color:var(--text);font-family:'Share Tech Mono',monospace;min-height:100vh;padding:20px;}
  body::before{content:'';position:fixed;top:0;left:0;right:0;bottom:0;background:repeating-linear-gradient(0deg,transparent,transparent 2px,rgba(0,245,255,0.01) 2px,rgba(0,245,255,0.01) 4px);pointer-events:none;z-index:0;}

  .header{text-align:center;padding:20px 0 30px;position:relative;z-index:1;}
  .header h1{font-family:'Orbitron',sans-serif;font-size:2rem;font-weight:900;letter-spacing:8px;
    background:linear-gradient(90deg,var(--accent),var(--accent2));-webkit-background-clip:text;-webkit-text-fill-color:transparent;}
  .header p{color:var(--dim);font-size:.8rem;letter-spacing:3px;margin-top:6px;}
  .status-bar{display:flex;justify-content:center;gap:20px;margin-bottom:10px;font-size:.75rem;}
  .status-dot{width:8px;height:8px;border-radius:50%;background:var(--accent3);display:inline-block;margin-right:6px;animation:pulse 1.5s infinite;}
  @keyframes pulse{0%,100%{opacity:1;box-shadow:0 0 6px var(--accent3);}50%{opacity:.4;box-shadow:none;}}

  .alert-bar{background:rgba(255,56,110,.15);border:1px solid var(--accent2);border-radius:8px;
    padding:12px 20px;margin-bottom:20px;font-size:.85rem;color:var(--accent2);
    display:none;position:relative;z-index:1;}
  .alert-bar.active{display:block;animation:alertFlash 1s infinite;}
  @keyframes alertFlash{0%,100%{background:rgba(255,56,110,.15);}50%{background:rgba(255,56,110,.3);}}

  .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:20px;position:relative;z-index:1;}
  .card{background:var(--panel);border:1px solid var(--border);border-radius:12px;padding:20px;
    transition:.3s;position:relative;overflow:hidden;}
  .card::before{content:'';position:absolute;top:0;left:0;right:0;height:2px;}
  .card.mpu::before{background:linear-gradient(90deg,var(--accent),transparent);}
  .card.bme::before{background:linear-gradient(90deg,var(--accent3),transparent);}
  .card.mq2::before{background:linear-gradient(90deg,var(--accent2),transparent);}
  .card.max::before{background:linear-gradient(90deg,#ff69b4,transparent);}
  .card:hover{border-color:var(--accent);transform:translateY(-2px);box-shadow:0 8px 30px rgba(0,245,255,.1);}

  .card-title{font-family:'Orbitron',sans-serif;font-size:.8rem;font-weight:700;letter-spacing:4px;
    margin-bottom:16px;display:flex;align-items:center;gap:10px;}
  .card-icon{font-size:1.2rem;}

  .row{display:flex;justify-content:space-between;align-items:center;
    padding:8px 0;border-bottom:1px solid rgba(255,255,255,.04);}
  .row:last-child{border:none;}
  .label{color:var(--dim);font-size:.78rem;letter-spacing:1px;}
  .value{font-size:.95rem;color:var(--text);}
  .value.accent{color:var(--accent);}
  .value.good{color:var(--accent3);}
  .value.warn{color:var(--warn);}
  .value.danger{color:var(--accent2);}
  .value.pink{color:#ff69b4;}

  .big-val{font-family:'Orbitron',sans-serif;font-size:2.5rem;font-weight:700;
    text-align:center;margin:10px 0;color:var(--accent);}
  .big-val.heart{color:#ff69b4;}

  .bar-wrap{height:8px;background:rgba(255,255,255,.05);border-radius:4px;overflow:hidden;margin:6px 0;}
  .bar-fill{height:100%;border-radius:4px;transition:width .5s ease;background:var(--accent3);}
  .bar-fill.warn{background:var(--warn);}
  .bar-fill.danger{background:var(--accent2);}

  .badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:.7rem;letter-spacing:2px;}
  .badge.good{background:rgba(57,255,20,.15);color:var(--accent3);border:1px solid rgba(57,255,20,.3);}
  .badge.warn{background:rgba(255,184,0,.15);color:var(--warn);border:1px solid rgba(255,184,0,.3);}
  .badge.danger{background:rgba(255,60,110,.15);color:var(--accent2);border:1px solid rgba(255,60,110,.3);}

  .three-col{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px;margin:10px 0;}
  .mini-card{background:rgba(0,0,0,.3);border-radius:8px;padding:10px;text-align:center;}
  .mini-label{font-size:.65rem;color:var(--dim);letter-spacing:1px;margin-bottom:4px;}
  .mini-val{font-size:1rem;color:var(--accent);}

  .heartbeat{display:flex;justify-content:center;align-items:center;gap:6px;margin:8px 0;}
  .heart-icon{font-size:1.5rem;animation:heartbeat 1s infinite;}
  @keyframes heartbeat{0%,100%{transform:scale(1);}50%{transform:scale(1.2);}}

  .finger-msg{text-align:center;color:var(--dim);font-size:.8rem;padding:20px;border:1px dashed var(--border);border-radius:8px;}

  .tilt-viz{text-align:center;font-size:2rem;margin:10px 0;}
  .update-time{text-align:center;color:var(--dim);font-size:.7rem;margin-top:20px;letter-spacing:2px;position:relative;z-index:1;}
</style>
</head>
<body>

<div class="header">
  <h1>ESP32 SENSOR HUB</h1>
  <p>REAL-TIME ENVIRONMENTAL & HEALTH MONITOR</p>
  <div class="status-bar">
    <span><span class="status-dot"></span>LIVE</span>
    <span id="ip-display">CONNECTING...</span>
  </div>
</div>

<div class="alert-bar" id="alertBar">
  🚨 <strong>ALERT:</strong> <span id="alertMsg"></span>
</div>

<div class="grid">

  <!-- BME280 CARD -->
  <div class="card bme">
    <div class="card-title"><span class="card-icon">🌡️</span>BME280 — ENVIRONMENT</div>
    <div class="three-col">
      <div class="mini-card">
        <div class="mini-label">TEMP</div>
        <div class="mini-val" id="bme-temp">--°C</div>
      </div>
      <div class="mini-card">
        <div class="mini-label">HUMIDITY</div>
        <div class="mini-val" id="bme-hum">--%</div>
      </div>
      <div class="mini-card">
        <div class="mini-label">PRESSURE</div>
        <div class="mini-val" id="bme-pres">-- hPa</div>
      </div>
    </div>
    <div class="row">
      <span class="label">ALTITUDE</span>
      <span class="value accent" id="bme-alt">-- m</span>
    </div>
    <div class="row">
      <span class="label">PRESSURE TREND</span>
      <span class="value" id="bme-trend">--</span>
    </div>
    <div class="row">
      <span class="label">WEATHER</span>
      <span class="value" id="bme-weather">--</span>
    </div>
    <div class="row">
      <span class="label">HUMIDITY STATUS</span>
      <span id="bme-hum-status"></span>
    </div>
  </div>

  <!-- MQ-2 CARD -->
  <div class="card mq2">
    <div class="card-title"><span class="card-icon">💨</span>MQ-2 — GAS SENSOR</div>
    <div class="row">
      <span class="label">DETECTED GAS</span>
      <span class="value" id="mq-gas">--</span>
    </div>
    <div class="row">
      <span class="label">DIGITAL ALERT</span>
      <span id="mq-digital">--</span>
    </div>
    <div style="margin-top:12px;">
      <div style="margin-bottom:10px;">
        <div style="display:flex;justify-content:space-between;font-size:.75rem;margin-bottom:3px;">
          <span class="label">LPG</span><span id="mq-lpg-val">-- ppm</span>
        </div>
        <div class="bar-wrap"><div class="bar-fill" id="mq-lpg-bar" style="width:0%"></div></div>
      </div>
      <div style="margin-bottom:10px;">
        <div style="display:flex;justify-content:space-between;font-size:.75rem;margin-bottom:3px;">
          <span class="label">SMOKE</span><span id="mq-smoke-val">-- ppm</span>
        </div>
        <div class="bar-wrap"><div class="bar-fill" id="mq-smoke-bar" style="width:0%"></div></div>
      </div>
      <div style="margin-bottom:10px;">
        <div style="display:flex;justify-content:space-between;font-size:.75rem;margin-bottom:3px;">
          <span class="label">CO</span><span id="mq-co-val">-- ppm</span>
        </div>
        <div class="bar-wrap"><div class="bar-fill" id="mq-co-bar" style="width:0%"></div></div>
      </div>
      <div>
        <div style="display:flex;justify-content:space-between;font-size:.75rem;margin-bottom:3px;">
          <span class="label">CH4 / H2</span><span id="mq-ch4-val">-- ppm</span>
        </div>
        <div class="bar-wrap"><div class="bar-fill" id="mq-ch4-bar" style="width:0%"></div></div>
      </div>
    </div>
  </div>

  <!-- MAX30102 CARD -->
  <div class="card max">
    <div class="card-title"><span class="card-icon">❤️</span>MAX30102 — HEALTH</div>
    <div id="finger-present">
      <div class="heartbeat">
        <span class="heart-icon">❤️</span>
        <div class="big-val heart" id="max-bpm">--</div>
        <span style="color:var(--dim);font-size:.8rem;">BPM</span>
      </div>
      <div class="three-col">
        <div class="mini-card">
          <div class="mini-label">AVG BPM</div>
          <div class="mini-val" id="max-bpm-avg">--</div>
        </div>
        <div class="mini-card">
          <div class="mini-label">SpO2</div>
          <div class="mini-val" id="max-spo2">--%</div>
        </div>
        <div class="mini-card">
          <div class="mini-label">STATUS</div>
          <div class="mini-val" id="max-status">--</div>
        </div>
      </div>
      <div class="bar-wrap" style="margin-top:10px;">
        <div class="bar-fill" id="max-hr-bar" style="width:0%;background:#ff69b4;"></div>
      </div>
    </div>
    <div class="finger-msg" id="finger-absent" style="display:none;">
      👆 Place finger on sensor
    </div>
  </div>

  <!-- MPU6050 CARD -->
  <div class="card mpu">
    <div class="card-title"><span class="card-icon">📐</span>MPU6050 — MOTION</div>
    <div class="tilt-viz" id="tilt-icon">⬜</div>
    <div class="row">
      <span class="label">POSITION</span>
      <span class="value accent" id="mpu-tilt">--</span>
    </div>
    <div class="row">
      <span class="label">TEMPERATURE</span>
      <span class="value" id="mpu-temp">-- °C</span>
    </div>
    <div style="margin-top:12px;">
      <div style="font-size:.7rem;color:var(--dim);letter-spacing:2px;margin-bottom:8px;">ACCELEROMETER (g)</div>
      <div class="three-col">
        <div class="mini-card"><div class="mini-label">X</div><div class="mini-val" id="mpu-ax">--</div></div>
        <div class="mini-card"><div class="mini-label">Y</div><div class="mini-val" id="mpu-ay">--</div></div>
        <div class="mini-card"><div class="mini-label">Z</div><div class="mini-val" id="mpu-az">--</div></div>
      </div>
      <div style="font-size:.7rem;color:var(--dim);letter-spacing:2px;margin:10px 0 8px;">GYROSCOPE (°/s)</div>
      <div class="three-col">
        <div class="mini-card"><div class="mini-label">X</div><div class="mini-val" id="mpu-gx">--</div></div>
        <div class="mini-card"><div class="mini-label">Y</div><div class="mini-val" id="mpu-gy">--</div></div>
        <div class="mini-card"><div class="mini-label">Z</div><div class="mini-val" id="mpu-gz">--</div></div>
      </div>
    </div>
  </div>

</div>

<div class="update-time" id="update-time">WAITING FOR DATA...</div>

<script>
function badge(txt,type){return`<span class="badge ${type}">${txt}</span>`;}
function barColor(pct){return pct>80?'danger':pct>50?'warn':'';}
function setBar(id,val,max){
  const pct=Math.min((val/max)*100,100);
  const el=document.getElementById(id);
  el.style.width=pct+'%';
  el.className='bar-fill'+(pct>80?' danger':pct>50?' warn':'');
}

function update(){
  fetch('/data').then(r=>r.json()).then(d=>{
    // --- ALERTS ---
    const ab=document.getElementById('alertBar');
    if(d.alerts.active){
      ab.classList.add('active');
      document.getElementById('alertMsg').textContent=d.alerts.message;
    } else {
      ab.classList.remove('active');
    }

    // --- BME280 ---
    document.getElementById('bme-temp').textContent=d.bme.temp.toFixed(1)+'°C';
    document.getElementById('bme-hum').textContent=d.bme.humidity.toFixed(0)+'%';
    document.getElementById('bme-pres').textContent=d.bme.pressure.toFixed(1)+' hPa';
    document.getElementById('bme-alt').textContent=d.bme.altitude.toFixed(1)+' m';
    document.getElementById('bme-trend').textContent=d.bme.trend;
    document.getElementById('bme-weather').textContent=d.bme.weather;
    const h=d.bme.humidity;
    document.getElementById('bme-hum-status').innerHTML=
      h<20?badge('TOO DRY','danger'):h>80?badge('TOO HUMID','danger'):h>70?badge('HUMID','warn'):badge('COMFORTABLE','good');

    // --- MQ-2 ---
    document.getElementById('mq-gas').textContent=d.mq2.gas;
    document.getElementById('mq-digital').innerHTML=d.mq2.digital?badge('GAS DETECTED','danger'):badge('NORMAL','good');
    document.getElementById('mq-lpg-val').textContent=d.mq2.lpg.toFixed(0)+' ppm';
    document.getElementById('mq-smoke-val').textContent=d.mq2.smoke.toFixed(0)+' ppm';
    document.getElementById('mq-co-val').textContent=d.mq2.co.toFixed(0)+' ppm';
    document.getElementById('mq-ch4-val').textContent=d.mq2.ch4.toFixed(0)+' ppm';
    setBar('mq-lpg-bar',d.mq2.lpg,3000);
    setBar('mq-smoke-bar',d.mq2.smoke,3000);
    setBar('mq-co-bar',d.mq2.co,3000);
    setBar('mq-ch4-bar',d.mq2.ch4,3000);

    // --- MAX30102 ---
    if(d.max.finger){
      document.getElementById('finger-present').style.display='block';
      document.getElementById('finger-absent').style.display='none';
      document.getElementById('max-bpm').textContent=d.max.bpm>0?d.max.bpm.toFixed(0):'--';
      document.getElementById('max-bpm-avg').textContent=d.max.bpmAvg>0?d.max.bpmAvg:'--';
      document.getElementById('max-spo2').textContent=d.max.spo2Valid?(d.max.spo2+'%'):'--';
      const hr=d.max.bpmAvg;
      const s=hr<40||hr>120?'danger':hr<60||hr>100?'warn':'good';
      document.getElementById('max-status').innerHTML=badge(hr<40?'LOW':hr>120?'HIGH':'OK',s);
      setBar('max-hr-bar',hr,200);
      document.getElementById('max-hr-bar').style.background='#ff69b4';
    } else {
      document.getElementById('finger-present').style.display='none';
      document.getElementById('finger-absent').style.display='block';
    }

    // --- MPU6050 ---
    document.getElementById('mpu-tilt').textContent=d.mpu.tilt;
    document.getElementById('mpu-temp').textContent=d.mpu.temp.toFixed(1)+' °C';
    document.getElementById('mpu-ax').textContent=d.mpu.ax.toFixed(2);
    document.getElementById('mpu-ay').textContent=d.mpu.ay.toFixed(2);
    document.getElementById('mpu-az').textContent=d.mpu.az.toFixed(2);
    document.getElementById('mpu-gx').textContent=d.mpu.gx.toFixed(1);
    document.getElementById('mpu-gy').textContent=d.mpu.gy.toFixed(1);
    document.getElementById('mpu-gz').textContent=d.mpu.gz.toFixed(1);

    const tiltIcons={'FLAT (face up)':'⬛','UPSIDE DOWN':'🔄','TILTED RIGHT':'➡️','TILTED LEFT':'⬅️','ANGLED':'↗️'};
    document.getElementById('tilt-icon').textContent=tiltIcons[d.mpu.tilt]||'❓';

    document.getElementById('update-time').textContent=
      'LAST UPDATE: '+new Date().toLocaleTimeString()+' | AUTO-REFRESH: 2s';

  }).catch(e=>{
    document.getElementById('update-time').textContent='CONNECTION ERROR — RETRYING...';
  });
}

update();
setInterval(update,2000);
</script>
</body>
</html>
)rawhtml";

void handleRoot(){
  server.send_P(200,"text/html",INDEX_HTML);
}

// ============================================================
//   SETUP
// ============================================================
void setup(){
  Serial.begin(115200);
  delay(1000);

  pinMode(BUZZER_PIN,OUTPUT);
  digitalWrite(BUZZER_PIN,LOW);
  pinMode(MQ2_AO_PIN,INPUT);
  pinMode(MQ2_DO_PIN,INPUT);
  pinMode(SDA_PIN,INPUT_PULLUP);
  pinMode(SCL_PIN,INPUT_PULLUP);

  Wire.begin(SDA_PIN,SCL_PIN);
  Wire.setClock(100000);

  Serial.println("==============================");
  Serial.println("  ESP32 COMBINED SENSOR HUB   ");
  Serial.println("==============================");

  // --- BME280 ---
  Serial.print("BME280... ");
  if(bmeFind()){
    bmeWrite(0xE0,0xB6); delay(300);
    bmeCalibration();
    bmeWrite(0xF2,0x01); bmeWrite(0xF4,0x6F); bmeWrite(0xF5,0x90);
    Serial.println("OK at 0x"+String(BME280_ADDR,HEX));
  } else Serial.println("NOT FOUND");

  // --- MPU6050 ---
  Serial.print("MPU6050... ");
  mpuWrite(0x6B,0x00); delay(200);  // wake up, 200ms stable
  mpuWrite(0x1A,0x05);              // DLPF 10Hz — smooth & stable
  mpuWrite(0x1C,0x10);              // Accel ±8g
  mpuWrite(0x1B,0x08);              // Gyro ±500°/s
  Serial.println("OK");

  // --- MAX30102 ---
  Serial.print("MAX30102... ");
  if(particleSensor.begin(Wire,I2C_SPEED_FAST)){
    particleSensor.setup(60,4,2,100,411,4096);
    particleSensor.setPulseAmplitudeRed(0x0A);
    particleSensor.setPulseAmplitudeGreen(0);
    // Fill initial buffer
    for(byte i=0;i<BUFFER_LENGTH;i++){
      while(!particleSensor.available()) particleSensor.check();
      redBuffer[i]=particleSensor.getRed();
      irBuffer[i]=particleSensor.getIR();
      particleSensor.nextSample();
    }
    maxim_heart_rate_and_oxygen_saturation(irBuffer,BUFFER_LENGTH,redBuffer,&spo2,&spo2Valid,&heartRate_val,&hrValid);
    max_found=true;
    Serial.println("OK");
  } else Serial.println("NOT FOUND");

  // --- MQ-2 Warmup & Calibrate ---
  Serial.println("MQ-2 Warming up (20s)...");
  for(int i=20;i>0;i--){ Serial.print(i); Serial.print("s "); delay(1000); }
  Serial.println("\nCalibrating MQ-2...");
  float rsSum=0;
  for(int i=0;i<50;i++){ rsSum+=mqGetRS(); delay(100); }
  mq_R0=(rsSum/50.0)/9.8;
  mq_calibrated=true;
  Serial.print("MQ-2 R0="); Serial.println(mq_R0,4);

  // --- WiFi ---
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int tries=0;
  while(WiFi.status()!=WL_CONNECTED && tries<20){
    delay(500); Serial.print("."); tries++;
  }
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\n✅ WiFi Connected!");
    Serial.print("📡 Dashboard → http://"); Serial.println(WiFi.localIP());
    // Startup beep
    digitalWrite(BUZZER_PIN,HIGH); delay(200);
    digitalWrite(BUZZER_PIN,LOW);  delay(100);
    digitalWrite(BUZZER_PIN,HIGH); delay(200);
    digitalWrite(BUZZER_PIN,LOW);
  } else {
    Serial.println("\n⚠️  WiFi Failed — Running offline");
  }

  server.on("/",handleRoot);
  server.on("/data",handleData);
  server.begin();

  Serial.println("==============================");
  Serial.println("All sensors ready!");
  Serial.println("==============================");
}

// ============================================================
//   LOOP
// ============================================================
void loop(){
  server.handleClient();

  if(millis()-lastSensorUpdate>2000){
    readBME280();
    readMPU6050();
    readMQ2();
    readMAX30102();
    checkAlerts();
    lastSensorUpdate=millis();

    // Serial output
    Serial.println("--- SENSOR UPDATE ---");
    Serial.printf("BME: %.1fC  %.0f%%  %.1fhPa  %s\n",bme_temp,bme_humidity,bme_pressure,weatherStatus.c_str());
    Serial.printf("MPU: ax=%.2f ay=%.2f az=%.2f | %s\n",ax,ay,az,tiltStatus.c_str());
    Serial.printf("MQ2: LPG=%.0f SMOKE=%.0f CO=%.0f | %s\n",mq_lpg,mq_smoke,mq_co,gasDetected.c_str());
    if(fingerPresent) Serial.printf("MAX: BPM=%d SpO2=%d%%\n",beatAvg,spo2);
    else Serial.println("MAX: No finger");
    if(buzzerActive) Serial.println("🚨 ALERT: "+activeAlerts);
  }

  // Push frame to Mine Guard Flask backend (every BACKEND_PERIOD_MS)
  if(millis()-lastBackendSend > BACKEND_PERIOD_MS){
    postToBackend();
    lastBackendSend = millis();
  }
}
