#ifdef ESP8266
  #include <ESP8266WebServer.h>
  #include <ESP8266WiFi.h>  // Install https://github.com/esp8266/arduino-esp8266fs-plugin
  #include "FS.h" // Include SPIFFS filesystem
  // Board      Generic ESP8285 Module
  // Upload Speed    115200
  // CPU Frequency 80 MHz
  // Crystal Frequency 26 MHz
  // Flash Size    2M (FS: 256KB OTA: ~896KB)
  // [SPIFFS] upload  : C:\Users\[user]\AppData\Local\Temp\.../uStepperWiFi.spiffs.bin
  #define ESPWebServer ESP8266WebServer
  #define LED_PIN 4
  #define INVERT_LED false
#endif

#ifdef ESP32
  #include <WebServer.h>
  #include <WiFi.h>
  #include <ESPmDNS.h>
  #include <SPIFFS.h> // Install https://github.com/me-no-dev/arduino-esp32fs-plugin/releases/
  #define ESPWebServer WebServer
  #define RXD2 16
  #define TXD2 17
  #define LED_PIN 2
  #define INVERT_LED true
#endif

#include <WebSocketsServer.h>
#include "GCode.h" // Include GCode class
#include "WebOTA.h"

ESPWebServer server(80);
WebSocketsServer websocket = WebSocketsServer(81);
GCode gcode;

const char* VERSION = "0.1.0";
const char *ssid = "uStepper-Arm";
const char *password = "12345679";

bool isRecording = false;
bool playRecording = false;
uint32_t lastPackage = 0;
char * response = NULL;
int32_t playStepsDelay = 0;
// Led blinking variables
bool ledState = LOW;
float servoValue = 0.0;
bool pumpState = false;
uint8_t statusLed = LED_PIN;
uint32_t previousBlink = 0;
float playBackValue = 10.0;

// Local path to save the GCode recordings
char recordPath[] = "/recording.txt";
uint16_t recordLineCount = 0;
bool positionReached = false;
double x, y, z;

void setup() {
  // Init Serial port for UART communication
  Serial.begin(115200
    #ifdef ESP32
    , SERIAL_8N1, RXD2, TXD2
    #endif
  );
  gcode.setSerialPort(&Serial);
  gcode.setBufferSize(1);

  // Built-in LED is pin 5
  pinMode(statusLed, OUTPUT);
  initSPIFFS();

  webota.init(&server, "/webota");

  initWiFi();
  initWebsocket();
  initWebserver();

  /* String str = "";
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
          str += dir.fileName();
      str += " / ";
      str += dir.fileSize();
      str += "\r\n";
    }
    Serial.print(str);
  } */
}
    
void loop() {

  websocket.loop();
  server.handleClient();
//#ifdef ESP8266
//  MDNS.update();
//#endif

  // Listen for messages coming from master
  if (gcode.run()) {

    // Read newest response from fifo buffer
    response = gcode.getNextValue();

    // If response is a position, pull the values from the message and send to ESP
    if (gcode.check((char *)"POS", response) ) {
      gcode.value((char *)"X", &x);
      gcode.value((char *)"Y", &y);
      gcode.value((char *)"Z", &z);
      
      websocket.broadcastTXT(response);
    }

    // For knowing when the latest sent line from the recording is reached
    else if (gcode.check((char *)"REACHED", response)) {
      websocket.broadcastTXT("LINE REACHED");  // Debugging
      positionReached = true;
      playStepsDelay = millis() + 1000;
    }
  
    else {
      // Probably an error or other important information, send it to GUI for inspection
      websocket.broadcastTXT(response);
    }
  }

  if (playRecording) {

    // Send next line from the recording
    if( positionReached ){
      if(millis() > playStepsDelay)
      {
        positionReached = false;
    
        playNextLine();
      }
    }
    
  }

  if (millis() - lastPackage < 500) {
    if (millis() - previousBlink >= 100) {
      previousBlink = millis();
      ledState = !ledState;
      digitalWrite(statusLed, ledState ^ INVERT_LED);
    }
  } else {
    if (WiFi.softAPgetStationNum() > 0) 
      digitalWrite(statusLed, LOW ^ INVERT_LED);
    else
      digitalWrite(statusLed, HIGH ^ INVERT_LED);
  }
}


