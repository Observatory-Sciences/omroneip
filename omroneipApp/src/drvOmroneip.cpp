#include "drvOmroneip.h"

bool iocStarted = false; // Set to 1 after IocInit() which starts pollers

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
      omronExiting_(false),
      initialized_(false),
      startPollers_(false)

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
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Driver initialisation failed.\n", driverName, functionName);
  }
  utilities = new omronUtilities(this); //smart pointerify this
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
  drvInfoMap keyWords = this->utilities->drvInfoParser(drvInfo);
  if (keyWords.at("stringValid") != "true")
  {
    readFlag = false;
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
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Duplicate tag exists, reusing tag %d for this parameter.\n", driverName, functionName, previousTag.second->tagIndex);
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
  this->lock(); //lock to ensure that the pollers do not attempt polling while tags are being created and destroyed
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
          drvUser->tagIndex = masterStruct.second;
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

asynStatus drvOmronEIP::loadStructFile(const char * portName, const char * filePath)
{
  const char * functionName = "loadStructFile";
  std::ifstream infile(filePath); 
  structDtypeMap structMap;
  std::vector<std::string> row; 
  std::string line, word; 
  asynStatus status = asynSuccess;

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
    status = this->utilities->createStructMap(structMap);
    return status;
}

void drvOmronEIP::readPoller()
{
  static const char *functionName = "readPoller";
  std::string threadName = epicsThreadGetNameSelf();
  std::string tag;
  int offset;
  int status;
  bool still_pending = 1;
  int timeTaken = 0;
  auto timeoutStartTime = std::chrono::system_clock::now();
  double timeoutTimeTaken = 0; //time that we have been waiting for the current read request to be answered
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

    bool readFailed = false;
    timeoutStartTime = std::chrono::system_clock::now();
    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName)
      {
        offset = x.second->tagOffset;
        still_pending = 1;
        while (still_pending)
        {
          status = plc_tag_status(x.second->tagIndex);
          // It should be rare that data for the first tag has not arrived before the last tag is read
          // Therefore this if statement will normally be skipped, or called once by the first few tags as we 
          // are asynchronously waiting for all tags in this poller to be read. 
          if (status == PLCTAG_STATUS_PENDING)
          {
            // Wait for the timeout specified in the records INP/OUT field
            // To be precise, this is the time between when the last read request for this poll was sent and the current time,
            // this means that the first read requests will have slightly longer than their timeout period for their data to return.
            timeoutTimeTaken = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - timeoutStartTime).count())*0.001; //seconds
            if (timeoutTimeTaken>=x.second->timeout)
            {
              setParamStatus(x.first, asynTimeout);
              readFailed = true;
              asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Timeout finishing read tag %d: %s. Decrease the polling rate or increase the timeout.\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
              still_pending = 0;
            }
          }
          else if (status < 0)
          {
            setParamStatus(x.first, asynError);
            readFailed = true;
            asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Error finishing read tag %d: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
            still_pending=0;
          }
          else {
            still_pending=0;
          }
        }

        if (!readFailed)
        {
          // libplctag has thread protection for single API calls. However there is potential that while we are reading a tag on this poller,
          // from the plc, we can be simultaneously reading data from the tag in libplctag. This could lead to the data being read, being 
          // overwritten as it is read, therefor we must lock the tag while reading it.
          status = plc_tag_lock(x.second->tagIndex);
          if (status !=0){
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while trying to lock tag: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
            continue;
          }
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
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
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
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
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
            asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
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
            if (pasynTrace->getTraceMask(pasynUserSelf) & ASYN_TRACEIO_DRIVER){
              char hexString[bytes*2+1]{};
              for (int i = 0; i < bytes; i++)
              {
                sprintf(hexString+strlen(hexString), "%02X", rawData[i]);
              }
              asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type: %s\n", driverName, functionName, x.first, x.second->tagIndex, hexString, x.second->dataType.c_str());
            }
            
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
          status = plc_tag_unlock(x.second->tagIndex);
          if (status !=0){
            asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while trying to unlock tag: %s\n", driverName, functionName, x.second->tagIndex, plc_tag_decode_error(status));
            continue;
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
  int status = 0;
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
  int status = 0;
  delete utilities;
  for (auto mi : pollerList_)
  {
    delete mi.second;
  }

  for (auto mi: tagMap_)
  {
    plc_tag_destroy(mi.second->tagIndex);
  }

  for (auto mi : tagMap_)
  {
    while (status != PLCTAG_STATUS_OK) 
      status += plc_tag_status(mi.second->tagIndex);
  }
  plc_tag_shutdown();
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
    else if (iocStarted){
      std::cout<<"Structure definition file must be loaded before database files."<<std::endl;
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
