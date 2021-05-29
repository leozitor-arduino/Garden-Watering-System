#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FirebaseESP8266.h>
#include <WiFiUdp.h>
#include <time.h>
#include <ArduinoOTA.h>

#ifndef STASSID
#define STASSID "<WIFI NAME>"
#define STAPSK  "<PASSWORD>"
#endif

#define __ASSERT_USE_STDERR
#define MAX_TIME_VALVE 120 // emergency stop in minutes
#define DB_REFRESH 1 // TIME WHICH DB WILL UPDATE


const char *ssid = STASSID;
const char *password = STAPSK;

const int timezone = -4 * 3600;
const int dst = 0;

const int max_time = MAX_TIME_VALVE * 60; // limiting the max minutes of valve to keep open each day in minutes converted
const int db_refresh = DB_REFRESH * 60000; // limiting the max minutes of valve to keep open each day in minutes converted
bool valve_active = false; // emergency breaking valve system

const int sink_valve = D0; // Pin Sink Valve is connected
unsigned long sendDataPrevMillis = 0;  // data prev

// Set Daily Schedule Time for the Valve to be open
struct tm start_t, end_t = {0}; // time struct pointers
double diff_start, diff_end, valve_time;

/* ---- config for Firebase --- */
//Provide the token generation process info.
#include "addons/TokenHelper.h"
//Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

/* API Key */
#define API_KEY "<DB_KEY>"

/* RTDB URL */
#define DATABASE_URL "<DB_URL>" //<databaseName>.firebaseio.com or <databaseName>.<region>.firebasedatabase.app

/* 4. Define the user Email and password that alreadey registerd or added in your project */
#define USER_EMAIL "<USER_EMAIL>"
#define USER_PASSWORD "<PASSWORD>"

//Define Firebase Data object
FirebaseData fbdo;

FirebaseAuth auth;
FirebaseConfig config;
/* ---- config for Firebase --- */

/* ----------  Firmware Update OTA ----------  */
void setupFirmUpdateOTA() {
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
}
/* ----------  Firmware Update OTA ----------  */

/* ---- setup Firebase --- */
void setupFirebase() {
    Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
    /* Assign the api key (required) */
    config.api_key = API_KEY;

    /* Assign the user sign in credentials */
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    /* Assign the RTDB URL (required) */
    config.database_url = DATABASE_URL;

    /* Assign the callback function for the long running token generation task */
    config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

    Firebase.begin(&config, &auth);
}

/* ---- setup Firebase --- */

void setupTime() {
    /* ----------  Load Current Time Online ----------  */
    configTime(timezone, dst, "pool.ntp.org", "time.nist.gov");
    Serial.println("\nWaiting for Internet time");

    while (!time(nullptr)) {
        Serial.print("*");
        delay(1000);
    }
    Serial.println("\nTime response....OK");
}

    /* ----------  Print Time ----------  */
void printTime(struct tm *p_tm) {
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

/* ----------  Get Current Time ----------  */
struct tm *getTimeNow() {

    time_t now = time(nullptr);
    struct tm *p_tm = localtime(&now);
    return p_tm;
}

    /* ----------  Update day month and year to current time ----------  */
void updateDate(struct tm *a_tm) {

    struct tm *p_tm = getTimeNow();

    a_tm->tm_year = p_tm->tm_year;
    a_tm->tm_mon = p_tm->tm_mon;
    a_tm->tm_mday = p_tm->tm_mday;
}

    /* ----------  Update day month and year of Start and End Schedule to current ----------  */
void updateScheduleDay() {
    struct tm *p_tm = getTimeNow();

    updateDate(&start_t);
    updateDate(&end_t);
}

void valveSafetyCheck(){
    valve_time = difftime(mktime(&end_t), mktime(&start_t)); // lenght in minutes the valve should be open
    if ((valve_time > 0) && (valve_time <= max_time)) { // check if start time is less then end time
        valve_active = true;
    } else {
      Serial.println("Schedule time wrong or above maximum defined, valve Deactivated");
      valve_active = false;
    }
    //add case the year is below 2000, that is not get current time from internet
}

void valveOpeningSystem() {
      diff_start = difftime(mktime(&start_t), mktime(getTimeNow())); // difference to open valve
      diff_end = difftime(mktime(&end_t), mktime(getTimeNow())); // difference to close valve
//      printf("Seconds left to Open Valve = %f\n", diff_start);
//      printf("Seconds left to Close Valve = %f\n", diff_end);
      if ((diff_start < 0) && (diff_end > 0) && valve_active) {
          digitalWrite(sink_valve, LOW); // apaga o LED
          Serial.println("Torneira aberta");
      } else {
          digitalWrite(sink_valve, HIGH); // acende o LED
      }
}

void setup() {
    setupFirmUpdateOTA(); // setup firmware update OTA
    setupTime(); // setup current time
    setupFirebase(); // Setup firebase connection
    pinMode(sink_valve, OUTPUT); // pino 13
    digitalWrite(sink_valve, HIGH); // close valve
    // start schedule
    start_t.tm_hour = 17;
    start_t.tm_min = 00;
    // end schedule
    end_t.tm_hour = 18;
    end_t.tm_min = 00;
}


void loop() {
    ArduinoOTA.handle(); // Firmware Update OTA
    updateScheduleDay(); // update day/mont/year of start and end dates
    valveSafetyCheck();
    valveOpeningSystem();
//    printTime(getTimeNow()); // print current time
//    printTime(&start_t);
//    printTime(&end_t);
    if (Firebase.ready() && (millis() - sendDataPrevMillis > db_refresh || sendDataPrevMillis == 0)){
      Serial.print("Current Time: ");
      printTime(getTimeNow()); // print current time
      Serial.print("Start Time: ");
      printTime(&start_t);
      Serial.print("End Time: ");
      printTime(&end_t);
        sendDataPrevMillis = millis();
        start_t.tm_hour = Firebase.getInt(fbdo, "/valve_schedule/start_t/hour") ? fbdo.intData() : 0;
        start_t.tm_min = Firebase.getInt(fbdo, "/valve_schedule/start_t/min") ? fbdo.intData() : 0;

        // end schedule
        end_t.tm_hour = Firebase.getInt(fbdo, "/valve_schedule/end_t/hour") ? fbdo.intData() : 0;
        end_t.tm_min = Firebase.getInt(fbdo, "/valve_schedule/end_t/min") ? fbdo.intData() : 0;
        
        // setting vale active
        Firebase.setBool(fbdo,  "/valve_active", valve_active);
    }
}
