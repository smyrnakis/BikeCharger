#include <Arduino.h>
#include <math.h>
#include <SoftwareSerial.h>

#include <WiFiUdp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
// #include <WiFiManager.h>
// #include <WiFiClientSecure.h>
#include <ESP8266HTTPClient.h>

#include "DHT.h"

#include "secrets.h"

#define DHTTYPE DHT11
#define DHTPIN D1

#define RX_PIN D2
#define TX_PIN D3

#define ESPLED D4       // 2
#define PCBLED D0       // 16 , LED_BUILTIN
#define ROAD_LED D5

#define REED_SENSOR D8


char access_SSID[] = ACCESS_POINT_SSID;
char access_PASS[] = ACCESS_POINT_PASS;


const unsigned long interval_LED                = 1000;
const unsigned long interval_DHT11              = 5000;
const unsigned long interval_minimum_revolution = 2500;
const unsigned long interval_ThingSpeakUpload   = 15000;

unsigned long lastRevolutionTime    = 0;
unsigned long lastLEDblinkTime      = 0;
unsigned long lastUploadTime        = 0;
unsigned long lastDHT11Time         = 0;


const wheel_radius = 40;            // in CM

float humidity;
float temperature;

bool underOperation = false;

int data_rpm                    = 0;
int data_usageTime              = 0;
unsigned long startTime         = 0;
unsigned long stopTime          = 0;
int calc_bicycleSpeed;          = 0;    // average (total distance / usage time) OR momentary (rpm to kpm)
int calc_energyProduced         = 0;
int calc_distanceTravelled;     = 0;    // we need total rpm per ride
unsigned long rideRevolutions   = 0;


// Software serial used to communicate with 2nd ESP8266 (internet_relay)
SoftwareSerial Serial_internet_relay(RX_PIN, TX_PIN);

ESP8266WebServer server(80);

DHT dht(DHTPIN, DHTTYPE);


float circumference_CM(int radiusCM) {                                          // output in CM
    return 2.0 * M_PI * radius;
}


float kilometers_per_hour(int rpm, float circumferenceCM) {
    return circumference * rpm * 60.0 / 1000.0;
}


int usage_seconds(unsigned long startMILLIS, unsigned long stopMILLIS) {
    return (stop - start) / 1000;
}


float distance_travelled_KM(unsigned long totalRev, float circumferenceCM) {    // output in KM
    return totalRev * circumference / 1000.0;
}


float average_speed_KPH(float distanceKM, int timeSEC) {                        // output KPH
    return distanceKM / (timeSEC / 60.0 / 60.0);
}


// interrupt handler
void revolution() {
    lastRevolutionTime = millis();
    if (millis() < lastRevolutionTime + interval_minimum_revolution) {
        rideRevolutions += 1;
        underOperation = true;
    }
}



void setup() {
    pinMode(ESPLED, OUTPUT);
    pinMode(PCBLED, OUTPUT);
    pinMode(ROAD_LED, OUTPUT);
    pinMode(REED_SENSOR, INPUT);
    attachInterrupt(digitalPinToInterrupt(REED_SENSOR), revolution, CHANGE);

    digitalWrite(ESPLED, HIGH);
    digitalWrite(PCBLED, HIGH);
    digitalWrite(ROAD_LED, HIGH);
    
    Serial.begin(9600);                                   // serial over USB (with PC)
    delay(200);

    Serial_internet_relay.begin(9600);                    // serial with "internet_relay"
    delay(200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(access_SSID, access_PASS);

    IPAddress myIP = WiFi.softAPIP();
    Serial.print("\n[SUCCESS] HotSpot IP : ");
    Serial.println(myIP);

    // if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    //     Serial.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
    //     Serial_internet_relay.println("[ERROR] connecting on WiFi. System will reboot in 5\" ...");
    //     delay(5000);
    //     ESP.restart();
    // }

    server.on("/", onConnect_default);
    server.on("/restart", onConnect_restart);
    server.on("/settings", onConnect_settings);
    server.on("/about", onConnect_about);
    server.onNotFound(onConnect_notFound);

    server.begin();
    Serial.println("[SUCCESS] HTTP server started");
    delay(200);

    dht.begin();
    Serial.println("[SUCCESS] DHT started");
    delay(200);
}


void loop() {
    server.handleClient();

    if (millis() < lastRevolutionTime + interval_minimum_revolution) {
        underOperation = false;
    }
    
}


// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ HTML code ~~~~~~~~~~~~~~~~~~~~~~~
String HTML_REFRESH_TO_SETTINGS() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/settings\">";
    html_page += "</head>";

    return html_page;
}

String HTML_REFRESH_TO_ROOT() {
    String html_page = "<HEAD>";
    html_page += "<meta http-equiv=\"refresh\" content=\"0;url=/\">";
    html_page += "</head>";

    return html_page;
}

String HTML_NOT_FOUND() {
    String html_page = "<!DOCTYPE html> <html>\n";
    html_page += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
    html_page += "<title>RJD Monitor</title>\n";
    html_page += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: left;}\n";
    html_page += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
    html_page += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
    html_page += "</style>\n";
    html_page += "</head>\n";
    html_page += "<body>\n";
    html_page += "<div id=\"webpage\">\n";
    html_page += "<h1>You know this 404 thing ?</h1>\n";
    html_page += "<p>What you asked can not be found... :'( </p>";
    html_page += "</div>\n";
    html_page += "</body>\n";
    html_page += "</html>\n";

    return html_page;
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ web pages ~~~~~~~~~~~~~~~~~~~~~~~
void onConnect_default() {
    digitalWrite(ESPLED, LOW);
    get_sensorData();
    server.send(200, "text/html", HTML_LANDING_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_settings() {
    digitalWrite(ESPLED, LOW);}
    get_sensorData();
    server.send(200, "text/html", HTML_SETTINGS_PAGE());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_about() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/plain", "Bike and save energy! (C)");
    digitalWrite(ESPLED, HIGH);
}

void onConnect_notFound(){
    digitalWrite(ESPLED, LOW);
    server.send(404, "text/html", HTML_NOT_FOUND());
    digitalWrite(ESPLED, HIGH);
}

void refreshToRoot() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_REFRESH_TO_ROOT());
    digitalWrite(ESPLED, HIGH);
}

void refreshToSettings() {
    digitalWrite(ESPLED, LOW);
    server.send(200, "text/html", HTML_REFRESH_TO_SETTINGS());
    digitalWrite(ESPLED, HIGH);
}

void onConnect_restart() {
    refreshToRoot();
    delay(3000);
    ESP.restart();
}
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~