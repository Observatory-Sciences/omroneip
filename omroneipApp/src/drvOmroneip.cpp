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
  epicsThreadSleep(3);
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
                          int debugLevel,
                          double timezoneOffset)

    : asynPortDriver(portName,
                     1,                                                                                                                                                  /* maxAddr */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynOctetMask | asynDrvUserMask |
                     asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask | 
                     asynFloat64ArrayMask, /* Interface mask */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynOctetMask | 
                     asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask | 
                     asynFloat64ArrayMask, /* Interrupt mask */
                     ASYN_CANBLOCK | ASYN_MULTIDEVICE,                                                                                                                   /* asynFlags */
                     1,                                                                                                                                                  /* Autoconnect */
                     0,                                                                                                                                                  /* Default priority */
                     0),                                                                                                                                                 /* Default stack size*/
      omronExiting_(false),
      initialized_(false),
      startPollers_(false),
      timezoneOffset_(timezoneOffset)

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
  int thisAsynStatus = asynSuccess;
  int libplctagStatus = 0;
  bool readFlag = true;

  if (initialized_ == false)
  {
    pasynManager->enable(pasynUser, 0);
    return asynDisabled;
  }

  thisAsynStatus = getAddress(pasynUser, &addr);
  if (thisAsynStatus == asynSuccess) 
  {
    thisAsynStatus = findParam(addr, drvInfo, &asynIndex);
  }
  else 
  {
    return asynError;
  }

  // Only called if an asyn parameter has not already been created for drvInfo, otherwise we use the existing parameter
  if (thisAsynStatus != asynSuccess)
  {
    // Get the required data from the drvInfo string
    drvInfoMap keyWords = this->utilities->drvInfoParser(drvInfo);
    if (keyWords.at("stringValid") != "true")
    {
      readFlag = false;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, drvInfo string is invalid, record with drvInfo: '%s' was not created!\n", driverName, functionName, drvInfo);
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
        "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");
    }
    else if (keyWords.at("optimise") == "1")
    {
      //We dont make a tag here for optimise tags, instead their tags are created in the optimiseTags function
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
        "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");
      tagIndex = 0;
    }
    else 
    {
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
            "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");

      //check if a duplicate tag has already been created, must be using the same poller
      for (auto previousTag: tagMap_)
      {
        if (tag == previousTag.second->tag && keyWords.at("pollerName") == previousTag.second->pollerName)
        {
          /* Potential extension here to allow tags with different pollers to be combined, but must check the polling duration so that the shortest polling duration is used as the master tag */
          asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warning, duplicate tag exists, reusing tag %d for this parameter.\n", driverName, functionName, previousTag.second->tagIndex);
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
      libplctagStatus = plc_tag_status(tagIndex);
      if (libplctagStatus != PLCTAG_STATUS_OK){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, tag creation failed! Reported: %s. Asyn parameter is not valid. drvInfo: '%s', tag string: '%s'\n", 
                    driverName, functionName, plc_tag_decode_error(libplctagStatus), drvInfo, tag.c_str());
      }
    }

    /* Initialise the drvUser datatype which will store everything need to access data for a tag */
    /* Some of these values may be updated during optimisations and some may become outdated */
    omronDrvUser_t *newDrvUser = (omronDrvUser_t *)callocMustSucceed(1, sizeof(omronDrvUser_t), functionName);
    if (libplctagStatus == PLCTAG_STATUS_OK && keyWords.at("stringValid") == "true"){
      for (auto type: omronDataTypeList)
        if (type.first == keyWords.at("dataType"))
          newDrvUser->dataType = type;
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
      newDrvUser->offsetReadSize = std::stoi(keyWords.at("offsetReadSize"));
      newDrvUser->readAsString = std::stoi(keyWords.at("readAsString"));
      newDrvUser->optimise = std::stoi(keyWords.at("optimise"));
    }
    { /* Create the asyn param with the interface that matches the datatype */
      if (keyWords.at("dataType") == "BOOL"){
        thisAsynStatus = createParam(drvInfo, asynParamUInt32Digital, &asynIndex);
      }
      else if (keyWords.at("dataType") == "SINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "INT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "DINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "LINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
      }
      else if (keyWords.at("dataType") == "USINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "UINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "UDINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
      }
      else if (keyWords.at("dataType") == "ULINT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
      }
      else if (keyWords.at("dataType") == "REAL"){
        thisAsynStatus = createParam(drvInfo, asynParamFloat64, &asynIndex);
      }
      else if (keyWords.at("dataType") == "LREAL"){
        thisAsynStatus = createParam(drvInfo, asynParamFloat64, &asynIndex);
      }
      else if (keyWords.at("dataType") == "STRING"){
        thisAsynStatus = createParam(drvInfo, asynParamOctet, &asynIndex);
      }
      else if (keyWords.at("dataType") == "WORD"){
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
      }
      else if (keyWords.at("dataType") == "DWORD"){
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
      }
      else if (keyWords.at("dataType") == "LWORD"){
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
      }
      else if (keyWords.at("dataType") == "UDT"){
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
      }
      else if (keyWords.at("dataType") == "TIME"){
        if (newDrvUser->readAsString)
          thisAsynStatus = createParam(drvInfo, asynParamOctet, &asynIndex);
        else
          thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
      }
      else{
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid datatype: %s\n", driverName, functionName, keyWords.at("dataType").c_str());
      }
    }

    if (thisAsynStatus == asynSuccess)
    {
      if (libplctagStatus == PLCTAG_STATUS_OK && newDrvUser->optimise)
      {
        tagMap_[asynIndex] = newDrvUser;
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Created asyn index: %d for tag string: %s.\n", driverName, functionName, asynIndex, tag.c_str());
      }
      else if (libplctagStatus == PLCTAG_STATUS_OK)
      { 
        // Add successfull tags to the tagMap
        tagMap_[asynIndex] = newDrvUser;
        readData(newDrvUser, asynIndex); //do initial read of read and write tags
        callParamCallbacks();
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Created libplctag tag with tag index: %d, asyn index: %d and tag string: %s\n", driverName, functionName, tagIndex, asynIndex, tag.c_str());
      }
      // For unsuccessfull tags, set record to error, they will never connect to the PLC or be useful, but will be valid asynParameters so are not disabled
      else if (libplctagStatus == PLCTAG_STATUS_PENDING) 
      {
        setParamStatus(asynIndex, asynTimeout);
      }
      else
      {
        setParamStatus(asynIndex, asynError);
      }
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Creation of asyn parameter failed for drvInfo: %s Asyn status:%d\n", driverName, functionName, drvInfo, thisAsynStatus);
      setParamStatus(asynIndex, asynError);
    }
  }

  pasynUser->reason = asynIndex;
  // Create the link from the record to the param
  thisAsynStatus = asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
  if (thisAsynStatus!=asynSuccess || libplctagStatus!=PLCTAG_STATUS_OK){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Creation of asyn parameter failed for drvInfo: %s Asyn status:%d\n", driverName, functionName, drvInfo, thisAsynStatus);
    return asynSuccess; // Yes this is stupid but if i return asynError or asynDisabled, asyn seg faults when setting up the waveform interface
  }
  return asynSuccess;
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
      // no optimisation is attempted if the user has not specified &optimise=
      if (!thisTag.second->optimise)
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
      if (thisTag.second->dataType.first == "UDT"){
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
        thisTag.second->readFlag = false;
        thisTag.second->tagOffset=0; //optimisation failed, so point to start of dtype
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Error, attempt to create optimised tag for asyn index: %d failed.\n", driverName, functionName, thisTag.first);
      }
    }
  }
  int optimiseCounter = 0;
  for (auto tag: tagMap_)
  {
    if (tag.second->optimise){optimiseCounter++;}
  }
  if (optimiseCounter==0){ //No tags to optimise so start immediately
    this->unlock();
    this->startPollers_ = true;
    return asynSuccess;
  }


  size_t countNeeded = 2; // The number of tags which need to reference a struct before we decide it is more efficient to fetch the entire struct
  // For each struct which is being referenced at least countNeeded times and which is not already in structIDList, create tag and add to list
  for (auto commonStruct : commonStructMap)
  {
    if (commonStruct.second.size()>= countNeeded)
    {
      if (structIDList.find(commonStruct.first) == structIDList.end()){
        // We must create a new libplctag tag and then add it to structIDList if valid
        // Uses UDT string attributes
        std::string tag = this->tagConnectionString_ +
                        "&name=" + commonStruct.first +
                        "&elem_count=1&allow_packing=1&str_is_counted=0&str_count_word_bytes=0&str_is_zero_terminated=1";

        int tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);

        if (tagIndex < 0) {
          const char* error = plc_tag_decode_error(tagIndex);
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Error, attempt to create optimised tag for asyn index: %d failed. %s\n", driverName, functionName, commonStruct.second[0], error);
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
      // Case where a child is found, but it is the only one, therefor no optimisation is possible and we print an error
      tagMap_.find(commonStruct.second[0])->second->optimisationFlag = "no optimisation possible";
      tagMap_.find(commonStruct.second[0])->second->readFlag = false;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, attempt to optimise asyn index: %d failed. Parameter will not be used.\n", driverName, functionName, commonStruct.second[0]);
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
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, attempt to optimise asyn index: %d failed. Parameter will not be used.\n", driverName, functionName, asynIndex);
        drvUser->optimisationFlag = "optimisation failed"; // Could not find an optimisation so this tag will be read/written directly
        drvUser->tagOffset=0; //optimisation failed, so point to start of dtype
        drvUser->readFlag = false;
        break;
      }

      for (auto masterStruct : structIDList)
      {
        if (commonStruct.first == masterStruct.first)
        {
          // we have found a tag that already exists which gets this struct, so we piggy back off this
          // now we get the drvUser for the asyn parameter which is to be optimised and set its tagIndex to the tagIndex we piggy back off
          int oldTag = drvUser->tagIndex;
          matchFound = true;
          destroyList.push_back(oldTag);
        }
      }

      for (auto masterStruct : structIDList)
      {
        if (commonStruct.first == masterStruct.first)
        {
          // we must update the tagIndex of this drvUser and also any other drvUser which uses its old tagIndex, as this will be deleted
          for (auto tag: tagMap_){
            if (tag.second->tagIndex == drvUser->tagIndex){
              if (tag.second->optimisationFlag != "master")
              {
                tag.second->readFlag = false;
                tag.second->optimisationFlag = "optimised";
              }
              else
                tag.second->readFlag = true;
              tag.second->tagIndex = masterStruct.second;
              asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn index: %d Optimised to use libplctag tag with libplctag index: %d\n", driverName, functionName, tag.first, masterStruct.second);
            }
          }
        }
      }

      if (!matchFound && commonStruct.second.size() >= countNeeded)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Error, attempt to optimise asyn index: %d failed. The parameter will not be used.\n", driverName, functionName, asynIndex);
        drvUser->optimisationFlag = "optimisation failed";
        drvUser->readFlag = "false";
      }
    }
  }

  std::sort(destroyList.begin(), destroyList.end());
  std::vector<int>::iterator it = std::unique(destroyList.begin(), destroyList.end());
  destroyList.erase(it, destroyList.end());
  for (auto index: destroyList) {
    if (index!=0)
    {
      status = plc_tag_destroy(index); // Destroy old tags
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Destroyed redundant libplctag: %d\n", driverName, functionName, index);
      if (status!=0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Error, libplctag tag destruction process failed for libplctag index %d\n", driverName, functionName, index);
      }
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

void drvOmronEIP::readData(omronDrvUser_t* drvUser, int asynIndex)
{
  const char * functionName = "extractFetchedData";
  int status;
  std::string datatype = drvUser->dataType.first;
  int offset = drvUser->tagOffset;
  int sliceSize = drvUser->sliceSize;
  int still_pending = 1;
  bool readFailed = false;
  auto timeoutStartTime = std::chrono::system_clock::now();
  double timeoutTimeTaken = 0; //time that we have been waiting for the current read request to be answered
  asynParamType myParam;
  getParamType(asynIndex, &myParam);
  while (still_pending)
  {
    status = plc_tag_status(drvUser->tagIndex);
    // It should be rare that data for the first tag has not arrived before the last tag is read
    // Therefore this if statement will normally be skipped, or called once by the first few tags as we 
    // are asynchronously waiting for all tags in this poller to be read. 
    if (status == PLCTAG_STATUS_PENDING)
    {
      // Wait for the timeout specified in the records INP/OUT field
      // To be precise, this is the time between when the last read request for this poll was sent and the current time,
      // this means that the first read requests will have slightly longer than their timeout period for their data to return.
      timeoutTimeTaken = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - timeoutStartTime).count())*0.001; //seconds
      if (timeoutTimeTaken>=drvUser->timeout)
      {
        setParamStatus(asynIndex, asynTimeout);
        readFailed = true;
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Timeout finishing read tag %d: %s. Decrease the polling rate or increase the timeout.\n", 
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        still_pending = 0;
      }
    }
    else if (status < 0)
    {
      setParamStatus(asynIndex, asynError);
      setParamAlarmStatus(asynIndex, asynError);
      setParamAlarmSeverity(asynIndex, MAJOR_ALARM);
      readFailed = true;
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Error finishing read tag %d: %s\n", 
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
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
    status = plc_tag_lock(drvUser->tagIndex);
    if (status !=0){
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while trying to lock tag: %s\n", 
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
      return;
    }
    if (datatype == "BOOL")
    {
      epicsUInt32 data;
      data = plc_tag_get_bit(drvUser->tagIndex, offset);
      status = setUIntDigitalParam(asynIndex, data, 0xFF, 0xFF);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %d My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, data, datatype.c_str());
    }
    else if (datatype == "SINT")
    {
      epicsInt8 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_int8(drvUser->tagIndex, (offset + i));
        dataString+=std::to_string(data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt8Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "INT")
    {
      epicsInt16 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_int16(drvUser->tagIndex, (offset + i*2));
        dataString+=std::to_string(data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt16Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "DINT")
    {
      epicsInt32 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_int32(drvUser->tagIndex, (offset + i*4));
        dataString+=std::to_string(data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt32Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "LINT")
    {
      //We do not natively support reading arrays of Int64, these must be read as UDTs
      epicsInt64 data;
      data = plc_tag_get_int64(drvUser->tagIndex, offset);
      status = setInteger64Param(asynIndex, data);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %lld My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, data, datatype.c_str());
    }
    else if (datatype == "USINT")
    {
      epicsUInt8 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_uint8(drvUser->tagIndex, (offset + i));
        dataString+=std::to_string(data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt8Array((epicsInt8*)data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "UINT")
    {
      // We read a uint and then must save it as an int to use the asyn Int16Array. The waveform record allows it to be displayed as uint
      epicsUInt16 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_uint16(drvUser->tagIndex, (offset + i*2));
        dataString+=std::to_string((uint16_t)data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt16Array((epicsInt16*)data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "UDINT")
    {
      // We read a udint and then must save it as a dint to use the asyn Int32Array interface. 
      // The waveform record allows it to be displayed as udint
      epicsUInt32 data[sliceSize];
      std::string dataString;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_uint32(drvUser->tagIndex, (offset + i*4));
        dataString+=std::to_string((uint32_t)data[i])+' ';
      }
      if (sliceSize==1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt32Array((epicsInt32*)data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "ULINT")
    {
      epicsUInt64 data;
      data = plc_tag_get_uint64(drvUser->tagIndex, offset);
      status = setInteger64Param(asynIndex, data);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %llu My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, data, datatype.c_str());
    }
    else if (datatype == "REAL")
    {
      epicsFloat32 data[sliceSize];
      std::string dataString;
      std::stringstream ss;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_float32(drvUser->tagIndex, (offset + i*4));
        ss<<data[i] << ' ';
      }
      dataString = ss.str();
      if (sliceSize==1)
        status = setDoubleParam(asynIndex, data[0]);
      else
        status = doCallbacksFloat32Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "LREAL")
    {
      epicsFloat64 data[sliceSize];
      std::string dataString;
      std::stringstream ss;
      for (int i = 0; i<sliceSize; i++){
        data[i] = plc_tag_get_float64(drvUser->tagIndex, (offset + i*8));
        ss<<data[i] << ' ';
      }
      dataString = ss.str();
      if (sliceSize==1)
        status = setDoubleParam(asynIndex, data[0]);
      else
        status = doCallbacksFloat64Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "STRING")
    {
      int bufferSize = plc_tag_get_size(drvUser->tagIndex);
      int string_length;
      int string_capacity;

      if (drvUser->strCapacity != 0){
        // If we are getting a string from a UDT, we must overwrite the string capacity
        string_capacity = drvUser->strCapacity;
        if (drvUser->optimise){string_length = drvUser->strCapacity;}
        else {string_length = drvUser->strCapacity - drvUser->tagOffset;}
      }
      else {
        string_capacity = plc_tag_get_string_capacity(drvUser->tagIndex, 0);
        string_length = plc_tag_get_string_length(drvUser->tagIndex, 0)+1;
      }

      if ((bufferSize<= string_capacity) && !drvUser->optimise){
        plc_tag_set_size(drvUser->tagIndex, string_capacity+1);
        bufferSize = string_capacity+1;
      }

      if ((offset >= bufferSize-1) && !drvUser->optimise)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, attempting to read at an offset beyond tag buffer!\n",
                    driverName, functionName);
        return;
      }
      else if (string_length>string_capacity+1)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error, offset does not point to valid string! Did you set the string size? Is offsetReadSize > str_max_capacity? My asyn parameter ID: %d My tagIndex: %d\n", 
                    driverName, functionName, asynIndex, drvUser->tagIndex);
        return;
      }
      char *pData = (char *)malloc((size_t)(unsigned int)(string_length));
      if (drvUser->optimise){
        status = plc_tag_get_string(drvUser->tagIndex, offset, pData, string_length);
      }
      else {
        status = plc_tag_get_string(drvUser->tagIndex, 0, pData, string_length);
      }
      if (status !=0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing STRING data: %s\n", 
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      
      if (drvUser->optimise){
        //for optimise case, we already accounted for the offset when getting the data from libplctag
        status = setStringParam(asynIndex, pData);
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", 
            driverName, functionName, asynIndex, drvUser->tagIndex, pData, datatype.c_str());
      }
      else {
        std::string correctedString(pData);
        if (drvUser->offsetReadSize!=0 && correctedString.size()>=drvUser->offsetReadSize+offset)
        {
          //Try and get the substring requested by the user
          //If the string size is less than the readSize + offset, just print what string we do have, this may happen due to Omron 
          //only sending the part of the string which has been written to, this may be less than the offset requested to read at.
          correctedString = correctedString.substr(offset, drvUser->offsetReadSize);
        }
        else if (correctedString.size()>=(size_t)offset)
            correctedString = correctedString.substr(offset);
        else 
          asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warning, the string length < offset+offset_read_size. Printing the entire string instead. Are the str attributes correct?\n", 
                    driverName, functionName);

        status = setStringParam(asynIndex, correctedString.c_str());
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n", 
                    driverName, functionName, asynIndex, drvUser->tagIndex, correctedString.c_str(), datatype.c_str());
      }
      free(pData);
    }

    else if (datatype == "WORD")
    {
      int bytes = 2;
      int size = bytes*sliceSize;
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status !=0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing WORD data: %s\n",
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size*2+1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes-1;
        for (int j = 0; j < bytes; j++){
          sprintf(hexString+strlen(hexString), "%02X", rawData[n+i*bytes]);
          pData[j+i*bytes] = rawData[n+i*bytes];
          n--;
        }
      }
      status = doCallbacksInt8Array(pData, size, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, hexString, datatype.c_str());
      free(rawData);
      free(pData);
    }
    else if (datatype == "DWORD")
    {
      int bytes = 4;
      int size = bytes*sliceSize;
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status !=0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing DWORD data: %s\n",
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size*2+1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes-1;
        for (int j = 0; j < bytes; j++){
          sprintf(hexString+strlen(hexString), "%02X", rawData[n+i*bytes]);
          pData[j+i*bytes] = rawData[n+i*bytes];
          n--;
        }
      }
      status = doCallbacksInt8Array(pData, size, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, hexString, datatype.c_str());
      free(rawData);
      free(pData);
    }
    else if (datatype == "LWORD")
    {
      int bytes = 8;
      int size = bytes*sliceSize;
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status !=0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing LWORD data: %s\n",
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size*2+1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes-1;
        for (int j = 0; j < bytes; j++){
          sprintf(hexString+strlen(hexString), "%02X", rawData[n+i*bytes]);
          pData[j+i*bytes] = rawData[n+i*bytes];
          n--;
        }
      }
      status = doCallbacksInt8Array(pData, size, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, hexString, datatype.c_str());
      free(rawData);
      free(pData);
    }
    else if (datatype == "UDT")
    {
      int tagSize = plc_tag_get_size(drvUser->tagIndex);
      int bytes = 0;
      if (drvUser->offsetReadSize != 0){
        bytes = drvUser->offsetReadSize; //user may request a byte size rather than reading the entire UDT
      }
      if (bytes+offset <= plc_tag_get_size(drvUser->tagIndex)){
        bytes = tagSize-(offset); //read all data after offset
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n", 
                    driverName, functionName, drvUser->tagIndex);
      }
      else {
        bytes = 0;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n", 
                    driverName, functionName, drvUser->tagIndex);
      }
      uint8_t *rawData = (uint8_t *)malloc(bytes * sizeof(uint8_t));
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, bytes);
      if (status !=0){
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while accessing UDT data: %s\n", 
                    driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
      memcpy(pData, rawData, bytes);
      status = doCallbacksInt8Array(pData, bytes, asynIndex, 0);
      if (pasynTrace->getTraceMask(pasynUserSelf) & ASYN_TRACEIO_DRIVER){
        char hexString[bytes*2+1]{};
        for (int i = 0; i < bytes; i++)
        {
          sprintf(hexString+strlen(hexString), "%02X", rawData[i]);
        }
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: 0x%s My type: %s\n", 
                    driverName, functionName, asynIndex, drvUser->tagIndex, hexString, datatype.c_str());
      }
      
      free(rawData);
      free(pData);
    }
    else if (datatype == "TIME")
    {
      epicsInt64 data;
      data = plc_tag_get_int64(drvUser->tagIndex, offset);
      if (myParam == asynParamOctet) {
        // first we modify the incoming time by the timezone offset defined at driver creation
        // then we convert from this timezone to the local timezone and output as a formatted string
        data += timezoneOffset_*-3.6e12; //offset in hours * number of nanoseconds in an hour
        char buff[128];
        std::chrono::duration<int64_t, std::nano> dur(data);
        auto tp = std::chrono::system_clock::time_point(std::chrono::duration_cast<std::chrono::system_clock::duration>(dur));
        std::time_t in_time_t = std::chrono::system_clock::to_time_t(tp);
        strftime(buff, 128, "%Y-%m-%d %H:%M:%S", localtime(&in_time_t));
        std::string resDate(buff);
    
        status = setStringParam(asynIndex, resDate.c_str());
        
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My raw data: %lld My converted data: %s My type %s\n", 
                    driverName, functionName, asynIndex, drvUser->tagIndex, data, resDate.c_str(), datatype.c_str());
      }
      else {
        status = setInteger64Param(asynIndex, data);
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %lld My type %s\n", 
                    driverName, functionName, asynIndex, drvUser->tagIndex, data, datatype.c_str());
      }
    }
    setParamStatus(asynIndex, (asynStatus)status);
    if (status==asynError) {
      setParamStatus(asynIndex, asynError);
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error occured while updating asyn parameter with asyn ID: %d tagIndex: %d Datatype %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, datatype.c_str());
    }
    else if (status==asynTimeout) {
      setParamStatus(asynIndex, asynTimeout);
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Timeout occured while updating asyn parameter with asyn ID: %d tagIndex: %d Datatype %s\n", 
                  driverName, functionName, asynIndex, drvUser->tagIndex, datatype.c_str());
    }
    else if (status==asynSuccess){
      setParamStatus(asynIndex, asynSuccess);
    }
    status = plc_tag_unlock(drvUser->tagIndex);
    if (status !=0){
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Tag index: %d Error occured in libplctag while trying to unlock tag: %s\n", 
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
      return;
    }
  }
  return;
}

