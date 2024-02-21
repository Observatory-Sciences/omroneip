#include "omroneip.h"
#include "drvOmroneip.h"
#include <sstream>

/*
    Each PV which talks to PLC data is an omronDrvUser with values based on the asyn parameter name.
*/

struct omronDrvUser_t {
  int startIndex;
  int sliceSize;
  std::string tag;
  std::string dataType;
  int32_t tagIndex;
};

typedef struct {
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
    {dataTypeString, "STRING"},
    {dataTypeWord, "WORD"},
    {dataTypeDWord, "DWORD"},
    {dataTypeLWord, "LWORD"},
    {dataTypeUDT, "UDT"}
};

/* Local variable declarations */
static const char *driverName = "drvOmronEIP";           /* String for asynPrint */

static void readPollerC(void *drvPvt)
{
  drvOmronEIP *pPvt = (drvOmronEIP *)drvPvt;
  pPvt->readPoller();
}

/********************************************************************
**  global driver functions
*********************************************************************
*/

static void omronExitCallback(void *pPvt) {
    drvOmronEIP *pDriver = (drvOmronEIP*)pPvt;
    pDriver->omronExiting_ = true;
}

drvOmronEIP::drvOmronEIP(const char *portName,
                          const char *plcType)

            : asynPortDriver(portName,
                              1, /* maxAddr */
                              asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
                              asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask,                   /* Interrupt mask */
                              ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags */
                              1, /* Autoconnect */
                              0, /* Default priority */
                              0), /* Default stack size*/    
            initialized_(false),
            cats_(true)

{
  int status = 6;
  printf("%s\n", portName);

  status = (epicsThreadCreate("readPoller",
                              epicsThreadPriorityMedium,
                              epicsThreadGetStackSize(epicsThreadStackMedium),
                              (EPICSTHREADFUNC)readPollerC,
                              this) == NULL);

  epicsAtExit(omronExitCallback, this);
  initialized_ = true;
}

/*
    Takes the asyn parameter name defined for each pv in the loaded database and matches it to a libplctag tag.
    This function is called twice, once before and once after database initialization, the first time we just return asynDisabled.
*/

