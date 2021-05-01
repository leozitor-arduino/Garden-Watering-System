/*******************************************************************************
* Garden Watering System
*
* turns on/off a solenoid valve for watering system, depending on a time schedule
*
*
* 02 Aug 2020 Leozítor Floro de Souza
*******************************************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <time.h>
#include <ArduinoOTA.h>

#ifndef STASSID
#define STASSID "WiFi House"
#define STAPSK  "casa1234"
#endif

#define __ASSERT_USE_STDERR
#define MAX_TIME_VALVE 360 


const char* ssid = STASSID;
const char* password = STAPSK;

const int timezone = -4 * 3600;
const int dst = 0;

const int max_time = MAX_TIME_VALVE; // limiting the max minutes of valve to keep open each day in minutes

const int pino_led = D4; // pino onde o LED está conectado

// Set Daily Schedule Time for the Valve to be open
struct tm start_t, end_t = {0}; // time struct pointers
double diff_start, diff_end, valve_time;

const int sinkValve = D0; // Relay of Solenoid Sink Valve controlled by pin D1

void setupFirmUpdateOTA() {
  /* ----------  Firmware Update OTA ----------  */
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Wifi Connected Success!");
  Serial.print("NodeMCU IP Address : ");
  Serial.println(WiFi.localIP());
  /* ----------  Firmware Update OTA ----------  */

}

void setupTime(){
  /* ----------  Load Current Time Online ----------  */
  configTime(timezone, dst, "pool.ntp.org","time.nist.gov");
  Serial.println("\nWaiting for Internet time");

  while(!time(nullptr)){
     Serial.print("*");
     delay(1000);
  }
  Serial.println("\nTime response....OK");
}

void printTime(struct tm* p_tm){
   /* ----------  Print Time ----------  */
  Serial.print(p_tm->tm_mday);
  Serial.print("/");
  Serial.print(p_tm->tm_mon + 1);
  Serial.print("/");
  Serial.print(p_tm->tm_year + 1900);

  Serial.print(" ");

  Serial.print(p_tm->tm_hour);
  Serial.print(":");
  Serial.print(p_tm->tm_min);
  Serial.print(":");
  Serial.println(p_tm->tm_sec);
}

struct tm* getTimeNow(){
  /* ----------  Get Current Time ----------  */
  time_t now = time(nullptr);
  struct tm* p_tm = localtime(&now);
  return p_tm;
}

void updateDate(struct tm* a_tm ){
  /* ----------  Update day month and year to current time ----------  */
  struct tm* p_tm = getTimeNow();
  
  a_tm->tm_year = p_tm->tm_year;
  a_tm->tm_mon = p_tm->tm_mon;
  a_tm->tm_mday = p_tm->tm_mday;
}

void updateScheduleDay(){
  /* ----------  Update day month and year of Start and End Schedule to current ----------  */
  struct tm* p_tm = getTimeNow();
  
  updateDate(&start_t);
  updateDate(&end_t);
}

void setup() {

  setupFirmUpdateOTA(); // setup firmware update OTA
  setupTime(); // setup current time
  pinMode(pino_led, OUTPUT); // pino D2
  pinMode(sinkValve, OUTPUT); // pin D0
  digitalWrite(sinkValve, HIGH);

  /* setting schedule time */
  // start schedule
  start_t.tm_hour = 15;
  start_t.tm_min = 8;
  // end schedule
  end_t.tm_hour = 15;
  end_t.tm_min = 9;

  valve_time = difftime(mktime(&end_t), mktime(&start_t)); // lenght in minutes the valve should be open
  if((valve_time > 0) && (valve_time <= max_time)){ // check if start time is less then end time
    Serial.println("Schedule time wrong or above maximum defined");
  }// give that i couldn't use assert
}

void loop() { 
  ArduinoOTA.handle(); // Firmware Update OTA
  updateScheduleDay(); // update day/mont/year of start and end dates
  printTime(getTimeNow()); // print current time
  diff_start = difftime(mktime(&start_t), mktime(getTimeNow())); // difference to open valve
  diff_end = difftime(mktime(&end_t), mktime(getTimeNow())); // difference to close valve
if ((diff_start < 0) && (diff_end > 0)) {
  digitalWrite(sinkValve, LOW); // opens the valve
  digitalWrite(pino_led, LOW); // apaga o LED
  Serial.println("LED Ligado");
}else {
  digitalWrite(sinkValve, HIGH); // opens the valve
  digitalWrite(pino_led, HIGH); // acende o LED
}
delay(1000);
}
