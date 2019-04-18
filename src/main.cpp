/**
   A BLE client for the Xiaomi Mi Plant Sensor, pushing measurements to an MQTT server.
   
   MIT License
   Copyright (c) 2017 Sven Henkel
   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
   Update June 2018:
   Support for multiple Mi Floras
   
*/
#include <Arduino.h>
#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>

#include "config.h"

RTC_DATA_ATTR int bootCount = 0;

// The remote service we wish to connect to.
static BLEUUID serviceUUID("00001204-0000-1000-8000-00805f9b34fb");

// The characteristic of the remote service we are interested in.
static BLEUUID uuid_version_battery("00001a02-0000-1000-8000-00805f9b34fb");
static BLEUUID uuid_sensor_data("00001a01-0000-1000-8000-00805f9b34fb");
static BLEUUID uuid_write_mode("00001a00-0000-1000-8000-00805f9b34fb");

static int doConnect = 0;
static boolean connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic;

byte whichOne[MIFLORACOUNT];

WiFiClient espClient;
PubSubClient client(espClient);


bool getSensorData(BLEAddress pAddress, bool getBattery, String mac) {
  Serial.println();
  Serial.println("============================================");
  Serial.println();
  Serial.print("Trying to form connection to MiFlora device at ");
  Serial.println(pAddress.toString().c_str());

  BLEClient*  pClient  = BLEDevice::createClient();

  // Connect to the remove BLE Server.
  if (!pClient->connect(pAddress)) {
      return false;
  }
  Serial.println(" - Connected to MiFlora");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    return false;
  }
  Serial.println(" - Found our service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(uuid_write_mode);
  uint8_t buf[2] = {0xA0, 0x1F};
  pRemoteCharacteristic->writeValue(buf, 2, true);

  delay(500);

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pRemoteCharacteristic = pRemoteService->getCharacteristic(uuid_sensor_data);
  Serial.println(pRemoteService->toString().c_str());
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(uuid_sensor_data.toString().c_str());
    return false;
  }
  Serial.println(" - Found our characteristic");


  // Read the value of the characteristic.
  std::string value = pRemoteCharacteristic->readValue();
  Serial.print("The characteristic value was: ");
  const char *val = value.c_str();

  Serial.print("Hex: ");
  for (int i = 0; i < 16; i++) {
    Serial.print((int)val[i], HEX);
    Serial.print(" ");
  }
  Serial.println(" ");

  float temp = (val[0] + val[1] * 256) / ((float)10.0);
  int moisture = val[7];
  int light = val[3] + val[4] * 256;
  int conductivity = val[8] + val[9] * 256;
  
  char buffer[64];
  char topic[100];
  char addr[18];
  String tmp;
  tmp = mac;
  tmp.remove(14,1);
  tmp.remove(11,1);
  tmp.remove(8,1);
  tmp.remove(5,1);
  tmp.remove(2,1);
  tmp.toCharArray(addr,18);
  
  Serial.print("Temperature: ");
  Serial.println(temp);
  if (temp!=0 && temp>-20 && temp<40) {
    snprintf(buffer, 64, "%f", temp);
    strcpy(topic,MQTT_BASE);
    strcat(topic,addr);
    strcat(topic,MQTT_SEPARATOR);
    strcat(topic,MQTT_TEMPERATURE);
    if (client.publish(topic, buffer)) {
      Serial.println(" >> Published");
    }
  }

  Serial.print("Moisture: ");
  Serial.println(moisture);
  if (moisture<=100 && moisture>=0) {
    snprintf(buffer, 64, "%d", moisture);
    strcpy(topic,MQTT_BASE);
    strcat(topic,addr);
    strcat(topic,MQTT_SEPARATOR);
    strcat(topic,MQTT_MOISTURE);
    if (client.publish(topic, buffer)) {
      Serial.println(" >> Published");
    }
  }

  Serial.print("Light: ");
  Serial.println(light);
  if (light>=0) {
    snprintf(buffer, 64, "%d", light);
    strcpy(topic,MQTT_BASE);
    strcat(topic,addr);
    strcat(topic,MQTT_SEPARATOR);
    strcat(topic,MQTT_LIGHT);
    if (client.publish(topic, buffer)) {
      Serial.println(" >> Published");
    }
  }

  Serial.print("Conductivity: ");
  Serial.println(conductivity);
  if (conductivity>=0 && conductivity<5000) {
    snprintf(buffer, 64, "%d", conductivity);
    strcpy(topic,MQTT_BASE);
    strcat(topic,addr);
    strcat(topic,MQTT_SEPARATOR);
    strcat(topic,MQTT_CONDUCTIVITY);
    if (client.publish(topic, buffer)) {
      Serial.println(" >> Published");
    }
  }

  if (getBattery) {
    Serial.println();
    Serial.println("Trying to retrieve battery level...");
    pRemoteCharacteristic = pRemoteService->getCharacteristic(uuid_version_battery);
    if (pRemoteCharacteristic == nullptr) {
      Serial.print("Failed to find our characteristic UUID: ");
      Serial.println(uuid_sensor_data.toString().c_str()); 
      return false;
    }
    Serial.println(" - Found our characteristic");

    // Read the value of the characteristic...
    value = pRemoteCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    const char *val2 = value.c_str();
    Serial.print("Hex: ");
    for (int i = 0; i < 16; i++) {
      Serial.print((int)val2[i], HEX);
      Serial.print(" ");
    }
    Serial.println(" ");

    int battery = val2[0];
    Serial.print("Battery: ");
    Serial.println(battery);
    snprintf(buffer, 64, "%d", battery);
    strcpy(topic,MQTT_BASE);
    strcat(topic,addr);
    strcat(topic,MQTT_SEPARATOR);
    strcat(topic,MQTT_BATTERY);
    if (client.publish(topic, buffer)) {
      Serial.println(" >> Published");
    }
  }

  pClient->disconnect();
}

