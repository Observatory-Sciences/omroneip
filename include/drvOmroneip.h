/* ANSI C includes  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* EPICS includes */
#include <dbAccess.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsTime.h>
#include <epicsEndian.h>
#include <epicsExit.h>
#include <cantProceed.h>
#include <errlog.h>
#include <osiSock.h>
#include <iocsh.h>

/* Asyn includes */
#include "asynPortDriver.h"
#include "asynOctetSyncIO.h"
#include "asynCommonSyncIO.h"

#include <epicsExport.h>

class epicsShareClass drvOmronEIP : public asynPortDriver {
public:
    drvOmronEIP(const char *portName, const char *octetPortName,
                  int modbusSlave, int modbusFunction,
                  int modbusStartAddress, int modbusLength,
                  int dataType,
                  int pollMsec,
                  const char *plcType);
     bool omronExiting_;
  };
