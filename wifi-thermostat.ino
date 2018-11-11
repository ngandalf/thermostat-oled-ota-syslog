#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Syslog.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include "SSD1306.h"
#include <PubSubClient.h>

#define VERSION "0.4"


char message[100];

//define your default values here, if there are different values in config.json, they are overwritten.
//length should be max size + 1
char mqtt_server[40]="192.168.1.2";
char mqtt_username[40]="xxxxxxxxx";
char mqtt_password[40]="xxxxxxxxx";
char mqtt_heater_read[255] = "home/chaudiere/set";
char mqtt_temperature_sonde[255] = "/data/RFLINK/Oregon Temp/CC20/R/TEMP";
char mqtt_temperature_desired[255] = "home/climate/temperature";
char syslog_server[16] = "192.168.1.2";

//default custom static IP
char static_ip[16] = "192.168.1.30";
char static_gw[16] = "192.168.1.254"; 
char static_sn[16] = "255.255.255.0";

//flag for saving data
bool shouldSaveConfig = false;
const char *configfile = "/config.json";

// OLED
SSD1306 display(0x3c, D3, D5);

// Syslog
#define SYSLOG_PORT 514
#define DEVICE_HOSTNAME "thermostat"
#define APP_NAME "thermostat"
WiFiUDP udpClient;


// Create a new empty syslog instance
Syslog syslog(udpClient, SYSLOG_PROTO_IETF);


// server WEB
ESP8266WebServer server(80);

//mqtt
WiFiClient espClient;
PubSubClient client(espClient);

//temperature
String temp;
char heater[]="off";
String tempdesired;

//relay
int relay = D6;
String stateRelay = "off";

//security
int interval = 0;

//wifi
WiFiManager wifiManager;
char PasswordAP[] = "xxxxxxxxx";

