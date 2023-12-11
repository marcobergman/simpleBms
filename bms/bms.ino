/**
   bms.ino
    Marco Bergman
    Created on: 10-DEC-2023

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "ESP8266TimerInterrupt.h"             //https://github.com/khoih-prog/ESP8266TimerInterrupt
#include <Adafruit_ADS1X15.h>

// User configuration starts here
String wifiSsid        =  "openplotter";
String wifiPassword    =  "Pandan123!";
String signalkIpString =  "10.10.10.1";
int    signalkUdpPort  =  30330;
String signalkSource   =  "DIY BMS";

// Calibration for the voltage dividers
float referenceVoltage = 10.00;
float value0V[4] = {3, 3, 3, 3};
float valueRef[4] = {14161, 14161, 14161, 14161};

// BMS parameters
float chargeDisconnectVoltage = 3.6;
float chargeDisconnectSoc = 90;
float chargeDisconnectTemp = 3;
float chargeDisconnectCurrent = 30;
float chargeReconnectVoltage = 3.5;
float chargeReconnectSoc = 30;
float chargeReconnectTemp = 4;
String chargeReconnectCurrent = "MANUAL";
float chargeAlarmVoltage = 3.4;
float chargeAlarmSoc = 85;
float chargeAlarmTemp = 5;
float chargeAlarmCurrent = 25;
float dischargeAlarmVoltage = 3.25;
float dischargeAlarmSoc = 25;
float dischargeAlarmTemp = 50;
float dischargeAlarmCurrent = 50;
float dischargeReconnectVoltage = 3.2;
float dischargeReconnectSoc = 30;
float dischargeReconnectTemp = 60;
String dischargeReconnectCurrent = "MANUAL";
float dischargeDisconnectVoltage = 3.1;
float dischargeDisconnectSoc = 20;
float dischargeDisconnectTemp = 70;
float dischargeDisconnectCurrent = 60;
// User configuration ends here

// For setRelais():
const int CHARGERELAIS = 0; // D3 = GPIO0
const int DISCHARGERELAIS = 2; // D4 = GPIO2 'LOAD'
const int CONNECT = 1;
const int DISCONNECT = 0;

String bmsStatus = "OK";
String chargeStatus = "";
String dischargeStatus = "";

float cell0Voltage = 0;
float cell1Voltage = 0;
float cell2Voltage = 0;
float cell3Voltage = 0;

WiFiClient client;
WiFiUDP udp;
const String wifiStatus[8] = {"WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "unknown", "WL_CONNECTED", "WL_CONNECT_FAILED", "", "WL_CONNECT_WRONG_PASSWORD", "WL_DISCONNECTED"};

Adafruit_ADS1115 ads;

ESP8266Timer timer;
#define TIMER_INTERVAL_MS 20000

IPAddress signalkIp;
bool x = signalkIp.fromString(signalkIpString);

int i = 1; // metadata counter

int mustSendConfig = 1;


void startWifi() {
  Serial.println("\nWifi connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPassword);
}


void sendSignalkMessage (String message) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  //Serial.println(message);
  udp.beginPacket(signalkIp, signalkUdpPort);
  udp.write(message.c_str());
  udp.endPacket();  
}


void sendBmsConfig () {
  // send configuration parameters to SignalK
  String value = "{";
  value = value  + "\"chargeDisconnectVoltage\": " + String(chargeDisconnectVoltage) +", ";
  value = value  + "\"chargeDisconnectSoc\": " + String(chargeDisconnectSoc) +", ";
  value = value  + "\"chargeDisconnectTemp\": " + String(chargeDisconnectTemp) +", ";
  value = value  + "\"chargeDisconnectCurrent\": " + String(chargeDisconnectCurrent) +", ";
  value = value  + "\"chargeReconnectVoltage\": " + String(chargeReconnectVoltage) +", ";
  value = value  + "\"chargeReconnectSoc\": " + String(chargeReconnectSoc) +", ";
  value = value  + "\"chargeReconnectTemp\": " + String(chargeReconnectTemp) +", ";
  value = value  + "\"chargeReconnectCurrent\": \"" + String(chargeReconnectCurrent) +"\", ";
  value = value  + "\"chargeAlarmVoltage\": " + String(chargeAlarmVoltage) +", ";
  value = value  + "\"chargeAlarmSoc\": " + String(chargeAlarmSoc) +", ";
  value = value  + "\"chargeAlarmTemp\": " + String(chargeAlarmTemp) +", ";
  value = value  + "\"chargeAlarmCurrent\": " + String(chargeAlarmCurrent) +", ";
  value = value  + "\"dischargeAlarmVoltage\": " + String(dischargeAlarmVoltage) +", ";
  value = value  + "\"dischargeAlarmSoc\": " + String(dischargeAlarmSoc) +", ";
  value = value  + "\"dischargeAlarmTemp\": " + String(dischargeAlarmTemp) +", ";
  value = value  + "\"dischargeAlarmCurrent\": " + String(dischargeAlarmCurrent) +", ";
  value = value  + "\"dischargeReconnectVoltage\": " + String(dischargeReconnectVoltage) +", ";
  value = value  + "\"dischargeReconnectSoc\": " + String(dischargeReconnectSoc) +", ";
  value = value  + "\"dischargeReconnectTemp\": " + String(dischargeReconnectTemp) +", ";
  value = value  + "\"dischargeReconnectCurrent\": \"" + String(dischargeReconnectCurrent) +"\", ";
  value = value  + "\"dischargeDisconnectVoltage\": " + String(dischargeDisconnectVoltage) +", ";
  value = value  + "\"dischargeDisconnectSoc\": " + String(dischargeDisconnectSoc) +", ";
  value = value  + "\"dischargeDisconnectTemp\": " + String(dischargeDisconnectTemp) +", ";
  value = value  + "\"dischargeDisconnectCurrent\": " + String(dischargeDisconnectCurrent);
  value = value + "}";

  String message = "{\"updates\":[{\"$source\": \""+ signalkSource + "\", \"values\":[{\"path\":\"bms.config\",\"value\":" + value + "}]}]}";

  sendSignalkMessage (message);
}


void sendBmsState(float packSoc, float packCurrent, String bmsStatus, float packTemp) {
  // send bsm status to SignalK
  String value = "{";
  value = value  + "\"packSoc\": " + String(packSoc) +", ";
  value = value  + "\"packCurrent\": " + String(packCurrent) +", ";
  value = value  + "\"packTemp\": " + String(packTemp) +", ";
  value = value  + "\"bmsStatus\": \"" + String(bmsStatus) + "\"";
  value = value + "}";

  String message = "{\"updates\":[{\"$source\": \""+ signalkSource + "\", \"values\":[{\"path\":\"bms.state\",\"value\":" + value + "}]}]}";

  sendSignalkMessage (message);
}


void reportWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print ("Wifi connected with IP address ");
    Serial.println (WiFi.localIP());
  }
  else {
    Serial.printf ("WIFI Connection status: %d: ", WiFi.status());
    Serial.println (wifiStatus[WiFi.status()]);
    WiFi.printDiag(Serial);
  }
}


void signalkSendValue (String path, String value, String units) {
  // send one particular value to signalk. Every now and then, set the 'units' metadata.
  String message = "{\"updates\":[{\"$source\": \""+ signalkSource + "\", \"values\":[{\"path\":\"" + path + "\",\"value\":" + value + "}]}]}";

  i -= 1; if (i == 0) {  // to start, then periodically, update the 'units' metadata:
    message = "{\"updates\":[{\"meta\":[{\"path\":\"" + path + "\",\"value\":{\"units\":\"" + units + "\"}}]}]}";
    i = 99;
  }
  sendSignalkMessage (message);
}


void IRAM_ATTR TimerHandler() {
  mustSendConfig = 1;
}


void setup() {
  Serial.begin(115200);
  startWifi();
  udp.begin(33333);
  pinMode(DISCHARGERELAIS, OUTPUT);
  pinMode(CHARGERELAIS, OUTPUT);
  ads.setGain(GAIN_TWO);
  ads.begin();
  cell0Voltage = readCellVoltage(0);
  cell1Voltage = readCellVoltage(1);
  cell2Voltage = readCellVoltage(2);
  cell3Voltage = readCellVoltage(3);

  bool x = timer.attachInterruptInterval(TIMER_INTERVAL_MS * 1000, TimerHandler);
}


float interpolate (float cellVoltage, float x, float y, float a, float b) {
  // temporary, see readPckSoc()
  float d=y-x;
  float e=b-a;
  return ((cellVoltage - x) / d) * e + a;
}


float readPackSoc(float cellVoltage) {
  // temporary. Since I don't have the power management chip yet, I derive the SOC from the cell0 voltage:
  float soc = 0;

  if (cellVoltage < 3.65 && cellVoltage > 3.61) {soc=interpolate(cellVoltage, 3.61, 3.65, 99, 100);}
  if (cellVoltage < 3.61 && cellVoltage > 3.46) {soc=interpolate(cellVoltage, 3.46, 3.61, 95, 99);}
  if (cellVoltage < 3.46 && cellVoltage > 3.32) {soc=interpolate(cellVoltage, 3.32, 3.46, 90, 95);}
  if (cellVoltage < 3.32 && cellVoltage > 3.31) {soc=interpolate(cellVoltage, 3.31, 3.32, 80, 90);}
  if (cellVoltage < 3.31 && cellVoltage > 3.3) {soc=interpolate(cellVoltage, 3.3, 3.31, 70, 80);}
  if (cellVoltage < 3.3 && cellVoltage > 3.29) {soc=interpolate(cellVoltage, 3.29, 3.3, 60, 70);}
  if (cellVoltage < 3.29 && cellVoltage > 3.28) {soc=interpolate(cellVoltage, 3.28, 3.29, 50, 60);}
  if (cellVoltage < 3.28 && cellVoltage > 3.27) {soc=interpolate(cellVoltage, 3.27, 3.28, 40, 50);}
  if (cellVoltage < 3.27 && cellVoltage > 3.25) {soc=interpolate(cellVoltage, 3.25, 3.27, 30, 40);}
  if (cellVoltage < 3.25 && cellVoltage > 3.22) {soc=interpolate(cellVoltage, 3.22, 3.25, 20, 30);}
  if (cellVoltage < 3.22 && cellVoltage > 3.2) {soc=interpolate(cellVoltage, 3.2, 3.22, 17, 20);}
  if (cellVoltage < 3.2 && cellVoltage > 3.12) {soc=interpolate(cellVoltage, 3.12, 3.2, 14, 17);}
  if (cellVoltage < 3.12 && cellVoltage > 3) {soc=interpolate(cellVoltage, 3, 3.12, 9, 14);}
  if (cellVoltage < 3 && cellVoltage > 2.5) {soc=interpolate(cellVoltage, 2.5, 3, 0, 9);}

  return soc;
}

float readPackTemp() {
  // temporary, awaiting temp sensors
  return 15;
}

float readPackCurrent() {
  // temporary, awaiting power management chip
  return 3.55;
}

float readCellVoltage(int index) {
  // Read voltage from a particular cell and apply calibration
  int16_t value = ads.readADC_SingleEnded(index);
  float voltage =  (value - value0V[index])/(valueRef[index] - value0V[index]) * referenceVoltage;
  //Serial.println("adc[" + String(index) + "]: " + String(value) + ": " + String(voltage, 3) + "V");
  return voltage;
}

float maxVoltage (float a, float b, float c, float d) {
  float m = max(a, b);
  m = max (m, c);
  m = max (m, d);
  return m;
}

float minVoltage (float a, float b, float c, float d) {
  float m = min(a, b);
  m = min (m, c);
  m = min (m, d);
  return m;
}

void sendCellVoltages(float cell0Voltage, float cell1Voltage, float cell2Voltage, float cell3Voltage) {
  signalkSendValue("cell0.voltage", String(cell0Voltage, 3), "V");
  signalkSendValue("cell1.voltage", String(cell1Voltage, 3), "V");
  signalkSendValue("cell2.voltage", String(cell2Voltage, 3), "V");
  signalkSendValue("cell3.voltage", String(cell3Voltage, 3), "V");
}


void setRelais (int gpio, int value) {
  // Connect or disconnect charge or load relais
    if (value == CONNECT) {
      digitalWrite(gpio, HIGH);
    }
    else {
      digitalWrite(gpio, LOW);
    }
  //Serial.println ("setRelais: " + String(gpio) + ": " + String(value));
}

int timestamp = millis();

void T(String comment){
  Serial.print (millis() - timestamp);
  Serial.println (" ms; now at " + comment);
  timestamp = millis();
}

void loop() {

  // each measurement takes 32ms:
  cell0Voltage = 0.9 * cell0Voltage + 0.1 * readCellVoltage(0);
  cell1Voltage = 0.9 * cell1Voltage + 0.1 * readCellVoltage(1);
  cell2Voltage = 0.9 * cell2Voltage + 0.1 * readCellVoltage(2);
  cell3Voltage = 0.9 * cell3Voltage + 0.1 * readCellVoltage(3);

  // for now, only 1 cell ;-)
  //cell3Voltage = cell3Voltage - cell2Voltage;
  //cell2Voltage = cell2Voltage - cell1Voltage;
  //cell1Voltage = cell1Voltage - cell0Voltage;

  float maxCellVoltage = maxVoltage(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);
  float minCellVoltage = minVoltage(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);

  float packSoc = readPackSoc(cell0Voltage);  // Temporarily provide cell voltage to fake presence of power management chip
  float packTemp = readPackTemp();
  float packCurrent = readPackCurrent();

  // Charge reconnect
  if (chargeStatus == "chargeDisconnectVoltage" && maxCellVoltage < chargeReconnectVoltage ) {
    chargeStatus = "";
  }
  if (chargeStatus == "chargeDisconnectSoc" && packSoc < chargeReconnectSoc) {
    chargeStatus = "";
  }
  if (chargeStatus == "chargeDisconnectTemp" && packTemp > chargeReconnectTemp) {
    chargeStatus = "";
  }
  //if (chargeStatus == "chargeDisconnectCurrent" && packCurrent < chargeReconnectCurrent) {
  //   chargeStatus = "";
  //}

  // Disable charge alarms
  if (chargeStatus == "chargeAlarmVoltage" && maxCellVoltage < chargeAlarmVoltage) {
    chargeStatus = "";
  }
  if (chargeStatus == "chargeAlarmSoc" && packSoc < chargeAlarmSoc) {
    chargeStatus = "";
  }
  if (chargeStatus == "chargeAlarmTemp" && packTemp > chargeAlarmTemp) {
    chargeStatus = "";
  }
  if (chargeStatus == "chargeAlarmCurrent" && packCurrent < chargeAlarmCurrent) {
    chargeStatus = "";
  }

  // Charge alarms
  if (chargeStatus == "" && maxCellVoltage > chargeAlarmVoltage) {
    chargeStatus = "chargeAlarmVoltage";
  }
  if (chargeStatus == "" && packSoc > chargeAlarmSoc) {
    chargeStatus = "chargeAlarmSoc";
  }
  if (chargeStatus == "" && packTemp < chargeAlarmTemp) {
    chargeStatus = "chargeAlarmTemp";
  }
  if (chargeStatus == "" && packCurrent > chargeAlarmCurrent) {
    chargeStatus = "chargeAlarmCurrent";
  }

  // Charge disconnect
  if (maxCellVoltage > chargeDisconnectVoltage) {
    chargeStatus = "chargeDisconnectVoltage";
    setRelais (CHARGERELAIS, DISCONNECT);
  }
  if (packSoc > chargeDisconnectSoc) {
    chargeStatus = "chargeDisconnectSoc";
    setRelais (CHARGERELAIS, DISCONNECT);
  }
  if (packTemp < chargeDisconnectTemp) {
    chargeStatus = "chargeDisconnectTemp";
    setRelais (CHARGERELAIS, DISCONNECT);
  }
  if (packCurrent > chargeDisconnectCurrent) {
    chargeStatus = "chargeDisconnectCurrent";
    setRelais (CHARGERELAIS, DISCONNECT);
  }


  // Discharge reconnect
  if (dischargeStatus == "dischargeDisconnectVoltage" && minCellVoltage > dischargeReconnectVoltage ) {
    dischargeStatus = "";
  }
  if (dischargeStatus == "dischargeDisconnectSoc" && packSoc > dischargeReconnectSoc) {
    dischargeStatus = "";
  }
  if (dischargeStatus == "dischargeDisconnectTemp" && packTemp < dischargeReconnectTemp) {
    dischargeStatus = "";
  }
  //if (dischargeStatus == "dischargeDisconnectCurrent" && packCurrent > - dischargeReconnectCurrent) {
  //  dischargeStatus = "";
  //}

  // Disable discharge  alarms
  if (dischargeStatus == "dischargeAlarmVoltage" && maxCellVoltage > dischargeAlarmVoltage) {
    dischargeStatus = "";
  }
  if (dischargeStatus == "dischargeAlarmSoc" && packSoc > dischargeAlarmSoc) {
    dischargeStatus = "";
  }
  if (dischargeStatus == "dischargeAlarmTemp" && packTemp < dischargeAlarmTemp) {
    dischargeStatus = "";
  }
  if (dischargeStatus == "dischargeAlarmCurrent" && packCurrent > - dischargeAlarmCurrent) {
    dischargeStatus = "";
  }

  // Discharge alarms
  if (dischargeStatus == "" && maxCellVoltage < dischargeAlarmVoltage) {
    dischargeStatus = "dischargeAlarmVoltage";
  }
  if (dischargeStatus == "" && packSoc < dischargeAlarmSoc) {
    dischargeStatus = "dischargeAlarmSoc";
  }
  if (dischargeStatus == "" && packTemp > dischargeAlarmTemp) {
    dischargeStatus = "dischargeAlarmTemp";
  }
  if (dischargeStatus == "" && packCurrent < - dischargeAlarmCurrent) {
    dischargeStatus = "dischargeAlarmCurrent";
  }

  // discharge disconnect
  if (maxCellVoltage < dischargeDisconnectVoltage) {
    dischargeStatus = "dischargeDisconnectVoltage";
    setRelais (DISCHARGERELAIS, DISCONNECT);
  }
  if (packSoc < dischargeDisconnectSoc) {
    dischargeStatus = "dischargeDisconnectSoc";
    setRelais (DISCHARGERELAIS, DISCONNECT);
  }
  if (packTemp > dischargeDisconnectTemp) {
    dischargeStatus = "dischargeDisconnectTemp";
    setRelais (DISCHARGERELAIS, DISCONNECT);
  }
  if (packCurrent < - dischargeDisconnectCurrent) {
    dischargeStatus = "dischargeDisconnectCurrent";
    setRelais (DISCHARGERELAIS, DISCONNECT);
  }

  if (chargeStatus.substring(0,16) != "chargeDisconnect") {
    setRelais(CHARGERELAIS, CONNECT);
  }

  if (dischargeStatus.substring(0,19) != "dischargeDisconnect") {
    setRelais(DISCHARGERELAIS, CONNECT);
  }

  bmsStatus = chargeStatus + dischargeStatus;

  Serial.println ("adc[0] = " + String(cell0Voltage, 6) + "; bmsStatus = " + bmsStatus);

  sendCellVoltages(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);

  sendBmsState(packSoc, packCurrent, bmsStatus, packTemp);

  if (mustSendConfig == 1) {
    sendBmsConfig();
    reportWifi();
    mustSendConfig = 0;
  }

  delay(1000);
}
