#include "drvOmroneip.h"

bool iocStarted = false; // Set to 1 after IocInit() which starts the optimiseTags() thread
bool omronExiting = false;

static void readPollerC(void *drvPvt)
{
  drvOmronEIP *pPvt = (drvOmronEIP *)drvPvt;
  pPvt->readPoller();
}

// this thread runs once after iocInit to optimise the tag map before setting startPollers_=1 to begin the polling threads
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

omronEIPPoller::omronEIPPoller(const char *portName, const char *pollerName, double updateRate, int spreadRequests) : belongsTo_(portName),
                                                                                                                      pollerName_(pollerName),
                                                                                                                      updateRate_(updateRate),
                                                                                                                      spreadRequests_(spreadRequests),
                                                                                                                      myTagCount_(0)
{
}

/* Gives the read pollers time to finish their processing loop and sets omronExiting to true to tell the pollers to destruct */
static void omronExitCallback(void *pPvt)
{
  omronExiting = true;
  epicsThreadSleep(3);
}

/* This function is called by the IOC load system after iocInit() or iocRun() have completed */
static void myInitHookFunction(initHookState state)
{
  switch (state)
  {
  case initHookAfterIocRunning:
    iocStarted = true;
    break;
  default:
    break;
  }
}

drvOmronEIP::drvOmronEIP(const char *portName,
                         const char *gateway,
                         const char *path,
                         const char *plcType,
                         int debugLevel,
                         double timezoneOffset)

    : asynPortDriver(portName,
                     1, /* maxAddr */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynOctetMask | asynDrvUserMask |
                         asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask |
                         asynFloat64ArrayMask, /* Interface mask */
                     asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynOctetMask |
                         asynInt8ArrayMask | asynInt16ArrayMask | asynInt32ArrayMask | asynInt64ArrayMask | asynFloat32ArrayMask |
                         asynFloat64ArrayMask,         /* Interrupt mask */
                     ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags */
                     1,                                /* Autoconnect */
                     0,                                /* Default priority */
                     0),                               /* Default stack size*/
      initialized_(false),
      startPollers_(false),
      timezoneOffset_(timezoneOffset)

{
  static const char *functionName = "drvOmronEIP";
  tagConnectionString_ = "protocol=ab-eip&gateway=" + (std::string)gateway + "&path=" + (std::string)path + "&plc=" + (std::string)plcType;
  std::cout << "Starting driver with connection string: " << tagConnectionString_ << std::endl;
  //Defaults to 1994
  if (strcmp(plcType,"plc5") == 0 || strcmp(plcType,"slc500") == 0 || strcmp(plcType,"logixpccc") == 0 || strcmp(plcType,"micrologix") == 0){
    MAX_CIP_MESSAGE_DATA_SIZE_ = 244;
  }
  else if (strcmp(plcType,"micrologix800") == 0 || strcmp(plcType,"compactlogix") == 0){
    MAX_CIP_MESSAGE_DATA_SIZE_ = 504;
  } 
  // Some of these max message sizes may be higher?

  epicsAtExit(omronExitCallback, this);
  plc_tag_set_debug_level(debugLevel);
  initHookRegister(myInitHookFunction);
  asynStatus status = (asynStatus)(epicsThreadCreate("optimiseTags",
                                                     epicsThreadPriorityMedium,
                                                     epicsThreadGetStackSize(epicsThreadStackMedium),
                                                     (EPICSTHREADFUNC)optimiseTagsC,
                                                     this) == NULL);
  if (status != 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Driver initialisation failed.\n", driverName, functionName);
  }
  utilities = new omronUtilities(this);
  initialized_ = true;
}

asynStatus drvOmronEIP::createPoller(const char *portName, const char *pollerName, double updateRate, int spreadRequests)
{
  int status;
  omronEIPPoller *pPoller = new omronEIPPoller(portName, pollerName, updateRate, spreadRequests);
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
  int asynIndex = -1;    // index of asyn parameter
  int32_t tagIndex = -1; // index of libplctag tag
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
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, drvInfo string is invalid, record with drvInfo: '%s' was not created!\n", driverName, functionName, drvInfo);
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
            "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");
    }
    else if (keyWords.at("optimise") == "1")
    {
      // We dont make a tag here for optimise tags, instead their tags are created in the optimiseTags function
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
            "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");
      tagIndex = 0;
    }
    else
    {
      tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
            "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");

      // check if a duplicate tag has already been created, must be using the same poller
      for (auto previousTag : tagMap_)
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
        libplctagTagCount +=1;
      }

      /* Check and report failure codes. An Asyn param will be created even on failure but the record will be given error status */
      libplctagStatus = plc_tag_status(tagIndex);
      if (libplctagStatus != PLCTAG_STATUS_OK)
      {
        readFlag = false;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, tag creation failed! Reported: %s. Asyn parameter is not valid. drvInfo: '%s', tag string: '%s'\n",
                  driverName, functionName, plc_tag_decode_error(libplctagStatus), drvInfo, tag.c_str());
      }
    }

    /* Initialise the drvUser datatype which will store everything need to access data for a tag */
    /* Some of these values may be updated during optimisations and some may become outdated */
    omronDrvUser_t *newDrvUser = (omronDrvUser_t *)callocMustSucceed(1, sizeof(omronDrvUser_t), functionName);
    if (libplctagStatus == PLCTAG_STATUS_OK && keyWords.at("stringValid") == "true")
    {
      /* Copy values from keyWords map into newDrvUser*/
      initialiseDrvUser(newDrvUser,keyWords,tagIndex,tag,readFlag,pasynUser);
    }

    { /* Create the asyn param with the interface that matches the datatype */
      if (keyWords.at("dataType") == "BOOL")
      {
        thisAsynStatus = createParam(drvInfo, asynParamUInt32Digital, &asynIndex);
        setUIntDigitalParam(0, asynIndex, 0, 0xFF);
      }
      else if (keyWords.at("dataType") == "SINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "INT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "DINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "LINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
        setInteger64Param(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "USINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "UINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "UDINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "ULINT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
        setInteger64Param(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "REAL")
      {
        thisAsynStatus = createParam(drvInfo, asynParamFloat64, &asynIndex);
        setDoubleParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "LREAL")
      {
        thisAsynStatus = createParam(drvInfo, asynParamFloat64, &asynIndex);
        setDoubleParam(asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "STRING")
      {
        thisAsynStatus = createParam(drvInfo, asynParamOctet, &asynIndex);
        setStringParam(asynIndex, "");
      }
      else if (keyWords.at("dataType") == "WORD")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
        doCallbacksInt8Array(0, 1, asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "DWORD")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
        doCallbacksInt8Array(0, 1, asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "LWORD")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
        doCallbacksInt8Array(0, 1, asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "UDT")
      {
        thisAsynStatus = createParam(drvInfo, asynParamInt8Array, &asynIndex);
        doCallbacksInt8Array(0, 1, asynIndex, 0);
      }
      else if (keyWords.at("dataType") == "TIME")
      {
        if (newDrvUser->readAsString)
        {
          thisAsynStatus = createParam(drvInfo, asynParamOctet, &asynIndex);
          setStringParam(asynIndex, "");
        }
        else
        {
          thisAsynStatus = createParam(drvInfo, asynParamInt64, &asynIndex);
          setInteger64Param(asynIndex, 0);
        }
      }
      else
      {
        // If we get here then there is an error, but we create an asyn parameter anyway and put it in an alarm state
        createParam(drvInfo, asynParamInt32, &asynIndex);
        setIntegerParam(asynIndex, 0);
        thisAsynStatus = asynError;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid datatype: %s\n", driverName, functionName, keyWords.at("dataType").c_str());
      }
    }
    asynParamCount +=1;
    if (thisAsynStatus == asynSuccess)
    {
      if (libplctagStatus == PLCTAG_STATUS_OK && newDrvUser->optimise)
      {
        tagMap_[asynIndex] = newDrvUser;
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Created asyn index: %d for drvInfo: %s.\n", driverName, functionName, asynIndex, drvInfo);
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Optimisations requested. A libplctag tag will only be made if this process is successfull. \n", driverName, functionName);
      }
      else if (libplctagStatus == PLCTAG_STATUS_OK)
      {
        // Add successfull tags to the tagMap
        tagMap_[asynIndex] = newDrvUser;
        readData(newDrvUser, asynIndex); // do initial read of read and write tags
        callParamCallbacks();
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Created libplctag tag with tag index: %d, asyn index: %d and tag string: %s\n", driverName, functionName, tagIndex, asynIndex, tag.c_str());
      }
      // For unsuccessfull tags, set record to error, they will never connect to the PLC or be useful, but will be valid asynParameters
      else if (libplctagStatus == PLCTAG_STATUS_PENDING)
      {
        setParamStatus(asynIndex, asynTimeout);
        setParamAlarmStatus(asynIndex, asynTimeout);
        setParamAlarmSeverity(asynIndex, INVALID_ALARM);
      }
      else
      {
        setParamStatus(asynIndex, asynError);
        setParamAlarmStatus(asynIndex, asynError);
        setParamAlarmSeverity(asynIndex, INVALID_ALARM);
      }
    }
    else
    {
      setParamStatus(asynIndex, asynError);
      setParamAlarmStatus(asynIndex, asynError);
      setParamAlarmSeverity(asynIndex, INVALID_ALARM);
    }
  }

  pasynUser->reason = asynIndex;
  // Create the link from the record to the param
  thisAsynStatus = asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);

  if (thisAsynStatus != asynSuccess || libplctagStatus != PLCTAG_STATUS_OK)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Creation of asyn parameter failed for drvInfo: %s \n", driverName, functionName, drvInfo);
    return asynSuccess; // Yes this is a bit stupid but if I return asynError or asynDisabled, asyn seg faults when setting up the waveform interface
  }
  return asynSuccess;
}

