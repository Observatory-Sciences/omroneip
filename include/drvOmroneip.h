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

  typedef struct {
    std::string attribName;
    bool value;
} libplctagDefaultAttribsStruct;

  //We overwrite some of libplctags defaults for omron PLCs
  std::unordered_map<std::string, bool> libplctagDefaultAttribs_ = {
    {"allow_packing", 1},
    {"str_is_zero_terminated", 0},
    {"str_is_fixed_length", 0},
    {"str_is_counted", 1},
    {"str_count_word_bytes", 2},
    {"str_pad_bytes", 0}
  };
  std::unordered_map<int, omronDrvUser_t*> tagMap_; /* Maps the index of each registerd param to the EIP data registered in the PV */
  omronDrvUser_t *drvUser_;   /* Drv user structure */ 
};
