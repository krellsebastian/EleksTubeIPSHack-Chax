#include "WiFi.h"
#include "HardwareSerial.h"
#include <cmath>
#include <string>
#include "esp32-hal.h"
#include "WebPoll.h"
#include <HTTPClient.h>

void WebPoll::begin(StoredConfig::Config::PollingEndpointList *endpoints, TFTs* t) {
  scfg = endpoints;
  if(scfg->is_valid == StoredConfig::valid)
  {
    tCfgRot = scfg->tRot;
    tCfgPoll = scfg->tPoll;
  }
  tfts = t;
  for(int i = 0; i < StoredConfig::endpoint_buffer_size; i++)
  {
    endpoint_meta[i].failedCount = FAILCOUNT;
    endpoint_meta[i].lastValidAmount = 0;
    endpoint_meta[i].tLastPoll = 0;
  }
  
  //http.setReuse(false);
  http.setConnectTimeout(500);
  http.setTimeout(500);
}

void WebPoll::listEndpointsOnSerial() {
  Serial.println("Valid Endpoints:");
  
  if(scfg->is_valid == StoredConfig::valid)
  {
    for(int i = 0; i < StoredConfig::endpoint_buffer_size; i++)
    {
      if(scfg->ep[i].is_valid == StoredConfig::valid){
        std::string report = "URL: " + std::string(scfg->ep[i].endpointUrl) + ", filename: " + std::string(scfg->ep[i].logoFile);
        Serial.println(report.c_str());
      }
    }
  }
}

void WebPoll::loop() {
  unsigned long curMil = millis();
  if(curMil > tLastRot + tCfgRot){
    tLastRot = curMil;
    Serial.println("Running Webpoll loop update");
    StoredConfig::Config::PollingEndpoint* nextEndpoint = getNextValidEndpoint();
    if(nextEndpoint)
    {
      int amount = getEndpointPollFailsafe(curMil);
      drawEndpoint(nextEndpoint, amount);
    }else{
      //no valid Endpoint
    }
  }
}

int WebPoll::getEndpointPollFailsafe(unsigned long curMil)
{
  int amount = endpoint_meta[lastValidEndpoint].lastValidAmount;
  if(curMil > endpoint_meta[lastValidEndpoint].tLastPoll + tCfgPoll)
  {
    Serial.println("Running Poll");
    endpoint_meta[lastValidEndpoint].tLastPoll = curMil;
    int pollamount = pollEndpoint(&(scfg->ep[lastValidEndpoint]));
    if(pollamount < 0 && endpoint_meta[lastValidEndpoint].failedCount-- <= 0)
    {
      //Endpoint failed for multiple times
      amount = pollamount;
      endpoint_meta[lastValidEndpoint].lastValidAmount = pollamount;
      Serial.println("Poll Failed");
      Serial.print("Wifi connected: "); Serial.println(WiFi.isConnected());
      Serial.print("WiFi Status: "); Serial.println(WiFi.status());
    }else{
      if(pollamount >= 0)
      {
        //endpoint updated
        amount = pollamount;
        endpoint_meta[lastValidEndpoint].lastValidAmount = pollamount;
        endpoint_meta[lastValidEndpoint].failedCount = FAILCOUNT;
      } //else endpoint failed for a few times
    }
  }
  return amount;
}

void WebPoll::drawEndpoint(StoredConfig::Config::PollingEndpoint* ep, int amount){
  //reset all screens
  tfts->chip_select.setDigit(5);
  tfts->drawSdJpeg(("/"+std::string(ep->logoFile)).c_str(),0,0);
  if(amount < 0)
  {
    //no amount, paint black and write on it the error
    tfts->chip_select.setDigitMap(0x1B);
    tfts->drawSdJpeg("/b.jpg",0,0);
    tfts->chip_select.setDigit(2);
    tfts->drawSdJpeg("/q.jpg",0,0);
    tfts->chip_select.setDigit(0);
    tfts->setTextColor(TFT_WHITE, TFT_BLACK);
    tfts->setCursor(0, 0, 2);
    tfts->println("Error getting data.");
    tfts->print("WIFI: ");tfts->println(WiFi.isConnected()?"true":"false");
    tfts->print("Errorcode: ");tfts->println(amount);
  }else{
    Serial.println("Drawing valid update. amount:");
    Serial.println(amount);

    bool isThousands = false;
    if(amount > 99999)
    {
      isThousands = true;
      amount = amount / 100;
    }
    //paint all needed digits
    for(int i = 4; i >= 0; i--)
    {
      int lampInd = i;
      int div = pow(10,i);

      if(isThousands && lampInd == 0)
      {
        tfts->chip_select.setDigit(lampInd);
        tfts->drawSdJpeg("/k.jpg",0,0);
      }else if(amount / div > 0 || (amount == 0 && lampInd == 0))
      {
        int drawdigit = (amount / div) % 10;
        tfts->setDigit(lampInd, drawdigit, TFTs::force);
      }else{
        tfts->chip_select.setDigit(lampInd);
        tfts->drawSdJpeg("/b.jpg", 0, 0);
      }
    }
  }
}

StoredConfig::Config::PollingEndpoint* WebPoll::getNextValidEndpoint(){
  int nextValid = -1;
  for(int i = lastValidEndpoint +1; i < StoredConfig::endpoint_buffer_size; i++)
  {
    if(scfg->ep[i].is_valid == StoredConfig::valid)
    {
      lastValidEndpoint = i;
      return &(scfg->ep[i]);
    }
  }
  for(int i = 0; i < lastValidEndpoint; i++)
  {
    if(scfg->ep[i].is_valid == StoredConfig::valid)
    {
      lastValidEndpoint = i;
      return &(scfg->ep[i]);
    }
  }
  if(scfg->ep[lastValidEndpoint].is_valid == StoredConfig::valid)
  {
    //return old
    return &(scfg->ep[lastValidEndpoint]);
  }
  //no valid found
  return NULL;
}


int WebPoll::pollEndpoint(StoredConfig::Config::PollingEndpoint* ep){
    Serial.println("Requesting Endpoint");
    Serial.println(ep->endpointUrl);

    http.begin(ep->endpointUrl);
    int httpCode = http.GET();       //Make the request
    Serial.println("Request done");
    if (httpCode == 200) { //Check for the returning code
      String payload = http.getString();
      Serial.println(("received Payload: " +payload).c_str());
      return payload.toInt();
    }else{
      Serial.print("Failed. Status code: ");
      Serial.println(httpCode);
      if(httpCode > 0)
      {
        httpCode *= -1;
      }
    }
    return httpCode;
}
