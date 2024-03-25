#include "omroneip.h"
#include "drvOmroneip.h"
#include <sstream>

// This stores information about each communication tag to the PLC.
// A new instance will be made for each record which requsts to uniquely read/write to the PLC
struct omronDrvUser_t
{
  int startIndex;
  int sliceSize;
  std::string tag;
  std::string dataType;
  int32_t tagIndex;
  int dataCounter;
  std::string pollerName;
  int tagOffset;
  double timeout;
};

typedef struct
{
  omronDataType_t dataType;
  const char *dataTypeString;
} omronDataTypeStruct;

// Supported PLC datatypes
static omronDataTypeStruct omronDataTypes[MAX_OMRON_DATA_TYPES] = {
    {dataTypeBool, "BOOL"},
    {dataTypeInt, "INT"},
    {dataTypeDInt, "DINT"},
    {dataTypeLInt, "LINT"},
    {dataTypeUInt, "UINT"},
    {dataTypeUDInt, "UDINT"},
    {dataTypeULInt, "ULINT"},
    {dataTypeUInt_BCD, "UINT_BCD"},
    {dataTypeUDInt_BCD, "UDINT_BCD"},
    {dataTypeULInt_BCD, "ULINT_BCD"},
    {dataTypeReal, "REAL"},
    {dataTypeLReal, "LREAL"},
    {dataTypeString, "STRING"},
    {dataTypeWord, "WORD"},
    {dataTypeDWord, "DWORD"},
    {dataTypeLWord, "LWORD"},
    {dataTypeUDT, "UDT"},
    {dataTypeUDT, "TIME"}};

bool startPollers = false; // Set to 1 after IocInit() which starts pollers

static const char *driverName = "drvOmronEIP"; /* String for asynPrint */

static void readPollerC(void *drvPvt)
{
  drvOmronEIP *pPvt = (drvOmronEIP *)drvPvt;
  pPvt->readPoller();
}

omronEIPPoller::omronEIPPoller(const char *portName, const char *pollerName, double updateRate):
                              belongsTo_(portName),
                              pollerName_(pollerName),
                              updateRate_(updateRate)
{
}

static void omronExitCallback(void *pPvt)
{
  drvOmronEIP *pDriver = (drvOmronEIP *)pPvt;
  pDriver->omronExiting_ = true;
}

/* This function is called by the IOC load system after iocInit() or iocRun() have completed */
static void myInitHookFunction(initHookState state)
{
  switch(state) {
    case initHookAfterIocRunning:
      startPollers = true;
      break;
    default:
      break;
  }
}

drvOmronEIP::drvOmronEIP(const char *portName,
                          char *gateway,
                          char *path,
                          char *plcType,
                          int debugLevel)

    : asynPortDriver(portName,
                     1,                                                                                                                                                  /* maxAddr */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynDrvUserMask | asynInt8ArrayMask, /* Interface mask */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynInt8ArrayMask,                   /* Interrupt mask */
                     ASYN_CANBLOCK | ASYN_MULTIDEVICE,                                                                                                                   /* asynFlags */
                     1,                                                                                                                                                  /* Autoconnect */
                     0,                                                                                                                                                  /* Default priority */
                     0),                                                                                                                                                 /* Default stack size*/
      initialized_(false)

{
  static const char *functionName = "drvOmronEIP";
  tagConnectionString_ = "protocol=ab-eip&gateway="+(std::string)gateway+"&path="+(std::string)path+"&plc="+(std::string)plcType;
  std::cout<<"Starting driver with connection string: "<< tagConnectionString_<<std::endl;;
  epicsAtExit(omronExitCallback, this);
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Starting driver with libplctag debug level %d\n", driverName, functionName, debugLevel);
  plc_tag_set_debug_level(debugLevel);
  initHookRegister(myInitHookFunction);
  initialized_ = true;
}