void drvOmronEIP::readPoller()
{
  static const char *functionName = "readPoller";
  std::string threadName = epicsThreadGetNameSelf();
  std::string tag;
  int status;
  int timeTaken = 0;
  //The poller may not be fully initialised until the startPollers_ flag is set to 1, do not attempt to get the pPoller yet
  while (!this->startPollers_ && !omronExiting_) {epicsThreadSleep(0.1);}
  omronEIPPoller* pPoller = pollerList_.at(threadName);
  double interval = pPoller->updateRate_;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Starting poller: %s with interval: %f\n", driverName, functionName, threadName.c_str(), interval);
  while (!omronExiting_)
  {
    double waitTime = interval - ((double)timeTaken / 1000000);
    auto startTime = std::chrono::system_clock::now();
    if (waitTime >=0) {
      epicsThreadSleep(waitTime);
    }
    else {
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Warning! Reads taking longer than requested! %f > %f\n", driverName, functionName, ((double)timeTaken / 1000), interval);
    }

    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName && x.second->readFlag == true)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Reading tag: %d with polling interval: %f seconds\n", driverName, functionName, x.second->tagIndex, interval);
        plc_tag_read(x.second->tagIndex, 0); // read from plc as fast as possible, we will check status and timeouts later
      }
    }

    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName)
      {
        readData(x.second, x.first);
      }
    }

    status = callParamCallbacks();
    if (status!=asynSuccess) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Error performing asyn callbacks on read poller: %s\n", driverName, functionName, threadName.c_str());
    }
    auto endTime = std::chrono::system_clock::now();
    timeTaken = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count() - waitTime*1000000;
    if (timeTaken>0)
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Poller: %s finished processing in: %d msec\n\n", driverName, functionName, threadName.c_str(), timeTaken/1000);
  }
}

