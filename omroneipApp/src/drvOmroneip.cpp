#include "omroneip.h"
#include "drvOmroneip.h"
#include <sstream>

// This stores information about each communication tag to the PLC.
// A new instance will be made for each record which requsts to uniquely read/write to the PLC
struct omronDrvUser_t
{
  size_t startIndex;
  size_t sliceSize;
  std::string tag;
  std::string dataType;
  int32_t tagIndex;
  size_t dataCounter;
  std::string pollerName;
  size_t tagOffset;
  double timeout;
  std::string offsetFlag;
  size_t strCapacity;
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

bool iocStarted = false; // Set to 1 after IocInit() which starts pollers

static const char *driverName = "drvOmronEIP"; /* String for asynPrint */

static void readPollerC(void *drvPvt)
{
  drvOmronEIP *pPvt = (drvOmronEIP *)drvPvt;
  pPvt->readPoller();
}

// this thread runs once after iocInit to optimise the tag map before the thread poller begins
static int optimiseTagsC(void *drvPvt)
{
  drvOmronEIP *pPvt = (drvOmronEIP *)drvPvt;
  while (!iocStarted)
  {
    epicsThreadSleep(0.1);
  }
  return pPvt->optimiseTags();
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
  epicsThreadSleep(1);
}

/* This function is called by the IOC load system after iocInit() or iocRun() have completed */
static void myInitHookFunction(initHookState state)
{
  switch(state) {
    case initHookAfterIocRunning:
      iocStarted = true;
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
  plc_tag_set_debug_level(debugLevel);
  initHookRegister(myInitHookFunction);
  asynStatus status = (asynStatus)(epicsThreadCreate("optimiseTags",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)optimiseTagsC,
                          this) == NULL);
  if (status!=0){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Returned asyn status: %i\n", driverName, functionName, status);
  }
  initialized_ = true;
}

asynStatus drvOmronEIP::createPoller(const char *portName, const char *pollerName, double updateRate)
{
  int status;
  omronEIPPoller* pPoller = new omronEIPPoller(portName, pollerName, updateRate);
  pollerList_[pPoller->pollerName_] = pPoller;
  status = (epicsThreadCreate(pPoller->pollerName_,
                            epicsThreadPriorityMedium,
                            epicsThreadGetStackSize(epicsThreadStackMedium),
                            (EPICSTHREADFUNC)readPollerC,
                            this) == NULL);
  delete(pPoller);
  return (asynStatus)status;
}

asynStatus drvOmronEIP::drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)
{
  static const char *functionName = "drvUserCreate";
  std::string drvInfoString;
  std::string name;
  std::string tag;
  int asynIndex = 0; //index of asyn parameterr
  int32_t tagIndex; //index of libplctag tag
  int addr;
  asynStatus status = asynSuccess;

  if (initialized_ == false)
  {
    pasynManager->enable(pasynUser, 0);
    return asynDisabled;
  }

  status = getAddress(pasynUser, &addr);
  if (status == asynSuccess) {
    status = findParam(addr, drvInfo, &asynIndex);
  }
  else return asynError;

  if (status == asynSuccess)
  {
    // Parameter already exists
    return asynSuccess;
  }

  // Get the required data from the drvInfo string
  std::unordered_map<std::string, std::string> keyWords = drvInfoParser(drvInfo);
  if (keyWords.at("stringValid") != "true")
  {
    printf("drvInfo string is invalid, record: %s was not created\n", drvInfo);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s drvInfo string is invalid, record: %s was not created\n", driverName, functionName, drvInfo);
    return asynError;
  }

  tag = tagConnectionString_ +
        "&name=" + keyWords.at("tagName") +
        "&elem_count=" + keyWords.at("sliceSize")
        + keyWords.at("tagExtras");

  //check if a duplicate tag has already been created
  bool dupeTag = false;
  for (auto previousTag: tagMap_)
  {
    if (tag == previousTag.second->tag)
    {
      std::cout << "Duplicate tag exists, reusing tag " << previousTag.second->tagIndex << " for this parameter" <<std::endl;
      tagIndex = previousTag.second->tagIndex; 
      dupeTag = true;
      break;
    }
  }

  if (!dupeTag)
  {
    tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);
  }

  /* Check and report failure codes. An Asyn param will be created even on failure but the record will be given error status */
  if (tagIndex<0)
  {
    const char* error = plc_tag_decode_error(tagIndex);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Tag not added! %s\n", driverName, functionName, error);
  }
  else if (tagIndex == 1)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag not added! Timeout while creating tag.\n", driverName, functionName);
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
  newDrvUser->strCapacity = std::stoi(keyWords.at("strCapacity"));
  newDrvUser->offsetFlag = keyWords.at("offsetFlag");

  status = asynSuccess;
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

  if (asynIndex < 0 || status!=asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Creation of asyn parameter failed for drvInfo:%s Asyn status:%d\n", driverName, functionName, drvInfo, status);
    return status;
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
  if (status!=asynSuccess){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Creation of asyn parameter failed for drvInfo:%s Asyn status:%d\n", driverName, functionName, drvInfo, status);
    return status;
  }

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
    printf("Created libplctag tag with tag index: %d, asyn index: %d and tag string: %s\n", tagIndex, asynIndex, tag.c_str());
  }
  this->unlock();
  return status;
}

