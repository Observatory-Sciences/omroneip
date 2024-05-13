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
#include <fstream>

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
#include <epicsExport.h>

/* libplctag include */
#include "libplctag.h"

/* Asyn includes */
#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include "asynCommonSyncIO.h"
#include "asynParamType.h"

#define CREATE_TAG_TIMEOUT 1000 //ms

typedef enum {
  dataTypeBool,
  dataTypeInt,
  dataTypeDInt,
  dataTypeLInt,
  dataTypeUInt,
  dataTypeUDInt,
  dataTypeULInt,
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

/* Main class for the driver */
class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
  drvOmronEIP(const char *portName,
              char *gateway,
              char *path,
              char *plcType,
              int debugLevel);
  ~drvOmronEIP();
  bool omronExiting_;

  /* All reading of data is initiated from this function which runs at a predefined frequency */
  void readPoller();
  /* Takes a csv style file, where each line contains a structure name followed by a list of datatypes within the struct
     Stores the user input struct as a map containing Struct:field_list pairs. It then calls createStructMap and passes this map */   
  asynStatus loadStructFile(const char * portName, const char * filePath);
  /* Loops through each structure within the map and calls findOffsets which creates the final structure offset map */
  asynStatus createStructMap(std::unordered_map<std::string, std::vector<std::string>> rawMap);
  /* Takes the elements of a user defined structure requested by the user in the drvInfo string and finds their offsets
     from the structMap_ */
  size_t findRequestedOffset(std::vector<size_t> indices, std::string structMap);
  /* Recursively extract datatypes from an embedded array, this array may contain embedded structs in which case expandStructsRecursive is called */
  std::vector<std::string> expandArrayRecursive(std::unordered_map<std::string, std::vector<std::string>> const& rawMap, std::string arrayDesc);
  /* Recursively extract datatypes from embedded structures and list them in the correct order */
  std::vector<std::string> expandStructsRecursive(std::unordered_map<std::string, std::vector<std::string>> const& rawMap, std::string structName);
  /* Calculate the alignment rules of an embedded structure or array, if nextItem is a structure then lookup the largest item in the structure.
     If nextItem is an array, then follow the alignment rule of the item after nextItem, it this item is a struct, then look up the largest
     item in this struct */
  size_t getEmbeddedAlignment(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string structName, std::string nextItem, size_t i);
  /* A recursive function which is passed a row from the raw map and calculates the offset of each datatype in the row. If the datatype
     is the name of another structure, then the size of this structure must first be calculated by calling this function with the row for
     this structure. This process is repeated untill the offset for the lowest common structure is found which is returned by this function.
     The function then works its way back up. structMap is updated with the calculated offsets. */
  size_t findOffsets(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap);
  /* Finds the datatype which an array type is defined with*/
  std::string findArrayDtype(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string arrayDesc);
  /* Returns the largest standard datatype within a structure, this includes embedded structures and arrays and is used for calculating padding*/
  size_t getBiggestDtype(std::unordered_map<std::string, std::vector<std::string>> const&, std::string structName);
  /* Creates a new instance of the omronEIPPoller class and starts a new thread named after this new poller which reads data linked to the poller name.*/
  asynStatus createPoller(const char * portName, const char * pollerName, double updateRate);
  /* Reimplemented from asynDriver. This is called when each record is loaded into epics. It processes the drvInfo from the record and attempts
     to create a libplctag tag and an asynParameter. It saves the handles to these key objects within the tagMap_. This tagMap_ is then used to
     process read and write requests to the driver.*/
  asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)override;
  /* This is responsible for parsing drvInfo when records are created. It takes the drvInfo string and parses it for required data.
     It returns a map of all of the data required by the driver to setup the asyn parameter and a boolean which indicates the validity of the data. 
     This function is called twice, once before and once after database initialization, the first time we just return asynDisabled. */
  std::unordered_map<std::string, std::string> drvInfoParser(const char *drvInfo);
  /* Improves efficiency by merging duplicate tags and looking for situations where multiple UDT field read requests can be replaced with a single UDT read */
  asynStatus optimiseTags();
  
  /* The read interface only needs to be reimplemented for int8Arrays as the other read interfaces have helper functions to set the value of the
     asynParameters that they write to */
  asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)override;

  /* Write interfaces reimplemented from asynPortDriver. Write data from the asynParameter to the associated libplctag tag*/
  asynStatus writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)override;
  asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)override;
  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value)override;
  asynStatus writeInt64(asynUser *pasynUser, epicsInt64 value)override;
  asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value)override;
  asynStatus writeOctet(asynUser *pasynUser, const char * value, size_t nChars, size_t* nActual)override;

private:
  bool startPollers_;
  bool initialized_; // Tracks if the driver successfully initialized
  std::string tagConnectionString_; // Stores the basic PLC connection information common to all read/write requests
  // The key is the name of the struct, the vector is a list of byte offsets within the structure
  std::unordered_map<std::string, std::vector<int>> structMap_;
  /* The key is the name of the struct, the vector contains strings representing the dtypes and embbed structs/arrays. 
  Used to match user requests to offsets in the structMap */
  std::unordered_map<std::string, std::vector<std::string>> structDtypeMap_;
  std::unordered_map<std::string, std::vector<std::string>> structRawMap_;
  std::unordered_map<std::string, omronEIPPoller*> pollerList_ = {}; // Stores the name of each registered poller
  std::unordered_map<int, omronDrvUser_t*> tagMap_;
  /* Maps the index of each registered asynParameter to essential communications data for the parameter */
};

/* Class which stores information about each poller */
class omronEIPPoller{
  public:
      omronEIPPoller(const char* portName, const char* pollerName, double updateRate);
      ~omronEIPPoller();
      const char* belongsTo_;
      const char* pollerName_;
      double updateRate_;

};
