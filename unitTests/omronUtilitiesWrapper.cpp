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

std::tuple<std::string,std::string,bool> omronUtilitiesWrapper::wrap_checkValidName(const std::string str)
{
  return checkValidName(str);
}

std::string omronUtilitiesWrapper::wrap_checkValidDtype(const std::string str)
{
  return checkValidDtype(str);
}

std::tuple<std::string, std::string> omronUtilitiesWrapper::wrap_checkValidSliceSize(const std::string str, bool indexable, std::string dtype)
{
  return checkValidSliceSize(str, indexable, dtype);
}

std::tuple<std::string, std::string> omronUtilitiesWrapper::wrap_checkValidOffset(const std::string str)
{
  return checkValidOffset(str);
}

std::tuple<std::string, std::string> omronUtilitiesWrapper::wrap_checkValidExtras(const std::string str, drvInfoMap &keyWords)
{
  return checkValidExtras(str, keyWords);
}