asynStatus drvOmronEIP::writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
  const char * functionName = "writeInt8Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  size_t tagSize = plc_tag_get_size(tagIndex);
  double timeout = pasynUser->timeout*1000;
  if (nElements>tagSize)
  {
    // tagSize is calculated by the library based off the initial read of the tag, the user should not try and write more data than this
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more characters than can fit into the tag! nElements>tagSize:  %ld > %ld.\n",
               driverName, functionName, tagIndex, nElements, tagSize); 
    return asynError;
  }
  else if (nElements<sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize); 
  }
  if (datatype == "UDT")
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
    return asynSuccess;
  }
  else if (datatype == "WORD" || datatype == "DWORD" || datatype == "LWORD")
  {
    size_t bytes;
    if (datatype == "WORD") {bytes = 2;}
    else if (datatype == "DWORD") {bytes = 4;}
    else if (datatype == "LWORD") {bytes = 8;}

    if (nElements>sliceSize*bytes){
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more data than specified in sliceSize! nElements>sliceSize*bytes:  %ld > %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize*bytes); 
      return asynError;
    }
    else if (nElements<sliceSize*bytes){
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize*bytes:  %ld < %ld.\n",
                  driverName, functionName, tagIndex, nElements, sliceSize*bytes); 
    }

    uint8_t *pOutput = (uint8_t *)malloc(sliceSize * bytes * sizeof(uint8_t));
    int n;
    for (size_t i = 0; i < sliceSize; i++)
    {
      n = bytes-1;
      for (size_t j = 0; j < bytes; j++){
        if (nElements<=i)
          pOutput[j+i*bytes] = 0;
        else
          pOutput[j+i*bytes] = value[n+i*bytes];
        n--;
      }
    }
    status = plc_tag_set_raw_bytes(tagIndex, offset, pOutput ,sliceSize);
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
    return asynSuccess;
  }
  else if (datatype == "SINT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_int8(tagIndex,offset + i, 0); 
      else
        status = plc_tag_set_int8(tagIndex,offset + i, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    return asynSuccess;
  }
  else  if (datatype == "USINT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_uint8(tagIndex,offset + i, 0); 
      else
        status = plc_tag_set_uint8(tagIndex,offset + i, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
      return asynError;
    }
    return asynSuccess;
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }
}