void taskDeepSleep( void * parameter )
{
  delay(SLEEP_WAIT * 1000);
  esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000000ll);
  Serial.println("Going to sleep now.");
  esp_deep_sleep_start();
}


void setup() {
  Serial.begin(9600);
  Serial.println("");
  Serial.println("");
  Serial.print("Will connect to WiFi: ");
  Serial.println(wifi_ssid);
  Serial.println("");
  Serial.print("Going to try to connect to ");
  Serial.print(MIFLORACOUNT);
  Serial.print(" MiFlora clients");
  Serial.println("");
  
  delay(1000);
  
  xTaskCreate(      taskDeepSleep,          /* Task function. */
                    "TaskDeepSleep",        /* String with name of task. */
                    10000,                  /* Stack size in words. */
                    NULL,                   /* Parameter passed as input of the task */
                    1,                      /* Priority of the task. */
                    NULL);                  /* Task handle. */
  
  
  Serial.println("");
  Serial.println("Starting MiFlora client...");
  BLEDevice::init("");

  Serial.println("");
  Serial.println("Connecting to WiFi...");
  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);

  byte i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    i++;
    if (i>40) {
      Serial.println();
      i = 0;
    }
  }

  Serial.println("");
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("");

  byte ar[6];
  WiFi.macAddress(ar);
  char macAddr[18];
  sprintf(macAddr,"%02X%02X%02X%02X%02X%02X",ar[0],ar[1],ar[2],ar[3],ar[4],ar[5]);

  client.setServer(mqtt_server, 1883);
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection as client:");
    // Attempt to connect
    char mqttClient[25];
    sprintf(mqttClient,"MiFlora%s",macAddr);
    Serial.println(mqttClient);
    if (client.connect(mqttClient)) {
      Serial.println("Connected");
      char topic[100];
      strcpy(topic,"booting/esp/");
      strcat(topic,macAddr);
      if (client.publish(topic,"boot")) {
        Serial.println();
        Serial.println("MQTT boot published");
        Serial.println();
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  delay(1000);

  for (byte ii=0; ii<MIFLORACOUNT; ii++) {
    whichOne[ii] = ii;
  }

  byte maxOne = MIFLORACOUNT;
  byte choose;
  for (int ii=MIFLORACOUNT-1; ii>=0; ii--) {
    choose = random(ii+1);

    char tmp[18];
    MiFloras[whichOne[choose]].toCharArray(tmp,18);
    BLEAddress floraAddress(tmp);
    Serial.println();
    Serial.println("++++++++++++++++++++++");
    Serial.println();
    Serial.print("Number :");
    Serial.println(whichOne[choose]);
    Serial.print("Bluetooth id:");
    Serial.println(tmp);
    Serial.println();
    Serial.println("++++++++++++++++++++++");
    Serial.println();
    getSensorData(floraAddress,((bootCount%BATTERY_INTERVAL)==0),
      MiFloras[whichOne[choose]]);
    whichOne[choose] = whichOne[ii];
  }
  bootCount++;
} // End of setup.


// This is the Arduino main loop function.
void loop() {
  delay(10000);
} // End of loop