void drvOmronEIP::initialiseDrvUser(omronDrvUser_t *newDrvUser, const drvInfoMap keyWords, int tagIndex, std::string tag, bool readFlag, const asynUser *pasynUser)
{
  for (auto type : omronDataTypeList)
    if (type.first == keyWords.at("dataType")) {
      newDrvUser->dataType = type;
      break;
    }
  newDrvUser->tag = tag;
  newDrvUser->tagIndex = tagIndex;
  newDrvUser->pollerName = keyWords.at("pollerName");
  newDrvUser->sliceSize = std::stoi(keyWords.at("sliceSize")); // dtype checked elsewhere
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

asynStatus drvOmronEIP::findOptimisableTags(std::unordered_map<std::string, std::vector<int>> &commonStructMap)
{
  // Look at the name used for each tag, if the name references a structure (contains a "."), add the structure name to map
  // Each tag which references the structure adds its asyn index to the map under the same structure name key
  for (auto thisTag : tagMap_)
  {
    if (thisTag.second->pollerName != "none")
    { // Only interested in polled tags
      // no optimisation is attempted if the user has not specified &optimise=
      if (!thisTag.second->optimise)
      {
        // ensure that this flag is true
        thisTag.second->readFlag = true;
        continue;
      }
      std::string name = thisTag.second->tag;
      name = name.substr(name.find("name=") + 5);
      name = name.substr(0, name.find('&'));
    
      // Set the name to the original name minus the child/field part
      if (commonStructMap.find(name) == commonStructMap.end())
      {
        // If struct does not exist in the map, then we add it with this tags asyn index
        commonStructMap[name] = (std::vector<int>){thisTag.first};
      }
      else
      {
        commonStructMap.at(name).push_back(thisTag.first);
      }
    }
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::findArrayOptimisations(std::unordered_map<std::string, std::vector<int>> &commonStructMap, std::unordered_map<std::string, std::vector<int>> &commonArrayMap)
{
  const char *functionName = "findArrayOptimisations";
  asynStatus status = asynSuccess;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Looking for array slicing optimisations... \n", 
              driverName, functionName);
  for (auto commonStruct : commonStructMap)
  {
    std::string arrayName;
    std::string structName = commonStruct.first;
    size_t indexPos = structName.find("[");
    int arrayIndex = 0;
    int maxSlice = 0;
    if (indexPos != structName.npos)
    { 
      try
      {
        arrayName = structName.substr(0,indexPos);
        arrayIndex =  std::stoi(structName.substr(indexPos+1,structName.npos-indexPos-2));
        if (commonArrayMap.find(arrayName) == commonArrayMap.end())
        {
          //We may have found an array of structs, we attempt to read the element to get its size
          std::string tag = this->tagConnectionString_ +
                  "&name=" + structName +
                  "&elem_count=1&allow_packing=1&str_is_counted=0&str_count_word_bytes=0&str_is_zero_terminated=1";

          int tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);
          maxSlice = MAX_CIP_MESSAGE_DATA_SIZE_ / plc_tag_get_size(tagIndex);
          plc_tag_destroy(tagIndex); //clean up
          //If arrayName is not already in the map, we need to add it
          commonArrayMap[arrayName] = {maxSlice,arrayIndex};
        }
        else
        {
          commonArrayMap.at(arrayName).push_back(arrayIndex);
        }
      }
      catch(...)
      {
        status = asynError;
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid index found while optimising structure named: %s\n", 
                    driverName, functionName, structName.c_str());
      }
    }
  }

  // Sort indexes into ascending order
  for (auto &arrayItem : commonArrayMap){
    std::sort(arrayItem.second.begin()+1,arrayItem.second.end());
  }

  // Print debugging info
  if (!commonArrayMap.empty()){
    std::string flowString;
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Found the following array slicing optimisations: \n", 
                driverName, functionName);
    for (auto arrayItem : commonArrayMap){
      flowString = "Attempting to slice array: "+arrayItem.first+" into slices of size: "+std::to_string(arrayItem.second[0])+" for indexes: ";
      for (size_t i = 1; i<(arrayItem.second.size()+1); i++)
      {
        flowString += std::to_string(arrayItem.second[i]) + " ";
      }
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s %s \n", driverName, functionName, flowString.c_str());
    }
  }
  else {
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Found no possible array slicing optimisations. \n", 
                driverName, functionName);
  }
  return status;
}