asynStatus drvOmronEIP::writeInt16Array(asynUser *pasynUser, epicsInt16 *value, size_t nElements)
{
  const char * functionName = "writeInt16Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout*1000;
  if (nElements>sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
               driverName, functionName, tagIndex, nElements, sliceSize); 
    return asynError;
  }
  else if (nElements<sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize); 
  }

  if (datatype == "INT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_int16(tagIndex,offset + i*2, 0); 
      else
        status = plc_tag_set_int16(tagIndex,offset + i*2, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else if (datatype == "UINT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_uint16(tagIndex,offset + i*2, 0); 
      else
        status = plc_tag_set_uint16(tagIndex,offset + i*2, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements)
{
  const char * functionName = "writeInt32Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout*1000;
  if (nElements>sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
               driverName, functionName, tagIndex, nElements, sliceSize); 
    return asynError;
  }
  else if (nElements<sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize); 
  }

  if (datatype == "DINT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_int32(tagIndex,offset + i*4, 0); 
      else
        status = plc_tag_set_int32(tagIndex,offset + i*4, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else if (datatype == "UDINT")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_uint32(tagIndex,offset + i*4, 0); 
      else
        status = plc_tag_set_uint32(tagIndex,offset + i*4, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value, size_t nElements)
{
  const char * functionName = "writeFloat32Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout*1000;
  if (nElements>sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
               driverName, functionName, tagIndex, nElements, sliceSize); 
    return asynError;
  }
  else if (nElements<sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize); 
  }

  if (datatype == "REAL")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_float32(tagIndex,offset + i*4, 0); 
      else
        status = plc_tag_set_float32(tagIndex,offset + i*4, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements)
{
  const char * functionName = "writeFloat64Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout*1000;
  if (nElements>sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
               driverName, functionName, tagIndex, nElements, sliceSize); 
    return asynError;
  }
  else if (nElements<sliceSize){
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize); 
  }

  if (datatype == "LREAL")
  {
    for (size_t i = 0; i<sliceSize; i++){
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements<=i)
        status = plc_tag_set_float64(tagIndex,offset + i*8, 0); 
      else
        status = plc_tag_set_float64(tagIndex,offset + i*8, *(value+i)); 
      if (status < 0) {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
        return asynError;
      }
    }
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0) {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status)); 
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
  const char * functionName = "writeUInt32Digital";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout*1000;
  if (datatype == "BOOL")
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
    return asynSuccess;
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
  }
}