asynStatus drvOmronEIP::drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)
{
  static const char *functionName = "drvUserCreate";
  omronDataType_t dataType;
  std::string drvInfoString;
  std::string name;
  int startIndex;
  int len;
  std::string tag;
  bool state;
  int asynIndex = 0;
  int addr;
  asynStatus status = asynSuccess;

  if (initialized_ == false)
  {
    pasynManager->enable(pasynUser, 0);
    return asynDisabled;
  }

  status = getAddress(pasynUser, &addr);
  status = findParam(addr, drvInfo, &asynIndex);
  if (status == asynSuccess)
  {
    // Parameter already exists
    return asynSuccess;
  }

  std::unordered_map<std::string, std::string> keyWords = drvInfoParser(drvInfo);
  if (keyWords.at("stringValid") == "false")
  {
    printf("drvInfo string is invalid, record: %s was not created\n", drvInfo);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "drvInfo string is invalid, record: %s was not created\n", driverName, functionName, drvInfo);
    return asynError;
  }

  tag = tagConnectionString_ +
        "&name=" + keyWords.at("tagName") +
        "&elem_count=" + keyWords.at("sliceSize")
        + keyWords.at("tagExtras");

  int32_t tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);

  /* Check and report failure codes. An Asyn param will be created even on failure but the record will be given error status */
  if (tagIndex<0)
  {
    const char* error = plc_tag_decode_error(tagIndex);
    printf("Tag not added! %s\n", error);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Tag not added! %s\n", driverName, functionName, error);
  }
  else if (tagIndex == 1)
  {
    printf("Tag not added! Timeout while creating tag.\n");
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Tag not added! Timeout while creating tag.\n", driverName, functionName);
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Created libplctag tag index: %d with tag string: %s\n", driverName, functionName, tagIndex, tag.c_str());
  }

  /* Initialise each datatype*/
  omronDrvUser_t *newDrvUser = (omronDrvUser_t *)callocMustSucceed(1, sizeof(omronDrvUser_t), functionName);
  newDrvUser->dataType = keyWords.at("dataType");
  newDrvUser->tag = tag;
  newDrvUser->tagIndex = tagIndex;
  newDrvUser->pollerName = keyWords.at("pollerName");
  newDrvUser->sliceSize = std::stoi(keyWords.at("sliceSize")); //dtype checked elsewhere
  newDrvUser->startIndex = std::stoi(keyWords.at("startIndex"));
  newDrvUser->timeout = pasynUser->timeout;
  newDrvUser->tagOffset = std::stoi(keyWords.at("offset"));

  { /* Take care of different datatypes */
    if (keyWords.at("dataType") == "BOOL")
    {
      status = createParam(drvInfo, asynParamUInt32Digital, &asynIndex);
    }

    else if (keyWords.at("dataType") == "INT")
    {
      status = createParam(drvInfo, asynParamInt32, &asynIndex);
    }

    else if (keyWords.at("dataType") == "DINT")
    {
      status = createParam(drvInfo, asynParamInt32, &asynIndex);
    }

    else if (keyWords.at("dataType") == "LINT")
    {
      status = createParam(drvInfo, asynParamInt64, &asynIndex);
    }

    else if (keyWords.at("dataType") == "UINT")
    {
      status = createParam(drvInfo, asynParamInt32, &asynIndex);
    }

    else if (keyWords.at("dataType") == "UDINT")
    {
      status = createParam(drvInfo, asynParamInt32, &asynIndex);
    }

    else if (keyWords.at("dataType") == "ULINT")
    {
      status = createParam(drvInfo, asynParamInt64, &asynIndex);
    }

    else if (keyWords.at("dataType") == "REAL")
    {
      status = createParam(drvInfo, asynParamFloat64, &asynIndex);
    }

    else if (keyWords.at("dataType") == "LREAL")
    {
      status = createParam(drvInfo, asynParamFloat64, &asynIndex);
    }

    else if (keyWords.at("dataType") == "STRING")
    {
      status = createParam(drvInfo, asynParamOctet, &asynIndex);
    }

    else if (keyWords.at("dataType") == "WORD")
    {
      status = createParam(drvInfo, asynParamOctet, &asynIndex);
    }

    else if (keyWords.at("dataType") == "DWORD")
    {
      status = createParam(drvInfo, asynParamOctet, &asynIndex);
    }

    else if (keyWords.at("dataType") == "LWORD")
    {
      status = createParam(drvInfo, asynParamOctet, &asynIndex);
    }

    else if (keyWords.at("dataType") == "UDT")
    {
      status = createParam(drvInfo, asynParamInt8Array, &asynIndex);
    }

    else if (keyWords.at("dataType") == "TIME")
    {
      status = createParam(drvInfo, asynParamInt8Array, &asynIndex);
    }
  }

  if (asynIndex < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "Creation of asyn parameter failed for drvInfo: %s\n", driverName, functionName, tagIndex, drvInfo);
    return (asynStatus)asynIndex;
  }

  pasynUser->reason = asynIndex;
  pasynUser->drvUser = newDrvUser;
  this->lock(); /* Lock to ensure no other thread tries to use an asynIndex from the tagMap_ before it has been created */
  if (tagIndex > 1)
  { 
    // Add successfull tags to the tagMap
    tagMap_[asynIndex] = newDrvUser;
  }
  // Create the link from the record to the param
  status = asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
  // For unsuccessfull tags, set record to error, they will never be read from the PLC
  if (tagIndex < 0)
  {
    //Set record to error
    setParamStatus(asynIndex, asynError);
  }
  else if (tagIndex == 1)
  {
    setParamStatus(asynIndex, asynError);
  }
  else
  {
    std::cout << "Successfull creation of asyn parameter with index: "<<asynIndex<<" connected to libplctag index: "<<tagIndex<<std::endl;
    //asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "Successfull creation of asyn parameter with index: %d connected to liplctag index: %d\n", driverName, functionName, status, tagIndex);
  }
  this->unlock();
  return status;
}