void readConfigFile(const char *fileconfig)
{
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");

    if (SPIFFS.exists(fileconfig)) {

      //file exists, reading and loading
      Serial.println("reading config file");

      File configFile = SPIFFS.open(fileconfig, "r");

      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();

        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_username, json["mqtt_username"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_heater_read, json["mqtt_heater_read"]);
          strcpy(mqtt_temperature_sonde, json["mqtt_temperature_sonde"]);
          strcpy(mqtt_temperature_desired, json["mqtt_temperature_desired"]);
          strcpy(syslog_server, json["syslog_server"]);

          if (json["ip"]) {
            Serial.println("setting custom ip from config");
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            Serial.println(static_ip);
          } else {
            Serial.println("no custom ip in config");
          }
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


//wifi
void scanWifi(String ssid ){

  int j = 0;
  while ( j < 50)
  {
    int n = WiFi.scanNetworks();
    Serial.println("scan done");
    if (n == 0)
      Serial.println("no networks found");
    else
    {
      Serial.print(n);
      Serial.println(" networks found");
      for (int i = 0; i < n; ++i)
      {
        // Print SSID and RSSI for each network found
        Serial.print(ssid);
        Serial.printf(" => SSID is  %s\n", WiFi.SSID(i).c_str());

        if(WiFi.SSID(i) == ssid){

           Serial.println("The network you are looking for is available");
           j = 51;
           i = n;
        }
      }
    }

    if (j < 50)
    {
      delay(10000);
    }
      
    j++;
  }
}


void setupWifiManager()
{

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length

  WiFiManagerParameter custom_html_mqtt("<p>Mqtt params :</p>");
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_username("username", "mqtt username", mqtt_username, 40);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);
  WiFiManagerParameter custom_mqtt_heater_read("Topic chaudiere", "Topic chaudiere", mqtt_heater_read, 255);
  WiFiManagerParameter custom_mqtt_temperature_sonde("temperature_sonde", "Topic temperaturesonde", mqtt_temperature_sonde, 255);
  WiFiManagerParameter custom_mqtt_temperature_desired("Topic climate", "Topic temperature desirée", mqtt_temperature_desired, 255);
  WiFiManagerParameter custom_html_syslog("<p>Syslog params :</p>");
  WiFiManagerParameter custom_syslog_server("syslogServer", "server syslog", syslog_server, 16);

  WiFiManagerParameter custom_html_ip("<p>Ip params :</p>");
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  //WiFiManager wifiManager;


  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  IPAddress _ip, _gw, _sn;
  _ip.fromString(static_ip);
  _gw.fromString(static_gw);
  _sn.fromString(static_sn);

  wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);

  //add all your parameters here
  wifiManager.addParameter(&custom_html_mqtt);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_username);
  wifiManager.addParameter(&custom_mqtt_password);
  wifiManager.addParameter(&custom_mqtt_heater_read);
  wifiManager.addParameter(&custom_mqtt_temperature_sonde);
  wifiManager.addParameter(&custom_mqtt_temperature_desired);
  wifiManager.addParameter(&custom_html_syslog);
  wifiManager.addParameter(&custom_syslog_server);
  wifiManager.addParameter(&custom_html_ip);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(10);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  //if (SPIFFS.exists(configfile)) {

  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("PSK: %s\n", WiFi.psk().c_str());

  if (WiFi.SSID() && WiFi.SSID() != "")
  {
    scanWifi(WiFi.SSID().c_str());
  }

  if (!wifiManager.autoConnect("Thermostat-AP", PasswordAP)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }
  

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_username, custom_mqtt_username.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_heater_read, custom_mqtt_heater_read.getValue());
  strcpy(mqtt_temperature_sonde, custom_mqtt_temperature_sonde.getValue());
  strcpy(mqtt_temperature_desired, custom_mqtt_temperature_desired.getValue());
  strcpy(syslog_server, custom_syslog_server.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_username"] = mqtt_username;
    json["mqtt_password"] = mqtt_password;
    json["mqtt_heater_read"] = mqtt_heater_read;
    json["syslog_server"] = syslog_server;
    json["mqtt_temperature_sonde"] = mqtt_temperature_sonde;
    json["mqtt_temperature_desired"] = mqtt_temperature_desired;

    json["ip"] = WiFi.localIP().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["subnet"] = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open(configfile, "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());
  Serial.println(WiFi.subnetMask());
  Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
  Serial.printf("PSK: %s\n", WiFi.psk().c_str());


  //oled
  // Align text vertical/horizontal center
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display.setFont(ArialMT_Plain_10);
  display.drawString(display.getWidth() / 2, display.getHeight() / 2, "Ready for OTA:\n" + WiFi.localIP().toString());
  display.display();

}


//function display
void displayCenter(const char *message)
{
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display.drawString(display.getWidth() / 2, display.getHeight() / 2 - 10, message);
  display.display();
}


void displayInfos(String temp1, String temp2, String stateHeater, String staterelay, const char* errormessage)
{

  String line1 = temp1;
  line1.concat(" / ");
  line1.concat(temp2);

  String line2 = stateHeater;
  line2.concat(" / ");
  line2.concat(staterelay);

  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER_BOTH);
  display.drawString(display.getWidth() / 2, 12, DEVICE_HOSTNAME);
  display.drawString(display.getWidth() / 2, display.getHeight() / 2 - 5, line1);
  display.drawString(display.getWidth() / 2, display.getHeight() / 2 + 5, line2);
  display.drawString(display.getWidth() / 2, display.getHeight() / 2 + 5, errormessage);
  display.display();
 
}


void displayProgressBar(unsigned int progress, unsigned int total)
{
  display.drawProgressBar(4, 32, 120, 8, progress / (total / 100) );
  display.display();
}