asynStatus drvOmronEIP::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  const char * functionName = "writeInt32";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout*1000;
  if (datatype == "SINT")
    status = plc_tag_set_int8(tagIndex, offset, (epicsInt8)value);
  else if (datatype == "INT")
    status = plc_tag_set_int16(tagIndex, offset, (epicsInt16)value);
  else if (datatype == "DINT")
    status = plc_tag_set_int32(tagIndex, offset, value);
  else if (datatype == "USINT")
    status = plc_tag_set_uint8(tagIndex, offset, (epicsUInt8)value);
  else if (datatype == "UINT")
    status = plc_tag_set_uint16(tagIndex, offset, (epicsUInt16)value);
  else if (datatype == "UDINT")
    status = plc_tag_set_uint32(tagIndex, offset, (epicsUInt32)value);
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
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
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout*1000;
  if (datatype == "LINT" || datatype == "TIME")
    status = plc_tag_set_int64(tagIndex, offset, value);
  else if (datatype == "ULINT")
    status = plc_tag_set_int64(tagIndex, offset, (epicsUInt64)value);
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
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
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout*1000;
  if (datatype == "REAL")
    status = plc_tag_set_float32(tagIndex, offset, (epicsFloat32)value);
  else if (datatype == "LREAL")
    status = plc_tag_set_float64(tagIndex, offset, value);
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
    return asynError;
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
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout*1000;

  /* This is a bit ghetto because Omron does strings a bit different to what libplctag expects*/
  if (datatype == "STRING")
  {
    int string_capacity = plc_tag_get_string_capacity(tagIndex, 0);
    char stringOut[nChars + 1] = {'\0'}; // allow space for null character
    snprintf(stringOut, sizeof(stringOut), value);

    /* Set the tag buffer to the max size of string in PLC. Required as the tag size is set based
    on the current size of the tag in the PLC, but we may write a bigger string than this. */
    /* First check if the user has set this in the extras parameter, if not then we set to the size of nChars and hope it fits*/
    if (drvUser->strCapacity == 0) {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s A write to a string tag is being attempted without defining the capacity of this string, this may not work! Tag index: %d\n", driverName, functionName, tagIndex);       
      if (nChars > (size_t)string_capacity) {string_capacity = nChars;}
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
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str()); 
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
                                  int debugLevel,
                                  double timezoneOffset)
  {
    new drvOmronEIP(portName,
                    gateway,
                    path,
                    plcType,
                    debugLevel,
                    timezoneOffset);

    return asynSuccess;
  }

  /* iocsh functions */

  static const iocshArg ConfigureArg0 = {"Port name", iocshArgString};
  static const iocshArg ConfigureArg1 = {"Gateway", iocshArgString};
  static const iocshArg ConfigureArg2 = {"Path", iocshArgString};
  static const iocshArg ConfigureArg3 = {"PLC type", iocshArgString};
  static const iocshArg ConfigureArg4 = {"Debug level", iocshArgInt};
  static const iocshArg ConfigureArg5 = {"Timezone offset", iocshArgDouble};

  static const iocshArg *const drvOmronEIPConfigureArgs[6] = {
      &ConfigureArg0,
      &ConfigureArg1,
      &ConfigureArg2,
      &ConfigureArg3,
      &ConfigureArg4,
      &ConfigureArg5};

  static const iocshFuncDef drvOmronEIPConfigureFuncDef = {"drvOmronEIPConfigure", 6, drvOmronEIPConfigureArgs};

  static void drvOmronEIPConfigureCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPConfigure(args[0].sval, args[1].sval, args[2].sval, args[3].sval, args[4].ival, args[5].dval);
  }

  static void drvOmronEIPRegister(void)
  {
    iocshRegister(&drvOmronEIPConfigureFuncDef, drvOmronEIPConfigureCallFunc);
    iocshRegister(&drvOmronEIPConfigPollerFuncDef, drvOmronEIPConfigPollerCallFunc);
    iocshRegister(&drvOmronEIPStructDefineFuncDef, drvOmronEIPStructDefineCallFunc);
  }

  epicsExportRegistrar(drvOmronEIPRegister);

} // extern "C"