asynStatus drvOmronEIP::createPoller(const char *portName, const char *pollerName, double updateRate)
{
  int status;
  omronEIPPoller* pPoller = new omronEIPPoller(portName, pollerName, updateRate); //make this a smart pointer! Currently not freed
  pollerList_[pPoller->pollerName_] = pPoller;
  status = (epicsThreadCreate(pPoller->pollerName_,
                            epicsThreadPriorityMedium,
                            epicsThreadGetStackSize(epicsThreadStackMedium),
                            (EPICSTHREADFUNC)readPollerC,
                            this) == NULL);
  return (asynStatus)status;
}

std::unordered_map<std::string, std::string> drvOmronEIP::drvInfoParser(const char *drvInfo)
{
  const char * functionName = "drvInfoParser";
  const std::string str(drvInfo);
  char delim = ' ';
  char escape = '/';
  std::unordered_map<std::string, std::string> keyWords = {
      {"pollerName", "none"}, // optional
      {"tagName", "none"},  
      {"dataType", "none"}, 
      {"startIndex", "1"},   
      {"sliceSize", "1"},     
      {"offset", "0"},       
      {"tagExtras", "none"},  
      {"stringValid", "none"} // set to false if errors are detected which aborts creation of tag and asyn parameter
  };
  std::list<std::string> words;
  std::string substring;
  bool escaped = false;
  int pos = 0;

  for (int i = 0; i < str.size(); i++)
  {
    if (str[i] == escape)
    {
      if (escaped == false)
      {
        escaped = true;
      }
      else
      {
        escaped = false;
      }
    }
    if (str[i] == delim && !escaped)
    {
      if (i == 0)
      {
        pos += 1;
        continue;
      }
      else if (str[i - 1] == escape)
      {
        // delimeter after escape character
        substring = str.substr(pos + 1, (i - 1) - (pos + 1));
      }
      // regular delimeter
      else
      {
        substring = str.substr(pos, i - pos);
      }
      words.push_back(substring);
      pos = i + 1;
    }
    else if (i == str.size() - 1)
    {
      substring = str.substr(pos, str.size() - pos);
      words.push_back(substring);
    }
  }

  if (escaped)
  {
    std::cout << "Escape character never closed! Record invalid" << std::endl;
    keyWords.at("stringValid") = "false";
  }

  if (words.size() < 1)
  {
    std::cout << "No arguments supplied to driver" << std::endl;
    keyWords.at("stringValid") = "false";
  }

  if (words.front()[0] == '@')
  {
    keyWords.at("pollerName") = words.front().substr(1, words.front().size() - 1);
    words.pop_front();
  }

  if (words.size() < 5)
  {
    std::cout << "Record is missing parameters. Expected 5 space seperated terms (or 6 including poller) but recieved " << words.size() << std::endl;
    keyWords.at("stringValid") = "false";
  }

  int params = words.size();
  bool indexable = false;
  for (int i = 0; i < params; i++)
  {
    if (i == 0)
    {
      std::string startIndex;
      auto b = words.front().begin(), e = words.front().end();

      while ((b = std::find(b, e, '[')) != e)
      {
        auto n_start = ++b;
        b = std::find(b, e, ']');
        startIndex = std::string(n_start, b);
        if (!startIndex.empty())
        {
          break;
        }
      }
      if (!startIndex.empty())
      {
        keyWords.at("startIndex") = startIndex;
        indexable = true;
        try
        {                
          if (std::stoi(startIndex) < 1)
          {
            std::cout << "A startIndex of < 1 is forbidden" << std::endl;
            keyWords.at("stringValid") = "false";
          }
        }
        catch(...)
        {
          std::cout << "startIndex must be an integer" << std::endl;
          keyWords.at("stringValid") = "false";
        }
      }
      keyWords.at("tagName") = words.front();
      words.pop_front();
    }
    else if (i == 1)
    {
      // checking required
      bool validDataType = false;
      for (int t = 0; t < MAX_OMRON_DATA_TYPES; t++)
      {
        if (strcmp(words.front().c_str(), omronDataTypes[t].dataTypeString) == 0)
        {
          validDataType = true;
          keyWords.at("dataType") = words.front();
        }
      }
      if (!validDataType)
      {
        std::cout << "Datatype invalid" << std::endl;
        keyWords.at("stringValid") = "false";
      }
      words.pop_front();
    }
    else if (i == 2)
    {
      char *p;
      strtol(words.front().c_str(), &p, 10);
      if (*p == 0)
      {
        if (indexable && words.front() != "1")
        {
          keyWords.at("sliceSize") = words.front();
        }
        else if (words.front() == "0")
        {
          keyWords.at("sliceSize") = "1";
        }
        else if (words.front() != "1")
        {
          std::cout << "You cannot get a slice whole tag. Try tag_name[startIndex] to specify elements for slice" << std::endl;
          keyWords.at("stringValid") = "false";
        }
      }
      else
      {
        std::cout << "Invalid sliceSize, must be integer." << std::endl;
        keyWords.at("stringValid") = "false";
      }
      words.pop_front();
    }
    else if (i == 3)
    {
      int structIndex = 0; // will store user supplied index into user supplied struct
      int indexStartPos = 0; // stores the position of the first '[' within the user supplied string
      int offset = 0;
      // attempt to set offset to integer, if not possible then assume it is a structname
      try
      {
        offset = std::stoi(words.front());
      }
      catch(...)
      {
        // attempt to split name and integer
        for (int n = 0; n<words.front().size(); n++)
        {
          if (words.front().c_str()[n] == '[')
          {
            std::string offsetSubstring = words.front().substr(n+1,words.front().size()-(n+1));
            indexStartPos = n;
            for (int m = 0; m<offsetSubstring.size(); m++)
            {
              if (offsetSubstring.c_str()[m] == ']')
              {
                try
                {
                  //struct integer found
                  structIndex = std::stoi(offsetSubstring.substr(0,m));
                  goto findOffsetFromStruct;
                }
                catch(...)
                {
                  std::cout << "Error, could not find a valid index for the requested structure: " << words.front() << std::endl;
                  keyWords.at("stringValid") = "false";
                }
              }
            }
          }
        }
        std::cout<<"Invalid index for requested structure: " << words.front() << std::endl;
        keyWords.at("stringValid") = "false";
        
        //look for matching structure in structMap_
        //if found, look for the offset at the structIndex within the structure
        findOffsetFromStruct:
          std::string structName = words.front().substr(0,indexStartPos);
          bool structFound = false;
          for (auto item: this->structMap_)
          {
            if (item.first == structName)
            {
              //requested structure found
              structFound = true;
              if (structIndex>=item.second.size())
              {
                std::cout<<"Error, attempt to read index: " << structIndex << " from struct: " << item.first << " failed." << std::endl;
                keyWords.at("stringValid") = "false";
              }
              else
              {
                offset = item.second[structIndex];
                break;
              }
            }
          }
          if (!structFound)
          {
            std::cout << "Could not find structure requested: " << words.front() << ". Have you loaded a struct file?" << std::endl;
            keyWords.at("stringValid") = "false";
          }
      }
      keyWords.at("offset") = std::to_string(offset);
      words.pop_front();
    }
    else if (i == 4)
    {
      /*These attributes overwrite libplctag attributes, other attributes which arent overwritten are not mentioned here
        Users can overwrite these defaults and other libplctag defaults from their records */
      std::unordered_map<std::string, std::string> defaultTagAttribs = {
          {"allow_packing=", "1"},
          {"str_is_zero_terminated=", "0"},
          {"str_is_fixed_length=", "0"},
          {"str_is_counted=", "1"},
          {"str_count_word_bytes=", "2"},
          {"str_pad_bytes=", "0"}};
      std::string extrasString;
      if (words.front()!="0")
      {
        extrasString = words.front();
        for (auto &attrib : defaultTagAttribs)
        {
          auto pos = words.front().find(attrib.first);
          std::string size;
          if (pos != std::string::npos) // if attrib is one of our defined defaults
          {
            std::string remaining = words.front().substr(pos + attrib.first.size(), words.front().size());
            auto nextPos = remaining.find('&');
            if (nextPos != std::string::npos)
            {
              size = remaining.substr(0, nextPos);
              extrasString = words.front().erase(pos-1, attrib.first.size() + nextPos + 1);
            }
            else
            {
              size = remaining.substr(0, remaining.size());
              extrasString = words.front().erase(pos-1, words.front().size()-(pos-1));
            }

            if (size == attrib.second) // if defined value is identical to default, continue
            {
              continue;
            }
            else // set new default value
            {
              attrib.second = size;
            }
          }
        }
      }

      for (auto attrib : defaultTagAttribs)
      {
        if (attrib.first.substr(0,3) == "str" && keyWords.at("dataType") != "STRING")
        {
          continue;
        }
        extrasString += "&";
        extrasString += attrib.first;
        extrasString += attrib.second;
      }
      keyWords.at("tagExtras") = extrasString;
      words.pop_back();
    }
  }

  std::cout << "Creating libplctag tag with the following parameters:" << std::endl;

  for (auto i = keyWords.begin(); i != keyWords.end(); i++)
  {
    std::cout << i->first << " \t\t\t";
    if (i->first.size() < 7){std::cout<<"\t";}
    std::cout<< i->second <<std::endl;
    //asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s : %s\n", driverName, functionName, i->first.c_str(), i->second.c_str());
  }
  std::cout<<std::endl;
  return keyWords;
}