asynStatus drvOmronEIP::optimiseTags()
{
  const char * functionName = "drvOptimiseTags";
  std::unordered_map<std::string, int> structIDList; //contains a map of struct paths to the id of the asyn index which gets that struct
  std::unordered_map<std::string, std::vector<int>> commonStructMap; // Contains the structName and a vector of all the asyn Indexes which use this struct
  int status;
  this->lock();
  // Look at the name used for each tag, if the name references a structure (contains a "."), added structure name to map
  // Each tag which references the structure adds its asyn index to the map under the same structure name key
  for (auto thisTag:tagMap_)
  {
    // no optimisation is attempted if the user enters "none" for the offset
    if (thisTag.second->offsetFlag == "false")
    {
      // we set this flag to tell the read poller that this tag should be read
      thisTag.second->offsetFlag = "unique";
      continue;
    }
    size_t pos = 0;
    std::string parent;
    std::string name = thisTag.second->tag;
    name = name.substr(name.find("name=")+5);
    if ((pos = name.find('&')) != std::string::npos) {
      name = name.substr(0, pos);
    }
    // if the tag is a UDT type, then we just immediately add it to the structIDList as the entire struct is already being fetched
    if (thisTag.second->dataType == "UDT"){
      structIDList[name] = thisTag.first;
      continue;
    }
    std::string remaining = name;
    while ((pos = remaining.find('.')) != std::string::npos) {
      parent = remaining.substr(0, pos);
      remaining.erase(0, pos + 1);
    }
    // if parent exists
    if (parent != ""){
      // set the name to the original name minus the child/field part
      name = name.substr(0,name.size()-(remaining.size()+1));
      if (commonStructMap.find(name) == commonStructMap.end()){
        // if struct does not exist in the map, then we add it with this tags asyn index
        commonStructMap[name] = (std::vector<int>) {thisTag.first};
      }
      else {
        commonStructMap.at(name).push_back(thisTag.first);
      }
    }
  }

  size_t countNeeded = 2; // number of tags which need to reference a struct before we decide to fetch the entire struct
  // for each struct which is being referenced at least countNeeded times and which is not already in structIDList, create tag and add to list
  for (auto commonStruct : commonStructMap)
  {
    if (commonStruct.second.size()>= countNeeded)
    {
      if (structIDList.find(commonStruct.first) == structIDList.end()){
        //we must create a new tag and then add it to structIDList if valid
        std::string tag = this->tagConnectionString_ +
                        "&name=" + commonStruct.first +
                        "&elem_count=1&allow_packing=1&str_is_counted=0&str_count_word_bytes=0&str_is_zero_terminated=1";

        int tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);

        if (tagIndex < 0) {
          const char* error = plc_tag_decode_error(tagIndex);
          asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s  Attempt to create optimised tag for asyn index: %d failed. %s\n", driverName, functionName, commonStruct.second[0], error);
        }
        else {
          structIDList[commonStruct.first] = tagIndex;
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Attempting to optimise asyn index: %d, a tag was created with ID: %d and tag string: %s\n", driverName, functionName, commonStruct.second[0], tagIndex, tag.c_str());
        }

        // we must designate one of the asyn Params which gets data from this UDT as the param which reads from the PLC
        tagMap_.find(commonStruct.second[0])->second->offsetFlag = "unique";
      }
    }
  }

  // print out maps

  std::cout<<std::endl;
  for (auto const& i : structIDList) {
    std::cout << i.first << ": " << i.second <<std::endl;
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The structs which are accessed by multiple records are (struct: libplctag tag index list):\n", driverName, functionName);
  for (auto const& i : commonStructMap) {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s: ", i.first.c_str());
    for (auto const& j : i.second)
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%d ", j);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The structs alongside the libplctag tag index chosen to read them are:\n", driverName, functionName);
  for (auto const& i : structIDList) {
    std::cout << i.first << ": " << i.second <<std::endl;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s: %d \n", i.first.c_str());
  }

  //now that we have a map of structs and which index gets them as well as a map of structs and which indexes need to access them, we can optimise
  omronDrvUser_t * drvUser;
  for (auto commonStruct : commonStructMap)
  {
    for (int asynIndex : commonStruct.second)
    {
      bool matchFound = false;
      if (this->tagMap_.find(asynIndex)!=this->tagMap_.end()) {
        drvUser = this->tagMap_.at(asynIndex);
      }
      else {
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Attempt to optimise asyn index: %d failed.\n", driverName, functionName, asynIndex);
        break;
      }

      for (auto masterStruct : structIDList)
      {
        if (commonStruct.first == masterStruct.first)
        {
          matchFound = true;
          if (asynIndex == masterStruct.second) {
            // case where the asyn index we are optimising to, is our own index
            // we set this flag to tell the read poller that this tag should be read
            drvUser->offsetFlag = "unique";
            break;
          }
          // we have found a tag that already exists which gets this struct, so we piggy back off this
          // now we get the drvUser for the asyn parameter which is to be optimised and set its tagIndex to the tagIndex we piggy back off
          status = plc_tag_destroy(drvUser->tagIndex); //destroy old tag
          if (status!=0) {
            asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s  libplctag tag destruction process failed for id %d\n", driverName, functionName, drvUser->tagIndex);
          }
          drvUser->tagIndex = masterStruct.second;
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn index: %d Optimised to use libplctag tag with libplctag index: %d\n", driverName, functionName, asynIndex, masterStruct.second);
        }
      }
      if (!matchFound && commonStruct.second.size() >= countNeeded)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s  Attempt to optimise asyn index: %d failed.\n", driverName, functionName, asynIndex);
      }
    }
  }

  this->unlock();
  this->startPollers_ = true;
  return asynSuccess; // always return success as any failures to optimise should leave the driver in its pre omtimised state which should have no erros
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
      {"strCapacity", "0"}, // only needed for getting strings from UDTs  
      {"offsetFlag", "false"}, // set to true if the user enters a value for offset other than none, later set to "unique" to identify a tag as needing to be read from PLC
      {"stringValid", "true"} // set to false if errors are detected which aborts creation of tag and asyn parameter
  };
  std::list<std::string> words;
  std::string substring;
  bool escaped = false;
  size_t pos = 0;

  for (size_t i = 0; i < str.size(); i++)
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
    // Sort out potential readpoller reference
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
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Processing drvInfo parameter: %s\n", driverName, functionName, words.front().c_str());
    if (i == 0)
    {
      // Check for valid name or name[startIndex]
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
      // Checking for valid datatype
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
      // Checking for valid sliceSize
      char *p;
      if (words.front() == "none"){
        keyWords.at("sliceSize") = "1";
      }
      else {
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
      }
      words.pop_front();
    }
    else if (i == 3)
    {
      // Checking for valid offset
      size_t structIndex = 0; // will store user supplied index into user supplied struct
      size_t indexStartPos = 0; // stores the position of the first '[' within the user supplied string
      size_t offset = 0;
      // user has chosen not to use an offset
      if (words.front() == "none")
      {
        keyWords.at("offsetFlag") = "false";
        keyWords.at("offset") = "0";
      }
      else {
        // attempt to set offset to integer, if not possible then assume it is a structname
        try
        {
          offset = std::stoi(words.front());
          keyWords.at("offsetFlag") = "true";
        }
        catch(...)
        {
          // attempt to split name and integer
          for (size_t n = 0; n<words.front().size(); n++)
          {
            if (words.front().c_str()[n] == '[')
            {
              std::string offsetSubstring = words.front().substr(n+1,words.front().size()-(n+1));
              indexStartPos = n;
              for (size_t m = 0; m<offsetSubstring.size(); m++)
              {
                if (offsetSubstring.c_str()[m] == ']')
                {
                  try
                  {
                    //struct integer found
                    structIndex = std::stoi(offsetSubstring.substr(0,m));
                    keyWords.at("offsetFlag") = "true";
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
      }
      words.pop_front();
    }
    else if (i == 4)
    {
      // Check for valid extra attributes
      /* These attributes overwrite libplctag attributes, other attributes which arent overwritten are not mentioned here
        Users can overwrite these defaults and other libplctag defaults from their records */
      std::unordered_map<std::string, std::string> defaultTagAttribs = {
          {"allow_packing=", "1"},
          {"str_is_zero_terminated=", "0"},
          {"str_is_fixed_length=", "0"},
          {"str_is_counted=", "1"},
          {"str_count_word_bytes=", "2"},
          {"str_pad_bytes=", "0"}};
      std::string extrasString;
      if (words.front()!="0" && words.front()!="none")
      {
        // The user has specified attributes other than default, these will either be added to the list or replace existing default values
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

        // we check to see if str_capacity is set, this is needed to get strings from UDTs
        size_t pos = words.front().find("str_max_capacity=");
        std::string size;
        if (pos != std::string::npos)
        {
          std::string remaining = words.front().substr(pos + 17, words.front().size());
          auto nextPos = remaining.find('&');
          if (nextPos != std::string::npos)
          {
            size = remaining.substr(0, nextPos);
          }
          else
          {
            size = remaining.substr(0, remaining.size());
          }
          keyWords.at("strCapacity") = size; 
        }
      }

      for (auto attrib : defaultTagAttribs)
      {
        if (attrib.first.substr(0,3) == "str" && keyWords.at("dataType") != "STRING")
        {
          // If the user requests str type attributes for a non STRING record then ignore
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

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Creating libplctag tag with the following parameters:\n", driverName, functionName);
  for (auto i = keyWords.begin(); i != keyWords.end(); i++)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s \t\t\t %s\n", i->first.c_str(), i->second.c_str());
  }
  return keyWords;
}

asynStatus drvOmronEIP::loadStructFile(const char * portName, const char * filePath)
{
  const char * functionName = "loadStructFile";
  this->lock();
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

    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Loading in struct file with the following definitions:\n", driverName, functionName);
    for (auto const& i : structMap) {
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s: ", i.first.c_str());
      for (auto const& j : i.second)
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s ", j.c_str());
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");
    }
    this->createStructMap(structMap);
    return asynSuccess;
}

asynStatus drvOmronEIP::createStructMap(std::unordered_map<std::string, std::vector<std::string>> rawMap)
{
  const char * functionName = "createStructMap";
  asynStatus status = asynSuccess;
  std::unordered_map<std::string, std::vector<int>> structMap;
  for (auto kv: rawMap)
  {// look for kv.first in structMap through a recursive search
    status = (asynStatus) findOffsetsRecursive(rawMap, kv.first, structMap);
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The processed struct definitions with their calculated offsets are:\n", driverName, functionName);
  for (auto const& i : structMap) {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s: ", i.first.c_str());
    for (auto const& j : i.second)
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%d ", j);
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");
  }
  this->structMap_= structMap;
  if (status != asynSuccess)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s An error occured while calculating the offsets within the loaded struct file\n", driverName, functionName);
    return asynError;
  }
  this->unlock();
  return asynSuccess;
}

size_t drvOmronEIP::findOffsetsRecursive(std::unordered_map<std::string, std::vector<std::string>> const& rawMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap)
{
  std::vector<int> newRow = {0};
  size_t offSetsCounted = 0;
  std::vector<std::string> row = rawMap.at(structName);
  size_t lastOffset = 0;
  bool structInvalid = false;
  size_t thisOffset = 0;
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
        size_t strLength = 0;
        bool intFound = false;
        for (size_t i = 0; i<ss.size(); i++)
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
          return asynError;
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
            return asynError;
          }
          thisOffset = structSize+lastOffset;
        }
        catch (...)
        {
          structInvalid = true;
          std::cout<<"Failed to find the standard datatype: " << dtype << ". Definition for " << structName << " and its dependents failed" << std::endl;
          return asynError;
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
        return asynError;
    }
  }
  // Dont think its possible to get here
  return asynError;
}

void drvOmronEIP::readPoller()
{
  static const char *functionName = "readPoller";
  std::string threadName = epicsThreadGetNameSelf();
  std::string tag;
  int offset;
  int status;
  int still_pending = 1;
  int timeTaken = 0;
  //The poller may not be fully initialised until the startPollers_ flag is set to 1, do not attempt to get the pPoller yet
  while (!this->startPollers_ || !omronExiting_) {epicsThreadSleep(0.1);}
  omronEIPPoller* pPoller = pollerList_.at(threadName);
  double interval = pPoller->updateRate_;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Starting poller: %s with interval: %f\n", driverName, functionName, threadName.c_str(), interval);
  while (!omronExiting_)
  {
    double waitTime = interval - ((double)timeTaken / 1000);
    if (waitTime >=0) {
      epicsThreadSleep(waitTime);
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Warning! Reads taking longer than requested! %f > %f\n", driverName, functionName, ((double)timeTaken / 1000), interval);
    }

    auto startTime = std::chrono::system_clock::now();
    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName && x.second->offsetFlag == "unique")
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Reading tag: %d with polling interval: %f seconds\n", driverName, functionName, x.second->tagIndex, interval);
        plc_tag_read(x.second->tagIndex, 0); // read from plc as fast as possible, we will check status and timeouts later
      }
    }

    int waits = 0;
    bool readFailed = false;
    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName)
      {
        offset = x.second->tagOffset;
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
              asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Timeout finishing read tag %d: %s. Try decreasing the polling rate\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
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
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Error finishing read tag %d: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
            break;
          }
        }

        if (!readFailed)
        {
          if (x.second->dataType == "BOOL")
          {
            epicsUInt32 data;
            data = plc_tag_get_bit(x.second->tagIndex, offset);
            status = setUIntDigitalParam(x.first, data, 0xFF, 0xFF);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %d My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "INT")
          {
            epicsInt16 data;
            data = plc_tag_get_int16(x.second->tagIndex, offset);
            status = setIntegerParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %d My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "DINT")
          {
            epicsInt32 data;
            data = plc_tag_get_int32(x.second->tagIndex, offset);
            status = setIntegerParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %d My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "LINT")
          {
            epicsInt64 data;
            data = plc_tag_get_int64(x.second->tagIndex, offset);
            status = setInteger64Param(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %lld My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "UINT")
          {
            epicsUInt16 data;
            data = plc_tag_get_uint16(x.second->tagIndex, offset);
            status = setIntegerParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %u My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "UDINT")
          {
            epicsUInt32 data;
            data = plc_tag_get_uint32(x.second->tagIndex, offset);
            status = setIntegerParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %u My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "ULINT")
          {
            epicsUInt64 data;
            data = plc_tag_get_uint64(x.second->tagIndex, offset);
            status = setInteger64Param(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %llu My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "REAL")
          {
            epicsFloat32 data;
            data = plc_tag_get_float32(x.second->tagIndex, offset);
            status = setDoubleParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %f My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "LREAL")
          {
            epicsFloat64 data;
            data = plc_tag_get_float64(x.second->tagIndex, offset);
            status = setDoubleParam(x.first, data);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %f My type %s\n", driverName, functionName, x.first, x.second->tagIndex, data, x.second->dataType.c_str());
          }
          else if (x.second->dataType == "STRING")
          {
            int bufferSize = plc_tag_get_size(x.second->tagIndex);
            int string_length = plc_tag_get_string_length(x.second->tagIndex, offset)+1;
            int string_capacity = plc_tag_get_string_capacity(x.second->tagIndex, 0);
            if (offset >= bufferSize-1)
            {
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Attempting to read at an offset beyond tag buffer!\n", driverName, functionName);
              continue;
            }
            if (x.second->strCapacity != 0)
            {
              // If we are getting a string from a UDT, we must overwrite the string capacity
              string_capacity = x.second->strCapacity, string_length = x.second->strCapacity;
            }
            else if (string_length>string_capacity+1)
            {
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Offset does not point to valid string!\n", driverName, functionName);
              continue;
            }
            char *pData = (char *)malloc((size_t)(unsigned int)(string_length));
            status = plc_tag_get_string(x.second->tagIndex, offset, pData, string_length);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing STRING data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            status = setStringParam(x.first, pData);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, pData, x.second->dataType.c_str());
            free(pData);
          }
          else if (x.second->dataType == "WORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing WORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %x My type %s\n", driverName, functionName, x.first, x.second->tagIndex, pData, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "DWORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing DWORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %x My type %s\n", driverName, functionName, x.first, x.second->tagIndex, pData, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "LWORD")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing LWORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            for (int i = 0; i < bytes; i++)
            {
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %x My type %s\n", driverName, functionName, x.first, x.second->tagIndex, pData, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "UDT")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc(bytes * sizeof(uint8_t));
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing UDT data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            memcpy(pData, rawData, bytes);
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %x My type %s\n", driverName, functionName, x.first, x.second->tagIndex, rawData, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "TIME")
          {
            int bytes = plc_tag_get_size(x.second->tagIndex);
            uint8_t *rawData = (uint8_t *)malloc(bytes * sizeof(uint8_t));
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing TIME data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            memcpy(pData, rawData, bytes);
            status = setStringParam(x.first, pData);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, pData, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          setParamStatus(x.first, (asynStatus)status);
          if (status!=asynSuccess) {
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error occured while updating asyn parameter with asyn ID: %d tagIndex: %d Datatype %s\n", driverName, functionName, x.first, x.second->tagIndex, x.second->dataType.c_str());
          }
        }
      }
    }
    status = callParamCallbacks();
    if (status!=asynSuccess) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error performing asyn callbacks on read poller: %s\n", driverName, functionName, threadName.c_str());
    }
    auto endTime = std::chrono::system_clock::now();
    timeTaken = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    if (timeTaken>0)
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Poller: %s finished processing in: %d msec\n\n", driverName, functionName, threadName.c_str(), timeTaken);
  }
}