//function update OTA
void setup_ota() {

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(DEVICE_HOSTNAME);


  // No authentication by default
  //ArduinoOTA.setPassword((const char *)"730");

  ArduinoOTA.onStart([]() {

    //oled
    displayCenter("Ota update");

    //syslog
    syslog.log(LOG_INFO, "update OTA started");

    //serie
    Serial.println("Start");
  });

  ArduinoOTA.onEnd([]() {

    //oled
    displayCenter("Restart");

    //syslog
    syslog.log(LOG_INFO, "update OTA finished");

    //serie
    Serial.println("\nEnd");

  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {

    //oled
    displayProgressBar(progress, total);

    //sylog
    syslog.logf(LOG_INFO, "Progress: %u%%", (progress / (total / 100)));

    //serie
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if      (error == OTA_AUTH_ERROR   )
    {
      //oled
      displayCenter("OTA : auth failed");

      //serie
      Serial.println("OTA : Auth Failed");

      //syslog
      syslog.log(LOG_ERR, "OTA : Auth Failed");
    }
    else if (error == OTA_BEGIN_ERROR  )
    {

      //oled
      displayCenter("OTA : Begin failed");

      //serie
      Serial.println("OTA : Begin Failed");

      //syslog
      syslog.log(LOG_ERR, "OTA Begin Failed");
    }
    else if (error == OTA_CONNECT_ERROR)
    {
      //oled
      displayCenter("OTA : Connect Failed");

      //syslog
      syslog.log(LOG_ERR, "OTA : Connect Failed");
    }
    else if (error == OTA_RECEIVE_ERROR)
    {
      //oled
      displayCenter("OTA : receive failed");

      //serie
      Serial.println("OTA : Receive Failed");

      //syslog
      syslog.log(LOG_ERR, "OTA : Receive Failed");
    }
    else if (error == OTA_END_ERROR)
    {
      //oled
      displayCenter("OTA : End failed");

      //serie
      Serial.println("OTA :End Failed");

      //syslog
      syslog.log(LOG_ERR, "OTA : End Failed");
    }
  });

  ArduinoOTA.begin();
}



//function web server
void setup_http_server()
{
  // on definit les points d’entrée (les URL à saisir dans le navigateur web) et on affiche un simple texte
  server.on("/reset", []() {
    displayCenter("erase config wifi");
    delay(2000);
    wifiManager.resetSettings();
    restart();
  });

  server.on("/restart", []() {
    restart();
  });

  // on démarre le serveur web
  server.begin();
}

void restart ()
{
  displayCenter("restart");
  ESP.restart();
  delay(5000);
}

//function syslog
void setupSyslog()
{

  syslog.server(syslog_server, SYSLOG_PORT);
  syslog.deviceHostname(DEVICE_HOSTNAME);
  syslog.appName(APP_NAME);
  syslog.defaultPriority(LOG_KERN);
}


//function mqtt
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  unsigned int i = 0;
  for (i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    message[i] = payload[i];
  }
  message[i] = '\0';
  Serial.println();


  if ( String(topic) == mqtt_heater_read ) {
    if ((char)payload[0] == '0') {
      strcpy(heater, "off");
      Serial.println("HEATER -> OFF");
    }
    else {
      strcpy(heater, "on");
      Serial.println("HEATER -> ON");
    }

    //action the relay heater
    if (strcmp ("on", heater) == 0)
    {
      //push on 
      Serial.print("mqtt heater : ");
      Serial.println("on");
      digitalWrite(relay,HIGH);
      
      stateRelay = "on";
      Serial.print("stateRelay : ");
      Serial.println(stateRelay);

    }
    else
    {
      //push off 
      Serial.print("mqtt heater : ");
      Serial.println("off");
      digitalWrite(relay,LOW);
      
      stateRelay = "off";
      Serial.print("stateRelay : ");
      Serial.println(stateRelay);

    }

    char eventRelai[4];
    stateRelay.toCharArray(eventRelai, 4);
    syslog.logf(LOG_INFO, "new event relay:  %s ", eventRelai);
    displayInfos(temp,tempdesired,heater,stateRelay,"");
  }

  if ( String(topic) == mqtt_temperature_sonde )
  {
    Serial.println(String(message));
    temp = String(message);

    displayInfos(temp,tempdesired,heater,stateRelay,"");
    char eventTempProbe[5];
    temp.toCharArray(eventTempProbe, 5);
    syslog.logf(LOG_INFO, "new temperature detected (probe):  %s ", eventTempProbe);
  }

  if( String(topic) == mqtt_temperature_desired )
  {
    Serial.println(String(message));
    tempdesired = String(message);
 
    displayInfos(temp,tempdesired,heater,stateRelay,"");
    char eventTempDesired[5];
    tempdesired.toCharArray(eventTempDesired, 5);
    syslog.logf(LOG_INFO, "new temperature detected (desired):  %s ", eventTempDesired);
  }
  
  

}


