/**
 * Example: 
 */

#ifndef UTILS_H
#define UTILS_H

#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <WebSocketsServer.h>
#include <FS.h>
#include <DNSServer.h>

#define DBG(...) Serial.println( __VA_ARGS__ )

#ifndef DBG
#define DBG(...)
#endif


namespace Utils{
    typedef struct {
      String ssid;
      String pass;
      bool status;
    } wifi_creds;
    
    extern WebSocketsServer webSocket;
    extern DNSServer dnsServer;
    
    wifi_creds get_wifi_creds(String filename);
    bool connect(String filename);
    bool connect_ap(wifi_creds ap_creds);
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght);
}

#endif // UTILS_H