asynStatus drvOmronEIP::loadStructFile(const char * portName, const char * filePath)
{
  std::ifstream infile(filePath); 
  std::unordered_map<std::string, std::vector<std::string>> structMap;
  std::vector<std::string> row; 
  std::string line, word; 

  while (std::getline(infile, line)) { 

      row.clear(); 
      std::istringstream s(line); 
      while (std::getline(s, word, ',')) 
      {   
        row.push_back(word); 
      } 
      std::string structName = row.front();
      row.erase(row.begin());
      structMap[structName] = row;
    } 
    // add to debug
    // for (auto const& i : structMap) {
    //   std::cout << i.first;
    //   for (auto const& j : i.second)
    //     std::cout << " " << j;
    //   std::cout<<std::endl;
    // }
    this->createStructMap(structMap);
    return asynSuccess;
}

asynStatus drvOmronEIP::createStructMap(std::unordered_map<std::string, std::vector<std::string>> rawMap)
{
  std::unordered_map<std::string, std::vector<int>> structMap;
  for (auto kv: rawMap)
  {// look for kv.first in structMap
    findOffsetsRecursive(rawMap, kv.first, structMap);
  }
  // add to debug
  // for (auto const& i : structMap) {
  //   std::cout << i.first;
  //   for (auto const& j : i.second)
  //     std::cout << " " << j;
  //   std::cout<<std::endl;
  // }
  this->structMap_= structMap;
  return asynSuccess;
}

