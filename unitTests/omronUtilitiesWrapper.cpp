#include "omronUtilitiesWrapper.h"
    
omronUtilitiesWrapper::omronUtilitiesWrapper(drvOmronEIP *ptrDriver)
  : omronUtilities(ptrDriver)
{
}

omronUtilitiesWrapper::~omronUtilitiesWrapper()
{
}

drvInfoMap omronUtilitiesWrapper::wrap_drvInfoParser(const char *drvInfo)
{
  return drvInfoParser(drvInfo);
}