asynStatus drvOmronEIP::createOptimisedArrayTags(std::unordered_map<std::string, int> &structIDMap,
                                                  std::unordered_map<std::string, std::vector<int>> const commonStructMap, 
                                                    std::unordered_map<std::string, std::vector<int>> &commonArrayMap, 
                                                        std::unordered_map<int, std::string> &structTagMap)
{
  const char *functionName = "createOptimisedArrayTags";
  asynStatus status = asynSuccess;
  int maxSlice;
  int sliceStart = 1; // The first index of the previous slice
  bool newSlice = true; // Indicates that we need to make a new tag for a new slice
  int tagIndex;
  int masterAsynIndex;
  std::string arrayName;
  std::string tag;
  size_t tagsCreated = 0;
  for (auto &arrayItem : commonArrayMap)
  {
    // The first element of the vector is the max number of elements which can fit in a single CIP message, we save and then erase this
    maxSlice = arrayItem.second[0];
    arrayItem.second.erase(arrayItem.second.begin());
    int i = 0;
    int nextIndex = 0;
    for (int index : arrayItem.second)
    {
      arrayName = arrayItem.first + "[" + std::to_string(index) + "]";
      if ((i+1)<=(int)arrayItem.second.size()){
        nextIndex = arrayItem.second[i+1];
      }
      
      if (index-sliceStart >= maxSlice){
        //Check to see if this index needs a new slice
        newSlice = true;
      }
      if (nextIndex-index>maxSlice) {
        //Check to make sure that there are two elements being accessed within this slice so that the optimisation is useful
        newSlice = false;
      }
      if (newSlice)
      {
        //Create tag which gets array slice starting from the first index
        newSlice = false;
        sliceStart = index;
        tag = this->tagConnectionString_ + "&name=" + arrayName +
                    "&elem_count="+std::to_string(maxSlice)+"&allow_packing=1&str_is_counted=0&str_count_word_bytes=0&str_is_zero_terminated=1";

        tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);
        // if tagIndex = -7 then we are trying to read a slice beyond the end of the array
        if (tagIndex>=1)
        {
          tagsCreated ++;
          // The master drvUser which reads the slice is the drvUser designated by the first asynIndex in the vector for the structName
          // which gets the first element from the array slice.
          masterAsynIndex = commonStructMap.at(arrayName)[0];
          tagMap_.find(masterAsynIndex)->second->optimisationFlag = "master";
          tagMap_.find(masterAsynIndex)->second->readFlag = true;
        }
      }
      if (tagIndex>=1){
        // If tagIndex is valid, then we update the details for any asynParameter which will use it
        structTagMap[tagIndex] = tag;
        structIDMap[arrayName] = tagIndex; //Now that arrayName has been added, it wont be readded later in createOptimisedTags()
        for (auto asynIndex : commonStructMap.at(arrayName))
        {
          // Original offset was offset within an element, as we have a slice of elements, we must add the offset to the element within
          // the slice
          int elementSize = (plc_tag_get_size(tagIndex)/8);
          int oldOffset = tagMap_.find(asynIndex)->second->tagOffset;
          int newOffset;
          if (tagMap_.find(asynIndex)->second->dataType.first == "BOOL"){
            newOffset = oldOffset + 8*(elementSize * (index-sliceStart));
          }
          else {
            newOffset = oldOffset + elementSize * (index-sliceStart);
          }
          tagMap_.find(asynIndex)->second->tagOffset = newOffset;
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Attempting to optimise asyn index: %d, a tag was created with ID: %d and tag string: %s and offset: %d\n", 
                      driverName, functionName, asynIndex, tagIndex, tag.c_str(), newOffset);
        }  
      }
      else
      {
        for (auto asynIndex : commonStructMap.at(arrayName))
        {
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Array optimisations failed for asyn index: %d, an individual element optimisations will be attempted instead.\n", 
                      driverName, functionName, asynIndex);
        }
      }
      i++;
    }
  }
  libplctagTagCount+=tagsCreated;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s %ld new optimised array slice tags created\n", driverName, functionName, tagsCreated);
  return status;
}

asynStatus drvOmronEIP::createOptimisedTags(std::unordered_map<std::string, int> &structIDMap, std::unordered_map<std::string, std::vector<int>> const commonStructMap, std::unordered_map<int, std::string> &structTagMap)
{
  const char *functionName = "createOptimisedTags";
  asynStatus status = asynSuccess;
  size_t countNeeded = 2; /* The number of tags which need to reference a struct for it to be worth optimising. If less than this 
                             value reference a struct name, an error message is printed. */
  size_t tagsCreated = 0;
  for (auto commonStruct : commonStructMap)
  {
    if (commonStruct.second.size() >= countNeeded)
    {
      if (structIDMap.find(commonStruct.first) == structIDMap.end())
      {
        // We must create a new libplctag tag and then add it to structIDMap if valid
        // Uses UDT string attributes
        std::string tag = this->tagConnectionString_ +
                          "&name=" + commonStruct.first +
                          "&elem_count=1&allow_packing=1&str_is_counted=0&str_count_word_bytes=0&str_is_zero_terminated=1";

        int tagIndex = plc_tag_create(tag.c_str(), CREATE_TAG_TIMEOUT);
        tagsCreated +=1;

        if (tagIndex < 0)
        {
          const char *error = plc_tag_decode_error(tagIndex);
          asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Err, attempt to create optimised tag for asyn index: %d failed. %s\n", driverName, functionName, commonStruct.second[0], error);
          return asynError;
        }
        else
        {
          structIDMap[commonStruct.first] = tagIndex;
          structTagMap[tagIndex] = tag;
          asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Attempting to optimise asyn index: %d, a tag was created with ID: %d and tag string: %s\n", driverName, functionName, commonStruct.second[0], tagIndex, tag.c_str());
        }

        // We must designate one of the asynIndexes in the vector as the "master" index which has its libplctag tag read
        // To decide which one, we look at which has the fastest polling interval and use that one
        double pollingInterval = __DBL_MAX__;
        std::string pollerName;
        omronEIPPoller* pPoller;
        int master = 0;
        for (size_t i=0;i<commonStruct.second.size();i++){
          pollerName = tagMap_.find(commonStruct.second[i])->second->pollerName;
          pPoller = pollerList_.find(pollerName)->second;
          if (pPoller->updateRate_<pollingInterval){
            pollingInterval = pPoller->updateRate_;
            master=i;
          }
        }
        tagMap_.find(commonStruct.second[master])->second->optimisationFlag = "master";
        tagMap_.find(commonStruct.second[master])->second->readFlag = true;
      }
    }
    else
    {
      // Case where there are not enough asyn parameters referencing a structure for optimisation to be worthwhile
      tagMap_.find(commonStruct.second[0])->second->optimisationFlag = "no optimisation possible";
      tagMap_.find(commonStruct.second[0])->second->readFlag = false;
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, attempt to optimise asyn index: %d failed. You need at least two records accessing data from the same tag to optimise. Parameter will not be used.\n",
                driverName, functionName, commonStruct.second[0]);
      status = asynError;
    }
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s %ld new optimised tags created\n", driverName, functionName, tagsCreated);
  libplctagTagCount+=tagsCreated;
  return status;
}

