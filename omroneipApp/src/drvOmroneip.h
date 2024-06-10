/* C++ includes */
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
#include <iterator>
#include <sstream>

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

typedef struct
{
  omronDataType_t dataType;
  const char *dataTypeString;
} omronDataTypeStruct;


// This stores information about each communication tag to the PLC.
// A new instance will be made for each record which requsts to uniquely read/write to the PLC
struct omronDrvUser_t
{
  int32_t tagIndex; //Index of tag returned by libplctag
  std::string pollerName; //Optional name of poller which reads this asyn parameter
  bool readFlag; //Whether the poller should read this tag
  double timeout; //Timeout starting from when a read request is sent to the PLC
  std::string tag; //Tag string sent to libplctag when creating the tag
  size_t startIndex; //Index for addressing an array element in the PLC
  size_t sliceSize; //Number of array elements to return
  std::string dataType; //CIP datatype
  std::string optimisationFlag; //Contains a string describing the optimisation state of an asyn parameter
  size_t strCapacity; //Size of the string within the PLC
  size_t tagOffset; //Bytes offset within a tags data to read from
  size_t UDTreadSize; //Number of bytes to read from a UDT. Can be paired with tagOffset to read specific chunks of data from a UDT
};

typedef std::unordered_map<std::string, std::vector<std::string>> structDtypeMap;
typedef std::unordered_map<std::string, std::string> drvInfoMap;

struct omronDrvUser_t;
class omronEIPPoller;
class omronUtilities;

/* Main class for the driver */
class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
   const char *driverName = "drvOmronEIP"; /* String for asynPrint */
   drvOmronEIP(const char *portName,
               char *gateway,
               char *path,
               char *plcType,
               int debugLevel);
   ~drvOmronEIP();


   bool omronExiting_;

   /* All reading of data is initiated from this function which runs at a predefined frequency. Each poller runs this function
    * in its own thread. Each record which is registered with a named poller will call the readData function with its asynIndex
    * and drvUser */
   void readPoller();
   /* This waits for requested reads to come in and then takes them from libplctag and puts them into records */
   void readData(omronDrvUser_t* drvUser, int asynIndex);
   /* Creates a new instance of the omronEIPPoller class and starts a new thread named after this new poller which reads data linked to the poller name.*/
   asynStatus createPoller(const char * portName, const char * pollerName, double updateRate);
   /* Reimplemented from asynDriver. This is called when each record is loaded into epics. It processes the drvInfo from the record and attempts
      to create a libplctag tag and an asynParameter. It saves the handles to these key objects within the tagMap_. This tagMap_ is then used to
      process read and write requests to the driver.*/
   asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)override;
   /* Improves efficiency by merging duplicate tags and looking for situations where multiple UDT field read requests can be replaced with a single UDT read */
   asynStatus optimiseTags();
   /* Takes a csv style file, where each line contains a structure name followed by a list of datatypes within the struct
   Stores the user input struct as a map containing Struct:field_list pairs. It then calls createStructMap and passes this map */   
   asynStatus loadStructFile(const char * portName, const char * filePath);

   /* 
    * Write interfaces reimplemented from asynPortDriver. Write data from the asynParameter to the associated libplctag tag
    */
   /* This interface is used to write both UDTs and WORD, DWORD and LWORDS*/
   asynStatus writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)override;
   asynStatus writeInt16Array(asynUser *pasynUser, epicsInt16 *value, size_t nElements)override;
   asynStatus writeInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements)override;
   asynStatus writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value, size_t nElements)override;
   asynStatus writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements)override;

   asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)override;
   asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value)override;
   asynStatus writeInt64(asynUser *pasynUser, epicsInt64 value)override;
   asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value)override;
   asynStatus writeOctet(asynUser *pasynUser, const char * value, size_t nChars, size_t* nActual)override;

private:
   bool initialized_; // Tracks if the driver successfully initialized
   bool startPollers_;
   epicsEventId firstReadCompleteEvent_; // Triggered by the drvInitPoller finishing its poll
   std::string tagConnectionString_; // Stores the basic PLC connection information common to all read/write requests
   // The key is the name of the struct, the vector is a list of byte offsets within the structure
   std::unordered_map<std::string, std::vector<int>> structMap_;
   /* The key is the name of the struct, the vector contains strings representing the dtypes and embbed structs/arrays. 
   Used to match user requests to offsets in the structMap */
   structDtypeMap structDtypeMap_;
   structDtypeMap structRawMap_;
   std::unordered_map<std::string, omronEIPPoller*> pollerList_ = {}; // Stores the name of each registered poller
   /* Maps the index of each registered asynParameter to essential communications data for the parameter */
   std::unordered_map<int, omronDrvUser_t*> tagMap_;
   omronUtilities *utilities; //smart pointerify
   friend class omronUtilities;
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

/* Class which contains generic functions required by the driver */
class omronUtilities {
public:
   drvOmronEIP *pDriver;
   asynUser *pasynUserSelf;
   const char * driverName;
   omronUtilities(drvOmronEIP *pDriver);
   ~omronUtilities();

   /* Loops through each structure within the map and calls findOffsets which creates the final structure offset map */
   asynStatus createStructMap(structDtypeMap rawMap);
   /* Recursively extract datatypes from an embedded array, this array may contain embedded structs in which case expandStructsRecursive is called */
   std::vector<std::string> expandArrayRecursive(structDtypeMap const& rawMap, std::string arrayDesc);
   /* Recursively extract datatypes from embedded structures and list them in the correct order */
   std::vector<std::string> expandStructsRecursive(structDtypeMap const& rawMap, std::string structName);
   /* Calculate the alignment rules of an embedded structure or array, if nextItem is a structure then lookup the largest item in the structure.
      If nextItem is an array, then follow the alignment rule of the item after nextItem, it this item is a struct, then look up the largest
      item in this struct */
   int getEmbeddedAlignment(structDtypeMap const& expandedMap, std::string structName, std::string nextItem, size_t i);
   /* A recursive function which is passed a row from the raw map and calculates the offset of each datatype in the row. If the datatype
      is the name of another structure, then the size of this structure must first be calculated by calling this function with the row for
      this structure. This process is repeated untill the offset for the lowest common structure is found which is returned by this function.
      The function then works its way back up. structMap is updated with the calculated offsets. */
   int findOffsets(structDtypeMap const& expandedMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap);
   /* Finds the datatype which an array type is defined with*/
   std::string findArrayDtype(structDtypeMap const& expandedMap, std::string arrayDesc);
   /* Returns the largest standard datatype within a structure, this includes embedded structures and arrays and is used for calculating padding*/
   int getBiggestDtype(structDtypeMap const&, std::string structName);
   /* Takes the elements of a user defined structure requested by the user in the drvInfo string and finds their offsets
      from the structMap_. The main purpose of this algorithm is to skip over embedded structs/arrays as we count through the map.
      For example, if I had a structure A with 2 elements, but the first was a 100byte structure, referencing A[1] would need to skip over this
      100 byte structure. */
   int findRequestedOffset(std::vector<size_t> indices, std::string structMap);
   /* This is responsible for parsing drvInfo when records are created. It takes the drvInfo string and parses it for required data.
      It returns a map of all of the data required by the driver to setup the asyn parameter and a boolean which indicates the validity of the data. 
      This function is called twice, once before and once after database initialization, the first time we just return asynDisabled. */
   drvInfoMap drvInfoParser(const char *drvInfo);
};