asynStatus drvOmronEIP::drvUserCreate(asynUser *pasynUser, const char *drvInfo, const char **pptypeName, size_t *psize)
{
  static const char *functionName="drvUserCreate";
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

  if (initialized_ == false) {
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
  
  tag = (std::string)LIB_PLC_TAG_PROTOCOL + 
        "&name=" + keyWords.at("tagName") + 
        "&elem_count=" + keyWords.at("sliceSize");
  printf("%s\n", tag.c_str());
  int32_t tagIndex = plc_tag_create(tag.c_str(), 100);
  /* Check and report failure codes */ 
  checkTagStatus(tagIndex);

  /* Take care of different datatypes */
  int createStatus = createParam(drvInfo, asynParamFloat64, &asynIndex);

  /* Initialise each datatype*/

  omronDrvUser_t * newDrvUser = (omronDrvUser_t *) callocMustSucceed(1, sizeof(omronDrvUser_t), functionName);
  newDrvUser->dataType = keyWords.at("dataType");
  newDrvUser->sliceSize = std::stoi(keyWords.at("sliceSize"));
  newDrvUser->tag = keyWords.at("tagName");
  newDrvUser->startIndex = std::stoi(keyWords.at("startIndex"));
  newDrvUser->tagIndex = tagIndex;

  tagMap_[asynIndex] = newDrvUser;

  std::cout << tagIndex << " " << asynIndex <<std::endl;

  pasynUser->drvUser = newDrvUser;
  pasynUser->reason = asynIndex;
  readIndexList_.push_back(pasynUser->reason);
  return asynPortDriver::drvUserCreate(pasynUser, drvInfo, pptypeName, psize);
}

std::unordered_map<std::string, std::string> drvOmronEIP::drvInfoParser(const char *drvInfo)
{
  const std::string str(drvInfo);
  char delim = ' ';
  char escape = '/';
  std::unordered_map<std::string, std::string> keyWords = {
    {"pollerName", "none"}, //optional
    {"tagName", "none"}, //required
    {"dataType", "none"}, //optional
    {"startIndex", "1"}, //optional
    {"sliceSize", "1"}, //optional
    {"tagExtras", "none"}, //optional
    {"stringValid", "none"} //set to false if errors are detected which aborts creation of tag and asyn parameter
  };
  std::list<std::string> words;
  std::string substring;
  bool escaped = false;
  int pos = 0;


  for(int i = 0; i < str.size(); i++)
  {
    if (str[i] == '/')
    {
      if (escaped == false) {escaped = true;}
      else {escaped = false;}
    }
    if (str[i] == ' ' && !escaped)
    {
      if (i==0) 
      {
        pos+=1;
        continue;
      }
      else if (str[i-1] == escape)
      {
        // delimeter after escape character
        substring = str.substr(pos+1, (i-1)-(pos+1));
      }
      // regular delimeter
      else 
      {
        substring = str.substr(pos, i-pos);
      }
      words.push_back(substring);
      pos = i+1;
    }
    else if (i == str.size()-1)
    {
      substring = str.substr(pos, str.size()-pos);
      words.push_back(substring);
    }
  }

  if (escaped)
  {
    std::cout<<"Escape character never closed! Record invalid"<<std::endl;
    keyWords.at("stringValid") = "false";
  }

  if (words.size() < 1)
  {
    std::cout<<"No arguments supplied to driver"<<std::endl;
    keyWords.at("stringValid") = "false";
  }

  if (words.front()[0]=='@')
  {
    keyWords.at("pollerName") = words.front().substr(1,words.front().size()-1);
    words.pop_front();
  }

  int params = words.size();
  for (int i = 0;i<params;i++)
  {
    if (i == 0)
    {
      std::string startIndex;
      auto b=words.front().begin(), e=words.front().end();

      while ((b=std::find(b, e, '[')) != e)
      {
          auto n_start=++b;
          b=std::find(b, e, ']');
          startIndex=std::string(n_start, b);
          if (!startIndex.empty()) {break;}
      }
      if (!startIndex.empty())
      {
        keyWords.at("startIndex")=startIndex;
        if (std::stoi(startIndex)<1)
        {
          std::cout<<"A startIndex of < 1 is forbidden"<<std::endl;
          keyWords.at("stringValid") = "false";
        }
      }
      keyWords.at("tagName") = words.front();
      words.pop_front();
    }
    else if (i==1)
    {
      //checking required
      bool validDataType = false;
      for (int t=0; t<MAX_OMRON_DATA_TYPES; t++)
      {
        if (strcmp(words.front().c_str(), omronDataTypes[t].dataTypeString) == 0)
        {
          validDataType = true;
          keyWords.at("dataType") = words.front();
        }
      }
      if (!validDataType)
      {
        std::cout<<"Datatype invalid"<<std::endl;
        keyWords.at("stringValid") = "false";
      }
      words.pop_front();
    }
    else if (i==2)
    {
      char* p;
      strtol(words.front().c_str(), &p, 10);
      if (*p == 0)
      {
        keyWords.at("sliceSize") = words.front();
      }
      else
      {
        std::cout<<"Invalid sliceSize, must be integer."<<std::endl;
        keyWords.at("stringValid") = "false";
      }
      words.pop_front();
    }
    else if (i==3)
    {
      keyWords.at("tagExtras") = words.front();
      words.pop_front();
    }
  }

  for (auto i = keyWords.begin(); i != keyWords.end(); i++) 
    std::cout << i->first << " \t\t\t" << i->second << std::endl; 

  std::cout<<"Returning keyWords"<<std::endl;

  return keyWords;
}

bool drvOmronEIP::checkTagStatus(int32_t tagStatus)
{
  if (tagStatus >=0)
  {
    return true;
  }

  switch (tagStatus)
  {
  case -7:
    std::cout<<"Invalid tag creation attribute string: "<<tagStatus<<std::endl;
    return false;
  
  case -19:
    std::cout<<"Tag not found on PLC: "<<tagStatus<<std::endl;
    return false;
  
  default:
    std::cout<<"Unknown status: "<<tagStatus<<std::endl;
    return false;
  }
}

void drvOmronEIP::readPoller()
{
  double data;
  std::string tag;
  asynParamType type;
  ELLLIST *pclientList;
  interruptNode *pnode;
  asynUser *pasynUser;
  int offset;

  while (true)
  {
    epicsThreadSleep(3);
    for ( auto x : tagMap_)
    {
      getParamType(x.first, &type);
      if (std::find(readIndexList_.begin(), readIndexList_.end(), x.first) != readIndexList_.end())
      {
        plc_tag_read(x.second->tagIndex, 100);
        data = plc_tag_get_float32(x.second->tagIndex, 0);
        std::cout<<"My ID: " << x.first << " My tagIndex: "<<x.second->tagIndex<<" My data: "<<data<<std::endl;
        double temp; 
        getDoubleParam(x.first, &temp);
        setDoubleParam(x.first, data);
        
        callParamCallbacks(x.first);

        // pasynManager->interruptStart(asynStdInterfaces.float64InterruptPvt, &pclientList);
        // pnode = (interruptNode *)ellFirst(pclientList);
        // asynFloat64Interrupt *pFloat64;
        // pFloat64 = (asynFloat64Interrupt *)pnode->drvPvt;
        // pasynUser = pFloat64->pasynUser;
        // pasynManager->getAddr(pasynUser, &offset);
        // pFloat64->callback(pFloat64->userPvt, pasynUser, data);
        // pnode = (interruptNode *)ellNext(&pnode->node);
        // pasynManager->interruptEnd(asynStdInterfaces.float64InterruptPvt);
      }
    }
  }
}

asynStatus drvOmronEIP::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
  std::cout<<"Im requesting to read int!"<<std::endl;
  readIndexList_.push_back(pasynUser->reason);
  return asynSuccess;
}

asynStatus drvOmronEIP::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
  std::cout<<"Im requesting to read float!"<<std::endl;
  readIndexList_.push_back(pasynUser->reason);
  return asynSuccess;
}

extern "C" {
/*
** drvOmronEIPConfigure() - create and init an asyn port driver for a PLC
**
*/

/** EPICS iocsh callable function to call constructor for the drvModbusAsyn class. */
asynStatus drvOmronEIPConfigure(const char *portName,
                                char *plcType)
{
    new drvOmronEIP(portName,
                    plcType);
    
    
    return asynSuccess;
}








/* iocsh functions */

static const iocshArg ConfigureArg0 = {"Port name",            iocshArgString};
static const iocshArg ConfigureArg1 = {"PLC type",             iocshArgString};

static const iocshArg * const drvOmronEIPConfigureArgs[2] = {
    &ConfigureArg0,
    &ConfigureArg1
};

static const iocshFuncDef drvOmronEIPConfigureFuncDef={"drvOmronEIPConfigure", 2, drvOmronEIPConfigureArgs};

static void drvOmronEIPConfigureCallFunc(const iocshArgBuf *args)
{
  drvOmronEIPConfigure(args[0].sval, args[1].sval);
}

static void drvOmronEIPRegister(void)
{
  iocshRegister(&drvOmronEIPConfigureFuncDef, drvOmronEIPConfigureCallFunc);
}

epicsExportRegistrar(drvOmronEIPRegister);

} // extern "C"
