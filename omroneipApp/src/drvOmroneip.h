/* ANSI C includes  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <iostream>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <vector>
#include <list>
#include <array>
#include <chrono>

/* EPICS includes */
#include <dbAccess.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsTime.h>
#include <alarm.h>
#include <epicsEndian.h>
#include <epicsExit.h>
#include <cantProceed.h>
#include <errlog.h>
#include <osiSock.h>
#include <iocsh.h>
#include <initHooks.h>

/* libplctag include*/
#include "libplctag.h"

/* Asyn includes */
#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include "asynCommonSyncIO.h"
#include "asynParamType.h"


#include <epicsExport.h>

/* libplctag also supports modbus-tcp but we do not*/
#define DATA_TIMEOUT 1000 //ms


typedef enum {
  dataTypeBool,
  dataTypeInt,
  dataTypeDInt,
  dataTypeLInt,
  dataTypeUInt,
  dataTypeUDInt,
  dataTypeULInt,
  dataTypeUInt_BCD,
  dataTypeUDInt_BCD,
  dataTypeULInt_BCD,
  dataTypeReal,
  dataTypeLReal,
  dataTypeString,
  dataTypeWord,
  dataTypeDWord,
  dataTypeLWord,
  dataTypeUDT,
  dataTypeTIME,
  MAX_OMRON_DATA_TYPES
} omronDataType_t;

struct omronDrvUser_t;
class omronEIPPoller;

class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
  drvOmronEIP(const char *portName,
              char *gateway,
              char *path,
              char *plcType);

  bool omronExiting_;
  
  void readPoller();
  asynStatus createPoller(const char * portName, const char * pollerName, double updateRate);
  std::unordered_map<std::string, std::string> drvInfoParser(const char *drvInfo);
  asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)override;
  /* Must be implemented to support non I/O Interrupt records reading UDTs, not required for other asyn interfaces where defaults are used*/
  asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)override;
  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value)override;
  asynStatus writeInt64(asynUser *pasynUser, epicsInt64 value)override;
  asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value)override;
  asynStatus writeOctet(asynUser *pasynUser, const char * value, size_t nChars, size_t* nActual)override;
protected:

private:
  bool initialized_; // Tracks if the driver successfully initialized
  bool cats_;
  std::string tagConnectionString_;
  std::unordered_map<std::string, omronEIPPoller*> pollerList_ = {};
  std::unordered_map<int, omronDrvUser_t*> tagMap_; /* Maps the index of each registerd param to the EIP data registered in the PV */
  omronDrvUser_t *drvUser_;
  /* Drv user structure */
};

class omronEIPPoller{
  public:
      omronEIPPoller(const char* portName, const char* pollerName, double updateRate);
      const char* belongsTo_;
      const char* pollerName_;
      double updateRate_;
};