void playNextLine( void ){
  
  File file = SPIFFS.open(recordPath, "r");
  file.setTimeout(0); // Set timeout when no newline was found (no timeout plz).

  // Buffer to load each line into
  char buf[50];
  uint8_t len = 0;

  // Read through all lines until the wanted line is reached... Is there a better way?
  for(uint16_t i = 0; i <= recordLineCount; i++){
    memset(buf, 0, sizeof(buf));
    
    len = file.readBytesUntil('\n', buf, 50);
  }

  // Check if any line was read
  if (len != 0){
    
    // Append null termination to the buffer for good measure
    buf[len] = '\0'; 
    
    char command[100] = {'\0'};
    
    strcat(command, "G1 ");
    strcat(command, buf);
    strcat(command, " F"); // Set feedrate
    strcat(command, String(playBackValue).c_str()); //to playBackValue

    // For debugging
    // websocket.broadcastTXT("Playing line " + String(recordLineCount) + ": " + String(command));
    
    gcode.send(command, false); // False = do not add checksum

    recordLineCount++;
  }else{
    recordLineCount = 0;  
    playRecording = false;
    playStepsDelay = 0;
  }
}

void saveData(char *data) {

  // Open file and keep appending
  File f = SPIFFS.open(recordPath, "a");

  f.println(data);
  f.close();
}

void clearData( void ){
// Open file and keep appending
  File f = SPIFFS.open(recordPath, "w");

  f.print("");
  f.close();
}

// Process data being send from GUI by webSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t len) {

  // Pointer to hold reference to received payload
  char *packet;
  static bool test = 0;
  // Buffer for temporary storage
  char commandBuffer[50] = {'\0'};

  if (type == WStype_TEXT) {
    lastPackage = millis();

    packet = (char *)payload;
    
    // Start new recording
    if (strstr(packet, "M2 ")) {
      clearData();
      isRecording = true;
      return;
    }

    // Stop recording
    else if (strstr(packet, "M3 ")) {
      isRecording = false;
    }

    // Sniff Pump state (on)
    else if (strstr(packet, "M5 ")) {
      pumpState = true;
    }

    // Sniff Pump state (off)
    else if (strstr(packet, "M6 ")) {
      pumpState = false;
    }

    // Sniff servo pos
    else if (strstr(packet, "M4 ")) {
      Serial.println(packet);
      char *start;
      char *end;
      size_t len;
      int i;

      char buf[20] = {'\0'};

      // Find start of parameter value
      if (start = strstr(packet, "S")) {

        start++; // Not interested in the param name
        for(i = 0; *start >= '0' && *start <= '9'; i++)
        {
          buf[i] = *start++;
        }
        buf[i] = '\0';

        // Now convert the string in buf to a float
        servoValue = atof(buf);
      }
    }

    // Play recording
    else if (strstr(packet, "M11 ")) {
      Serial.println(packet);
      char *start;
      char *end;
      size_t len;
      int i;

      char buf[20] = {'\0'};

      // Find start of parameter value
      if (start = strstr(packet, "F")) {

        start++; // Not interested in the param name
        for(i = 0; (*start >= '0' && *start <= '9'); i++)
        {
          buf[i] = *start++;
        }
        buf[i] = '\0';

        // Now convert the string in buf to a float
        playBackValue = atof(buf);
      }

      if( playRecording ){
        // Reset playback of recording
        recordLineCount = 0;  
      }
      playRecording = true;
      positionReached = true; // In order to get playNextLine started.

      websocket.broadcastTXT("PLAY RECORDING"); // Debugging
      return;
    }

    // Pause recording
    else if (strstr(packet, "M12 ")) {
      playRecording = false;
      websocket.broadcastTXT("PAUSE RECORDING"); // Debugging
      return;
    }

    // Add line to recording
    else if (strstr(packet, "M13 ")) {
      char temp[50] = {'\0'};
      String recording = "X" + String(x) + " Y" + String(y) + " Z" + String(z) + " S" + String(servoValue) + " P" + String((uint8_t)pumpState);
      //String recording = "X" + String(x) + " Y" + String(y) + " Z" + String(z);
      recording.toCharArray(temp, sizeof(temp));
  
      saveData(temp);
      return;
    }

    // Emergency stop?
    else if (strstr(packet, "M0 ")) {
      playRecording = false;
      gcode.send((char *)"M10 X0.0 Y0.0 Z0.0");
    }

    // If a M10 command (xyz speeds) is received while playing drop the message
    else if( strstr( packet, "M10 ")){
      if(playRecording)
        return;
    }
    
    // Afterwards, just pass the data on to the uStepper
    strcpy(commandBuffer, packet);

    // Send GCode over UART
    gcode.send(commandBuffer);
  }
}

