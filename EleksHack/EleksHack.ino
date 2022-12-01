/*
 * Alterantive firmware for the EleksTubeIPS digital clock 
 * 
 * Instructions to build this sketch are found at
 * https://github.com/frankcohen/EleksTubeIPSHack/blob/main/README.md
 * 
 * Licensed under GPL v3
 * (c) Frank Cohen, All rights reserved. fcohen@votsh.com
 * Read the license in the license.txt file that comes with this code.
 * May 30, 2021
 * 
 */

#include <WiFi.h>
#include <HTTPSServer.hpp>
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>
#include <HTTPBodyParser.hpp>
#include <HTTPMultipartBodyParser.hpp>
#include <HTTPURLEncodedBodyParser.hpp>
#include <HTTPClient.h>

#define FS_NO_GLOBALS
#include <FS.h>
#include "Hardware.h"
#include "Backlights.h"
#include "ChipSelect.h"
#include "TFTs.h"
#include <TJpg_Decoder.h>
#include "Buttons.h"
#include "WebPoll.h"

// The HTTPS Server comes in a separate namespace. For easier use, include it here.
using namespace httpsserver;

// Don't use SPIFFS, it's deprecated, use LittleFS instead
//https://arduino-esp8266.readthedocs.io/en/latest/filesystem.html#spiffs-deprecation-warning
// From Arduino IDE Library Manager install LittleFS_esp32
#include <LittleFS.h> 

/* SSID & Password for the EleksTube to be a Web server */
const char* WIFI_SSID = "Chax Tube";  // Enter SSID here
const char* WIFI_PSK = "chaxtube";  //Enter Password here

IPAddress local_ip(192,168,1,1);
IPAddress gateway(192,168,1,1);
IPAddress subnet(255,255,255,0);

SSLCert * cert;
HTTPSServer * secureServer;

// Declare handler functions for the various URLs on the server
void handleRoot(HTTPRequest * req, HTTPResponse * res);
void handle404(HTTPRequest * req, HTTPResponse * res);
void handle_menu(HTTPRequest * req, HTTPResponse * res);
void handle_connectwifi(HTTPRequest * req, HTTPResponse * res);
void handle_connect(HTTPRequest * req, HTTPResponse * res);
void handle_manage(HTTPRequest * req, HTTPResponse * res);
void handle_favicon(HTTPRequest * req, HTTPResponse * res);
void handle_upload(HTTPRequest * req, HTTPResponse * res);
void handle_uploadform(HTTPRequest * req, HTTPResponse * res);
void handle_success(HTTPRequest * req, HTTPResponse * res);
void handle_delete(HTTPRequest * req, HTTPResponse * res);
void handle_endpoints(HTTPRequest * req, HTTPResponse * res);

void addResHeader(HTTPResponse * res);
void addResFooter(HTTPResponse * res);

long  gmtOffset_sec = 3600;
int   daylightOffset_sec = 3600;

File fsUploadFile;

#define SCREENWIDTH 135
#define SCREENHEIGHT 240

#include <TFT_eSPI.h> // Hardware-specific library
TFTs tfts;    // Display module driver

//Clock uclock;
StoredConfig stored_config;
Buttons button_class;
HTTPClient http;
WebPoll web_poll;
Backlights back_lights;

boolean playImages = false;
boolean playVideos = false;
boolean playClock = false;

#define BGCOLOR    0xAD75
#define GRIDCOLOR  0xA815
#define BGSHADOW   0x5285
#define GRIDSHADOW 0x600C
#define RED        0xF800
#define WHITE      0xFFFF

#include "mbedtls/base64.h"

File root;

// Borrowed from https://github.com/zenmanenergy/ESP8266-Arduino-Examples/blob/master/helloWorld_urlencoded/urlencode.ino

String urlencode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    char code2;
    for (int i =0; i < str.length(); i++){
      c=str.charAt(i);
      if (c == ' '){
        encodedString+= '+';
      } else if (isalnum(c)){
        encodedString+=c;
      } else{
        code1=(c & 0xf)+'0';
        if ((c & 0xf) >9){
            code1=(c & 0xf) - 10 + 'A';
        }
        c=(c>>4)&0xf;
        code0=c+'0';
        if (c > 9){
            code0=c - 10 + 'A';
        }
        code2='\0';
        encodedString+='%';
        encodedString+=code0;
        encodedString+=code1;
        //encodedString+=code2;
      }
      yield();
    }
    return encodedString;   
}

