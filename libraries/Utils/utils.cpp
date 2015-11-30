#include <utils.h>
#ifndef DBG
#define DBG(...) Serial.println( __VA_ARGS__ )
#endif
namespace Utils{
    /** variables */
    WebSocketsServer webSocket = WebSocketsServer(81); // note 81 for WS
    DNSServer dnsServer; //only used for WIFI_AP
        
    /** Read a credentials file from flash and return ssid/pass pair */
    wifi_creds get_wifi_creds(String fname){
        File f;
        wifi_creds creds;
        creds.status = false;

        if ( !SPIFFS.begin() ){
            DBG("Can't mount FS");
            return creds;
        }
        if ( !SPIFFS.exists(fname) ){
            DBG("Couldn't find file: "); DBG(fname);
            return creds;
        }
        f = SPIFFS.open ( fname, "r");
        if ( !f ){
            DBG("Error reading:");DBG(fname);
            return creds;
        }
        String content = f.readString();
        f.close();
        content.trim();
        uint8_t pos = content.indexOf("\n");
        if (pos == 0)
        {
            DBG("No second line");
            DBG(content);
            return creds;
        }

        // Store SSID and PSK into string vars.
        creds.ssid = content.substring(0, pos);
        creds.pass = content.substring(pos + 1);
        creds.status = true;
        DBG("content is:");DBG(content);
        DBG("ssid is:");DBG(creds.ssid.c_str());
        return  creds;
    }
    
    /** Connect using configuration file to STA then fallback to AP */
    bool connect(String fname){
        wifi_creds sta_creds = get_wifi_creds(fname);
        
        ///// No STA file found /////
        if ( ! sta_creds.status ){  
            DBG("Couldn't read /creds, Trying /creds_ap");
            wifi_creds ap_creds = get_wifi_creds(fname + "_ap");
            if ( ap_creds.status ){
                connect_ap(ap_creds);
            }
            else{ // last resort use "reflash/default pass"
                ap_creds = { "reflash", "default pass", true  };
                connect_ap(ap_creds);
            }
            return true;
        }

        /// STA file FOUND ////
        delay(1000);
        WiFi.mode(WIFI_AP_STA);
        WiFi.begin(sta_creds.ssid.c_str(), sta_creds.pass.c_str());

        uint8_t attempts = 3;
        
        while(WiFi.waitForConnectResult() != WL_CONNECTED){
            WiFi.begin(sta_creds.ssid.c_str(), sta_creds.pass.c_str());
            DBG("WiFi failed, retrying.");
            attempts--;
            //////// Can't connect as STA => try softAP ////
            if ( attempts == 0 ){
                DBG("Max attempts failed. Starting softAP.");
                wifi_creds ap_creds = get_wifi_creds(fname + "_ap");
                if ( ap_creds.status ){ //use /creds_ap credentials
                    connect_ap(ap_creds);
                }
                else{ // last resort use "reflash/default pass"
                    ap_creds = { "reflash", "default pass", true  };
                    connect_ap(ap_creds);
                }
            }
        }
        return true;
    }

    /** Starts softAP with as a captive portal */
    bool connect_ap(wifi_creds ap_creds){
        IPAddress apIP(192, 168, 5, 1);
        DNSServer dnsServer;
        
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
      
        WiFi.softAP(ap_creds.ssid.c_str(), ap_creds.pass.c_str());
        DBG("AP IP address: ");
        DBG(apIP.toString());
        dnsServer.start(53, "*", apIP); //expects dnsServer.processNextRequest() in loop()
        DBG("Started Capture Portal");
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