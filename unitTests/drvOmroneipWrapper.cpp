#include "drvOmroneipWrapper.h"


omronEIPPollerWrapper::omronEIPPollerWrapper(const char *portName, const char *pollerName, double updateRate, int spreadRequests)
  : omronEIPPoller(portName, pollerName, updateRate, spreadRequests)
{
}

drvOmronEIPWrapper::drvOmronEIPWrapper(const char *portName,
                         const char *gateway,
                         const char *path,
                         const char *plcType,
                         int debugLevel,
                         double timezoneOffset)
  : drvOmronEIP(portName, gateway, path, plcType, debugLevel, timezoneOffset)
{
}

asynStatus drvOmronEIPWrapper::wrap_createPoller(const char *portName, const char *pollerName, double updateRate, int spreadRequests)
{
  return createPoller(portName,pollerName,updateRate,spreadRequests);
}

void drvOmronEIPWrapper::wrap_initialiseDrvUser(omronDrvUser_t *newDrvUser, const drvInfoMap keyWords, int tagIndex, std::string tag, bool readFlag, const asynUser *pasynUser)
{
  return initialiseDrvUser(newDrvUser, keyWords, tagIndex, tag, readFlag, pasynUser);
}

drvOmronEIPWrapper::~drvOmronEIPWrapper()
{
}

omronEIPPollerWrapper::~omronEIPPollerWrapper()
{
}