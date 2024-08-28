#ifndef omronUtilities_H
#define omronUtilities_H

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

/* Asyn includes */
#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include "asynCommonSyncIO.h"
#include "asynParamType.h"

#define CREATE_TAG_TIMEOUT 1000 //ms

typedef std::unordered_map<std::string, std::vector<std::string>> structDtypeMap;
typedef std::unordered_map<std::string, std::string> drvInfoMap;

class drvOmronEIP;

/** Class which contains generic functions required by the driver */
class omronUtilities {
public:
   drvOmronEIP *pDriver;
   asynUser *pasynUserSelf;
   const char * driverName;
   omronUtilities(drvOmronEIP *pDriver);
   ~omronUtilities();

   /** The following group of functions are all used to calculate offsets form structure definition files*/
   /** Loops through each structure within the map and calls findOffsets which creates the final structure offset map */
   asynStatus createStructMap(structDtypeMap rawMap);
   /** Recursively extract a list of all of the datatypes within an embedded array, this array may contain embedded structs in which case 
      expandStructsRecursive is called */
   std::vector<std::string> expandArrayRecursive(structDtypeMap const& rawMap, std::string arrayDesc);
   /** Recursively extract a list of all of the datatypes within an structure and list them in the correct order */
   std::vector<std::string> expandStructsRecursive(structDtypeMap const& rawMap, std::string structName);
   /** Calculate the alignment rules of an embedded structure or array, if nextItem is a structure then lookup the largest item in the structure.
      If nextItem is an array, then follow the alignment rule of the item after nextItem, if this item is a struct, then look up the largest
      item in this struct */
   int getEmbeddedAlignment(structDtypeMap const& expandedMap, std::string structName, std::string nextItem, size_t i);
   /** This is the main function responsible for calculating offsets. It looks a the expanded map and calculates the offset to
      each datatype by taking into account the size of the datatype and many alignment rules. It must take into account alignment
      rules from embedded arrays and structs as well as regular datatypes. It must keep track of both the current datatype and the next
      datatype to work out the alignment rules.*/
   int findOffsets(structDtypeMap const& expandedMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap);
   /** Finds the datatype which an array type is defined with, this may be a structure name*/
   std::string findArrayDtype(structDtypeMap const& expandedMap, std::string arrayDesc);
   /** Returns the largest standard datatype within a structure, this includes embedded structures and arrays and is used for calculating alignment*/
   int getBiggestDtype(structDtypeMap const&, std::string structName);

   /** Takes the elements of a user defined structure requested by the user in the drvInfo string and finds their offsets
      from the previously created structMap_. The main purpose of this algorithm is to skip over embedded structs/arrays as we count through the map.
      For example, if I had a structure A with 2 elements, but the first was a 100byte structure, referencing A[2] would need to skip over this
      100 byte structure. */
   int findRequestedOffset(std::vector<size_t> indices, std::string structMap);

   /** Some attributes entered by the user into the extras part of drvInfo need special attention. This function takes care of this
      and updates extrasString and keyWords */
   void processExtrasExceptions(std::string thisWord, drvInfoMap &keyWords, std::string &extrasString, drvInfoMap &defaultTagAttribs);

   /** This is responsible for parsing drvInfo when records are created. It takes the drvInfo string and parses it for required data.
      It returns a map of all of the data required by the driver to setup the asyn parameter and a boolean which indicates the validity of the data. */
   drvInfoMap drvInfoParser(const char *drvInfo);

   /** Takes the drvInfo string and attempts to split it up around the spaces, then returns the values as a list of strings
      returns "true" or "false" to identify if drvInfo appears to be valid (additional checks are made later)*/
   std::string seperateDrvInfoVals(std::string str, std::list<std::string> & words);

   /** Returns a tuple of <stringValid,startIndex,indexable> based on the tag name given, if the name is valid */
   std::tuple<std::string,std::string,bool> checkValidName(const std::string str);
   /** Check that the user supplied dtype is valid, returns stringValid */
   std::string checkValidDtype(std::string str);
   /** Check that the user supplied sliceSize is valid, returns a tuple of stringValid and sliceSize */
   std::tuple<std::string,std::string> checkValidSliceSize(std::string str, bool indexable, std::string dtype);
   /** Check that the user supplied offset is valid, returns a tuple of stringValid and offset */
   std::tuple<std::string,std::string> checkValidOffset(std::string str);
   /** Check that the user supplied extras string is valid, returns a tuple of stringValid and extrasString */
   std::tuple<std::string,std::string> checkValidExtras(std::string str, drvInfoMap &keyWords);
};

#endif