unsigned char h2int(char c)
{
    if (c >= '0' && c <='9'){
        return((unsigned char)c - '0');
    }
    if (c >= 'a' && c <='f'){
        return((unsigned char)c - 'a' + 10);
    }
    if (c >= 'A' && c <='F'){
        return((unsigned char)c - 'A' + 10);
    }
    return(0);
}

String urldecode(String str)
{
    String encodedString="";
    char c;
    char code0;
    char code1;
    for (int i =0; i < str.length(); i++){
        c=str.charAt(i);
      if (c == '+'){
        encodedString+=' ';  
      }else if (c == '%') {
        i++;
        code0=str.charAt(i);
        i++;
        code1=str.charAt(i);
        c = (h2int(code0) << 4) | h2int(code1);
        encodedString+=c;
      } else{
        
        encodedString+=c;  
      }
      
      yield();
    }
    
   return encodedString;
}

static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do 
  {
    // feel free to do something here
  } while (millis() - start < ms);
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
   // Stop further decoding as image is running off bottom of screen
  if ( y >= tfts.height() ) return 0;
  // This function will clip the image block rendering automatically at the TFT boundaries
  tfts.pushImage(x, y, w, h, bitmap);
  // Return 1 to decode next block
  return 1;
}

void setup() {
  Serial.begin(115200);
  smartDelay(500);

  Serial.println();
  Serial.println( "EleksTube IPS Alternative Firmware" );
  Serial.println();
  
  randomSeed(analogRead(0));
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  char rx_byte = 0;

  if(!LittleFS.begin()){
      Serial.println("LITTLEFS/SPIFFS begin failed");
      Serial.println("Type Y and click Submit to format the SPIFFS");

      while (1)
      {
        while (Serial.available() > 0)
        {
          char rx_byte = Serial.read();
          if (rx_byte == 'Y')
          {
            Serial.println("Formatting..." );
            LittleFS.format();
            Serial.println("Formatting complete. Power cycle your clock.");
           }
        }
      }
  }
  
  tfts.begin();
  TJpgDec.setCallback(tft_output);

  tfts.fillScreen(TFT_BLACK);
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.chip_select.setDigit(5);
  tfts.setCursor(0, 0, 2);
  tfts.println("Booting Chax Tubes");

  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();
  
  stored_config.begin();
  stored_config.load();
  if(stored_config.config.wifi.is_valid == StoredConfig::valid)
  {
    Serial.println("Connecting to WLAN");
    tfts.println("Connecting to WLAN");
    tfts.println(("SSID: "+std::string(stored_config.config.wifi.ssid)).c_str());

    smartDelay(1000);
    WiFi.begin(stored_config.config.wifi.ssid, stored_config.config.wifi.password); 
    uint count = 7;
    while (WiFi.status() != WL_CONNECTED && count > 0) {
      delay(1500);
      Serial.println("Connecting to WiFi..");
      tfts.println("Connecting to WiFi..");
      count --;
    }
    if(WiFi.status() == WL_CONNECTED)
    {
      tfts.setTextColor(TFT_BLUE, TFT_BLACK );
      tfts.println("Wifi coneccted!");
      tfts.setTextColor(TFT_WHITE, TFT_BLACK);
      WiFi.setAutoReconnect(true);
    }else{
      tfts.setTextColor(TFT_RED, TFT_BLACK );
      tfts.println("Wifi failed, booting without!");
      tfts.setTextColor(TFT_WHITE, TFT_BLACK);
      WiFi.disconnect();
      WiFi.setAutoReconnect(false);
    }
  }else{
      tfts.setTextColor(TFT_RED, TFT_BLACK );
      tfts.println("No WiFi configured, booting without!");
      tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  tfts.setTextColor(TFT_GREEN, TFT_BLACK );
  tfts.println("Ready");
  delay(500);
  runSplashScreen();
  tfts.chip_select.setAll();
  
  //tfts.beginJpg();
  //uclock.begin(&stored_config.config.uclock);
  web_poll.begin(&stored_config.config.endpoints, &tfts);
  //updateClockDisplay(TFTs::force);
  button_class.begin();

  stored_config.config.backlights.is_valid = 0;
  back_lights.begin(&stored_config.config.backlights);
  back_lights.setPattern(Backlights::rainbow);
  
  Serial.println( "setup() done" );
}

void runSplashScreen(){
  int screenWidth = 135;
  int splashWidth = 117;
  int stepWidth = 12;
  const char* splash = "/splash.jpg";
  int off_left_center = 18;
  int off_top_center = (240-90)/2;

  Serial.println("Heap free size:");
  Serial.println(heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.println("Heap biggest block free size:");
  Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  File splashFile = LittleFS.open(splash,"r");
  uint32_t fsz = splashFile.size();
  Serial.println("Slpash size:");
  Serial.println(fsz);

  uint8_t* splashMemory = (uint8_t*) heap_caps_malloc(fsz, MALLOC_CAP_8BIT);
  splashFile.read(splashMemory,fsz);

  tfts.chip_select.setAll();
  tfts.fillScreen(TFT_BLACK);
  tfts.drawJpeg(splashMemory, off_left_center, off_top_center, fsz);
  delay(1000);
  tfts.chip_select.setDigitMap(0x3E);
  tfts.fillScreen(TFT_BLACK);

  //start move
  struct MoveMeta{
    uint8_t curLed;
    uint8_t nextLed;
    int off_cur;
    int off_next;
  };
  MoveMeta mm = {0,1,off_left_center,screenWidth};
  while(mm.curLed != 5 || mm.off_cur > off_left_center)
  {
    mm.off_cur -= stepWidth;
    
    tfts.chip_select.setDigit(mm.curLed);
    tfts.drawJpeg(splashMemory, mm.off_cur, off_top_center,fsz);

    if(mm.off_cur < 0)
    {
      mm.off_next = screenWidth + mm.off_cur + stepWidth;
      tfts.chip_select.setDigit(mm.nextLed);
      tfts.drawJpeg(splashMemory, mm.off_next, off_top_center,fsz);
    }

    if(mm.off_cur < splashWidth * -1)
    {
      //clean this, move to next
      tfts.chip_select.setDigit(mm.curLed);
      tfts.fillScreen(TFT_BLACK);

      mm.curLed = mm.nextLed;
      mm.off_cur = mm.off_next;
      mm.nextLed = mm.curLed + 1;
      mm.off_next = screenWidth;
    }
  }
  heap_caps_free(splashMemory);
  delay(1000);
  Serial.println("Done with splash");
}

bool ServerStarted = false;
void startupServer(){
  if(!ServerStarted)
  {
    tfts.chip_select.setDigit(5);
    tfts.fillScreen(TFT_BLACK);
    tfts.setCursor(0, 0, 2);
    tfts.setTextColor(TFT_BLUE, TFT_BLACK );
    tfts.println("Booting Webserver");
    Serial.println("Creating a new self-signed certificate.");
    Serial.println("This may take up to a minute");

    // First, we create an empty certificate:
    cert = new SSLCert();

    // Now, we use the function createSelfSignedCert to create private key and certificate.
    // The function takes the following paramters:
    // - Key size: 1024 or 2048 bit should be fine here, 4096 on the ESP might be "paranoid mode"
    //   (in generel: shorter key = faster but less secure)
    // - Distinguished name: The name of the host as used in certificates.
    //   If you want to run your own DNS, the part after CN (Common Name) should match the DNS
    //   entry pointing to your ESP32. You can try to insert an IP there, but that's not really good style.
    // - Dates for certificate validity (optional, default is 2019-2029, both included)
    //   Format is YYYYMMDDhhmmss
    int createCertResult = createSelfSignedCert(
      *cert,
      KEYSIZE_2048,
      "CN=myesp32.local,O=FancyCompany,C=DE",
      "20190101000000",
      "20300101000000"
    );

    if (createCertResult != 0) {
      Serial.printf("Create certificate failed. Error Code = 0x%02X, check SSLCert.hpp for details", createCertResult);
      tfts.setTextColor(TFT_BLUE, TFT_BLACK );
      tfts.println("Certificate failed.");
      while(true) delay(500);
    }
    Serial.println("Created the certificate successfully");
    Serial.println("Connect using WIFI");
    Serial.println(("SSID:"+ std::string(WIFI_SSID)).c_str());
    Serial.println(("Password:" + std::string(WIFI_PSK)).c_str());
    Serial.println("then browse");
    Serial.println("https://192.168.1.1");
    Serial.println("for a menu");
    Serial.println("of commands");

    tfts.setTextColor(TFT_WHITE, TFT_BLACK);
    tfts.println("AP Booted.");
    tfts.println(("SSID: "+ std::string(WIFI_SSID)).c_str());
    tfts.println(("Password: " + std::string(WIFI_PSK)).c_str());
    tfts.println("then browse");
    tfts.println("https://192.168.1.1");
    tfts.println("");
    tfts.println("Booting Server...");

    // TODO: Store certificate and the key in StoredConfig. This has the advantage that the certificate stays the same after a reboot
    // so your client still trusts your server, additionally you increase the speed-up of your application.
    // Some browsers like Firefox might even reject the second run for the same issuer name (the distinguished name defined above).
    //
    // Storing:
    //   For the key:
    //     cert->getPKLength() will return the length of the private key in bytes
    //     cert->getPKData() will return the actual private key (in DER-format, if that matters to you)
    //   For the certificate:
    //     cert->getCertLength() and ->getCertData() do the same for the actual certificate data.
    // Restoring:
    //   When your applications boots, check your non-volatile storage for an existing certificate, and if you find one
    //   use the parameterized SSLCert constructor to re-create the certificate and pass it to the HTTPSServer.
    //
    // A short reminder on key security: If you're working on something professional, be aware that the storage of the ESP32 is
    // not encrypted in any way. This means that if you just write it to the flash storage, it is easy to extract it if someone
    // gets a hand on your hardware. You should decide if that's a relevant risk for you and apply countermeasures like flash
    // encryption if neccessary

    WiFi.mode(WIFI_MODE_APSTA);
    smartDelay(1000);

    WiFi.softAP(WIFI_SSID, WIFI_PSK);
    WiFi.softAPConfig(local_ip, gateway, subnet);
    
    // Use the new certificate to setup our server
    secureServer = new HTTPSServer(cert);

    // For every resource available on the server, we need to create a ResourceNode
    // The ResourceNode links URL and HTTP method to a handler function
    ResourceNode * nodeRoot    = new ResourceNode("/", "GET", &handle_menu);
    ResourceNode * node404     = new ResourceNode("", "GET", &handle404);
    ResourceNode * nodeWifi    = new ResourceNode("/wifi", "GET", &handle_connectwifi);
    ResourceNode * nodeConnect    = new ResourceNode("/connect", "GET", &handle_connect);
    ResourceNode * nodeDir    = new ResourceNode("/dir", "GET", &handle_manage);
    ResourceNode * nodeManage    = new ResourceNode("/manage", "GET", &handle_manage);
    ResourceNode * nodeFavicon    = new ResourceNode("/favicon.ico", "GET", &handle_favicon);
    ResourceNode * nodeUpload    = new ResourceNode("/upload", "POST", &handle_upload);
    ResourceNode * nodeUploadform    = new ResourceNode("/uploadform", "GET", &handle_uploadform);
    ResourceNode * nodeSuccess    = new ResourceNode("/success", "GET", &handle_success);
    ResourceNode * nodeDelete    = new ResourceNode("/delete", "GET", &handle_delete);
    ResourceNode * nodeFormat    = new ResourceNode("/format", "GET", &handle_format);
    ResourceNode * nodeEndpointsConf    = new ResourceNode("/endpoints", "GET", &handle_endpoints);
    ResourceNode * nodeEndpoints    = new ResourceNode("/endpoints", "POST", &handle_enpoints_update);

    // Add the root node to the server
    secureServer->registerNode(nodeRoot);
    secureServer->registerNode(node404);
    secureServer->registerNode(nodeWifi);
    secureServer->registerNode(nodeConnect);
    secureServer->registerNode(nodeDir);
    secureServer->registerNode(nodeManage);
    secureServer->registerNode(nodeFavicon);
    secureServer->registerNode(nodeUpload);
    secureServer->registerNode(nodeUploadform);
    secureServer->registerNode(nodeSuccess);
    secureServer->registerNode(nodeDelete);
    secureServer->registerNode(nodeFormat);
    secureServer->registerNode(nodeEndpointsConf);
    secureServer->registerNode(nodeEndpoints);

    Serial.println("Starting server...");
    secureServer->start();
    Serial.println("Ready");
    tfts.println("Done");
    ServerStarted = true;
    smartDelay(5000);

  }
}

void handle_favicon(HTTPRequest * req, HTTPResponse * res)
{
  res->setStatusCode(404);
  res->setStatusText("Not Found");

  res->setHeader("Content-Type", "text/html");
  res->setStatusCode(404);
  addResHeader(res);
  res->println("No favicon available");
  addResFooter(res); 
}

void addResHeader(HTTPResponse * res) 
{
  res->setHeader("Content-Type", "text/html");
  res->setStatusCode(200);

  res->println("<html><head><style>html { font-family: Arial, sans-serif; color:black; display: inline-block; margin: 0px auto; text-align: left; font-size:20px; background-color: WhiteSmoke;} a:visited {color: black;}</style>");
  res->println("<title>ChaxTube Controller</title>\n");
  res->println("</head><body>");
}

void addResFooter(HTTPResponse * res) 
{
  res->println("</body>");
  res->println("</html>");  
}

void handle_NotFound(HTTPRequest * req, HTTPResponse * res) {
  Serial.println("handle_NotFound()");

  addResHeader(res);
  res->println("No handler for that URL");  
  addResFooter(res); 
}

void handle404(HTTPRequest * req, HTTPResponse * res) {
  // Discard request body, if we received any
  // We do this, as this is the default node and may also server POST/PUT requests
  req->discardRequestBody();

  // Set the response status
  res->setStatusCode(404);
  res->setStatusText("Not Found");

  // Set content type of the response
  res->setHeader("Content-Type", "text/html");

  // Write a tiny HTTP page
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Not Found</title></head>");
  res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
  res->println("</html>");
}

void handle_enpoints_update(HTTPRequest * req, HTTPResponse * res) {
  Serial.println("handle_endpoints_update()");

  std::string active_id = "ep_active_";
  std::string url_id = "ep_url_";
  std::string file_id = "ep_file_";
  std::string tmp;

  HTTPURLEncodedBodyParser parser(req);
  if(parser.endOfField() > 0)
  {
    Serial.println("Parser found, starting reset");
    stored_config.config.endpoints.tRot = web_poll.tCfgRot;
    stored_config.config.endpoints.tPoll = web_poll.tCfgPoll;
    stored_config.config.endpoints.is_valid = StoredConfig::valid;
    //reset all to inactive
    for(int i = 0; i < StoredConfig::endpoint_buffer_size; i++)
    {
      StoredConfig::Config::PollingEndpoint *ep = &(stored_config.config.endpoints.ep[i]);
      ep->is_valid = 0;
    }
  }
  while(parser.nextField()) {
    if(parser.getFieldName().compare("epRot") == 0)
    {
      char buf[512];
      size_t readLength = parser.read((byte *)buf, 512);
      uint16_t rotTime = atoi(buf);
      if(rotTime > 0)
      {
        stored_config.config.endpoints.tRot = rotTime;
        web_poll.tCfgRot = rotTime;
      }
    }
    if(parser.getFieldName().compare("epPoll") == 0)
    {
      char buf[512];
      size_t readLength = parser.read((byte *)buf, 512);
      uint16_t pollTime = atoi(buf);
      if(pollTime > 0)
      {
        stored_config.config.endpoints.tPoll = pollTime;
        web_poll.tCfgPoll = pollTime;
      }
    }


    if(parser.getFieldName().find("ep_") != std::string::npos)
    {
      uint8_t endpoints_ind = parser.getFieldName().at(parser.getFieldName().length()-1) - '0';
      Serial.println(("Updating Endpoint " + intToString(endpoints_ind) + " " + parser.getFieldName()).c_str());
      StoredConfig::Config::PollingEndpoint *ep = &(stored_config.config.endpoints.ep[endpoints_ind]);

      if(parser.getFieldName().find(active_id) != std::string::npos)
      {
        ep->is_valid = StoredConfig::valid;
      }

      if(parser.getFieldName().find(url_id) != std::string::npos)
      {
        char buf[512];
        size_t readLength = parser.read((byte *)buf, 512);
        strcpy(ep->endpointUrl, std::string(buf, readLength).c_str());
      }

      if(parser.getFieldName().find(file_id) != std::string::npos)
      {
        char buf[512];
        size_t readLength = parser.read((byte *)buf, 512);
        strcpy(ep->logoFile, std::string(buf, readLength).c_str());
      }
    }
  }

  stored_config.save();
  Serial.println("Saved");
  web_poll.listEndpointsOnSerial();
  handle_endpoints(req, res);
}

void handle_endpoints(HTTPRequest * req, HTTPResponse * res) {
  Serial.println("handle_endpoints()");
  addResHeader(res);
  res->println("<h1>Enpoint Config</h1>");

  std::string active_id = "ep_active_";
  std::string url_id = "ep_url_";
  std::string file_id = "ep_file_";
  std::string tmp;

  //list Filenames
  res->println("<p>Possible Filenames:</br><ul>");

  String path = "/";
  File root = LittleFS.open(path);
  path = String();

  if(root.isDirectory()){
      File file = root.openNextFile();
      while(file){
        res->println("<li>");
        res->print(file.name());
        res->println("</li>");
        file = root.openNextFile();
      }
  }else{
    res->println("None...");
  }
  res->println("</ul></p></br>");


  res->println("<h3>Settings:</h3>");
  res->println("<form method='POST' action='/endpoints'>");
  res->print("<label for='epRot'>Rotation interval (ms):</label>");
  res->print(("<input type='text' name='epRot' value='"+ intToString(web_poll.tCfgRot) +"'>").c_str());
  res->println("</br>");
  res->print("<label for='epPoll'>Update interval (ms):</label>");
  res->print(("<input type='text' name='epPoll' value='"+ intToString(web_poll.tCfgPoll) +"'>").c_str());
  res->println("</br>");

  for(int i = 0; i < StoredConfig::endpoint_buffer_size; i++)
  {
    StoredConfig::Config::PollingEndpoint *ep = &(stored_config.config.endpoints.ep[i]);
    std::string ind_str = intToString(i);
    std::string ident;

    std::string url;
    std::string logo;
    bool active = false;

    if(ep->is_valid == StoredConfig::valid)
    {
      active = true;
      logo = ep->logoFile;
      url = ep->endpointUrl;
    }
    
    res->print(("<h3>Endpoint "+ intToString(i+1) + "</h3>").c_str());
    
    ident = active_id + ind_str;
    res->print(("<label for='"+ ident +"'>Active:</label>").c_str());
    res->print(("<input type='checkbox' name='"+ ident +"' "+ (active?"checked='checked'":"") + "' value='true'>").c_str());
    res->println("</br>");

    ident = file_id + ind_str;
    res->print(("<label for='"+ ident +"'>Picture File:</label>").c_str());
    res->print(("<input type='text' name='"+ ident +"' value='"+ logo +"'>").c_str());
    res->println("</br>");
    
    ident = url_id + ind_str;
    res->print(("<label for='"+ ident +"'>URL:</label>").c_str());
    res->print(("<input type='text' name='"+ ident +"' value='"+ url +"' size='80'>").c_str());
    res->println("</br></br>");

  }
  res->println("<input type='submit' name='submit' value='Submit'>");
  res->println("</form>");

  addResFooter(res);
}

void handle_format(HTTPRequest * req, HTTPResponse * res){
  Serial.println("handle_format()");

  addResHeader(res);
  res->println("<h1>Formatted SPIFFS file system</h1><br><br>");

  LittleFS.format();

  res->println("<p>Format complete</p>" );

  addResFooter(res);
}

void handle_connectwifi(HTTPRequest * req, HTTPResponse * res) {
  Serial.println("handle_connectwifi()");

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>");
  res->println("<form action='connect' method='GET'>");
  res->println("SSID: ");
  res->println("<input type='text' name='ssid'><br/>");
  res->println("Pass: ");
  res->println("<input type='password' name='pass'><br/>");
  res->println( "<input type=\"submit\" value=\"Submit\">" );
  res->println("</form>");
  res->println("<h3>After setting up the device will restart!</h3>");   
  addResFooter(res); 
}

void handle_connect(HTTPRequest * req, HTTPResponse * res){
  Serial.println("handle_connect()");

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>" );

  std::string ssid;
  req->getParams()->getQueryParameter("ssid", ssid);

  std::string pass;
  req->getParams()->getQueryParameter("pass", pass);

  if(ssid.empty())
  {
    stored_config.config.wifi.is_valid = 0;
    stored_config.save();
  }else{
    strcpy(stored_config.config.wifi.ssid, urldecode(String(ssid.c_str())).c_str());
    strcpy(stored_config.config.wifi.password, urldecode(String(pass.c_str())).c_str());
    stored_config.config.wifi.is_valid = StoredConfig::valid;
    stored_config.save();
  }

  addResFooter(res); 
  delay(1000);
  ESP.restart();
}

void handle_menu(HTTPRequest * req, HTTPResponse * res)
{
  Serial.println( "Handle Menu" );

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1>" );
  
  // Not yet, still neeed to figure out TFT_eSPI's DMA mode for MPEGs
  // probably not going to happen. EleksTube IPS has less than 4 Mbytes of memory.
  // instead, see the ReflectionsOS project. It has Gbytes of memory.
  // https://github.com/frankcohen/ReflectionsOS
  // res->println("<p><a href=\"/movies\">Play movies</a></p>" );
  
  res->println("<p><a href=\"/manage\">Manage media</a></p>" );
  res->println("<p><a href=\"/endpoints\">Manage Endpoints</a></p>" );
  res->println("<p><a href=\"/wifi\">Connect to WiFi</a></p>" );

  addResFooter(res); 
}

void handle_manage(HTTPRequest * req, HTTPResponse * res)
{
  Serial.println( "Handle Manage" );

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>" );
  res->println("<p><a href=\"/uploadform\">Upload</a></p>" );
  res->println("<p><a href=\"/format\">Format SPIFFs file system to LITTLEFS</a> (Caution: Will remove all data)</p>");
  res->println("<p><a href=\"/\">Back</a></p>");

  res->println("<br><br><h3>File List:</h3><ul>" );

  String path = "/";
  File root = LittleFS.open(path);
  path = String();

  if(root.isDirectory()){
      File file = root.openNextFile();
      while(file){

        res->println("<li>" );
        res->println( (file.isDirectory()) ? "dir: " : "file: " );
        res->println(" " );
        res->println( file.name() );
        res->println(", " );
        res->println( file.size() );
        res->println(", <a href=\"/delete?file=" );
        res->println( urlencode( file.name() ) );
        res->println("\"><button>delete</button></a></li>" );
        file = root.openNextFile();
      }
  }
  res->println("</ul>" );

  res->println("<br>totalBytes=" );
  res->println(LittleFS.totalBytes() );
  res->println(", usedBytes=" );
  res->println(LittleFS.usedBytes() );
  res->println(", freeBytes=" );
  res->println(LittleFS.totalBytes() - LittleFS.usedBytes() );

  addResFooter(res); 
}

void handle_uploadform(HTTPRequest * req, HTTPResponse * res)
{
  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>" );

  res->println("<p>Upload an image or movie</p>" );
  
  res->println("<form action=\"upload\" method=\"post\" enctype=\"multipart/form-data\">"  );
  res->println("<input type=\"file\" name=\"name\">"  );
  res->println("<input class=\"button\" type=\"submit\" value=\"Upload\">"  );
  res->println("</form>"  );

  res->println("<br><br>totalBytes=" );
  res->println(LittleFS.totalBytes() );
  res->println(", usedBytes=" );
  res->println(LittleFS.usedBytes() );
  res->println(", freeBytes=" );
  res->println(LittleFS.totalBytes() - LittleFS.usedBytes() );
  
  res->println("<br><br><p><a href=\"/\">Menu</a></p>" );

  addResFooter(res); 
}

// Borrowed from https://tttapa.github.io/ESP8266/Chap12%20-%20Uploading%20to%20Server.html

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

File file;

void handle_upload(HTTPRequest * req, HTTPResponse * res)
{ 
  Serial.println("Handle_upload");

  HTTPBodyParser *parser;
  parser = new HTTPMultipartBodyParser(req);
  bool didwrite = false;

  bool decode64 = false;
  std::string decval = req->getHeader( "Decode64" );
  String decstr = decval.c_str();
  if ( decstr == "true" )
  {
    decode64 = true;
    Serial.println( "Decode64 = true" );
  }
  
  while(parser->nextField()) {

    std::string name = parser->getFieldName();
    std::string filename = parser->getFieldFilename();
    std::string mimeType = parser->getFieldMimeType();
    Serial.printf("handleFormUpload: field name='%s', filename='%s', mimetype='%s'\n", name.c_str(), filename.c_str(), mimeType.c_str() );
    
    if ( ! (filename.rfind("/", 0) == 0) )
    {
      filename = "/" + filename;
    }
    
    Serial.print("handle_upload Name: "); 
    Serial.println(filename.c_str()  );
    
    fsUploadFile = LittleFS.open( filename.c_str(), "w", true);            // Open the file for writing in SPIFFS (create if it doesn't exist)

    size_t fileLength = 0;
    didwrite = true;
    
    while (!parser->endOfField()) {
      byte buf[512];
      size_t readLength = parser->read(buf, 512);

      if ( decode64 && ( readLength>0 ) )
      {
        size_t outlen;
        unsigned char output[ 512 ];
        
        mbedtls_base64_decode(output, 512, &outlen, buf, readLength);

        fsUploadFile.write(output, outlen);
        fileLength += outlen;
      }
      else
      {
        fsUploadFile.write(buf, readLength);
        fileLength += readLength;
      }

    }
    
    fsUploadFile.close();
    res->printf("<p>Saved %d bytes to %s</p>", (int)fileLength, filename.c_str() );
  }

  if (!didwrite) {
    res->println("<p>Did not write any file contents</p>");
  }
  
  delete parser;
   
  Serial.print( "LittleFS totalBytes=" );
  Serial.print( LittleFS.totalBytes() );
  Serial.print( ", usedBytes=" );
  Serial.print( LittleFS.usedBytes() );
  Serial.print( ", free bytes=" );
  Serial.println( LittleFS.totalBytes() - LittleFS.usedBytes() );
    
  if(didwrite) 
  {                                    // If the file was successfully created
    res->setHeader("Location", "/success");
    res->setStatusCode(303);
  }
  else 
  {
    res->setStatusCode(500);
    res->setStatusText("Upload failed");
  }
}

void handle_success(HTTPRequest * req, HTTPResponse * res)
{
  Serial.println( "Handle_success" );

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>" );

  res->println("<p>Success</p><br><br>" );

  res->println("<form action=\"upload\" method=\"post\" enctype=\"multipart/form-data\">"  );
  res->println("<input type=\"file\" name=\"name\">"  );
  res->println("<input class=\"button\" type=\"submit\" value=\"Upload\">"  );
  res->println("</form>"  );
  
  res->println("<br><br><p><a href=\"/\">Menu</a></p>"  );

  addResFooter(res); 
}

void handle_delete(HTTPRequest * req, HTTPResponse * res)
{
  Serial.println( "Handle_delete");

  addResHeader(res);
  res->println("<h1>ChaxTube Control</h1><br><br>" );

  std::string file;
  req->getParams()->getQueryParameter("file", file);

  file = "\/" + file;
  res->println("<p>Deleting file " );
  res->println( urldecode( file.c_str() ) );
  res->println("</p>" );

  if ( LittleFS.remove( urldecode( file.c_str() ) ) )
  {
    res->println("<p>File deleted</p><br><br>" );
    Serial.println("File deleted");
  } 
  else 
  {
    res->println("<p>Delete failed</p><br><br>" );
    Serial.println("Delete failed");
  }
  
  res->println("<br><br><p><a href=\"/\">Menu</a></p>" );

  addResFooter(res); 
}

long timeForMore = millis();
long timeForMoreSlice = millis();

void loop() {
  if(ServerStarted)
  {
    secureServer->loop();
  }

  back_lights.loop();
  web_poll.loop();

  if(button_class.left.tick() == true)
  {
    Serial.println("Left clicked");
    if(!ServerStarted)
    {
      startupServer();
    }
  }

  if(button_class.right.tick() == true)
  {
    Serial.println("Richt clicked");
    runSplashScreen();
  }

  if(button_class.power.tick() == true)
  {
    Serial.println("Restarting now");
    ESP.restart();
  }
}