asynStatus drvOmronEIP::updateOptimisedParams(std::unordered_map<std::string, int> const structIDMap, std::unordered_map<std::string, std::vector<int>> const commonStructMap, std::unordered_map<int, std::string> const structTagMap)
{
  const char *functionName = "updateOptimisedParams";
  asynStatus status = asynSuccess;
  omronDrvUser_t *drvUser;
  for (auto commonStruct : commonStructMap)
  {
    // For each asyn parameter which references this struct name
    for (int asynIndex : commonStruct.second)
    {
      bool matchFound = false;
      if (this->tagMap_.find(asynIndex) != this->tagMap_.end())
      {
        drvUser = this->tagMap_.at(asynIndex);
      }
      else
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, attempt to optimise asyn index: %d failed. Parameter will not be used.\n", driverName, functionName, asynIndex);
        drvUser->optimisationFlag = "optimisation failed"; // Could not find an optimisation so this tag will be read/written directly
        drvUser->tagOffset = 0;                            // optimisation failed, so point to start of dtype
        drvUser->readFlag = false;
        status = asynError;
      }

      // look through the map which stores which libplctag that should be used to read the struct name
      for (auto masterStruct : structIDMap)
      {
        // we have found the correct libplctag for the asyn index
        if (commonStruct.first == masterStruct.first)
        {
          matchFound = true;
          for (auto tag : tagMap_)
          {
          // we must update the tagIndex of this drvUser (using its asyn Index) and also any other drvUser which uses the same (now redundant libplctag index)
          // we dont update if the tagIndexes already match as they are up to date, or if they have not been intialised yet (equal to 0) as this will
          // be done later in this loop
            if (tag.first == asynIndex || ((tag.second->tagIndex == drvUser->tagIndex) && (drvUser->tagIndex != 0) && (tag.second->tagIndex != masterStruct.second)))
            {
              if (tag.second->optimisationFlag != "master")
              {
                tag.second->readFlag = false;
                tag.second->optimisationFlag = "optimised";
              }
              else
                tag.second->readFlag = true;
              tag.second->tagIndex = masterStruct.second;
              tag.second->tag = structTagMap.at(masterStruct.second);
              asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn index: %d Optimised to use libplctag tag with libplctag index: %d\n", driverName, functionName, tag.first, masterStruct.second);
            }
          }
        }
      }

      if (!matchFound)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s  Err, attempt to optimise asyn index: %d failed. The parameter will not be used.\n", driverName, functionName, asynIndex);
        drvUser->optimisationFlag = "optimisation failed";
        drvUser->readFlag = "false";
        status =  asynError;
      }
    }
  }
  return status;
}

asynStatus drvOmronEIP::optimiseTags()
{
  const char *functionName = "drvOptimiseTags";
  asynStatus status = asynSuccess;
  std::unordered_map<std::string, int> structIDMap;                  // Contains a map of struct paths along with the id of the asyn index which gets that struct
  std::unordered_map<int, std::string> structTagMap;                 // Contains a map of new libplctag tag indexes paired with the tag string
  std::unordered_map<std::string, std::vector<int>> commonStructMap; // Contains the structName along with a vector of all the asyn Indexes which use this struct
  std::unordered_map<std::string, std::vector<int>> commonArrayMap;  // Contains arrayName:maxSlice,index1,index2...
  this->lock();                                                      // lock to ensure that the pollers do not attempt polling while tags are being created and destroyed
  int optimiseCounter = 0;
  for (auto tag : tagMap_)
  {
    if (tag.second->optimise)
    {
      optimiseCounter++;
    }
  }
  if (optimiseCounter != 0)
  { // We have tags to optimise
    // looks through the tags for those which have requested to use optimisations, adds these to lists for further processing
    status = findOptimisableTags(commonStructMap);

    // Looks for multiple asynParameters which are reading from the same array of structs
    if (status==asynSuccess)
      status = findArrayOptimisations(commonStructMap, commonArrayMap);


    // Attempts to create tags which read slices of arrays, updates drvUsers to match the new tag and offsets required
    if (status==asynSuccess)
      status = createOptimisedArrayTags(structIDMap, commonStructMap, commonArrayMap, structTagMap);

    // For each struct which is being referenced at least countNeeded times and which is not already in structIDMap, create a libplctag tag 
    // and add it to structIDMap
    if (status==asynSuccess)
      status = createOptimisedTags(structIDMap, commonStructMap, structTagMap);

    if (status==asynSuccess) {
      std::string flowString;
      for (auto const &i : commonStructMap)
      {
        flowString += "Optimising to read struct: " + i.first + " Into asyn indexes:";
        for (auto const &j : i.second)
          flowString += std::to_string(j) + " ";
        for (auto const &k : structIDMap)
        {
          if (k.first == i.first){
            flowString += "Using new libplctag with ID:" + std::to_string(k.second);
          }
        }
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
        flowString.clear();
      }
    }
    // Now that we have a map of structs and which index gets them as well as a map of structs and which indexes need to access them,
    // we can optimise
    if (status==asynSuccess)
      status = updateOptimisedParams(structIDMap, commonStructMap, structTagMap);

    if (status==asynSuccess){
      for (auto tag : tagMap_)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Asyn param: %d optimisationFlag: '%s' readFlag: %s\n", 
                    driverName, functionName, tag.first, tag.second->optimisationFlag.c_str(), tag.second->readFlag ? "true" : "false");
      }
    }
  }

  if (status != asynSuccess) 
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Err, Errors detected during optimisation! You should fix these.\n", driverName, functionName);
  else
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Driver initialisation complete. Created %ld asyn parameters and %ld libplctag tags.\n", driverName, functionName, this->asynParamCount, this->libplctagTagCount);
  
  this->unlock();
  this->startPollers_ = true;
  return status;
}
 
asynStatus drvOmronEIP::loadStructFile(const char *portName, const char *filePath)
{
  const char *functionName = "loadStructFile";
  std::ifstream infile(filePath);
  structDtypeMap structMap;
  std::vector<std::string> row;
  std::string line, word;
  asynStatus status = asynSuccess;

  if (infile.fail())
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Could not find file: %s. Either missing or invalid permissions.\n", driverName, functionName, filePath);
    return asynError;
  }

  if (!structMap_.empty())
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Only one structure definition file can be loaded at once!\n", driverName, functionName);
    return asynError;
  }

  while (std::getline(infile, line))
  {
    row.clear();
    std::istringstream s(line);
    while (std::getline(s, word, ','))
    {
      if (word[0] == '#')
        break;
      row.push_back(word);
    }
    if (row.size() == 1)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s structure: %s contains no valid members\n", driverName, functionName, row[0].c_str());
    }
    else if (!row.empty())
    {
      std::string structName = row.front();
      row.erase(row.begin());
      structMap[structName] = row;
    }
  }

  std::string flowString;
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Loading in struct file with the following definitions:\n", driverName, functionName);
  for (auto const &i : structMap)
  {
    flowString += i.first + ": ";
    for (auto const &j : i.second)
      flowString += j + " ";
    asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s\n", flowString.c_str());
    flowString.clear();
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "\n");
  status = this->utilities->createStructMap(structMap);
  return status;
}

