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
#include <epicsEndian.h>
#include <epicsExit.h>
#include <cantProceed.h>
#include <errlog.h>
#include <osiSock.h>
#include <iocsh.h>

/* libplctag include*/
#include "libplctag.h"

/* Asyn includes */
#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include "asynCommonSyncIO.h"
#include "asynParamType.h"


#include <epicsExport.h>

#define MAX_TAG_LENGTH 100
#define LIB_PLC_TAG_PROTOCOL "protocol=ab-eip&gateway=10.2.2.57&path=18,10.2.2.57&plc=omron-njnx"


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
  MAX_OMRON_DATA_TYPES
} omronDataType_t;

struct omronDrvUser_t;

class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
    drvOmronEIP(const char *portName,
                const char *plcType);

    bool omronExiting_;
    
    void readPoller();
    bool checkTagStatus(int32_t tagStatus); // Checks a tags status, if bad, prints an error message and returns false
    std::unordered_map<std::string, std::string> drvInfoParser(const char *drvInfo);
    asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)override;
protected:

private:
  bool initialized_; // Tracks if the driver successfully initialized
  bool cats_;
  std::array<int, MAX_OMRON_DATA_TYPES> recordTypeCounters_; /* Keeps track of how many of each type of record we have. */
  std::vector<int> readIndexList_; /* A list of parameters which are scheduled to read data at the next polling interval */
  std::unordered_map<int, omronDrvUser_t*> tagMap_; /* Maps the index of each registerd param to the EIP data registered in the PV */
  omronDrvUser_t *drvUser_;   /* Drv user structure */ 
};