/* Reimplementation of asyn interfaces*/

asynStatus drvOmronEIP::readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn)
{
  const char * functionName = "readInt8Array";
  int status;
  for (auto x : tagMap_)
  {
    int offset = x.second->tagOffset;
    if (x.second->dataType == "UDT")
    {
      size_t bytes = plc_tag_get_size(x.second->tagIndex);
      uint8_t *pOutput = (uint8_t *)malloc(bytes * sizeof(uint8_t));
      status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, pOutput, bytes); /* +1 for the zero termination */
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Read attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
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
      free(pOutput);
    }
  }
  return asynSuccess;
}


asynStatus drvOmronEIP::writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
  const char * functionName = "writeInt8Array";
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  int status;
  size_t tagSize = plc_tag_get_size(tagIndex);
  double timeout = pasynUser->timeout*1000;
  if (nElements>tagSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    nElements=tagSize;
  }
  if (drvUser->dataType == "UDT")
  {
    uint8_t *pOutput = (uint8_t *)malloc(nElements * sizeof(uint8_t));
    memcpy(pOutput, value, nElements);
    status = plc_tag_set_raw_bytes(tagIndex, offset, pOutput ,nElements);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    free(pOutput);
  }
  else if (drvUser->dataType == "WORD" || drvUser->dataType == "DWORD" || drvUser->dataType == "LWORD")
  {
    uint8_t *pOutput = (uint8_t *)malloc(nElements * sizeof(uint8_t));
    int j = nElements-1;
    for (size_t i=0; i<nElements; i++)
    {
      pOutput[i] = value[j];
      j--;
    }
    status = plc_tag_set_raw_bytes(tagIndex, offset, pOutput ,nElements);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    free(pOutput);
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
  const char * functionName = "writeUInt32Digital";
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "BOOL")
  {
    status = plc_tag_set_bit(tagIndex, offset, value);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
  }
  return asynError;
}

