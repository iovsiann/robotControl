// https://github.com/scottchiefbaker/ESP-WebOTA
// Arduino build process info: https://github.com/arduino/Arduino/wiki/Build-Process

#define WEBOTA_VERSION "0.1.5"

#include "WebOTA.h"
#include <Arduino.h>
#include <WiFiClient.h>

#ifdef ESP32
#include <WebServer.h>
//#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>
#endif

#ifdef ESP8266
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
//#include <ESP8266mDNS.h>
#endif

WebOTA webota;

long WebOTA::max_sketch_size() {
  long ret = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;

  return ret;
}

// R Macro string literal https://en.cppreference.com/w/cpp/language/string_literal
const char ota_version_html[] PROGMEM = "<h1>WebOTA Version: " WEBOTA_VERSION "</h1>";

const char ota_upload_form[] PROGMEM = R"!^!(

<form method="POST" action="#" enctype="multipart/form-data" id="upload_form">
    <input type="file" name="update" id="file">
    <input type="submit" value="Update">
</form>

<div id="prg_wrap" style="border: 0px solid; width: 100%;">
   <div id="prg" style="text-shadow: 2px 2px 3px black; padding: 5px 0; display: none; border: 1px solid #008aff; background: #002180; text-align: center; color: white;"></div>
</div>

<script>
var domReady = function(callback) {
  document.readyState === "interactive" || document.readyState === "complete" ? callback() : document.addEventListener("DOMContentLoaded", callback);
};

domReady(function() {
  var myform = document.getElementById('upload_form');
  var filez  = document.getElementById('file');

  myform.onsubmit = function(event) {
    event.preventDefault();

    var formData = new FormData();
    var file     = filez.files[0];

    if (!file) { return false; }

    formData.append("files", file, file.name);

    var xhr = new XMLHttpRequest();
    xhr.upload.addEventListener("progress", function(evt) {
      if (evt.lengthComputable) {
        var per = Math.round((evt.loaded / evt.total) * 100);
        var prg = document.getElementById('prg');

        prg.innerHTML     = per + "%"
        prg.style.width   = per + "%"
        prg.style.display = "block"
      }
    }, false);
    xhr.open('POST', location.href, true);

    // Set up a handler for when the request finishes.
    xhr.onload = function () {
      if (xhr.status === 200) {
        //alert('Success');
      } else {
        //alert('An error occurred!');
      }
    };

    xhr.send(formData);
   }
});
</script>)!^!";


#ifdef ESP8266
void WebOTA::init(ESP8266WebServer *server, const char *path) {
#endif
#ifdef ESP32
void WebOTA::init(WebServer *server, const char *path) {
#endif
  // Index page
  //server->on("/", HTTP_GET, [server]() {
  //  server->send(200, "text/html", "<h1>WebOTA</h1>");
  //});

  // Upload firmware page
  server->on(path, HTTP_GET, [server,this]() {
    String html = "";
    //if (this->custom_html != NULL) {
    //  html += this->custom_html;
    //} else {
    html += FPSTR(ota_version_html);
    //}

    html += FPSTR(ota_upload_form);
    server->send_P(200, "text/html", html.c_str());
  });

  // Handling uploading firmware file
  server->on(path, HTTP_POST, [server,this]() {
    server->send(200, "text/plain", (Update.hasError()) ? "Update: fail\n" : "Update: OK!\n");
    //delay(500);


    // If the MCU is in a delay() it cannot respond to HTTP OTA requests
    // We do a "fake" looping delay and listen for incoming HTTP requests while waiting
    // Borrowed from mshoe007 @ https://github.com/scottchiefbaker/ESP-WebOTA/issues/8
    decltype(millis()) last = millis();

    while ((millis() - last) < 500) {
      server->
      handleClient();
      ::delay(5);
    }
    
    ESP.restart();
  }, [server,this]() {
    HTTPUpload& upload = server->upload();

    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Firmware update initiated: %s\r\n", upload.filename.c_str());

      //uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      uint32_t maxSketchSpace = this->max_sketch_size();

      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }

      // Store the next milestone to output
      uint16_t chunk_size  = 51200;
      static uint32_t next = 51200;

      // Check if we need to output a milestone (100k 200k 300k)
      if (upload.totalSize >= next) {
        Serial.printf("%dk ", next / 1024);
        next += chunk_size;
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("\r\nFirmware update successful: %u bytes\r\nRebooting...\r\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
}

/*
void WebOTA::set_custom_html(char const * const html) {
  this->custom_html = html;
}
///////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////

String ip2string(IPAddress ip) {
  String ret = String(ip[0]) + "." +  String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);

  return ret;
}
*/