void setup_mqtt()
{
  delay(2000);
  client.setServer(mqtt_server, 1883 );
  client.setCallback(callback);

  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");

    if (client.connect("ESP8266Client", mqtt_username, mqtt_password )) {

      Serial.println("connected");

    } else {

      if ( interval == 900 )
      {
        //disable by security
        digitalWrite(relay,LOW);
        stateRelay="off";
      }
      
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);

    }
  }

  while (!client.subscribe(mqtt_heater_read) )
  {
    Serial.print("subscribe on topic mqtt_header_read");
    syslog.logf(LOG_ERR, "subscribe on %s failed", mqtt_heater_read);
    delay(5);
  }
  while (!client.subscribe(mqtt_temperature_sonde) )
  {
    Serial.print("subscribe on topic mqtt_temperature_sonde");
    syslog.logf(LOG_ERR, "subscribe on %s failed", mqtt_temperature_sonde);
  }
  while (!client.subscribe(mqtt_temperature_desired) )
  {
    Serial.print("subscribe on topic mqtt_temperature_desired");
    syslog.logf(LOG_ERR, "subscribe on %s failed", mqtt_temperature_desired);
  }  
}

void setup(  ) {

  // setup serial
  Serial.begin(9600);
  Serial.println();

  pinMode(relay, OUTPUT);

  // Init OLED SSD1603
  display.init();
  display.flipScreenVertically();
  displayCenter("Starting...");
  displayProgressBar(1, 100);

//SPIFFS.format();
  delay(1000);
  displayCenter("reading config");
  displayProgressBar(25, 100);
  readConfigFile(configfile);

  delay(1000);
  WiFi.setAutoReconnect(true);
  displayCenter("Configuring Wifi");
  displayProgressBar(50, 100);
  setupWifiManager();

  delay(1000);
  displayCenter("Service syslog");
  displayProgressBar(65,100);
  setupSyslog();
  syslog.logf(LOG_INFO, "Firmware : %s started", VERSION);


  delay(1000);
  displayCenter("Service OTA");
  displayProgressBar(70,100);
  setup_ota();

  delay(1000);
  displayCenter("Service http");
  displayProgressBar(85, 100);
  setup_http_server();
  

  delay(1000);
  displayCenter("Service mqtt");
  displayProgressBar(100, 100);
  setup_mqtt();
 


}

void loop() {
  // put your main code here, to run repeatedly:


  if (WiFi.status() == WL_CONNECTED) {

    
    ArduinoOTA.handle();
    server.handleClient();

    if (client.connected())
    {
      client.loop();
      interval = 0;
      
    }
    else
    {
      displayInfos(temp,tempdesired,heater,stateRelay,"mqtt not connected");
      Serial.print("mqtt not connected");
      setup_mqtt();
    }
  }
  else
  {
    Serial.println("wifi disconnected ! waiting to reconnect");
    displayInfos(temp,tempdesired,heater,stateRelay,"wifi loss");
    if ( interval == 900 )
    {
      //disable by security
      digitalWrite(relay,LOW);
      stateRelay="off";
    }
    Serial.println(interval);
    delay(2000);
    interval++;
  }
}
