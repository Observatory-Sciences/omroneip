#include "omroneip.h"
#include "drvOmroneip.h"
#include <sstream>

/*
    Each PV which talks to PLC data is an omronDrvUser with values based on the asyn parameter name.
*/

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
};

typedef struct
{
  omronDataType_t dataType;
  const char *dataTypeString;
} omronDataTypeStruct;

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

/* Local variable declarations */
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

/********************************************************************
**  global driver functions
*********************************************************************
*/


drvOmronEIP::drvOmronEIP(const char *portName,
                          char *gateway,
                          char *path,
                          char *plcType)

    : asynPortDriver(portName,
                     1,                                                                                                                                                  /* maxAddr */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynDrvUserMask | asynInt8ArrayMask, /* Interface mask */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynInt8ArrayMask,                   /* Interrupt mask */
                     ASYN_CANBLOCK | ASYN_MULTIDEVICE,                                                                                                                   /* asynFlags */
                     1,                                                                                                                                                  /* Autoconnect */
                     0,                                                                                                                                                  /* Default priority */
                     0),                                                                                                                                                 /* Default stack size*/
      initialized_(false),
      cats_(true)

{
  tagConnectionString_ = "protocol=ab-eip&gateway="+(std::string)gateway+"&path="+(std::string)path+"&plc="+(std::string)plcType;
  std::cout<<"Starting driver with connection string: "<< tagConnectionString_<<std::endl;;
  epicsAtExit(omronExitCallback, this);
  //plc_tag_set_debug_level(3);
  initHookRegister(myInitHookFunction);
  initialized_ = true;
}

/*
    Takes the asyn parameter name defined for each pv in the loaded database and matches it to a libplctag tag.
    This function is called twice, once before and once after database initialization, the first time we just return asynDisabled.
*/

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
    return asynError;
  }

  tag = tagConnectionString_ +
        "&name=" + keyWords.at("tagName") +
        "&elem_count=" + keyWords.at("sliceSize")
        + keyWords.at("tagExtras");

  printf("%s\n", tag.c_str());
  int32_t tagIndex = plc_tag_create(tag.c_str(), DATA_TIMEOUT);
  /* Check and report failure codes. An Asyn param will be created even on failure but the record will be given error status */
  if (tagIndex<0)
  {
    const char* error = plc_tag_decode_error(tagIndex);
    printf("Tag not added! %s\n", error);
  }
  else if (tagIndex == 1)
  {
    printf("Tag not added! Timeout while creating tag.\n");
  }

  /* Initialise each datatype*/

  omronDrvUser_t *newDrvUser = (omronDrvUser_t *)callocMustSucceed(1, sizeof(omronDrvUser_t), functionName);
  newDrvUser->dataType = keyWords.at("dataType");
  newDrvUser->tag = tag;
  newDrvUser->tagIndex = tagIndex;
  newDrvUser->pollerName = keyWords.at("pollerName");
  newDrvUser->sliceSize = std::stoi(keyWords.at("sliceSize")); //these all need type protection
  newDrvUser->startIndex = std::stoi(keyWords.at("startIndex"));
  newDrvUser->tagOffset = std::stoi(keyWords.at("offset"));

  { /* Take care of different datatypes */
    if (keyWords.at("dataType") == "INT")
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
    return (asynStatus)asynIndex;
  }

  std::cout << tagIndex << " " << asynIndex << std::endl;

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
  const std::string str(drvInfo);
  char delim = ' ';
  char escape = '/';
  std::unordered_map<std::string, std::string> keyWords = {
      {"pollerName", "none"}, // optional
      {"tagName", "none"},    // required
      {"dataType", "none"},   // optional
      {"startIndex", "1"},    // optional
      {"sliceSize", "1"},     // optional
      {"offset", "0"},        // optional
      {"tagExtras", "none"},  // optional
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
        if (std::stoi(startIndex) < 1)
        {
          std::cout << "A startIndex of < 1 is forbidden" << std::endl;
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
      keyWords.at("offset") = words.front();
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


  for (auto i = keyWords.begin(); i != keyWords.end(); i++)
  {
    std::cout << i->first << " \t\t\t";
    if (i->first.size() < 7){std::cout<<"\t";}
    std::cout<< i->second <<std::endl;
  }
  std::cout << "Returning keyWords" << std::endl;

  return keyWords;
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
        status = plc_tag_read(x.second->tagIndex, 0); // read as fast as possible, we will check status and timeouts later
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
            still_pending = 1;
            waits++;
            epicsThreadSleep(0.01);
          }
          else if (status < 0)
          {
            setParamStatus(x.first, asynError);
            readFailed = true;
            fprintf(stderr, "Error finishing read tag %ld: %s\n", x.second->tagIndex, plc_tag_decode_error(status));
            break;
          }
          if (waits*0.01>=0.1 && waits*0.01>= 0.9*interval)
          {
            // If we have waited for 100ms and at least 90% of the polling interval, then give up
            setParamStatus(x.first, asynTimeout);
            readFailed = true;
            fprintf(stderr, "Timeout finishing read tag %ld: %s. Try decreasing the polling rate\n", x.second->tagIndex, plc_tag_decode_error(status));
            asynStatus stat;
            getParamStatus(x.first, &stat);
            std::cout<<stat<<std::endl;
            break;
          }
        }

        if (!readFailed)
        {
          if (x.second->dataType == "INT")
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
    std::cout << "Time taken(msec): " << timeTaken << std::endl;
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

asynStatus drvOmronEIP::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;

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

  status = plc_tag_write(tagIndex, 100);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt64(asynUser *pasynUser, epicsInt64 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  if (drvUser->dataType == "LINT")
  {
    plc_tag_set_int64(tagIndex, offset, value);
  }
  else if (drvUser->dataType == "ULINT")
  {
    plc_tag_set_int64(tagIndex, offset, (epicsUInt64)value);
  }
  status = plc_tag_write(tagIndex, 100);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  if (drvUser->dataType == "REAL")
  {
    plc_tag_set_float32(tagIndex, offset, (epicsFloat32)value);
  }
  else if (drvUser->dataType == "LREAL")
  {
    plc_tag_set_float32(tagIndex, offset, value);
  }
  status = plc_tag_write(tagIndex, 300);
  return asynSuccess;
}

asynStatus drvOmronEIP::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;

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
    status = plc_tag_write(tagIndex, 300);

    memcpy(nActual, &nChars, sizeof(size_t));
  }
  else if (drvUser->dataType == "UDT")
  {
  }
  return asynSuccess;
}

