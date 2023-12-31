/**
   bms.ino
    Marco Bergman
    Created on: 10-DEC-2023

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "ESP8266TimerInterrupt.h"
#include <Adafruit_ADS1X15.h>
#include <Adafruit_INA228.h>
#include "ESPTelnet.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// User configuration starts here
String wifiSsid        =  "openplotter";
String wifiPassword    =  "12345678";
String signalkIpString =  "10.10.10.1";
int    signalkUdpPort  =  30330;
String signalkSource   =  "DIY BMS";
int    udpClientSocket =  30330;
int    telnetServerSocket = 23;

// Calibration for the voltage dividers
bool calibrationTime = false;
float referenceVoltage = 10.00;
float value0V[4] = {2, 1, 1, 1};
float valueRef[4] = {14408, 14513, 14550, 14524};

// BMS parameters
float chargeDisconnectVoltage = 3.65;         // LF280K specs Ch. 3.4
float chargeDisconnectSoc = 90;
float chargeDisconnectTemp = 3;
float chargeDisconnectCurrent = 30;
float chargeReconnectVoltage = 3.5;
float chargeReconnectSoc = 80;
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
float dischargeReconnectVoltage = 2.6;
float dischargeReconnectSoc = 30;
float dischargeReconnectTemp = 60;
String dischargeReconnectCurrent = "MANUAL";
float dischargeDisconnectVoltage = 2.5;        // LF280K specs Ch. 3.4
float dischargeDisconnectSoc = 20;
float dischargeDisconnectTemp = 70;
float dischargeDisconnectCurrent = 60;

float calibrationVoltageMax = 3.35;            // LF280K specs Appendix V
float calibrationVoltageMin = 3.16;            // LF280K specs Appendix V
float calibrationHysteresisVoltage = 0.1;
float calibrationSocMax = 100;                 // LF280K specs Appendix V
float calibrationSocMin = 10;                  // LF280K specs Appendix V
float packCapacity = 280; // Ah
float actualDischarge = 0; // Ah
float shuntResistance = 0.000947; // Ohm
float maxShuntCurrent = 80.0;
// User configuration ends here

// For setRelais():
const int CHARGERELAIS = 13; // D7 = GPIO13
const int DISCHARGERELAIS = 15; // D8 = GPI15 'LOAD'
const int CONNECT = 1;
const int DISCONNECT = 0;

const int LED = 2; // GPIO2 =  Wemos onboard LED
const int BUTTON = 12; // GPIO12 = D6 button to ground to reset accumulator
const unsigned int MAX_MESSAGE_LENGTH = 25;

String chargeStatus = "";
String dischargeStatus = "";
String previousBmsStatus = "";

WiFiClient client;
WiFiUDP udp;
ESPTelnet telnet;
Adafruit_ADS1115 ads;
Adafruit_INA228 ina228 = Adafruit_INA228();
OneWire oneWire(0); // D3 = GPIO0
DallasTemperature tempSensors(&oneWire);

const String wifiStatus[8] = {"WL_IDLE_STATUS", "WL_NO_SSID_AVAIL", "unknown", "WL_CONNECTED", "WL_CONNECT_FAILED", "", "WL_CONNECT_WRONG_PASSWORD", "WL_DISCONNECTED"};

IPAddress signalkIp;
bool x = signalkIp.fromString(signalkIpString);

int i = 1; // metadata counter

ESP8266Timer timer;
#define TIMER_INTERVAL_MS 10000  // Call TimerHandler every 10 seconds
int bmsClock = 0;
bool mustSendConfig = false;
bool mustTestWifi = false;
bool mustWakeWifi = false;
bool wifiAsleep = false;
bool telnetStarted = false;
bool capacitySet = false;
unsigned long timeOffset = 0;


void IRAM_ATTR TimerHandler() {
  bmsClock += 1;

  if (bmsClock % 6 == 0) {
    mustTestWifi = true;
  }
  if ((bmsClock + 4) % 5 == 0) {
    mustSendConfig = true;
  }
  if (bmsClock % 6 == 0 && wifiAsleep) {
    mustWakeWifi = true;
  }
}


void bmsPrint (String str) {
  Serial.print(str);
  telnet.print(str);
}

void bmsPrintln (String str) {
  Serial.println(str);
  telnet.println(str);
}

void onTelnetConnect(String ip) {
  telnet.println("\nSimpleBMS Telnet CLI. {parameter=value | [q]uit | [v]alues}");
}

void onTelnetInput(String str) {
  if (processMessage (str)) {
    telnet.disconnectClient();
  };
}


void setup() {
//TEMPORARY: TEST WITH 18650 cell pack
chargeDisconnectVoltage = 4.2;
chargeReconnectVoltage = 4.1;
chargeAlarmVoltage = 4.05;
dischargeAlarmVoltage = 3.75;
dischargeReconnectVoltage = 3.7;
dischargeDisconnectVoltage = 3.6;
calibrationVoltageMax = 4.17;
calibrationVoltageMin = 3.50;
calibrationSocMin = 10;
packCapacity = 2.6; // Ah

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  startWifi();
  udp.begin(udpClientSocket);
  pinMode(DISCHARGERELAIS, OUTPUT);
  pinMode(CHARGERELAIS, OUTPUT);
  pinMode(LED, OUTPUT); 
  pinMode(BUTTON, INPUT_PULLUP); // pulling down D6 resets CHARGE
  ads.setGain(GAIN_TWO);
  ads.begin();

  if (!ina228.begin()) {
    Serial.println("Couldn't find INA228 chip");
    while (1)
      ;
  }
  ina228.setShunt(shuntResistance, maxShuntCurrent);

  tempSensors.begin();
  tempSensors.setResolution(9);
  tempSensors.setWaitForConversion(false);
  
  bool x = timer.attachInterruptInterval(TIMER_INTERVAL_MS * 1000, TimerHandler);

  telnet.onConnect(onTelnetConnect);
  telnet.onInputReceived(onTelnetInput);

  Serial.println("Setup completed");
}


void telnetLoop() {
  if (WiFi.status() == WL_CONNECTED && !telnetStarted) {
    if (telnet.begin(telnetServerSocket)) {
      Serial.println("Telnet listener running");
      telnetStarted = true;
    } else {
      Serial.println("Telnet listener error");
    }
  }
  telnet.loop();
}


void startWifi() {
  Serial.println("\nWifi connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid, wifiPassword);
}


void testWifi() {
  if (wifiAsleep) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    //Serial.print ("Wifi connected with IP address ");
    //Serial.println (WiFi.localIP());
  }
  else if (WiFi.status() == WL_NO_SSID_AVAIL) {
    Serial.println ("Wifi base station not found. Wifi going to sleep to preserve energy.");
    WiFi.setSleepMode (WIFI_MODEM_SLEEP);
    WiFi.forceSleepBegin ();
    wifiAsleep = true;
    telnetStarted = false;
  }
  else {
    Serial.printf ("WIFI Connection status: %d: ", WiFi.status());
    Serial.println (wifiStatus[WiFi.status()]);
    WiFi.printDiag(Serial);
    telnetStarted = false;
  }
}


void wakeWifi() {
  Serial.println ("Wifi waking up");
  WiFi.forceSleepWake();
  wifiAsleep = false;
  bmsClock = 5;
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
  // Serial.println ("Sending BMS config parameters");
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


void sendBmsState(float packSoc, float packDischargeCurrent, String bmsStatus, float packTemp) {
  // send bms status to SignalK
  String value = "{";
  value = value  + "\"packSoc\": " + String(packSoc) +", ";
  value = value  + "\"packDischargeCurrent\": " + String(packDischargeCurrent) +", ";
  value = value  + "\"packTemp\": " + String(packTemp) +", ";
  value = value  + "\"bmsStatus\": \"" + String(bmsStatus) + "\", ";
  value = value  + "\"ipAddress\": \"" + WiFi.localIP().toString() + "\", ";
  value = value  + "\"telnetServerSocket\": " + String(telnetServerSocket) + "";
  value = value + "}";

  String message = "{\"updates\":[{\"$source\": \""+ signalkSource + "\", \"values\":[{\"path\":\"bms.state\",\"value\":" + value + "}]}]}";

  sendSignalkMessage (message);
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


float readPackSoc() {
  float soc;
  actualDischarge = ina228.readCharge();
  soc = (packCapacity - actualDischarge) / packCapacity * 100;
  return soc;
}


float readPackDischargeCurrent() {
  return ina228.readCurrent()/1000; // Convert milli amps to amps
}


void checkCalibration(float minCellVoltage, float maxCellVoltage, float packDischargeCurrent) {
  if (maxCellVoltage > calibrationVoltageMax && packDischargeCurrent > 0) { // not charging
    ina228.resetAcc(); // Reset accumulator registers on the INA228: "reset the on-chip coulomb counter"
    bmsPrintln("Defining pack to be at " + String(calibrationSocMax) + " %.");
  }
  if (minCellVoltage < calibrationVoltageMin && packDischargeCurrent > 0 && !capacitySet) { // not charging
    packCapacity = actualDischarge / (calibrationSocMax - calibrationSocMin) * 100;
    capacitySet = true; 
    bmsPrintln("Setting packCapacity to " + String (packCapacity, 1) + "Ah.");
  }
  if (minCellVoltage > calibrationVoltageMin + calibrationHysteresisVoltage && packDischargeCurrent > 0 && capacitySet) {
    capacitySet = false;
  }
}


float readPackTemp() {
  // temporary, awaiting temp sensors
  // return ina228.readDieTemp();
  float tempC = tempSensors.getTempCByIndex(0); // get current temperature value
  tempSensors.requestTemperaturesByIndex(0); // initiate next conversion, but don't wait for it

  if (tempC != DEVICE_DISCONNECTED_C)
    return tempC;
  else 
    Serial.println("Error: Could not read temperature data");
  
  return 10;
}


float readVoltage(int index) {
  // Read voltage from a particular cell and apply calibration
  int16_t value = ads.readADC_SingleEnded(index);
  float voltage = (value - value0V[index])/(valueRef[index] - value0V[index]) * referenceVoltage;
  if (calibrationTime) {
      bmsPrintln("adc[" + String(index) + "]: " + String(value) + ": " + String(voltage, 3) + "V");
  }
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
      digitalWrite(gpio, LOW);
    }
    else {
      digitalWrite(gpio, HIGH);
    }
}


void blink() {
  // 2 blinks = connected to Wifi, 1 blink = power saving mode
  const int blinkMs = 2;
  digitalWrite(LED, LOW);
  delay (blinkMs);
  digitalWrite(LED, HIGH);
  if (!wifiAsleep){
    delay (100);
    digitalWrite(LED, LOW);
    delay (blinkMs);
    digitalWrite(LED, HIGH);
  }
}


void printCurrentValues() {
  // For CLI purposes:
  //
  bmsPrintln("cdv: chargeDisconnectVoltage: " + String(chargeDisconnectVoltage, 3));
  bmsPrintln("cds: chargeDisconnectSoc: " + String(chargeDisconnectSoc));
  bmsPrintln("cdt: chargeDisconnectTemp: " + String(chargeDisconnectTemp));
  bmsPrintln("cdc: chargeDisconnectCurrent: " + String(chargeDisconnectCurrent));
  bmsPrintln("crv: chargeReconnectVoltage: " + String(chargeReconnectVoltage, 3));
  bmsPrintln("crs: chargeReconnectSoc: " + String(chargeReconnectSoc));
  bmsPrintln("crt: chargeReconnectTemp: " + String(chargeReconnectTemp));
  bmsPrintln("cav: chargeAlarmVoltage: " + String(chargeAlarmVoltage, 3));
  bmsPrintln("cas: chargeAlarmSoc: " + String(chargeAlarmSoc));
  bmsPrintln("cat: chargeAlarmTemp: " + String(chargeAlarmTemp));
  bmsPrintln("cac: chargeAlarmCurrent: " + String(chargeAlarmCurrent));
  bmsPrintln("dav: dischargeAlarmVoltage: " + String(dischargeAlarmVoltage, 3));
  bmsPrintln("das: dischargeAlarmSoc: " + String(dischargeAlarmSoc));
  bmsPrintln("dat: dischargeAlarmTemp: " + String(dischargeAlarmTemp));
  bmsPrintln("dac: dischargeAlarmCurrent: " + String(dischargeAlarmCurrent));
  bmsPrintln("drv: dischargeReconnectVoltage: " + String(dischargeReconnectVoltage, 3));
  bmsPrintln("drs: dischargeReconnectSoc: " + String(dischargeReconnectSoc));
  bmsPrintln("drt: dischargeReconnectTemp: " + String(dischargeReconnectTemp));
  bmsPrintln("ddv: dischargeDisconnectVoltage: " + String(dischargeDisconnectVoltage, 3));
  bmsPrintln("dds: dischargeDisconnectSoc: " + String(dischargeDisconnectSoc));
  bmsPrintln("ddt: dischargeDisconnectTemp: " + String(dischargeDisconnectTemp));
  bmsPrintln("ddc: dischargeDisconnectCurrent: " + String(dischargeDisconnectCurrent));
  bmsPrintln("cvm: calibrationVoltageMax: " + String(calibrationVoltageMax, 3));
  bmsPrintln("cvn: calibrationVoltageMin: " + String(calibrationVoltageMin, 3));	
  bmsPrintln("csm: calibrationHysteresisVoltage: " + String(calibrationHysteresisVoltage));
  bmsPrintln("csm: calibrationSocMax: " + String(calibrationSocMax));
  bmsPrintln("csn: calibrationSocMin: " + String(calibrationSocMin));	
  bmsPrintln("pc: packCapacity: " + String(packCapacity));
  bmsPrintln("ad: actualDischarge: " + String(actualDischarge));
  bmsPrintln("sr: shuntResistance: " + String(shuntResistance, 8));	
  bmsPrintln("ct: calibrationTime: " + String(calibrationTime));
}


bool processMessage(String iMessage) {
  // process message from the cli provider

  String message = iMessage;
  if (message.endsWith("\n")) {
    message.remove(message.length()-1, 1); 
  }

  if (message == "") {
    return false;
  }

  if (message == "q") {
    // quit cli
    return true;
  }

  if (message == "v" || message == "?") {
    printCurrentValues();
    return false;
  }

  int delimiterPos = message.indexOf ("=");
  if (delimiterPos == -1) {
    bmsPrintln("Format: parameter=value");
    return false;
  }

  // split message into parameter and value
  String parameter = message.substring(0, delimiterPos);
  parameter.toLowerCase();
  String value = message.substring(delimiterPos+1);
  bool parameterUnknown = false;
	
  if      (parameter == "cdv") chargeDisconnectVoltage = value.toFloat();
  else if (parameter == "cds") chargeDisconnectSoc = value.toFloat();
  else if (parameter == "cdt") chargeDisconnectTemp = value.toFloat();
  else if (parameter == "crv") chargeReconnectVoltage = value.toFloat();
  else if (parameter == "crs") chargeReconnectSoc = value.toFloat();
  else if (parameter == "crt") chargeReconnectTemp = value.toFloat();
  else if (parameter == "crc") chargeReconnectCurrent = value.toFloat();
  else if (parameter == "cav") chargeAlarmVoltage = value.toFloat();
  else if (parameter == "cas") chargeAlarmSoc = value.toFloat();
  else if (parameter == "cat") chargeAlarmTemp = value.toFloat();
  else if (parameter == "cac") chargeAlarmCurrent = value.toFloat();
  else if (parameter == "dav") dischargeAlarmVoltage = value.toFloat();
  else if (parameter == "das") dischargeAlarmSoc = value.toFloat();
  else if (parameter == "dat") dischargeAlarmTemp = value.toFloat();
  else if (parameter == "dac") dischargeAlarmCurrent = value.toFloat();
  else if (parameter == "drv") dischargeReconnectVoltage = value.toFloat();
  else if (parameter == "drs") dischargeReconnectSoc = value.toFloat();
  else if (parameter == "drt") dischargeReconnectTemp = value.toFloat();
  else if (parameter == "ddv") dischargeDisconnectVoltage = value.toFloat();
  else if (parameter == "dds") dischargeDisconnectSoc = value.toFloat();
  else if (parameter == "ddt") dischargeDisconnectTemp = value.toFloat();
  else if (parameter == "ddc") dischargeDisconnectCurrent = value.toFloat();
  else if (parameter == "cvm") calibrationVoltageMax = value.toFloat();
  else if (parameter == "cvn") calibrationVoltageMin = value.toFloat();
  else if (parameter == "chv") calibrationHysteresisVoltage = value.toFloat();
  else if (parameter == "csm") calibrationSocMax = value.toFloat();
  else if (parameter == "csn") calibrationSocMin = value.toFloat();
  else if (parameter == "pc") packCapacity = value.toFloat();
  else if (parameter == "ad") actualDischarge = value.toFloat();
  else if (parameter == "sr") { shuntResistance = value.toFloat(); ina228.setShunt(shuntResistance, maxShuntCurrent);}
  else if (parameter == "ct") calibrationTime = value.toFloat();
  else if (parameter == "time") setTime(value);
  else parameterUnknown = true;

  if (parameterUnknown) {
    bmsPrintln("Unknown parameter " + parameter + "; type ? for parameters.");
    return false;
  }

  bmsPrintln ("parameter " + parameter + " set to " + value);
  mustSendConfig = true;
  return false;
}


void provideSerialCli() {
  // Stop processing for a while (60 sec timeout) and provide a command line interface to change parameters
  // Activate: any character
  // Arduino IDE Serial Monitor "Carriage Return"
  // Putty Serial: "Local echo: Force on", "Serial Flow Control: None"

  static char message[MAX_MESSAGE_LENGTH];
  static unsigned int message_pos = 0;
  bool finished = false;
  int cliTimer = 0;

  Serial.print("SimpleBMS Serial CLI. {parameter=value | [q]uit | [v]alues} timeout 60s");
  while (!finished) {
    while (Serial.available() > 0) { 
      cliTimer = 0;
      char inByte = Serial.read();

      if ( inByte != '\r' && (message_pos < MAX_MESSAGE_LENGTH - 1) ) {
        message[message_pos] = inByte;
        message_pos++;
      }
      else {
        message[message_pos] = '\0';
        Serial.println("");
        message_pos = 0;
        finished = processMessage(message);
        if (!finished) {
          Serial.print ("> ");
        }
      }
    }
    while (Serial.available() == 0 && ! finished) { 
      cliTimer += 1;
      delay(100);
      if (cliTimer > 600) {
        return;
      }
    }
  }
}


void setTime(String timeString) {
  // set the clock time to timeString by calculating an offset from the millis()
  int hours = timeString.substring(0, 2).toInt();
  int minutes = timeString.substring(3, 5).toInt();
  int seconds = timeString.substring(6, 8).toInt();
  timeOffset = seconds * 1000 + minutes * 60 * 1000 + hours * 60 * 60 * 1000 - millis();
}


String now() {
  // return a string that denotes the current time of day, e.g. "16:48:12"
  unsigned long currentMillis = millis() + timeOffset;
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  char s[25];
  sprintf(s, "%02d:%02d:%02d", hours, minutes, seconds);
  return (String(s));
}


int timestamp = millis();

void T(String comment){
  Serial.print (millis() - timestamp);
  Serial.println (" ms; now at " + comment);
  timestamp = millis();
}


void testIna228() {
  bmsPrintln("Discharge current: " + String (ina228.readCurrent()) + " mA");
  bmsPrintln("Shunt voltage: " + String(ina228.readShuntVoltage(), 6) + " mV");
  bmsPrintln("Charge: " + String(ina228.readCharge(), 8) + " Ah");
  bmsPrintln("Bus voltage: " + String(ina228.readBusVoltage(), 8) + " mV");
}


void loop() {
  const float dampingFactor = 0.8;

  // each measurement takes 32ms:
  float voltageGnd = ina228.readBusVoltage() / 1000; //as the readout is in mV
  float voltage0 = readVoltage(0);
  float voltage1 = readVoltage(1);
  float voltage2 = readVoltage(2);
  float voltage3 = readVoltage(3);

  float cell0Voltage = voltage0 - voltageGnd;
  float cell1Voltage = voltage1 - voltage0;
  float cell2Voltage = voltage2 - voltage1;
  float cell3Voltage = voltage3 - voltage2;

  float maxCellVoltage = maxVoltage(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);
  float minCellVoltage = minVoltage(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);

  float packSoc = readPackSoc();
  float packTemp = readPackTemp();
  float packDischargeCurrent = readPackDischargeCurrent();

  checkCalibration(minCellVoltage, maxCellVoltage, packDischargeCurrent);

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
  if (chargeStatus == "chargeAlarmCurrent" && packDischargeCurrent < chargeAlarmCurrent) {
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
  if (chargeStatus == "" && packDischargeCurrent > chargeAlarmCurrent) {
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
  if (packDischargeCurrent > chargeDisconnectCurrent) {
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
  if (dischargeStatus == "dischargeAlarmCurrent" && packDischargeCurrent > - dischargeAlarmCurrent) {
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
  if (dischargeStatus == "" && packDischargeCurrent < - dischargeAlarmCurrent) {
    dischargeStatus = "dischargeAlarmCurrent";
  }

  // discharge disconnect
  if (minCellVoltage < dischargeDisconnectVoltage) {
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
  if (packDischargeCurrent < - dischargeDisconnectCurrent) {
    dischargeStatus = "dischargeDisconnectCurrent";
    setRelais (DISCHARGERELAIS, DISCONNECT);
  }

  if (chargeStatus.substring(0,16) != "chargeDisconnect") {
    setRelais(CHARGERELAIS, CONNECT);
  }

  if (dischargeStatus.substring(0,19) != "dischargeDisconnect") {
    setRelais(DISCHARGERELAIS, CONNECT);
  }

  String bmsStatus = chargeStatus + " " + dischargeStatus;

  if (bmsStatus != previousBmsStatus) {
    bmsPrintln(now() + " " + bmsStatus + " " + String(cell0Voltage, 3) + " " + String(cell1Voltage, 3) + " " + String(cell2Voltage, 3) + " " + String(cell3Voltage, 3));
    previousBmsStatus = bmsStatus;
  }

  // Serial.println (now() + " adc[0] = " + String(cell0Voltage, 6) + "; bmsStatus = " + bmsStatus); // + "\r"

  sendCellVoltages(cell0Voltage, cell1Voltage, cell2Voltage, cell3Voltage);

  sendBmsState(packSoc, packDischargeCurrent, bmsStatus, packTemp);

  if (mustSendConfig) {
    sendBmsConfig();
    mustSendConfig = false;
  }
  if (mustTestWifi) {
    testWifi();
    mustTestWifi = false;
  }
  if (mustWakeWifi) {
    wakeWifi();
    mustWakeWifi = false;
  }

  if (calibrationTime) {
    testIna228();
  }

  blink();

  if (! digitalRead(BUTTON)) { // D6 low = 
    ina228.resetAcc();
    bmsPrintln("Resetting accumulators");
  }

  if (Serial.available() > 0) {
    provideSerialCli();
  }
  
  for (int i = 0; i < 10; i++) {
    telnetLoop();
    delay(100);
  }


}
