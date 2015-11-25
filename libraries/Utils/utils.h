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

#ifndef DBG //usually defined in main
#define DBG(...)
#endif


namespace Utils{
    extern WebSocketsServer webSocket;
    bool connect(String filename);
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght);
}

#endif // UTILS_H