extern "C"
{
    /*
  ** drvOmronEIPConfigPoller() - create and init an asyn port driver for a PLC
  */

  /** EPICS iocsh callable function to call constructor for the drvOmronEIP class. */

  /* Creates a new poller with user provided settings and adds it to the driver */
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
                                  char *plcType)
  {
    new drvOmronEIP(portName,
                    gateway,
                    path,
                    plcType);

    return asynSuccess;
  }

  /* iocsh functions */

  static const iocshArg ConfigureArg0 = {"Port name", iocshArgString};
  static const iocshArg ConfigureArg1 = {"Gateway", iocshArgString};
  static const iocshArg ConfigureArg2 = {"Path", iocshArgString};
  static const iocshArg ConfigureArg3 = {"PLC type", iocshArgString};

  static const iocshArg *const drvOmronEIPConfigureArgs[4] = {
      &ConfigureArg0,
      &ConfigureArg1,
      &ConfigureArg2,
      &ConfigureArg3};

  static const iocshFuncDef drvOmronEIPConfigureFuncDef = {"drvOmronEIPConfigure", 4, drvOmronEIPConfigureArgs};

  static void drvOmronEIPConfigureCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPConfigure(args[0].sval, args[1].sval, args[2].sval, args[3].sval);
  }

  static void drvOmronEIPRegister(void)
  {
    iocshRegister(&drvOmronEIPConfigureFuncDef, drvOmronEIPConfigureCallFunc);
    iocshRegister(&drvOmronEIPConfigPollerFuncDef, drvOmronEIPConfigPollerCallFunc);
  }

  epicsExportRegistrar(drvOmronEIPRegister);

} // extern "C"