asynStatus drvOmronEIP::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  const char * functionName = "writeInt32";
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;

  if (drvUser->dataType == "INT")
  {
    status = plc_tag_set_int16(tagIndex, offset, (epicsInt16)value);
  }
  else if (drvUser->dataType == "DINT")
  {
    status = plc_tag_set_int32(tagIndex, offset, value);
  }
  else if (drvUser->dataType == "UINT")
  {
    status = plc_tag_set_int32(tagIndex, offset, (epicsUInt16)value);
  }
  else if (drvUser->dataType == "UDINT")
  {
    status = plc_tag_set_int32(tagIndex, offset, (epicsUInt32)value);
  }
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt64(asynUser *pasynUser, epicsInt64 value)
{
  const char * functionName = "writeInt64";
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "LINT")
  {
    status = plc_tag_set_int64(tagIndex, offset, value);
  }
  else if (drvUser->dataType == "ULINT")
  {
    status = plc_tag_set_int64(tagIndex, offset, (epicsUInt64)value);
  }
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  const char * functionName = "writeFloat64";
  int status = 0;
  omronDrvUser_t *drvUser = (omronDrvUser_t *)pasynUser->drvUser;
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  double timeout = pasynUser->timeout*1000;
  if (drvUser->dataType == "REAL")
  {
    status = plc_tag_set_float32(tagIndex, offset, (epicsFloat32)value);
  }
  else if (drvUser->dataType == "LREAL")
  {
    status = plc_tag_set_float64(tagIndex, offset, value);
  }
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
  const char * functionName = "writeOctet";
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
    /* First check if the user has set this in the extras parameter, if not then we set to the size of nChars and hope it fits*/
    if (drvUser->strCapacity == 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s A write to a string tag is being attempted without defining the capacity of this string, this may not work! Tag index: %d\n", driverName, functionName, tagIndex);       
      string_capacity = nChars;
    }
    status = plc_tag_set_size(tagIndex, string_capacity + 2);     // Allow room for string length
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Resizing libplctag tag buffer returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_set_string(tagIndex, offset, stringOut); // Set the data
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_set_size(tagIndex, nChars + 2);              // Reduce the tag buffer to delete any data beyond the string we pass in
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Resizing libplctag tag buffer returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    memcpy(nActual, &nChars, sizeof(size_t));
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s WriteOctet is only allowed for STRING type.\n", driverName, functionName); 
    return asynError;
  }
  return asynSuccess;
}

drvOmronEIP::~drvOmronEIP()
{
  std::cout<<"drvOmronEIP shutting down"<<std::endl;
}

omronEIPPoller::~omronEIPPoller()
{
  std::cout<<"Poller " << this->pollerName_<< " shutting down"<<std::endl;
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
