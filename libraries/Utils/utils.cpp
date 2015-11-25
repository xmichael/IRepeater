#include <utils.h>
#ifndef DBG
#define DBG(...) Serial.println( __VA_ARGS__ )
#endif
namespace Utils{
    /** variables */
    WebSocketsServer webSocket = WebSocketsServer(81); // note 81 for WS

    /** Connect using configuration file uploaded with Tools->Data Upload */
    bool connect(String fname){
    File f;  
    if ( !SPIFFS.begin() ){
        DBG("Can't mount FS");
        return false;
    }
    if ( !SPIFFS.exists(fname) ){
        DBG("Couldn't find file: "); DBG(fname);
        return false;
    }
    f = SPIFFS.open ( fname, "r");
    if ( !f ){
        DBG("Error reading:");DBG(fname);
        return false;
    }
    String content = f.readString();
    f.close();
    content.trim();
    uint8_t pos = content.indexOf("\n");
    if (pos == 0)
    {
        DBG("No second line");
        DBG(content);
        return false;
    }

    // Store SSID and PSK into string vars.
    String ssid = content.substring(0, pos);
    String pass = content.substring(pos + 1);
    DBG("content is:");DBG(content);
    DBG("ssid is:");DBG(ssid.c_str());
    DBG("pass is:");DBG(pass.c_str());
    delay(1000);
    WiFi.mode(WIFI_AP_STA); //TODO make this configurable?
    WiFi.begin(ssid.c_str(), pass.c_str());

    while(WiFi.waitForConnectResult() != WL_CONNECTED){
        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.println("WiFi failed, retrying.");
    }
    return true;
    }

    
    // Keep this until we are sure it works. Then move to DBG(...)
    #define USE_SERIAL Serial
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
        switch(type) {
            case WStype_DISCONNECTED:
                USE_SERIAL.printf("[%u] Disconnected!\n", num);
                break;
            case WStype_CONNECTED:
                {
                    IPAddress ip = webSocket.remoteIP(num);
                    USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
                }
                break;
            case WStype_TEXT:
                USE_SERIAL.printf("[%u] get Text: %s\n", num, payload);

                // echo data back to browser
                webSocket.sendTXT(num, payload, lenght);

                // send data to all connected clients
                //webSocket.broadcastTXT(payload, lenght);
                break;
            case WStype_BIN:
                USE_SERIAL.printf("[%u] get binary lenght: %u\n", num, lenght);
                hexdump(payload, lenght);

                // echo data back to browser
                webSocket.sendBIN(num, payload, lenght);
                break;
        }
    }

}