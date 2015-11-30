/*
 * 1. Get online -- You will need to upload a spiffs file /cred with your wifi user/pass using the Tools->Data upload
 * 2. mDns
 * 3. Web server -- custom
 * 4.OTA (/update)
*/
#include <Arduino.h>
#include <WebSocketsServer.h>
#include <Hash.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>

#include <DNSServer.h>

#include <ESP8266httpUpdate.h>


#include <IRremoteESP8266.h>
#include <IRremoteInt.h>

#include <utils.h>
//#include <eboard.h>


const char* host = "irper";
//static Eboard brd;
static ESP8266WebServer httpServer(80);

//IR-specific
static int RECV_PIN = 2; //only seems to work with D2
static IRrecv irrecv(RECV_PIN);
static IRsend irsend(5); //IR led at D5 (note D4<->D5 at yellow ebay board)
static decode_results results;
static File fsUploadFile; //holds the current upload

using Utils::connect;
using Utils::webSocket;
using Utils::webSocketEvent;
using Utils::dnsServer;

/* Add custom routes here! until we get wildcard support */

void myFsUpdater(ESP8266WebServer* server){

  server->on ( "/inline", [server]() {
    server->send ( 200, "text/plain", "Works!" );
  } );

  server->on ( "/ls", [server]() {
    Dir dir = SPIFFS.openDir("/");
    String output = "[";
    while(dir.next()){
        File entry = dir.openFile("r");
        if (output != "[") output += ',';
        bool isDir = false;
        output += "{\"type\":\"";
        output += (isDir)?"dir":"file";
        output += "\",\"name\":\"";
        output += String(entry.name()).substring(1);
        output += "\"}";
        entry.close();
    }
    output += "]";
    server->send(200, "text/json", output);
  } );

  // curl -F "file=@myfile" host/upload

  server->on("/upload", HTTP_POST, [server](){ server->send(200, "text/plain", ""); });
  server->onFileUpload([server] (){
        String msg;
        String filename;

        if(server->uri() != "/upload")
            return;
        HTTPUpload& upload = server->upload();
        filename = upload.filename;
        if(upload.status == UPLOAD_FILE_START){
            if(!filename.startsWith("/"))
                filename = "/"+filename;
            msg = String("handleFileUpload Name: ") + filename + String("\n");
            webSocket.broadcastTXT(msg.c_str(), msg.length());
            fsUploadFile = SPIFFS.open(filename, "w");
            if ( ! fsUploadFile ){
                msg = String("/upload: File open failed\n");
                webSocket.broadcastTXT(msg.c_str(), msg.length());
                return;
            }
        } else if(upload.status == UPLOAD_FILE_WRITE){
            //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
            if(fsUploadFile)
                fsUploadFile.write(upload.buf, upload.currentSize);
        } else if(upload.status == UPLOAD_FILE_END){
            if(fsUploadFile)
                fsUploadFile.close();
            msg = String("handleFileUpload Size: ") + String(upload.totalSize) + String("\n");
            webSocket.broadcastTXT(msg.c_str(), msg.length());
        }
     });

/* To upload through terminal you can use something like: 
    curl 'esp.local/updater?host=<server_ip>&port=<port>&path=/my_compiled_file.bin'
You can serve your compiled files including the bin with e.g. 
    cd /tmp/buildXXXX && python -m SimpleHTTPServer <port>
*/
// Needs 3 params to your pc e.g. /update?host=192.168.1.1&port=80&path=/myfirmware.bin
  server->on ( "/updater", [server]() {
        String msg;
        String host;
        uint16_t port;
        String path;
        t_httpUpdate_return ret;

        if ( server->args() != 3 ) {
            msg += "Expected 3 arguments for update. Got ";
            msg += String(server->args());
            server->send ( 404, "text/plain", msg.c_str() );
            return;
        }

        host = server->arg(0);
        port = (uint16_t) server->arg(1).toInt();
        path = server->arg(2);
    
        msg = "Fetching " + host + ":" + String(port) + path;
        DBG(msg);
        webSocket.broadcastTXT(msg.c_str(), msg.length());

        ret = ESPhttpUpdate.update(host, port, path);
        if (ret != HTTP_UPDATE_OK){
            server->send ( 304, "text/plain", "Update not OK" );
            return;
        }
        server->send ( 200, "text/plain", "Update Complete!" );
  } );

  /* Example /gpio?foo=bar:
      Method: GET
      Arguments: 1
      foo: bar
      */
    server->on ( "/gpio", [server]() {
        String msg;
        DBG("URI: " + server->uri());
        DBG("Method: ");DBG(( server->method() == HTTP_GET ) ? "GET" : "POST");
        for ( uint8_t i = 0; i < server->args(); i++ ) {
            int name = server->argName(i).toInt();
            int value = server->arg(i).toInt();
            msg = "Setting ";
            msg+= name;
            msg+= " = ";
            msg+= value;
            DBG(msg);
            webSocket.broadcastTXT(msg.c_str(), msg.length());
            pinMode(name,OUTPUT); digitalWrite(name,value);
        }

        server->send ( 200, "text/plain", msg );
    });

    /* args are 0=type, 1=code, 2=bits 3,4,5,... RAW -- length is from server->args */
    server->on ( "/irsend", [server]() {
        decode_type_t type;
        uint32_t code;
        uint32_t bits;
        uint32_t buffer[100];
        String msg;
        
        DBG("URI: " + server->uri());
        DBG("Method: ");DBG(( server->method() == HTTP_GET ) ? "GET" : "POST");
        if ( server->args() > 103){ //buffer overflow check
            msg = "IR buffer full. Aborting";
            DBG(msg);
            server->send ( 200, "text/plain", msg );            
            return;
        }
        if ( server->args() < 3){ //buffer overflow check
            msg = "Incomplete arguments (< 3). Aborting";
            DBG(msg);
            server->send ( 200, "text/plain", msg );            
            return;
        }

        type = (decode_type_t) server->arg(0).toInt();
        code = server->arg(1).toInt();
        bits = server->arg(2).toInt();
                
        // TODO support repeat-values according to example z3t0/Arduino-IRremote/.../IRrecord.ino
        switch (type){
            case UNKNOWN:
                DBG("Sending Raw... Pray");
                //skip [type,code,bits]
                for ( uint8_t i = 3; i < server->args(); i++ ) { 
                    buffer[i-3] =  server->arg(i).toInt();
                }
                irsend.sendRaw(buffer, server->args() - 3 ,36);
                break;
            case NEC:
                msg = "Sending NEC " + String(code, HEX) + "  " + String(bits);
                DBG(msg);
                irsend.sendNEC(code, bits);
                break;
            case SONY:
                msg = "Sending SONY " + String(code, HEX) + "  " + String(bits);
                DBG(msg);
                irsend.sendSony(code, bits);
                break;
            case RC5:
                msg = "Sending RC5 " + String(code, HEX) + "  " + String(bits);
                code = code | (1 << (bits - 1));
                DBG(msg);
                irsend.sendRC5(code, bits);
                break;
            case RC6:
                msg = "Sending RC6 " + String(code, HEX) + "  " + String(bits);
                code = code | (1 << (bits - 1));
                DBG(msg);
                irsend.sendRC6(code, bits);
                break;
            default:
                ;
        }
        server->send ( 200, "text/plain", "Complete!" );
  });

  server->onNotFound ( [server]() {
    String message = "File Not Found\n\nURI: ";
    message += server->uri();
    message += "\nMethod: ";
    message += ( server->method() == HTTP_GET ) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server->args();
    message += "\n";
    for ( uint8_t i = 0; i < server->args(); i++ ) {
      message += " " + server->argName ( i ) + ": " + server->arg ( i ) + "\n";
    }
    server->send ( 404, "text/plain", message );
  });
  server->serveStatic("/", SPIFFS, "/index.html");
}

    
// Call this after IRrecv::decode()
void dump(decode_results *results) {
    uint32 buf[104];  // [ type (NEC), code (0xBEEF), bits (36), length<=100, raw_data_with_legth_size ]
    String msg;
    
    //buffer overflow protection
    if (results->rawlen > 100){
        msg = "IR > 100 length. Ignored!";
        webSocket.broadcastTXT(msg.c_str(), msg.length());
        return;
    }
    
    buf[0] = results->decode_type;
    buf[1] = results->value;
    buf[2] = results->bits;
    buf[3] = results->rawlen;
    
    //ignore repeats
    if ( (buf[0] == 0xFFFFFFFF) || (buf[1] == 0xffffffff) ){
        msg = String("Ignoring Repeated IR");
        webSocket.broadcastTXT(msg.c_str(), msg.length());
        return;
    }
    memcpy((void*) &buf[4], (void*) results->rawbuf, sizeof(uint32) * results->rawlen);
    //dbg
    msg = "type: " + String(buf[0]) + " value: "  + String(buf[1], HEX);
    msg += String(" Raw (") + String(buf[3], DEC)+"): ";
    webSocket.broadcastTXT(msg.c_str(), msg.length());
    for (int i = 1; i < buf[3]; i++) {
        if (i & 1) {
        Serial.print(results->rawbuf[i]*USECPERTICK, DEC);
        }
        else {
        Serial.write('-');
        Serial.print((unsigned long) results->rawbuf[i]*USECPERTICK, DEC);
        }
        Serial.print(" ");
    }
    Serial.println();
    // debug end

    
    //brutal websockets needs uint8 and you will need to pack  uint32 -> uint8 * 4  (hopefully both are little endian)
    webSocket.broadcastBIN( (uint8_t*) buf, sizeof(buf) ); //sizeof is already in bytes
}



void setup(void){
    //see if this required and send a patch for  enableIRIn()
    pinMode(RECV_PIN,INPUT_PULLUP);

    Serial.begin(115200);
    Serial.println();
    Serial.println("Booting Sketch...");
    //brd.startRGB();
    //brd.setRGB(0xFF,0,0);

    Utils::connect("/cred");
    //brd.setRGB(0,0,0xFF);

    MDNS.begin(host);  
    myFsUpdater(&httpServer); // inject my SPIFS endpoints
    httpServer.begin();
    MDNS.addService("http", "tcp", 80);

    webSocket.begin();
    webSocket.onEvent(Utils::webSocketEvent);

    irrecv.enableIRIn(); // Start the receiver
    irsend.begin(); //setup sender

    //brd.setRGB(0,0xFF,0); // all green;)
}

void loop(void){
  httpServer.handleClient();
  yield();
  webSocket.loop();
  yield();
  if (irrecv.decode(&results)) {
    Serial.println(results.value, HEX);
    dump(&results);
    irrecv.resume(); // Receive the next value
  }
  delay(1);
  /// enable captive portal for AP
  if(WiFi.getMode() == WIFI_AP){
      dnsServer.processNextRequest();
  }
} 
