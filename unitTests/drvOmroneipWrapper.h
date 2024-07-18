#ifndef drvOmroneipWrapper_H
#define drvOmroneipWrapper_H

#include "drvOmroneip.h"

class drvOmronEIPWrapper : public drvOmronEIP {
public:
   drvOmronEIPWrapper(const char *portName,
               const char *gateway,  //IP address that ethernet/IP packets are sent to initially
               const char *path,  //path to device from the initial gateway
               const char *plcType, //used to change the PLC type from omron-njnx to other PLC types supported by libplctag
               int debugLevel, //libplctag debug level
               double timezoneOffset); //time in hours that the PLC is ahead/behind of UTC
   asynStatus wrap_createPoller(const char *portName, const char *pollerName, double updateRate, int spreadRequests);
   void wrap_initialiseDrvUser(omronDrvUser_t *newDrvUser, const drvInfoMap keyWords, int tagIndex, std::string tag, bool readFlag, const asynUser *pasynUser);
   virtual ~drvOmronEIPWrapper();
};

class omronEIPPollerWrapper : public omronEIPPoller {
  public:
      omronEIPPollerWrapper(const char* portName, const char* pollerName, double updateRate, int spreadRequests);
      virtual ~omronEIPPollerWrapper();
};

#endif