int drvOmronEIP::findOffsetsRecursive(auto const& rawMap, std::string structName, auto& structMap)
{
  std::vector<int> newRow = {0};
  int offSetsCounted = 0;
  std::vector<std::string> row = rawMap.at(structName);
  int lastOffset = 0;
  bool structInvalid = false;
  int thisOffset = 0;
  // loop through each datatype and find its byte size
  for (std::string dtype : row)
  {
    offSetsCounted++;
    lastOffset = newRow.back();
    if (dtype =="BOOL")
      thisOffset = lastOffset+1;
    else if (dtype =="UINT" || dtype =="INT" || dtype =="WORD")
      thisOffset = lastOffset+2;
    else if (dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD")
      thisOffset = lastOffset+4;
    else if (dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD")
      thisOffset = lastOffset+8;
    else if (dtype =="UINT")
      thisOffset = lastOffset+2;
    else
    {
      if (dtype.substr(0,7) == "STRING[")
      {
        auto ss = dtype.substr(7,dtype.size()-7);
        int i = 0;
        int strLength = 0;
        bool intFound = false;
        for (int i = 0; i<ss.size(); i++)
        {
          if (ss.substr(i,1)== "]")
          {
            intFound = true;
            try
            {
              strLength = std::stoi(ss.substr(0,i+1));
              if (strLength < 0) throw 1;
            }
            catch (...)
            {
              std::cout<<"STRING length must be an integer"<<std::endl;
              strLength = 0;
            }
            break;
          }
        }
        thisOffset = lastOffset+strLength;
        if (!intFound)
        {
          structInvalid = true;
          std::cout<<"STRING type must specify size, " << structName << " definition is invalid" <<std::endl;
          return -1;
        }
      }
      else
      {
        // If we get here, then we assume that we have a structure, but it could be a typo
        try
        {
          std::vector<std::string> nextRow = rawMap.at(dtype);
          //recursively call this function to calculate the size of the named structure
          int structSize = findOffsetsRecursive(rawMap, dtype, structMap);
          if (structSize <= 0)
          {
            structInvalid = true;
            std::cout<<"Error calculating the size of structure: " << dtype << ". Definition for " << structName << " is invalid" << std::endl; 
          }
          thisOffset = structSize+lastOffset;
        }
        catch (...)
        {
          structInvalid = true;
          std::cout<<"Failed to find the standard datatype: " << dtype << ". Definition for " << structName << " and its dependents failed" << std::endl;
          return -1;
        }
      }
    }
    if (offSetsCounted < row.size())
    {
      newRow.push_back(thisOffset);
    }
    else
    {
      // if this is the last offset, then do not add it to the row as it points to the end of the struct
      // instead we return thisOffset
      if (!structInvalid)
      {
        structMap[structName] = newRow;
        return thisOffset;
      }
      else
        return -1;
    }
  }
}

void drvOmronEIP::readPoller()
{
  std::string threadName = epicsThreadGetNameSelf();
  omronEIPPoller* pPoller = pollerList_.at(threadName);
  std::string tag;
  int offset;
  int status;
  int still_pending = 1;
  double interval = pPoller->updateRate_;
  int timeTaken = 0;
  while (!startPollers) {epicsThreadSleep(0.1);}
  while (true)
  {
    double waitTime = interval - ((double)timeTaken / 1000);
    //std::cout<<"Reading tags on poller "<<threadName<<" with interval "<<interval<<" seconds"<<std::endl;
    if (waitTime >=0) {epicsThreadSleep(waitTime);}
    else {
      std::cout<<"Reads taking longer than requested! " << ((double)timeTaken / 1000) << " > " << interval<<std::endl;
    }

    auto startTime = std::chrono::system_clock::now();
    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName)
      {
        //std::cout<<"Reading tag " <<x.second->tagIndex<<" on poller "<<threadName<<" with interval "<<interval<<" seconds"<<std::endl;
        status = plc_tag_read(x.second->tagIndex, 0); // read from plc as fast as possible, we will check status and timeouts later
      }
    }

    int waits = 0;
    bool readFailed = false;
    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName)
      {
        int offset = x.second->tagOffset;
        still_pending = 1;
        
        while (still_pending)
        {
          // It should be rare that data for the first tag has not arrived before the last tag is read
          // Therefor this loop should rarely be entered and would only be called by the first few tags as we 
          // are asynchronously waiting for all tags in this poller to be read. 
          still_pending = 0;
          status = plc_tag_status(x.second->tagIndex);

          if (status == PLCTAG_STATUS_PENDING)
          {
            // Wait for the timeout specified in the record
            if (waits*0.01>=x.second->timeout)
            {
              setParamStatus(x.first, asynTimeout);
              readFailed = true;
              fprintf(stderr, "Timeout finishing read tag %ld: %s. Try decreasing the polling rate\n", x.second->tagIndex, plc_tag_decode_error(status));
              asynStatus stat;
              getParamStatus(x.first, &stat);
              std::cout<<stat<<std::endl;
              break;
            }
            else
            {
              still_pending = 1;
              waits++;
              epicsThreadSleep(0.01);
            }
          }
          else if (status < 0)
          {
            setParamStatus(x.first, asynError);
            readFailed = true;
            fprintf(stderr, "Error finishing read tag %ld: %s\n", x.second->tagIndex, plc_tag_decode_error(status));
            break;
          }
        }

        if (!readFailed)
        {
          if (x.second->dataType == "BOOL")
          {
            epicsUInt32 data;
            data = plc_tag_get_bit(x.second->tagIndex, offset);
            setUIntDigitalParam(x.first, data, 0xFF, 0xFF);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "INT")
          {
            epicsInt16 data;
            data = plc_tag_get_int16(x.second->tagIndex, offset);
            setIntegerParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "DINT")
          {
            epicsInt32 data;
            data = plc_tag_get_int32(x.second->tagIndex, offset);
            setIntegerParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "LINT")
          {
            epicsInt64 data;
            data = plc_tag_get_int64(x.second->tagIndex, offset);
            setInteger64Param(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "UINT")
          {
            epicsUInt16 data;
            data = plc_tag_get_uint16(x.second->tagIndex, offset);
            setIntegerParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "UDINT")
          {
            epicsUInt32 data;
            data = plc_tag_get_uint32(x.second->tagIndex, offset);
            setIntegerParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "ULINT")
          {
            epicsUInt64 data;
            data = plc_tag_get_uint64(x.second->tagIndex, offset);
            setInteger64Param(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "REAL")
          {
            epicsFloat32 data;
            data = plc_tag_get_float32(x.second->tagIndex, offset);
            setDoubleParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "LREAL")
          {
            epicsFloat64 data;
            data = plc_tag_get_float64(x.second->tagIndex, offset);
            setDoubleParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "STRING")
          {
            int bufferSize = plc_tag_get_size(x.second->tagIndex);
            int string_length = plc_tag_get_string_length(x.second->tagIndex, offset)+1;
            int string_capacity = plc_tag_get_string_capacity(x.second->tagIndex, 0);
            if (offset >= bufferSize-1)
            {
              std::cout<<"Error! Attempting to read at an offset beyond tag buffer!"<<std::endl;
              continue;
            }
            if (string_length>string_capacity+1)
            {
              std::cout<<"Error! Offset does not point to valid string!"<<std::endl;
              continue;
            }
            char *data = (char *)malloc((size_t)(unsigned int)(string_length));
            status = plc_tag_get_string(x.second->tagIndex, offset, data, string_length);
            setStringParam(x.first, data);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "WORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            doCallbacksInt8Array(pData, bytes, x.first, 0);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: " <<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "DWORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            doCallbacksInt8Array(pData, bytes, x.first, 0);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: " <<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "LWORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            doCallbacksInt8Array(pData, bytes, x.first, 0);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: " <<data<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "UDT")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *pOutput = (uint8_t *)malloc(bytes * sizeof(uint8_t));
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, pOutput, bytes);
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            memcpy(pData, pOutput, bytes);
            doCallbacksInt8Array(pData, bytes, x.first, 0);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: " <<(int*)(pData)<< " My type: "<< x.second->dataType<<std::endl;
          }
          else if (x.second->dataType == "TIME")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *pOutput = (uint8_t *)malloc(bytes * sizeof(uint8_t));
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, pOutput, bytes);
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            memcpy(pData, pOutput, bytes);
            setStringParam(x.first, pData);
            // std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: " <<(int*)(pData)<< " My type: "<< x.second->dataType<<std::endl;
          }
          setParamStatus(x.first, (asynStatus) status);
        }
      }
    }
    callParamCallbacks();
    auto endTime = std::chrono::system_clock::now();
    timeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    std::cout << "Poller: "<<threadName<< " finished processing in " << timeTaken << " msec" <<std::endl;
    std::cout << std::endl;
  }
}