void initWebsocket(void) {
  websocket.begin();
  websocket.onEvent(webSocketEvent);
}

void initWebserver(void) {
  // Page handlers
  server.serveStatic("/", SPIFFS, "/index.html"); // Main website structure
  server.serveStatic("/assets/css/framework.css", SPIFFS, "/assets/css/framework.css"); // Responsive framework for the GUI
  server.serveStatic("/assets/css/fonticons.css", SPIFFS, "/assets/css/fonticons.css"); // Icon pack
  server.serveStatic("/assets/css/style.css", SPIFFS, "/assets/css/style.css"); // Main website style
  server.serveStatic("/assets/js/script.js", SPIFFS,"/assets/js/script.js"); // Javascript functionalities
  server.serveStatic("/assets/font/fonticons.ttf", SPIFFS,"/assets/font/fonticons.ttf"); // Javascript functionalities
  server.serveStatic("/assets/logo.png", SPIFFS,"/assets/logo.png"); // Javascript functionalities
  server.serveStatic(recordPath, SPIFFS, recordPath);
  server.on("/upload", HTTP_POST,[](){ server.send(200); }, uploadJob );
  server.begin();
}

void initWiFi(void) {
  if (!WiFi.softAP(ssid, password)) {
    Serial.println("Failed to initialise WiFi");
  }
  /*
  int init_wifi(const char *ssid, const char *password, const char *mdns_hostname) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("");
  Serial.print("Connecting to Wifi");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.printf("Connected to '%s'\r\n\r\n",ssid);

  //String ipaddr = ip2string(WiFi.localIP());
  //Serial.printf("IP address   : %s\r\n", ipaddr.c_str());
  //Serial.printf("MAC address  : %s \r\n", WiFi.macAddress().c_str());

  init_mdns(mdns_hostname);

  return 1;
  */
}

/*
  int init_mdns(const char *host) {
  // Use mdns for host name resolution
  if (!MDNS.begin(host)) {
    Serial.println("Error setting up MDNS responder!");

    return 0;
  }

  Serial.printf("mDNS started : %s.local\r\n", host);

  webota.mdns = host;

  return 1;
*/

void initSPIFFS(void) {
  // Begin file-system
  if (!SPIFFS.begin(
    #ifdef ESP32
    true
    #endif
    ))
  {
    Serial.println("Failed to initialise SPIFFS");
  }
}

File recordFile;

// Upload a new file to the SPIFFS
void uploadJob( void ){

  HTTPUpload& upload = server.upload();
   
  if( upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;

    if( ! filename.equals("recording.txt") ){
      server.send(500, "text/plain", "Wrong filename");
      return;
    }else{
      if(!filename.startsWith("/")) filename = "/"+filename;
    
      // Open the file for writing in SPIFFS (create if it doesn't exist)
      recordFile = SPIFFS.open(filename, "w");  
    }
           
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(recordFile){
       recordFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
    }
     
  } else if(upload.status == UPLOAD_FILE_END){
    if(recordFile) { // If the file was successfully created
      recordFile.close(); // Close the file again
      server.send(303);
    } else {
      server.send(500, "text/plain", "Couldn't create file");
    }
  }
}
