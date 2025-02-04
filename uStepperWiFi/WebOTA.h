#include <Arduino.h>

#ifdef ESP8266
#include <ESP8266WebServer.h>
#endif
#ifdef ESP32
#include <WebServer.h>
#endif

class WebOTA {
  public:
    unsigned int port;
    String path = "";
    //String mdns = "";

    void delay(unsigned int ms);

#ifdef ESP8266
    void init(ESP8266WebServer *server, const char *path);
#endif
#ifdef ESP32
    void init(WebServer *server, const char *path);
#endif

    //int handle();

    //void set_custom_html(char const * const html);

  private:
    //bool init_has_run;
    //char const * custom_html = NULL;
    //String get_ota_html();
    long max_sketch_size();
};

//int init_wifi(const char *ssid, const char *password, const char *mdns_hostname);

extern WebOTA webota;
