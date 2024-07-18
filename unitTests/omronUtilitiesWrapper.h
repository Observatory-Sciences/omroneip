#ifndef omronUtilitiesWrapper_H
#define omronUtilitiesWrapper_H

#include "omronUtilities.h"


/* Class which contains generic functions required by the driver */
class omronUtilitiesWrapper : omronUtilities {
public:
   omronUtilitiesWrapper(drvOmronEIP *pDriver);
   ~omronUtilitiesWrapper();
   drvInfoMap wrap_drvInfoParser(const char *drvInfo);
};

#endif