asynStatus drvOmronEIP::readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)
{
  int status;
  for (auto x : tagMap_)
  {
    int offset = x.second->tagOffset;
    if (x.second->dataType == "UDT")
    {
      int bytes = plc_tag_get_size(x.second->tagIndex);
      uint8_t *pOutput = (uint8_t *)malloc(bytes * sizeof(uint8_t));
      status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, pOutput, bytes); /* +1 for the zero termination */
      if (bytes > nElements)
      {
        memcpy(value, pOutput, nElements);
        memcpy(nIn, &nElements, sizeof(size_t));
      }
      else
      {
        memcpy(value, pOutput, bytes);
        memcpy(nIn, &bytes, sizeof(size_t));
      }
    }
  }
}



asynStatus drvOmronEIP::writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  int status;
  int tagSize = plc_tag_get_size(tagIndex);
  double timeout = pasynUser->timeout*1000;
  if (nElements>tagSize)
  {
    std::cout<<"Attempting to write beyond tag capacity, restricting write to " << tagSize << " chars" <<std::endl;
    nElements=tagSize;
  }
  if (drvUser->dataType == "UDT")
  {
    uint8_t *pOutput = (uint8_t *)malloc(nElements * sizeof(uint8_t));
    memcpy(pOutput, value, nElements);
    status = plc_tag_set_raw_bytes(tagIndex, offset, pOutput ,nElements);
    status = plc_tag_write(tagIndex, timeout);
    free(pOutput);
    return asynSuccess;
  }
  else if (drvUser->dataType == "WORD" || drvUser->dataType == "DWORD" || drvUser->dataType == "LWORD")
  {
    uint8_t *pOutput = (uint8_t *)malloc(nElements * sizeof(uint8_t));
    int j = nElements-1;
    int status;
    for (int i=0; i<nElements; i++)
    {
      pOutput[i] = value[j];
      j--;
    }
    status = plc_tag_set_raw_bytes(tagIndex, offset, pOutput ,nElements);
    status = plc_tag_write(tagIndex, timeout);
    free(pOutput);
    return asynSuccess;
  }
  else
  {
    return asynError;
  }
}