void drvOmronEIP::readData(omronDrvUser_t *drvUser, int asynIndex)
{
  const char *functionName = "extractFetchedData";
  int status;
  std::string datatype = drvUser->dataType.first;
  int offset = drvUser->tagOffset;
  int sliceSize = drvUser->sliceSize;
  int still_pending = 1;
  bool readFailed = false;
  auto timeoutStartTime = std::chrono::system_clock::now();
  double timeoutTimeTaken = 0; // time that we have been waiting for the current read request to be answered
  asynParamType myParam;
  getParamType(asynIndex, &myParam);
  // libplctag has thread protection for single API calls. However thedfsdddre is potential that while we are reading a tag on this poller,
  // from the plc, we can be simultaneously reading data from the tag in libplctag. This could lead to the data being read, being
  // overwritten as it is read, therefor we must lock the tag while reading it.
  status = plc_tag_lock(drvUser->tagIndex);
  if (status != 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, while locking Tag index: %d libplctag reports: %s\n",
              driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
    return;
  }
  while (still_pending)
  {
    status = plc_tag_status(drvUser->tagIndex);
    // It should be rare that data for the first tag has not arrived before the last tag is read
    // Therefore this if statement will normally be skipped, or called once by the first few tags as we
    // are asynchronously waiting for all tags in this poller to be read.
    if (status == PLCTAG_STATUS_PENDING)
    {
      epicsThreadSleep(0.01);
      timeoutTimeTaken = (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - timeoutStartTime).count()) * 0.001; // seconds
      // We wait so that we dont spam libplctag with status requests which can cause 100ms freezes
      if (timeoutTimeTaken >= drvUser->timeout)
      {
        // If the timeout specified in the records INP/OUT field is hit, we set the status to asynTimeout
        // To be precise, this is the timeout to enter this loop is the time between the last read request for this poller being sent and the current time,
        // this means that the first read requests will have slightly longer than their timeout period for their data to return.
        setParamStatus(asynIndex, asynTimeout);
        setParamAlarmStatus(asynIndex, asynTimeout);
        setParamAlarmSeverity(asynIndex, MAJOR_ALARM);
        readFailed = true;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Timeout finishing read tag %d: %s. Decrease the polling rate or increase the timeout.\n",
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
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, finishing read of tag %d: %s\n",
                driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
      still_pending = 0;
    }
    else
    {
      still_pending = 0;
    }
  }

  if (!readFailed)
  {
    if (datatype == "BOOL")
    {
      epicsUInt32 data = 0;
      std::string printData;
      uint8_t* dataArray;
      if (sliceSize == 1){
        data = plc_tag_get_bit(drvUser->tagIndex, offset); // takes a bit offset
      }
      else if (drvUser->optimise && sliceSize <= 32)
      {
        // If optimising and slice size is not 1, we are getting bools from an embedded array where they are packed at the bit level (1byte=8bools)
        dataArray = (uint8_t*)calloc(4, sizeof(uint8_t));
        status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset/8, dataArray, ((sliceSize-1)/8)+1);
        //combine 4 uint8 into a epicsUInt32
        data = dataArray[0] | (dataArray[1] << 8) | (dataArray[2] << 16) | (dataArray[3] << 24);
        free(dataArray);
      }
      else if (sliceSize <= 32)
      {
        // We are getting bools from a regular bool array where they are packed at the byte level (1byte=1bool)
        // We read up to 32 bits from the bit array, passing a byte offset to the array and specifying the number of bytes to read
        for (int i =0;i<sliceSize;i++){
          uint8_t thisBool = plc_tag_get_bit(drvUser->tagIndex, offset+i*8);
          data = data | thisBool<<i;
        }
      }
      else{
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Asyn index: %d Max slice size for BOOL type is 32, recieved: %d\n",
                  driverName, functionName, asynIndex, sliceSize);
      }

      if (status != PLCTAG_STATUS_OK)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while reading BOOL array: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      status = setUIntDigitalParam(asynIndex, data, 0xFFFFFFFF, 0xFFFFFFFF);
      printData = std::bitset<32>(data).to_string();
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                driverName, functionName, asynIndex, drvUser->tagIndex, printData.c_str(), datatype.c_str());
    }
    else if (datatype == "SINT")
    {
      epicsInt8 data[sliceSize];
      std::string dataString;
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_int8(drvUser->tagIndex, (offset + i));
        dataString += std::to_string(data[i]) + ' ';
      }
      if (sliceSize == 1)
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
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_int16(drvUser->tagIndex, (offset + i * 2));
        dataString += std::to_string(data[i]) + ' ';
      }
      if (sliceSize == 1)
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
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_int32(drvUser->tagIndex, (offset + i * 4));
        dataString += std::to_string(data[i]) + ' ';
      }
      if (sliceSize == 1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt32Array(data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "LINT")
    {
      // We do not natively support reading arrays of Int64, these must be read as UDTs
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
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_uint8(drvUser->tagIndex, (offset + i));
        dataString += std::to_string(data[i]) + ' ';
      }
      if (sliceSize == 1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt8Array((epicsInt8 *)data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "UINT")
    {
      // We read a uint and then must save it as an int to use the asyn Int16Array. The waveform record allows it to be displayed as uint
      epicsUInt16 data[sliceSize];
      std::string dataString;
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_uint16(drvUser->tagIndex, (offset + i * 2));
        dataString += std::to_string((uint16_t)data[i]) + ' ';
      }
      if (sliceSize == 1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt16Array((epicsInt16 *)data, sliceSize, asynIndex, 0);
      asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                driverName, functionName, asynIndex, drvUser->tagIndex, dataString.c_str(), datatype.c_str());
    }
    else if (datatype == "UDINT")
    {
      // We read a udint and then must save it as a dint to use the asyn Int32Array interface.
      // The waveform record allows it to be displayed as udint
      epicsUInt32 data[sliceSize];
      std::string dataString;
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_uint32(drvUser->tagIndex, (offset + i * 4));
        dataString += std::to_string((uint32_t)data[i]) + ' ';
      }
      if (sliceSize == 1)
        status = setIntegerParam(asynIndex, data[0]);
      else
        status = doCallbacksInt32Array((epicsInt32 *)data, sliceSize, asynIndex, 0);
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
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_float32(drvUser->tagIndex, (offset + i * 4));
        ss << data[i] << ' ';
      }
      dataString = ss.str();
      if (sliceSize == 1)
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
      for (int i = 0; i < sliceSize; i++)
      {
        data[i] = plc_tag_get_float64(drvUser->tagIndex, (offset + i * 8));
        ss << data[i] << ' ';
      }
      dataString = ss.str();
      if (sliceSize == 1)
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

      if (drvUser->strCapacity != 0)
      {
        // If we are getting a string from a UDT, we must overwrite the string capacity
        string_capacity = drvUser->strCapacity;
        if (drvUser->optimise)
        {
          string_length = drvUser->strCapacity;
        }
        else
        {
          string_length = drvUser->strCapacity - drvUser->tagOffset;
        }
      }
      else
      {
        string_capacity = plc_tag_get_string_capacity(drvUser->tagIndex, 0);
        string_length = plc_tag_get_string_length(drvUser->tagIndex, 0) + 1;
      }

      if ((bufferSize <= string_capacity) && !drvUser->optimise)
      {
        plc_tag_set_size(drvUser->tagIndex, string_capacity + 1);
        bufferSize = string_capacity + 1;
      }

      if ((offset >= bufferSize - 1) && !drvUser->optimise)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, attempting to read at an offset beyond tag buffer!\n",
                  driverName, functionName);
        return;
      }
      else if (string_length > string_capacity + 1)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, offset does not point to valid string! Did you set the string size? Is offsetReadSize > str_max_capacity? My asyn parameter ID: %d My tagIndex: %d\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex);
        return;
      }
      char *pData = (char *)malloc((size_t)(unsigned int)(string_length));
      if (drvUser->optimise)
      {
        status = plc_tag_get_string(drvUser->tagIndex, offset, pData, string_length);
      }
      else
      {
        status = plc_tag_get_string(drvUser->tagIndex, 0, pData, string_length);
      }
      if (status != 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while accessing STRING data: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }

      if (drvUser->optimise)
      {
        // for optimise case, we already accounted for the offset when getting the data from libplctag
        status = setStringParam(asynIndex, pData);
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %s My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, pData, datatype.c_str());
      }
      else
      {
        std::string correctedString(pData);
        if (drvUser->offsetReadSize != 0 && correctedString.size() >= drvUser->offsetReadSize + offset)
        {
          // Try and get the substring requested by the user
          // If the string size is less than the readSize + offset, just print what string we do have, this may happen due to Omron
          // only sending the part of the string which has been written to, this may be less than the offset requested to read at.
          correctedString = correctedString.substr(offset, drvUser->offsetReadSize);
        }
        else if (correctedString.size() >= (size_t)offset)
          correctedString = correctedString.substr(offset);
        else
          asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, the string length < offset+offset_read_size. Printing the entire string instead. Are the str attributes correct?\n",
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
      int tagSize = plc_tag_get_size(drvUser->tagIndex);
      int size = bytes * sliceSize;
      if (size + offset <= tagSize)
      {
        size = tagSize - offset; // read all data after offset
      }
      else
      {
        size = 0;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n",
                  driverName, functionName, drvUser->tagIndex);
      }
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status != 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while accessing WORD data: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size * 2 + 1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes - 1;
        for (int j = 0; j < bytes; j++)
        {
          sprintf(hexString + strlen(hexString), "%02X", rawData[n + i * bytes]);
          pData[j + i * bytes] = rawData[n + i * bytes];
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
      int tagSize = plc_tag_get_size(drvUser->tagIndex);
      int size = bytes * sliceSize;
      if (size + offset <= tagSize)
      {
        size = tagSize - offset; // read all data after offset
      }
      else
      {
        size = 0;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n",
                  driverName, functionName, drvUser->tagIndex);
      }
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status != 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while accessing DWORD data: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size * 2 + 1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes - 1;
        for (int j = 0; j < bytes; j++)
        {
          sprintf(hexString + strlen(hexString), "%02X", rawData[n + i * bytes]);
          pData[j + i * bytes] = rawData[n + i * bytes];
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
      int tagSize = plc_tag_get_size(drvUser->tagIndex);
      int size = bytes * sliceSize;
      if (size + offset <= tagSize)
      {
        size = tagSize - offset; // read all data after offset
      }
      else
      {
        size = 0;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n",
                  driverName, functionName, drvUser->tagIndex);
      }
      uint8_t *rawData = (uint8_t *)malloc((size_t)(uint8_t)size);
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, size);
      if (status != 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while accessing LWORD data: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(size * sizeof(epicsInt8));
      /* We flip around the hex numbers to match what is done in the PLC */
      int n;
      char hexString[size * 2 + 1]{};
      for (int i = 0; i < sliceSize; i++)
      {
        n = bytes - 1;
        for (int j = 0; j < bytes; j++)
        {
          sprintf(hexString + strlen(hexString), "%02X", rawData[n + i * bytes]);
          pData[j + i * bytes] = rawData[n + i * bytes];
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
      int bytes = 0;
      int tagSize = plc_tag_get_size(drvUser->tagIndex);
      if (drvUser->offsetReadSize != 0)
      {
        bytes = drvUser->offsetReadSize; // user may request a byte size rather than reading the entire UDT
      }
      if (bytes + offset <= tagSize)
      {
        bytes = tagSize - offset; // read all data after offset
      }
      else
      {
        bytes = 0;
        asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Tag index: %d You are attempting to read beyond the end of the buffer, output has been truncated\n",
                  driverName, functionName, drvUser->tagIndex);
      }
      uint8_t *rawData = (uint8_t *)malloc(bytes * sizeof(uint8_t));
      status = plc_tag_get_raw_bytes(drvUser->tagIndex, offset, rawData, bytes);
      if (status != 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while accessing UDT data: %s\n",
                  driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
        return;
      }
      epicsInt8 *pData = (epicsInt8 *)malloc(bytes * sizeof(epicsInt8));
      memcpy(pData, rawData, bytes);
      status = doCallbacksInt8Array(pData, bytes, asynIndex, 0);
      if (pasynTrace->getTraceMask(pasynUserSelf) & ASYN_TRACEIO_DRIVER)
      {
        char hexString[bytes * 2 + 1]{};
        for (int i = 0; i < bytes; i++)
        {
          sprintf(hexString + strlen(hexString), "%02X", rawData[i]);
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
      if (myParam == asynParamOctet)
      {
        // first we modify the incoming time by the timezone offset defined at driver creation
        // then we convert from this timezone to the local timezone and output as a formatted string
        data += timezoneOffset_ * -3.6e12; // offset in hours * number of nanoseconds in an hour
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
      else
      {
        status = setInteger64Param(asynIndex, data);
        asynPrint(pasynUserSelf, ASYN_TRACEIO_DRIVER, "%s:%s My asyn parameter ID: %d My tagIndex: %d My data: %lld My type %s\n",
                  driverName, functionName, asynIndex, drvUser->tagIndex, data, datatype.c_str());
      }
    }
    setParamStatus(asynIndex, (asynStatus)status);
  }

  if (status == asynError)
  {
    setParamStatus(asynIndex, asynError);
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err occured while updating asyn parameter with asyn ID: %d tagIndex: %d Datatype %s\n",
              driverName, functionName, asynIndex, drvUser->tagIndex, datatype.c_str());
  }
  else if (status == asynTimeout)
  {
    setParamStatus(asynIndex, asynTimeout);
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Timeout occured while updating asyn parameter with asyn ID: %d tagIndex: %d Datatype %s\n",
              driverName, functionName, asynIndex, drvUser->tagIndex, datatype.c_str());
  }
  else if (status == asynSuccess)
  {
    setParamStatus(asynIndex, asynSuccess);
    setParamAlarmStatus(asynIndex, asynSuccess);
    setParamAlarmSeverity(asynIndex, NO_ALARM);
  }
  status = plc_tag_unlock(drvUser->tagIndex);
  if (status != 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Tag index: %d Error occured in libplctag while trying to unlock tag: %s\n",
              driverName, functionName, drvUser->tagIndex, plc_tag_decode_error(status));
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
  double pollingDelay = 0; // To stop from overloading the PLC, we divide read requests throughout the polling interval
  // The poller may not be fully initialised until the startPollers_ flag is set to 1, do not attempt to get the pPoller yet
  while (!this->startPollers_ && !omronExiting)
  {
    epicsThreadSleep(0.1);
  }
  if (omronExiting) { return; }
  omronEIPPoller *pPoller = pollerList_.at(threadName);
  double interval = pPoller->updateRate_;
  for (auto x : tagMap_)
  {
    if (x.second->pollerName == threadName && x.second->readFlag == true)
      pPoller->myTagCount_ += 1;
  }
  asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Starting poller: %s with interval: %f\n", driverName, functionName, threadName.c_str(), interval);
  auto startTime = std::chrono::system_clock::now();
  while (!omronExiting)
  {
    startTime = std::chrono::system_clock::now();
    double waitTime = interval - ((double)timeTaken / 1E9);
    if (waitTime >= 0)
    {
      epicsThreadSleep(waitTime);
    }
    else
    {
      waitTime = 0;
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, Reads taking longer than requested! %f > %f\n", driverName, functionName, ((double)timeTaken / 1E9), interval);
    }

    for (auto x : tagMap_)
    {
      if (x.second->pollerName == threadName && x.second->readFlag == true)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Reading tag: %d with polling interval: %f seconds\n", 
                    driverName, functionName, x.second->tagIndex, interval);
        plc_tag_read(x.second->tagIndex, 0); // Send read request to plc, we will check status and timeouts later
        /* If spreadRequests is true, we sleep to split up read requests within timing interval, otherwise we can get traffic jams and missed 
           polling intervals */
        if (pPoller->myTagCount_ > 1 && pPoller->spreadRequests_)
        {
          pollingDelay = (interval - 0.2 * interval) / pPoller->myTagCount_;
          epicsThreadSleep(pollingDelay);
        }
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
    if (status != asynSuccess)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, while performing asyn callbacks on read poller: %s\n", driverName, functionName, threadName.c_str());
    }
    auto endTime = std::chrono::system_clock::now();
    timeTaken = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count() - waitTime * 1E9;
    if (timeTaken > 0)
      asynPrint(pasynUserSelf, ASYN_TRACE_FLOW, "%s:%s Poller: %s finished processing in: %d msec\n\n", driverName, functionName, threadName.c_str(), (int)(timeTaken / 1E6));
  }
}

