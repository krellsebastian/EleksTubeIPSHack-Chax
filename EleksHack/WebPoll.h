#ifndef WEBPOLL_H
#define WEBPOLL_H

#include <stdint.h>
#include <HTTPClient.h>

/*
 * A simple helper class to call common functions on all buttons at once.
 */

#include "StoredConfig.h"
#include "TFTs.h"

class WebPoll {
  public:
    WebPoll(): scfg(NULL), tLastRot(0), tCfgRot(10000), tCfgPoll(30000), lastValidEndpoint(-1) {}

    void begin(StoredConfig::Config::PollingEndpointList* endpoints, TFTs* t);
    void loop();
    void listEndpointsOnSerial();
    uint16_t tCfgRot;
    uint16_t tCfgPoll;
      
  private: 
    StoredConfig::Config::PollingEndpointList *scfg;
    HTTPClient http;
    TFTs* tfts;

    long tLastRot;
    struct EndpointMeta{
      long tLastPoll;
      int lastValidAmount;
      int failedCount;
    };
    EndpointMeta endpoint_meta[StoredConfig::endpoint_buffer_size];

    int getEndpointPollFailsafe(unsigned long curMil);
    int pollEndpoint(StoredConfig::Config::PollingEndpoint* ep);
    void drawEndpoint(StoredConfig::Config::PollingEndpoint* ep, int amount);
    void updateConfig();
    int lastValidEndpoint;
    StoredConfig::Config::PollingEndpoint* getNextValidEndpoint();
    const uint8_t FAILCOUNT = 3; 

};

#endif // WEBPOLL_H