asynStatus drvOmronEIP::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "BOOL")
  {
    status = plc_tag_set_bit(tagIndex, offset, value);
    status = plc_tag_write(tagIndex, timeout);
    return asynSuccess;
  }
  else
  {
    return asynError;
  }
}

asynStatus drvOmronEIP::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;

  if (drvUser->dataType == "INT")
  {
    plc_tag_set_int16(tagIndex, offset, (epicsInt16)value);
  }
  else if (drvUser->dataType == "DINT")
  {
    plc_tag_set_int32(tagIndex, offset, value);
  }
  else if (drvUser->dataType == "UINT")
  {
    plc_tag_set_int32(tagIndex, offset, (epicsUInt16)value);
  }
  else if (drvUser->dataType == "UDINT")
  {
    plc_tag_set_int32(tagIndex, offset, (epicsUInt32)value);
  }

  status = plc_tag_write(tagIndex, timeout);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt64(asynUser *pasynUser, epicsInt64 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "LINT")
  {
    plc_tag_set_int64(tagIndex, offset, value);
  }
  else if (drvUser->dataType == "ULINT")
  {
    plc_tag_set_int64(tagIndex, offset, (epicsUInt64)value);
  }
  status = plc_tag_write(tagIndex, timeout);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "REAL")
  {
    plc_tag_set_float32(tagIndex, offset, (epicsFloat32)value);
  }
  else if (drvUser->dataType == "LREAL")
  {
    plc_tag_set_float64(tagIndex, offset, value);
  }
  status = plc_tag_write(tagIndex, timeout);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;

  /* This is a bit ghetto because Omron does strings a bit different to what libplctag expects*/
  if (drvUser->dataType == "STRING")
  {
    int string_capacity = plc_tag_get_string_capacity(tagIndex, offset);
    char stringOut[nChars + 1] = {'\0'}; // allow space for null character
    snprintf(stringOut, sizeof(stringOut), value);

    /* Set the tag buffer to the max size of string in PLC. Required as the tag size is set based
    on the current size of the tag in the PLC, but we may write a bigger string than this. */
    plc_tag_set_size(tagIndex, string_capacity + 2);     // Allow room for string length
    status = plc_tag_set_string(tagIndex, offset, stringOut); // Set the data
    plc_tag_set_size(tagIndex, nChars + 2);              // Reduce the tag buffer to delete any data beyond the string we pass in
    status = plc_tag_write(tagIndex, timeout);

    memcpy(nActual, &nChars, sizeof(size_t));
  }
  return asynSuccess;
}

