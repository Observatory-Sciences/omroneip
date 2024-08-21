#ifndef drvOmroneip_H
#define drvOmroneip_H

#include "omronUtilities.h"

/* C++ includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctime>
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
#include <bitset>

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
#define MAX_CIP_MESSAGE_SIZE 1996 //includes a 2 bytes "CIP Sequencer Count" header
#define MAX_CIP_MESSAGE_DATA_SIZE 1994

typedef std::pair<std::string, uint16_t> omronDataType_t;
typedef std::unordered_map<std::string, std::vector<std::string>> structDtypeMap;
typedef std::unordered_map<std::string, std::string> drvInfoMap;

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
  omronDataType_t dataType; //CIP datatype string and byte size
  std::string optimisationFlag; //Contains a string describing the optimisation state of an asyn parameter
  size_t strCapacity; //Size of the string within the PLC
  size_t tagOffset; //Bytes offset within a tags data to read from
  size_t offsetReadSize; //Can be paired with tagOffset to read specific chunks of data from a UDT or string
  bool readAsString; //Whether to output data as a string (only valid for TIME dtypes atm)
  bool optimise; //if 0 then we use the offset to look within a datatype, if 1 then we use it to get a datatype from within an array/UDT
};


class omronEIPPoller;
class omronUtilities;

/* Main class for the driver */
class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
   const char *driverName = "drvOmronEIP"; /* String for asynPrint */
   drvOmronEIP(const char *portName,
               const char *gateway,  //IP address that ethernet/IP packets are sent to initially
               const char *path,  //path to device from the initial gateway
               const char *plcType, //used to change the PLC type from omron-njnx to other PLC types supported by libplctag
               int debugLevel, //libplctag debug level
               double timezoneOffset); //time in hours that the PLC is ahead/behind of UTC
   ~drvOmronEIP();

   std::vector<omronDataType_t> omronDataTypeList = {
      {"BOOL", 2},
      {"SINT", 1},
      {"USINT", 1},
      {"INT", 2},
      {"UINT", 2},
      {"DINT", 4},
      {"UDINT", 4},
      {"LINT", 8},
      {"ULINT", 8},
      {"REAL", 4},
      {"LREAL", 8},
      {"STRING", 0}, //we do not know the size of a string as they are variable sizes
      {"WORD", 2},
      {"DWORD", 4},
      {"LWORD", 8},
      {"UDT", 0}, //we do not know the size of a UDT as they are variable sizes
      {"TIME", 8} //this "TIME" dtype actually represents 4 Omron specific time codes, all of which are 8 bytes and treated the same
   };

   /* All reading of data is initiated from this function which runs at a predefined frequency. Each poller runs this function
    * in its own thread. This function sends read requests to the PLC and then calls readData() which gets the data from libplctag */
   void readPoller();
   /* Each record which is registered with a named poller will call the readData function with its asynIndex
    * and drvUser. It waits for previously requested reads to come in and then takes the data from libplctag and puts it into records */
   void readData(omronDrvUser_t* drvUser, int asynIndex);
   /* Creates a new instance of the omronEIPPoller class and starts a new thread named after this new poller which reads data linked to the poller name.*/
   asynStatus createPoller(const char * portName, const char * pollerName, double updateRate, int spreadRequests);
   /* Reimplemented from asynDriver. This is called when each record is loaded into epics. It processes the drvInfo from the record and attempts
      to create a libplctag tag and an asynParameter. It saves the handles to these key objects within the tagMap_. This tagMap_ is then used to
      process read and write requests to the driver.*/
   asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)override;
   /* Copy and convert values from the keyWords map returned from drvInfoParser into newDrvUser*/
   void initialiseDrvUser(omronDrvUser_t *newDrvUser, const drvInfoMap keyWords, int tagIndex, std::string tag, bool readFlag, const asynUser *pasynUser);
   
   /* Improves efficiency by looking for situations where multiple UDT field read requests can be replaced with a single UDT read */
   asynStatus optimiseTags();
   /* Fill commonStructMap with the names of the structures to be read and the asyn indexes which need to read from them*/
   asynStatus findOptimisableTags(std::unordered_map<std::string, std::vector<int>> &commonStructMap);
   /* Look within the commonStructMap for structs that are part of an array of structs and see if it is possible to download a slice
      of the array rather than each element seperately */
   asynStatus findArrayOptimisations(std::unordered_map<std::string, std::vector<int>> &commonStructMap, std::unordered_map<std::string, std::vector<int>> &commonArrayMap);
   /* Create tags which read a slice of an array of structs. Update drvUser structs with appropriate tag index and offsets. */
   asynStatus createOptimisedArrayTags(std::unordered_map<std::string, int> &structIDMap, std::unordered_map<std::string, std::vector<int>> const commonStructMap, std::unordered_map<std::string, std::vector<int>> &commonArrayMap, std::unordered_map<int, std::string> &structTagMap);
   /* Create a new libplctag tag to read each struct from commonStructMap. The index of the new tag is stored along with the struct name in
      the structIDMap */
   asynStatus createOptimisedTags(std::unordered_map<std::string, int> &structIDMap, std::unordered_map<std::string, std::vector<int>> const commonStructMap, std::unordered_map<int, std::string> &structTagMap);
   /* Now that the new tags have been created, we must link them to the correct asynParamater within tagMap_ and update other details*/
   asynStatus updateOptimisedParams(std::unordered_map<std::string, int> const structIDMap, std::unordered_map<std::string, std::vector<int>> const commonStructMap, std::unordered_map<int, std::string> const structTagMap);

   /* Takes a csv style file, where each line contains a structure name followed by a list of datatypes within the struct
   Stores the user input struct as a map containing Struct:field_list pairs. It then calls createStructMap and passes this map */   
   asynStatus loadStructFile(const char * portName, const char * filePath);

   /* 
    * Write interfaces reimplemented from asynPortDriver. They write data from the asynParameter to the associated libplctag tag
    */
   /* The writeInt8Array interface is used to write UDTs, WORD, DWORD, LWORDS, SINT, USINT and others*/
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
   bool startPollers_; // Tells the pollers when to start polling
   size_t libplctagTagCount = 0;
   size_t asynParamCount = 0;
   double timezoneOffset_; // Used to convert TIME data from the PLCs timezone
   std::string tagConnectionString_; // Stores the basic PLC connection information common to all libplctag tags
   /* Maps the index of each registered asynParameter to essential communications data for the parameter */
   std::unordered_map<int, omronDrvUser_t*> tagMap_;
   std::unordered_map<std::string, omronEIPPoller*> pollerList_ = {}; // Stores the name of each registered poller
   // The key is the name of the struct, the vector is a list of byte offsets within the structure
   std::unordered_map<std::string, std::vector<int>> structMap_;
   /* The key is the name of the struct, the vector contains strings representing the dtypes and embbed structs/arrays. 
   Used to match user requests to offsets in the structMap */
   structDtypeMap structDtypeMap_;
   /* Stores the struct definition data loaded in by the user. Where the key is the structure name and the vector of strings contains the 
      datatypes. */
   structDtypeMap structRawMap_;
   omronUtilities *utilities;
   friend class omronUtilities;
};

/* Class which stores information about each poller */
class omronEIPPoller{
  public:
      omronEIPPoller(const char* portName, const char* pollerName, double updateRate, int spreadRequests);
      ~omronEIPPoller();
      const char* belongsTo_;
      const char* pollerName_;
      double updateRate_;
      int spreadRequests_;
      int myTagCount_;
};

#endif