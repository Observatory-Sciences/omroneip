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
  std::string optimisationFlag;
  size_t strCapacity;
  bool readFlag;
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
  int status = pPvt->optimiseTags();
  return status;
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
      initialized_(false),
      omronExiting_(false)

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
    std::cout<< "Error, Driver initiation returned asyn status:" << status << std::endl;
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
  return (asynStatus)status;
}

asynStatus drvOmronEIP::drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)
{
  static const char *functionName = "drvUserCreate";
  std::string drvInfoString;
  std::string name;
  std::string tag;
  bool dupeTag = false;
  int asynIndex = -1; //index of asyn parameterr
  int32_t tagIndex = -1; //index of libplctag tag
  int addr = -1;
  asynStatus status = asynSuccess;
  bool readFlag = true;

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
    readFlag = false;
    printf("Error! drvInfo string is invalid, record: %s was not created\n", drvInfo);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s drvInfo string is invalid, record: %s was not created\n", driverName, functionName, drvInfo);
    tag = "Invalid tag!";
  }
  else {

    tag = tagConnectionString_ +
          "&name=" + keyWords.at("tagName") +
          "&elem_count=" + keyWords.at("sliceSize")
          + keyWords.at("tagExtras");

    //check if a duplicate tag has already been created, must use the same poller
    for (auto previousTag: tagMap_)
    {
      if (tag == previousTag.second->tag && keyWords.at("pollerName") == previousTag.second->pollerName)
      {
        /* Potential extension here to allow tags with different pollers to be combined, but must check the polling duration so that the shortest polling duration is used as the master tag */
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
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Tag not added! %s. drvInfo: %s\n", driverName, functionName, error, drvInfo);
    }
    else if (tagIndex == 1)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag not added! Timeout while creating tag. drvInfo: %s\n", driverName, functionName, drvInfo);
    }
  }


  /* Initialise each datatype */
  /* Some of these values may be updated during optimisations and some may become outdated */
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
  newDrvUser->optimisationFlag = keyWords.at("optimisationFlag");
  newDrvUser->readFlag = readFlag;

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
  std::unordered_map<std::string, int> structIDList; // Contains a map of struct paths along with the id of the asyn index which gets that struct
  std::unordered_map<std::string, std::vector<int>> commonStructMap; // Contains the structName along with a vector of all the asyn Indexes which use this struct
  std::vector<int> destroyList; // Contains a list of libplctag indexes to be destroyed as they are no longer used
  int status;
  this->lock();
  // Look at the name used for each tag, if the name references a structure (contains a "."), add the structure name to map
  // Each tag which references the structure adds its asyn index to the map under the same structure name key
  for (auto thisTag:tagMap_)
  {
    if (thisTag.second->pollerName!="none") { // Only interested in polled tags
      // no optimisation is attempted if the user enters "dont optimise" for the offset
      if (thisTag.second->optimisationFlag == "dont optimise")
      {
        // ensure that this flag is true
        thisTag.second->readFlag = true;
        continue;
      }
      size_t pos = 0;
      std::string parent;
      std::string name = thisTag.second->tag;
      name = name.substr(name.find("name=")+5);
      if ((pos = name.find('&')) != std::string::npos) {
        name = name.substr(0, pos);
      }
      // if the tag is a UDT type, then we just immediately add it to the commonStructMap as the entire struct is already being fetched
      if (thisTag.second->dataType == "UDT"){
        commonStructMap[name].insert(commonStructMap[name].begin(), thisTag.first);
        continue;
      }
      std::string remaining = name;
      while ((pos = remaining.find('.')) != std::string::npos) {
        parent = remaining.substr(0, pos);
        remaining.erase(0, pos + 1);
      }
      // Check for a valid parent
      if (parent != ""){
        // Set the name to the original name minus the child/field part
        name = name.substr(0,name.size()-(remaining.size()+1));
        if (commonStructMap.find(name) == commonStructMap.end()){
          // If struct does not exist in the map, then we add it with this tags asyn index
          commonStructMap[name] = (std::vector<int>) {thisTag.first};
        }
        else {
          commonStructMap.at(name).push_back(thisTag.first);
        }
      }
      else {
        // This tag is not a UDT or a child, so it cant be optimised, therefor we set the optimisationFlag to "optimisation failed"
        thisTag.second->optimisationFlag = "optimisation failed";
        thisTag.second->readFlag = true;
      }
    }
  }

  size_t countNeeded = 2; // The number of tags which need to reference a struct before we decide it is more efficient to fetch the entire struct
  // For each struct which is being referenced at least countNeeded times and which is not already in structIDList, create tag and add to list
  for (auto commonStruct : commonStructMap)
  {
    if (commonStruct.second.size()>= countNeeded)
    {
      if (structIDList.find(commonStruct.first) == structIDList.end()){
        // We must create a new libplctag tag and then add it to structIDList if valid
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

        // We arbitrarily designate the first asynIndex in the vector as the "master" index which has its libplctag tag read
        tagMap_.find(commonStruct.second[0])->second->optimisationFlag = "master";
        tagMap_.find(commonStruct.second[0])->second->readFlag = true;
      }
    }
    else {
      // Case where a child is found, but it is the only one, therefor no optimisation is possible and it is made unique
      tagMap_.find(commonStruct.second[0])->second->optimisationFlag = "optimisation failed";
      tagMap_.find(commonStruct.second[0])->second->readFlag = true;
    }
  }


  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The structs which are accessed by multiple records are (struct: asyn index list):\n", driverName, functionName);
  std::string flowString;
  for (auto const& i : commonStructMap) {
    flowString += i.first + ": ";
    for (auto const& j : i.second)
      flowString += std::to_string(j) + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The structs alongside the libplctag index chosen to read them are:\n", driverName, functionName);
  for (auto const& i : structIDList) {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s: %d \n", i.first.c_str(), i.second);
  }

  // Now that we have a map of structs and which index gets them as well as a map of structs and which indexes need to access them, we can optimise
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
        drvUser->optimisationFlag = "optimisation failed"; // Could not find an optimisation so this tag will be processed directly
        drvUser->readFlag = true;
        break;
      }

      for (auto masterStruct : structIDList)
      {
        if (commonStruct.first == masterStruct.first)
        {
          // we have found a tag that already exists which gets this struct, so we piggy back off this
          // now we get the drvUser for the asyn parameter which is to be optimised and set its tagIndex to the tagIndex we piggy back off
          matchFound = true;
          destroyList.push_back(drvUser->tagIndex);
          //std::cout<<"Old value was: "<<drvUser->tagIndex<<std::endl;
          drvUser->tagIndex = masterStruct.second;
          //std::cout<<"New value is: "<< this->tagMap_.find(asynIndex)->second->tagIndex<<std::endl;
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn index: %d Optimised to use libplctag tag with libplctag index: %d\n", driverName, functionName, asynIndex, masterStruct.second);
          if (drvUser->optimisationFlag != "master") {
            // If we are the master then our tagIndex will be read, otherwise we will rely on the data read by the master         
            drvUser->optimisationFlag = "optimised";
            drvUser->readFlag = false;
          }
        }
      }
      if (!matchFound && commonStruct.second.size() >= countNeeded)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s  Warning, Attempt to optimise asyn index: %d failed. The driver will use a default setup.\n", driverName, functionName, asynIndex);
        drvUser->optimisationFlag = "optimisation failed";
      }
    }
  }

  std::sort(destroyList.begin(), destroyList.end());
  std::vector<int>::iterator it = std::unique(destroyList.begin(), destroyList.end());
  destroyList.erase(it, destroyList.end());
  for (auto index: destroyList) {
    status = plc_tag_destroy(index); // Destroy old tags
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Destroyed redundant libplctag: %d\n", driverName, functionName, index);
    if (status!=0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s  Warning, libplctag tag destruction process failed for libplctag index %d\n", driverName, functionName, index);
    }
  }

  for (auto tag: tagMap_)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn param: %d optimisationFlag: '%s' readFlag: %s\n", driverName, functionName, tag.first, tag.second->optimisationFlag.c_str(), tag.second->readFlag ? "true" : "false");
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
      {"optimisationFlag", "not requested"}, // stores the status of optimisation, ("not requested", "attempt optimisation","dont optimise","optimisation failed","optimised","master")
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
      size_t indexStartPos = 0; // stores the position of the first '[' within the user supplied string
      size_t offset;
      std::vector<size_t> structIndices; // the indice(s) within the structure specified by the user
      bool indexFound = false;
      bool firstIndex = true;
      // user has chosen not to use an offset
      if (words.front() == "none")
      {
        keyWords.at("optimisationFlag") = "dont optimise";
        keyWords.at("offset") = "0";
      }
      else {
        // attempt to set offset to integer, if not possible then assume it is a structname
        try
        {
          offset = std::stoi(words.front());
          keyWords.at("optimisationFlag") = "attempt optimisation";
        }
        catch(...)
        {
          // attempt to split name and integer
          for (size_t n = 0; n<words.front().size(); n++)
          {
            if (words.front().c_str()[n] == '[')
            {
              std::string offsetSubstring = words.front().substr(n+1,words.front().size()-(n+1));
              if (firstIndex) {indexStartPos = n;} //only want to update indexStartPos, when we find the first index
              for (size_t m = 0; m<offsetSubstring.size(); m++)
              {
                if (offsetSubstring.c_str()[m] == ']')
                {
                  try
                  {
                    //struct integer found
                    structIndices.push_back(std::stoi(offsetSubstring.substr(0,m)));
                    keyWords.at("optimisationFlag") = "attempt optimisation";
                    indexFound = true;
                    firstIndex = false;
                  }
                  catch(...)
                  {
                    std::cout << "Error, could not find a valid index for the requested structure: " << words.front() << std::endl;
                    keyWords.at("stringValid") = "false";
                  }
                  break;
                }
              }
            }
          }
          if (!indexFound){
            std::cout<<"Invalid index for requested structure: " << words.front() << std::endl;
            keyWords.at("stringValid") = "false";
          }
          
          //look for matching structure in structMap_
          //if found, look for the offset at the structIndex within the structure
          std::string structName = words.front().substr(0,indexStartPos);
          bool structFound = false;
          for (auto item: this->structMap_)
          {
            if (item.first == structName)
            {
              //requested structure found
              structFound = true;
              offset = findRequestedOffset(structIndices, structName);
              if (offset == -1){
                offset=0;
                std::cout<<"Invalid index or structure name: " <<words.front() << std::endl;
                keyWords.at("stringValid") = "false";
              }
              break;
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

size_t drvOmronEIP::findRequestedOffset(std::vector<size_t> indices, std::string structName)
{
  size_t j = 0; // The element within structDtypeMap (this map includes start: and end: tags)
  size_t m = 0; // Element within structMap
  size_t i = 0; // User supplied element, essentially it keeps count within one "level" of the struct, each index within the indices vector is on a different "level"
  size_t n = 0; // Used to count through arrays to work out how big they are
  size_t k = 0; // Used to count through structs to work out how big they are
  size_t currentIndex = 0; // Used to keep track of which index we are currently on
  size_t offset; // The offset found from structMap based off user requested indices.
  std::string dtype;
  std::vector<std::string> dtypeRow; // check for error
  try {
    dtypeRow = structDtypeMap_.at(structName);
  }
  catch (...) {
    return -1;
  }

  for (size_t index : indices){
    std::cout << index << std::endl;
    currentIndex++;
    for (i = 0; i<=index; i++){
      if (i==index){
        j++; //if we have more than 1 index, we must have a start:structName or start:array, this skips that item
        if (currentIndex == indices.size()-1 && dtypeRow[j].substr(0,6) == "start:"){
          /* if we are in the deepest layer (l== indices.size-1) and the current dtype would be the start of that layer, 
            we must skip the tag so that we are processing the raw dtypes within it */
          j++; 
        }
        break;
      }
      
      if (j < dtypeRow.size()){
        dtype = dtypeRow[j];
      }
      else {
        std::cout<<"Invalid index: " << index << " for structure: " << structName << std::endl;
        return -1; 
      }

      if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
          dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || 
            dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD" || 
              dtype =="TIME" || dtype =="DATE_AND_TIME"){
        j++;m++;
      }
      else if ((structMap_.find(dtype.substr(6)))!=structMap_.end()){
        // look for the next instance of end:structMap and calculate the number of raw dtypes between it and start:structMap
        size_t structDtypes = 0;
        k=j+1; // set k to the first element after start:structMap
        while (dtypeRow[k]!="end:"+dtypeRow[j].substr(6))
        {
          dtype = dtypeRow[k];
          if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
                dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || 
                  dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD" || 
                    dtype =="TIME" || dtype =="DATE_AND_TIME"){
            structDtypes++;
          }
          k++;
          if (k>=dtypeRow.size()){
            break;
          }
        }
        j=k; // j should now be set to the dtype after the end:structName tag
        m+=structDtypes;
      }
      else if (dtype=="start:array"){
        // if this dtype is the start of an array, we get the number of dtypes in the array and skip over these
        for (n = j; n<dtypeRow.size(); n++){
          if (dtypeRow[n]=="end:array"){
            break;
          }
        }
        j += n + 2;
        m += n;
        n=0;
      }
      else if (dtype.substr(0,4)=="end:"){
        j++;
      }
      else {
        std::cout<<"Error"<<std::endl;
      }
    }
  }
  offset = structMap_.at(structName)[m];
  return offset;
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
    std::string flowString;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Loading in struct file with the following definitions:\n", driverName, functionName);
    for (auto const& i : structMap) {
      flowString += i.first + ": ";
      for (auto const& j : i.second)
        flowString += j + " ";
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
      flowString.clear();
    }
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");
    this->createStructMap(structMap);
    this->unlock();
    return asynSuccess;
}

asynStatus drvOmronEIP::createStructMap(std::unordered_map<std::string, std::vector<std::string>> rawMap)
{
  const char * functionName = "createStructMap";
  size_t status = asynSuccess;
  std::unordered_map<std::string, std::vector<int>> structMap;
  std::unordered_map<std::string, std::vector<std::string>> expandedMap = rawMap;
  this->structRawMap_ = rawMap;
  for (auto& kv: expandedMap)
  {// expand embedded structures so that the structure just contains standard dtypes
    kv.second = expandStructsRecursive(expandedMap, kv.first);
  }
  this->structDtypeMap_ = expandedMap;

  std::string flowString;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Struct defs with expanded embedded structs and arrays:\n", driverName, functionName);
  for (auto const& i : expandedMap) {
    flowString += i.first + ": ";
    for (auto const& j : i.second)
      flowString += j + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");

  for (auto kv: rawMap)
  {// look for kv.first in structMap through a recursive search
    status = findOffsets(expandedMap, kv.first, structMap);
  }

  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s The processed struct definitions with their calculated offsets are:\n", driverName, functionName);
  for (auto const& i : structMap) {
    flowString += i.first + ": ";
    for (auto const& j : i.second)
      flowString += std::to_string(j) + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");

  this->structMap_= structMap;
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s An error occured while calculating the offsets within the loaded struct file\n", driverName, functionName);
    return asynError;
  }
  return asynSuccess;
}

std::vector<std::string> drvOmronEIP::expandArrayRecursive(std::unordered_map<std::string, std::vector<std::string>> const& rawMap, std::string arrayDesc)
{
  // "ARRAY[x..y] OF z"
  std::vector<std::string> expandedData;
  std::string ss = arrayDesc.substr(7,arrayDesc.size()-7);
  size_t arrayLength = 0;
  bool dimsFound = false;
  std::string dtype;

  // Get the size of the array
  for (size_t i = 0; i<ss.size(); i++)
  {
    if (ss.substr(i,1)== "]")
    {
      // Get the x..y part and find x and y in order to get the array size
      std::string arrayDims = ss.substr(7, i);
      std::string arrayStartString = arrayDims.substr(0, arrayDims.size()-arrayDims.find(".."));
      std::string arrayEndString = arrayDims.substr(arrayDims.find("..")+2);
      try
      {
        size_t arrayStart = std::stoi(arrayStartString);
        size_t arrayEnd = std::stoi(arrayEndString);
        arrayLength = arrayEnd-arrayStart+1;
        if (arrayStart < 0 || arrayEnd < 0 || arrayLength < 0) throw 1;
        dimsFound = true;
      }
      catch (...)
      {
        std::cout<<"Array dimensions are invalid"<<std::endl;
      }
      break;
    }
  }
  if (!dimsFound)
  {
    std::cout<<"ARRAY type must be of the following format: \"ARRAY[x..y] OF z\", " << arrayDesc << " definition is invalid" <<std::endl;
    expandedData.push_back("Invalid");
    return expandedData;
  }
  
  // Get the datatype of the array
  dtype = ss.substr(ss.find_last_of(' ')+1, ss.size()-(ss.find_last_of(' ')+1)-1); // We dont want the closing "
  std::vector<std::string> singleExpandedData;
  if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || dtype =="UDINT" || 
      dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || 
        dtype == "LREAL" || dtype =="LWORD" || dtype.substr(0,6) == "STRING")
  {
    singleExpandedData.push_back(dtype);
  }
  else
  {
    // We assume that we have an array of structs and so we call the expandStructsRecursive function to get the raw dtypes from the struct
    std::vector<std::string> embeddedDtypes = expandStructsRecursive(rawMap, dtype);
    singleExpandedData.push_back(dtype);
    singleExpandedData.insert(std::end(singleExpandedData), std::begin(embeddedDtypes), std::end(embeddedDtypes));
    singleExpandedData.push_back("end:struct"+dtype);
  }

  for (size_t i = 0; i<arrayLength; i++) {
    expandedData.insert(std::end(expandedData), std::begin(singleExpandedData), std::end(singleExpandedData));
  }
  return expandedData;
}

std::vector<std::string> drvOmronEIP::expandStructsRecursive(std::unordered_map<std::string, std::vector<std::string>> const& rawMap, std::string structName)
{
  std::vector<std::string> row = rawMap.at(structName);
  std::vector<std::string> expandedRow;
  const std::string arrayIdentifier = "\"ARRAY[";
  for (std::string dtype : row)
  {
    if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD" || dtype.substr(0,6) == "STRING")
    {
      expandedRow.push_back(dtype);
    }
    else if (dtype.substr(0,7)==arrayIdentifier)
    {
      // Expand array and add "start:array" and "end:array" either side
      std::vector<std::string> arrayDtypes = expandArrayRecursive(rawMap, dtype);
      expandedRow.push_back("start:array");
      expandedRow.insert(std::end(expandedRow), std::begin(arrayDtypes), std::end(arrayDtypes));
      expandedRow.push_back("end:array");
    }
    else if (dtype.substr(0,6)=="start:" || dtype.substr(0,4)=="end:")
    {
      // The struct we are looking through has already been expanded and this is a tag denoting the struct, therefore we just copy it in
      expandedRow.push_back(dtype);
    }
    else
    {
      // If we get here, then we assume that we have a structure, but it could be a typo
      try
      {
        // Recursively call this function to get all of the embedded datatypes
        // Before each embedded struct we add the struct name and after we add "end", this is so we know when to apply extra padding required for embedded structs
        std::vector<std::string> embeddedDtypes = expandStructsRecursive(rawMap, dtype);
        expandedRow.push_back("start:"+dtype);
        expandedRow.insert(std::end(expandedRow), std::begin(embeddedDtypes), std::end(embeddedDtypes));
        expandedRow.push_back("end:"+dtype);
      }
      catch (...)
      {
        std::cout<<"Failed to find the standard datatype: " << dtype << ". Definition for " << structName << " and its dependents failed" << std::endl;
      }
    }
  }
  return expandedRow;
}

std::string drvOmronEIP::findArrayDtype(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string arrayDesc)
{
  std::list<std::string> dtypeSet = {"INT","DINT","LINT","UINT","UDINT","ULINT","REAL","LREAL","STRING","WORD","DWORD","LWORD","TIME"};
  std::string dtypeData = arrayDesc.substr(arrayDesc.find("] OF ")+5, arrayDesc.size()-(arrayDesc.find("] OF ")+5));
  for (std::string dtype : dtypeSet) {
    // Check to see if array contains a standard dtype
    if (dtypeData.find(dtype) != std::string::npos){
      return dtype;
    }
  }
  // Check to see if array contains a valid struct
  if (expandedMap.find(dtypeData)!=expandedMap.end()){
    return dtypeData;
  }

  // If we get here then we have an invalid array definition
  std::cout<<"Invalid array definition: "<< arrayDesc <<std::endl;
  return "Invalid";
}

size_t drvOmronEIP::getBiggestDtype(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string structName)
{
  size_t thisSize = 0;
  size_t biggestSize = 0;
  std::string arrayDtype;
  std::vector<std::string> expandedRow = expandedMap.at(structName);
  const std::string arrayIdentifier = "\"ARRAY[";
  for (std::string dtype : expandedRow) {
    // Extract the dtype from an array definition string
    if (dtype.substr(0,7) == arrayIdentifier) { //FIX ME !!!!!!
      dtype = findArrayDtype(expandedMap, dtype);
      if (dtype == "Invalid")
      {
        return -1;
      }
    }
    if (dtype == "LREAL" || dtype == "ULINT" || dtype == "LINT" || dtype == "TIME" || dtype == "DATE_AND_TIME") {return 8;}
    else if (dtype == "DWORD" || dtype == "UDINT" || dtype == "DINT" || dtype == "REAL") {thisSize = 4;}
    else if (dtype == "BOOL" || dtype == "WORD" || dtype == "UINT" || dtype == "INT") {thisSize = 2;}
    else if (dtype.substr(0,6) == "STRING") {thisSize = 1;}
    else if (dtype.substr(0,4)=="end:") {continue;} // We find the size of the struct from the start: tag, so the end: tag is skipped
    else if (expandedMap.find(dtype.substr(dtype.find("start:")+6))!=expandedMap.end()){
      // Check to see if any element within the expandedRow is a start:structName. If it is then we must lookup the biggest dtype in this struct
      thisSize = getBiggestDtype(expandedMap, dtype.substr(dtype.find("start:")+6));
    }
    else {
      std::cout <<"Could not find the size of dtype: " << dtype <<std::endl;
      return -1;
    }
    if (thisSize>biggestSize)
    {
      biggestSize=thisSize;
    }
  }
  return biggestSize;
}

// Return first raw dtype encountered, update alignment if we encounter the start or end of a struct
size_t drvOmronEIP::getEmbeddedAlignment(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string structName, std::string nextItem, size_t i){
  std::vector<std::string> expandedRow = expandedMap.at(structName);
  std::string nextNextItem;
  size_t alignment = 0;
  std::string nextStruct = nextItem.substr(nextItem.find(':')+1,nextItem.size()-(nextItem.find(':')+1)); // removes start: and end: from the string
  // get the bit after : If this string is a valid struct name, lookup the alignment from that struct. If it is an array, then the alignment is calculated
  // based off the next dtype which may be raw or a struct. If a struct then get biggest from struct
  // Get next raw dtype

  if (nextItem == "array"){
    // If the next item is an array start tag, then we need to get the alignment of the first item in the array, this may be a struct
    if (i+2 < expandedRow.size()){
      nextNextItem = expandedRow[i+2];
    }
    else {nextNextItem="none";}
    
    if (nextNextItem == "none") {alignment=0;}
    else if (nextNextItem == "LREAL" || nextNextItem == "ULINT" || nextNextItem == "LINT" || nextNextItem == "TIME" || nextNextItem == "DATE_AND_TIME") {alignment = 8;}
    else if (nextNextItem == "DWORD" || nextNextItem == "UDINT" || nextNextItem == "DINT" || nextNextItem == "REAL") {alignment = 4;}
    else if (nextNextItem == "BOOL" || nextNextItem == "WORD" || nextNextItem == "UINT" || nextNextItem == "INT") {alignment = 2;}
    else if (nextNextItem.substr(0,6) == "STRING") {alignment = 1;}
    else if (expandedMap.find(nextNextItem.substr(nextNextItem.find("start:")+6))!=expandedMap.end()){
      // Check to see if the array dtype is a start:structName. If it is then we must lookup the biggest dtype in this struct
      alignment = getBiggestDtype(expandedMap, nextNextItem.substr(nextNextItem.find("start:")+6));
    }
    else {
      std::cout <<"Could not find the alignment rule for: " << nextNextItem <<std::endl;
      return -1;
    }
  }
  else {
    // If the nextItem is a struct start or end, then we look up the largest item in the struct and use this to calculate alignment/padding
    try
    {
      expandedMap.at(nextStruct);
      alignment = getBiggestDtype(expandedMap, nextStruct);
    }
    catch (...)
    {
      std::cout<<"Invalid datatype: " << nextStruct << ". Definition for " << structName << " is invalid" << std::endl;
      return -1;
    }
  }
  return alignment;
}

size_t drvOmronEIP::findOffsets(std::unordered_map<std::string, std::vector<std::string>> const& expandedMap, std::string structName, std::unordered_map<std::string, std::vector<int>>& structMap)
{
  // Calculate the size of each datatype
  // Special case for strings which are sized based on their length
  // Special case for arrays where their size is based on the number of elements and their type, special case for bool arrays
  // Calculate the size of padding
  // For simple situations where the next item is a dtype then we just pad to the alignment of that dtype
  // If this item is a struct definition, then this offset is skipped and the next offset is padded based off biggest item in struct
  // If the next item is "end" then this offset is skipped and the next offset is padded based off biggest item in struct
  // Arrays are padded as normal based off the alignment rules for their dtype

  // I will need the next and previous dtypes. I will need the previous offset

  std::vector<int> newRow; // Stores the offsets as they are calculated
  std::vector<std::string> expandedRow = expandedMap.at(structName); // The row of datatypes that we are calculating the offsets for
  std::string nextItem;
  std::string prevItem;
  size_t thisOffset = 0; // offset position of the next item
  size_t dtypeSize = 0; // size of the basic dtype
  size_t paddingSize = 0; // size of padding
  size_t alignment = 0; // required alignment for thisOffset, can be 1,2,4,8
  size_t thisAlignment = 0; // temporarily stores the alignment returned from other functions which search for alignment
  bool insideArray = false;
  size_t arrayBools = 0;
  int i = 0;

  for (std::string dtype : expandedRow){
    // If this item is end:structName, start:structName, end:array or start:array, then no offset is required so we skip to the next item
    if (dtype == "start:array"){
      insideArray=true;
      i++;
      continue;
    }
    else if (dtype == "end:array"){
      insideArray=false;
      i++;
      continue;
    }
    else if (dtype.substr(0,4) == "end:" || dtype.substr(0,6) == "start:"){
      i++;
      continue;
    }

    // Get next raw dtype
    if (i+1 < expandedRow.size()){
      nextItem = expandedRow[i+1];
    }
    else {nextItem="none";}

    // Calculate size of the padding
    thisOffset += dtypeSize;
    if (thisOffset != 0){
      if (thisOffset % alignment != 0){
        paddingSize = alignment-(thisOffset % alignment);
      }
      else {paddingSize=0;}
    }
    else {paddingSize=0;}
    thisOffset += paddingSize;
    newRow.push_back(thisOffset); // This is the important bit which actually adds the start offset of the dtype currently being processed
    alignment = 0; // Reset alignment ready for next item

    // Calculate size of this dtype
    if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL" || 
              dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD" || 
                dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD" || dtype =="TIME" || dtype =="DATE_AND_TIME")
    {
      if (dtype == "BOOL" && insideArray) {
        // When a bool is inside an array instead of taking up 2 bytes, they actually take up 1 bit and are stored together inside bytes
        // If the number of bools inside the same array is divisible by 8, then we move to the next byte
        // We calculate the bit offset elsewhere
        size_t remainder = arrayBools % 8;
        if (remainder == 0) {
          dtypeSize = 1;
        }
        else {
          dtypeSize = 0;
        }
      }
      else if (dtype =="UINT" || dtype =="INT" || dtype =="WORD" || dtype =="BOOL"){
        dtypeSize=2;
      }
      else if (dtype =="UDINT" || dtype =="DINT" || dtype =="REAL" || dtype =="DWORD"){
        dtypeSize=4;
      }
      else if (dtype =="ULINT" || dtype =="LINT" || dtype == "LREAL" || dtype =="LWORD"){
        dtypeSize=8;
      }
    }
    else if (dtype.substr(0,7) == "STRING[")
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
      if (!intFound)
      {
        std::cout<<"STRING type must specify size, " << structName << " definition is invalid" <<std::endl;
        return -1;
      }
      dtypeSize = strLength;
    }
    else {
      std::cout<<"Failed to parse user input datatype: " << dtype << ". Definition for " << structName << " invalid." << std::endl;
      return -1;
    }

    // Calculate the alignment rule of the next dtype, from this we can calculate the amount of padding required
    // nextItem should only be one of the raw dtypes at this point

    if (nextItem =="BOOL" && insideArray){
      if (alignment < 1){alignment=1;}
    }
    else if (nextItem =="UINT" || nextItem =="INT" || nextItem =="WORD" || nextItem =="BOOL"){
      if (alignment < 2){alignment=2;}
    }
    else if (nextItem =="UDINT" || nextItem =="DINT" || nextItem =="REAL" || nextItem =="DWORD"){
      if (alignment < 4){alignment=4;}
    }
    else if (nextItem =="ULINT" || nextItem =="LINT" || nextItem == "LREAL" || nextItem =="LWORD"){
      alignment=8;
    }
    else if (nextItem.substr(0,7) == "STRING["){
      if (alignment < 1){alignment=1;}
    }
    else if (nextItem == "none"){} // We need no additional padding
    else if (nextItem.substr(0,4) == "end:" || nextItem.substr(0,6) == "start:")
    {
      // If nextItem is an arrayStart or end, then set nextItem to the item after, this must be done recursively as that item could also be a struct
      // We keep searching until we find a raw dtype or the end of the map. If we find the end, then return none and no additional alignment is given
      // based on the next dtype. While searching we also update the alignment if we come across a struct start or end with a larger internal
      // dtype than the current alignment.
      thisAlignment = getEmbeddedAlignment(expandedMap, structName, nextItem, i);
      if (alignment < thisAlignment){alignment=thisAlignment;} // Alignment should be 0 at this point, but we check just in case
    }
    else {
      std::cout<<"Failed to calculate the alignment of: " << nextItem << ". Definition for " << structName << " invalid." << std::endl;
      return -1;
    }
    i++;
  }
  structMap[structName] = newRow;
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
  while (!this->startPollers_ && !omronExiting_) {epicsThreadSleep(0.1);}
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
      if (x.second->pollerName == threadName && x.second->readFlag == true)
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
            int bytes = 2;
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing WORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            char hexString[bytes*2+1]{};
            for (int i = 0; i < bytes; i++)
            {
              sprintf(hexString+strlen(hexString), "%02X", rawData[n]);
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "DWORD")
          {
            int bytes = 4;
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing DWORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            char hexString[bytes*2+1]{};
            for (int i = 0; i < bytes; i++)
            {
              sprintf(hexString+strlen(hexString), "%02X", rawData[n]);
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
            free(rawData);
            free(pData);
          }
          else if (x.second->dataType == "LWORD")
          {
            int bytes = 8;
            uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)bytes);
            status = plc_tag_get_raw_bytes(x.second->tagIndex, offset, rawData, bytes);
            if (status !=0){
              asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing LWORD data: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              continue;
            }
            epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
            /* We flip around the hex numbers to match what is done in the PLC */
            int n = bytes-1;
            char hexString[bytes*2+1]{};
            for (int i = 0; i < bytes; i++)
            {
              sprintf(hexString+strlen(hexString), "%02X", rawData[n]);
              pData[i] = rawData[n];
              n--;
            }
            status = doCallbacksInt8Array(pData, bytes, x.first, 0);
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
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