extern "C"
{
  /* drvOmronEIPStructDefine - Loads structure definitions from file */
  asynStatus drvOmronEIPStructDefine(const char *portName, const char *filePath)
  {
    drvOmronEIP* pDriver = (drvOmronEIP*)findAsynPortDriver(portName);
    if (!pDriver){
      std::cout<<"Error, Port "<<portName<< " not found!"<<std::endl;
      return asynError;
    }
    else
    {
      return pDriver->loadStructFile(portName, filePath); 
    }
  }

  /* iocsh functions */

  static const iocshArg structDefConfigArg0 = {"Port name", iocshArgString};
  static const iocshArg structDefConfigArg1 = {"File path", iocshArgString};

  static const iocshArg *const drvOmronEIPStructDefineArgs[2] = {
      &structDefConfigArg0,
      &structDefConfigArg1};

  static const iocshFuncDef drvOmronEIPStructDefineFuncDef = {"drvOmronEIPStructDefine", 2, drvOmronEIPStructDefineArgs};

  static void drvOmronEIPStructDefineCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPStructDefine(args[0].sval, args[1].sval);
  }

  /* drvOmronEIPConfigPoller() - Creates a new poller with user provided settings and adds it to the driver */
  asynStatus drvOmronEIPConfigPoller(const char *portName,
                                  const char *pollerName, double updateRate)
  {
    drvOmronEIP* pDriver = (drvOmronEIP*)findAsynPortDriver(portName);
    if (!pDriver){
      std::cout<<"Error, Port "<<portName<< " not found!"<<std::endl;
      return asynError;
    }
    else
    {
      return pDriver->createPoller(portName, pollerName, updateRate); 
    }
  }

  /* iocsh functions */

  static const iocshArg pollerConfigArg0 = {"Port name", iocshArgString};
  static const iocshArg pollerConfigArg1 = {"Poller name", iocshArgString};
  static const iocshArg pollerConfigArg2 = {"Update rate", iocshArgDouble};

  static const iocshArg *const drvOmronEIPConfigPollerArgs[3] = {
      &pollerConfigArg0,
      &pollerConfigArg1,
      &pollerConfigArg2};

  static const iocshFuncDef drvOmronEIPConfigPollerFuncDef = {"drvOmronEIPConfigPoller", 3, drvOmronEIPConfigPollerArgs};

  static void drvOmronEIPConfigPollerCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPConfigPoller(args[0].sval, args[1].sval, args[2].dval);
  }

  /*
  ** drvOmronEIPConfigure() - create and init an asyn port driver for a PLC
  */

  /** EPICS iocsh callable function to call constructor for the drvModbusAsyn class. */
  asynStatus drvOmronEIPConfigure(const char *portName,
                                  char *gateway,
                                  char *path,
                                  char *plcType,
                                  int debugLevel)
  {
    new drvOmronEIP(portName,
                    gateway,
                    path,
                    plcType,
                    debugLevel);

    return asynSuccess;
  }

  /* iocsh functions */

  static const iocshArg ConfigureArg0 = {"Port name", iocshArgString};
  static const iocshArg ConfigureArg1 = {"Gateway", iocshArgString};
  static const iocshArg ConfigureArg2 = {"Path", iocshArgString};
  static const iocshArg ConfigureArg3 = {"PLC type", iocshArgString};
  static const iocshArg ConfigureArg4 = {"Debug level", iocshArgInt};

  static const iocshArg *const drvOmronEIPConfigureArgs[5] = {
      &ConfigureArg0,
      &ConfigureArg1,
      &ConfigureArg2,
      &ConfigureArg3,
      &ConfigureArg4};

  static const iocshFuncDef drvOmronEIPConfigureFuncDef = {"drvOmronEIPConfigure", 5, drvOmronEIPConfigureArgs};

  static void drvOmronEIPConfigureCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPConfigure(args[0].sval, args[1].sval, args[2].sval, args[3].sval, args[4].ival);
  }

  static void drvOmronEIPRegister(void)
  {
    iocshRegister(&drvOmronEIPConfigureFuncDef, drvOmronEIPConfigureCallFunc);
    iocshRegister(&drvOmronEIPConfigPollerFuncDef, drvOmronEIPConfigPollerCallFunc);
    iocshRegister(&drvOmronEIPStructDefineFuncDef, drvOmronEIPStructDefineCallFunc);
  }

  epicsExportRegistrar(drvOmronEIPRegister);

} // extern "C"
