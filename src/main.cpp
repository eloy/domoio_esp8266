// #define DEBUG_ESP_PORT Serial.printf
// #define DEBUG_ESP_HTTP_CLIENT
// #define DEBUG_SSL
// #define DEBUGV

#include <Arduino.h>
#include <EEPROM.h>
#include "FS.h"
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>

#include "cantcoap.h"
#include "domoio_core.h"
#include "domoio.h"
#include "wificonf.h"
#include "storage.h"
#include "reactduino.h"
#include "scenes.h"
#include <cstdio>

bool fatal_error = false;

void delete_credentials() {
  WifiConfig::reset();
  reset();
}

void reset() {
  delay(5000);
  ESP.reset();
  delay(5000);
}

HomeController home_controller;
namespace domoio {
  void setup() {
#ifdef SERIAL_LOG
    Serial.begin(115200);
    // Serial.setDebugOutput(true);
    Serial.println("\n\nStarting Domoio\n");
#endif
    Storage::begin();
    reactduino::push_controller(&home_controller);

    if (!verify_keys()) {
      reactduino::dispatch(REACT_FLASHING);
      fatal_error = true;
      return;
    }

    init_ports();
    WiFi.persistent(false);

    try_ota_update_from_file();
    connect_wifi();

  }

  void loop() {
    if (fatal_error) return;

    reactduino::loop();
    if (is_reconnect_requested()) {
      handle_event(EVENT_DISCONNECTED, NULL);
      disconnect();
    }

    if (!is_connected()) {
      handle_event(EVENT_DISCONNECTED, NULL);
      delay(1000);
      connect();
      return;
    }

    receive_messages();
    watchers_loop();

    if (is_ota_requested()) {
      disconnect();
      delay(3000);
      run_ota_update();
    }
  }
}