asynStatus drvOmronEIP::writeInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements)
{
  const char *functionName = "writeInt8Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  size_t offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  size_t tagSize = plc_tag_get_size(tagIndex);
  double timeout = pasynUser->timeout * 1000;
  bool writeOutOfBounds = 0;
  if (nElements > tagSize)
  {
    // tagSize is calculated by the library based off the initial read of the tag, the user should not try and write more data than this
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, with libplctag tag index: %d. Request to write more characters than can fit into the tag! nElements>tagSize:  %ld > %ld.\n",
              driverName, functionName, tagIndex, nElements, tagSize);
    return asynError;
  }
  else if (nElements < sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, with libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
  }
  if (datatype == "UDT")
  {
    uint8_t *pOutput = (uint8_t *)malloc(tagSize * sizeof(uint8_t));
    // Copy data to UDT, any values which are not defined, are written as 0
    for (size_t i = 0; i < tagSize; i++)
    {
      if (i >= offset && i < offset + nElements)
      {
        pOutput[i] = value[i - offset];
      }
      else
      {
        pOutput[i] = 0;
        writeOutOfBounds = 1;
      }
    }

    if (writeOutOfBounds)
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, with libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize*bytes:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, tagSize);
    status = plc_tag_set_raw_bytes(tagIndex, 0, pOutput, tagSize);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    free(pOutput);
    return asynSuccess;
  }
  else if (datatype == "WORD" || datatype == "DWORD" || datatype == "LWORD")
  {
    size_t bytes = 0;
    if (datatype == "WORD")
    {
      bytes = 2;
    }
    else if (datatype == "DWORD")
    {
      bytes = 4;
    }
    else if (datatype == "LWORD")
    {
      bytes = 8;
    }

    if (nElements + offset > sliceSize * bytes)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, with libplctag tag index: %d. Request to write more data than specified in sliceSize! nElements>sliceSize*bytes:  %ld > %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize * bytes);
      return asynError;
    }
    else if (nElements < sliceSize * bytes - offset)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, with libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize*bytes:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, sliceSize * bytes);
    }

    uint8_t *pOutput = (uint8_t *)malloc(sliceSize * bytes * sizeof(uint8_t));
    int n;
    for (size_t i = 0; i < sliceSize; i++)
    {
      n = bytes - 1;
      for (size_t j = 0; j < bytes; j++)
      {
        if (nElements <= i)
        {
          pOutput[j + i * bytes + offset] = 0;
          writeOutOfBounds = 1;
        }
        else
          pOutput[j + i * bytes + offset] = value[n + i * bytes];
        n--;
      }
    }
    if (writeOutOfBounds)
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, with libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize*bytes:  %ld < %ld.\n",
                driverName, functionName, tagIndex, nElements, tagSize);
    status = plc_tag_set_raw_bytes(tagIndex, 0, pOutput, sliceSize * bytes);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    free(pOutput);
    return asynSuccess;
  }
  else if (datatype == "SINT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_int8(tagIndex, offset + i, 0);
      else
        status = plc_tag_set_int8(tagIndex, offset + i, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    return asynSuccess;
  }
  else if (datatype == "USINT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_uint8(tagIndex, offset + i, 0);
      else
        status = plc_tag_set_uint8(tagIndex, offset + i, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    return asynSuccess;
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }
}

asynStatus drvOmronEIP::writeInt16Array(asynUser *pasynUser, epicsInt16 *value, size_t nElements)
{
  const char *functionName = "writeInt16Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout * 1000;
  if (nElements > sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, with libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
    return asynError;
  }
  else if (nElements < sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, with libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
  }

  if (datatype == "INT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_int16(tagIndex, offset + i * 2, 0);
      else
        status = plc_tag_set_int16(tagIndex, offset + i * 2, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else if (datatype == "UINT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_uint16(tagIndex, offset + i * 2, 0);
      else
        status = plc_tag_set_uint16(tagIndex, offset + i * 2, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt32Array(asynUser *pasynUser, epicsInt32 *value, size_t nElements)
{
  const char *functionName = "writeInt32Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout * 1000;
  if (nElements > sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
    return asynError;
  }
  else if (nElements < sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
  }

  if (datatype == "DINT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_int32(tagIndex, offset + i * 4, 0);
      else
        status = plc_tag_set_int32(tagIndex, offset + i * 4, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else if (datatype == "UDINT")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_uint32(tagIndex, offset + i * 4, 0);
      else
        status = plc_tag_set_uint32(tagIndex, offset + i * 4, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value, size_t nElements)
{
  const char *functionName = "writeFloat32Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout * 1000;
  if (nElements > sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
    return asynError;
  }
  else if (nElements < sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
  }

  if (datatype == "REAL")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_float32(tagIndex, offset + i * 4, 0);
      else
        status = plc_tag_set_float32(tagIndex, offset + i * 4, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements)
{
  const char *functionName = "writeFloat64Array";
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  size_t sliceSize = drvUser->sliceSize;
  int status = 0;
  double timeout = pasynUser->timeout * 1000;
  if (nElements > sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, libplctag tag index: %d. Request to write more values than the configured sliceSize! nElements>sliceSize:  %ld > %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
    return asynError;
  }
  else if (nElements < sliceSize)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s Warn, libplctag tag index: %d. Request to write less values than the configured sliceSize, missing data will be written as null. nElements<sliceSize:  %ld < %ld.\n",
              driverName, functionName, tagIndex, nElements, sliceSize);
  }

  if (datatype == "LREAL")
  {
    for (size_t i = 0; i < sliceSize; i++)
    {
      /* If nElements is less than sliceSize, the remaining data is written as zeroes */
      if (nElements <= i)
        status = plc_tag_set_float64(tagIndex, offset + i * 8, 0);
      else
        status = plc_tag_set_float64(tagIndex, offset + i * 8, *(value + i));
      if (status < 0)
      {
        asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
        return asynError;
      }
    }
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
  const char *functionName = "writeUInt32Digital";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout * 1000;
  if (datatype == "BOOL")
  {
    status = plc_tag_set_bit(tagIndex, offset, value);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    return asynSuccess;
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }
}

asynStatus drvOmronEIP::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  const char *functionName = "writeInt32";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout * 1000;
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
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeInt64(asynUser *pasynUser, epicsInt64 value)
{
  const char *functionName = "writeInt64";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout * 1000;
  if (datatype == "LINT" || datatype == "TIME")
    status = plc_tag_set_int64(tagIndex, offset, value);
  else if (datatype == "ULINT")
    status = plc_tag_set_int64(tagIndex, offset, (epicsUInt64)value);
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  const char *functionName = "writeFloat64";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout * 1000;
  if (datatype == "REAL")
    status = plc_tag_set_float32(tagIndex, offset, (epicsFloat32)value);
  else if (datatype == "LREAL")
    status = plc_tag_set_float64(tagIndex, offset, value);
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }

  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  status = plc_tag_write(tagIndex, timeout);
  if (status < 0)
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
    return asynError;
  }
  return asynSuccess;
}

asynStatus drvOmronEIP::writeOctet(asynUser *pasynUser, const char *value, size_t nChars, size_t *nActual)
{
  const char *functionName = "writeOctet";
  int status = 0;
  omronDrvUser_t *drvUser = tagMap_.at(pasynUser->reason);
  int tagIndex = drvUser->tagIndex;
  int offset = drvUser->tagOffset;
  std::string datatype = drvUser->dataType.first;
  double timeout = pasynUser->timeout * 1000;

  /* This is a bit messy because Omron does strings a bit differently to what libplctag expects*/
  if (datatype == "STRING")
  {
    int string_capacity = plc_tag_get_string_capacity(tagIndex, 0);
    char stringOut[nChars + 1] = {'\0'}; // allow space for null character
    snprintf(stringOut, sizeof(stringOut), value);

    /* Set the tag buffer to the max size of string in PLC. Required as the tag size is set based
    on the current size of the tag in the PLC, but we may write a bigger string than this. */
    /* First check if the user has set this in the extras parameter, if not then we set to the size of nChars and hope it fits*/
    if (drvUser->strCapacity == 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_WARNING, "%s:%s A write to a string tag is being attempted without defining the capacity of this string, this may not work! Tag index: %d\n", driverName, functionName, tagIndex);
      if (nChars > (size_t)string_capacity)
      {
        string_capacity = nChars;
      }
    }
    status = plc_tag_set_size(tagIndex, string_capacity + 2); // Allow room for string length
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Resizing libplctag tag buffer returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_set_string(tagIndex, offset, stringOut); // Set the data
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_set_size(tagIndex, nChars + 2); // Reduce the tag buffer to delete any data beyond the string we pass in
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Resizing libplctag tag buffer returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    status = plc_tag_write(tagIndex, timeout);
    if (status < 0)
    {
      asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Write attempt returned %s\n", driverName, functionName, plc_tag_decode_error(status));
      return asynError;
    }
    memcpy(nActual, &nChars, sizeof(size_t));
  }
  else
  {
    asynPrint(pasynUserSelf, ASYN_TRACE_ERROR, "%s:%s Err, Invalid asyn interface for dtype: %s\n", driverName, functionName, datatype.c_str());
    return asynError;
  }
  return asynSuccess;
}

omronDrvUser_t* drvOmronEIP::getDrvUser(int asynIndex)
{
  return (this->tagMap_.at(asynIndex));
}

drvOmronEIP::~drvOmronEIP()
{
  std::cout << "drvOmronEIP shutting down" << std::endl;
  if (!omronExiting){
    //This should be set true by the epicsExit callback, but if this destructor is called independently, then we can do it here
    //It tells the pollers to quit the polling loop and destruct.
    omronExiting = true;  
    epicsThreadSleep(2);
  }
  int status = 0;
  delete utilities;
  for (auto mi : pollerList_)
  {
    delete mi.second;
  }

  for (auto mi : tagMap_)
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
  std::cout << "Poller " << this->pollerName_ << " shutting down" << std::endl;
}

extern "C"
{
  /* drvOmronEIPStructDefine - Loads structure definitions from file */
  asynStatus drvOmronEIPStructDefine(const char *portName, const char *filePath)
  {
    drvOmronEIP *pDriver = (drvOmronEIP *)findAsynPortDriver(portName);
    if (!pDriver)
    {
      std::cout << "Error, Port " << portName << " not found!" << std::endl;
      return asynError;
    }
    else if (iocStarted)
    {
      std::cout << "Structure definition file must be loaded before database files." << std::endl;
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
                                     const char *pollerName,
                                     double updateRate,
                                     int spreadRequests)
  {
    drvOmronEIP *pDriver = (drvOmronEIP *)findAsynPortDriver(portName);
    if (!pDriver)
    {
      std::cout << "Error, Port " << portName << " not found!" << std::endl;
      return asynError;
    }
    else
    {
      return pDriver->createPoller(portName, pollerName, updateRate, spreadRequests);
    }
  }

  /* iocsh functions */

  static const iocshArg pollerConfigArg0 = {"Port name", iocshArgString};
  static const iocshArg pollerConfigArg1 = {"Poller name", iocshArgString};
  static const iocshArg pollerConfigArg2 = {"Update rate", iocshArgDouble};
  static const iocshArg pollerConfigArg3 = {"Spread requests", iocshArgInt};

  static const iocshArg *const drvOmronEIPConfigPollerArgs[4] = {
      &pollerConfigArg0,
      &pollerConfigArg1,
      &pollerConfigArg2,
      &pollerConfigArg3};

  static const iocshFuncDef drvOmronEIPConfigPollerFuncDef = {"drvOmronEIPConfigPoller", 4, drvOmronEIPConfigPollerArgs};

  static void drvOmronEIPConfigPollerCallFunc(const iocshArgBuf *args)
  {
    drvOmronEIPConfigPoller(args[0].sval, args[1].sval, args[2].dval, args[3].ival);
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
