#ifndef omronUtilitiesWrapper_H
#define omronUtilitiesWrapper_H

#include "omronUtilities.h"


/* Class which contains generic functions required by the driver */
class omronUtilitiesWrapper : omronUtilities {
public:
   omronUtilitiesWrapper(drvOmronEIP *pDriver);
   ~omronUtilitiesWrapper();
   drvInfoMap wrap_drvInfoParser(const char *drvInfo);
   std::tuple<std::string,std::string,bool> wrap_checkValidName(const std::string str);
   std::string wrap_checkValidDtype(const std::string str);
   std::tuple<std::string,std::string> wrap_checkValidSliceSize(const std::string str, bool indexable, std::string dtype);
   std::tuple<std::string,std::string> wrap_checkValidOffset(const std::string str);
   std::tuple<std::string,std::string> wrap_checkValidExtras(const std::string str, drvInfoMap &keyWords);
};

#endif