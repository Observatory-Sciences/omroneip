#include "drvOmronEIP.h"
#include "omronEIP.h"

/* Local variable declarations */
static const char *driverName = "drvOmronEIP";           /* String for asynPrint */

static void readPollerC(void *drvPvt);


/********************************************************************
**  global driver functions
*********************************************************************
*/

static void omronExitCallback(void *pPvt) {
    drvOmronEIP *pDriver = (drvOmronEIP*)pPvt;
    pDriver->omronExiting_ = true;
}

drvOmronEIP::drvOmronEIP(const char *portName, const char *octetPortName,
                             int modbusSlave, int modbusFunction,
                             int modbusStartAddress, int modbusLength,
                             int dataType,
                             int pollMsec,
                             const char *plcType)

   : asynPortDriver(portName,
                    1, /* maxAddr */
                    asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask | asynDrvUserMask, /* Interface mask */
                    asynInt32Mask | asynUInt32DigitalMask | asynInt64Mask | asynFloat64Mask | asynInt32ArrayMask | asynOctetMask,                   /* Interrupt mask */
                    ASYN_CANBLOCK | ASYN_MULTIDEVICE, /* asynFlags */
                    1, /* Autoconnect */
                    0, /* Default priority */
                    0) /* Default stack size*/

{
  int status = 6;
  printf("%s\n", portName);
}

extern "C" {
/*
** drvOmronEIPConfigure() - create and init an asyn port driver for a PLC
**
*/

/** EPICS iocsh callable function to call constructor for the drvModbusAsyn class. */
asynStatus drvOmronEIPConfigure(const char *portName, const char *octetPortName,
                                  int modbusSlave, int modbusFunction,
                                  int modbusStartAddress, int modbusLength,
                                  const char *dataTypeString,
                                  int pollMsec,
                                  char *plcType)
{
    new drvOmronEIP(portName, octetPortName,
                      modbusSlave, modbusFunction,
                      modbusStartAddress, modbusLength,
                      1,
                      pollMsec,
                      plcType);
    return asynSuccess;
}

/* iocsh functions */

static const iocshArg ConfigureArg0 = {"Port name",            iocshArgString};
static const iocshArg ConfigureArg1 = {"Octet port name",      iocshArgString};
static const iocshArg ConfigureArg2 = {"Modbus slave address", iocshArgInt};
static const iocshArg ConfigureArg3 = {"Modbus function code", iocshArgInt};
static const iocshArg ConfigureArg4 = {"Modbus start address", iocshArgInt};
static const iocshArg ConfigureArg5 = {"Modbus length",        iocshArgInt};
static const iocshArg ConfigureArg6 = {"Data type",            iocshArgString};
static const iocshArg ConfigureArg7 = {"Poll time (msec)",     iocshArgInt};
static const iocshArg ConfigureArg8 = {"PLC type",             iocshArgString};

static const iocshArg * const drvOmronEIPConfigureArgs[9] = {
    &ConfigureArg0,
    &ConfigureArg1,
    &ConfigureArg2,
    &ConfigureArg3,
    &ConfigureArg4,
    &ConfigureArg5,
    &ConfigureArg6,
    &ConfigureArg7,
    &ConfigureArg8
};

static const iocshFuncDef drvOmronEIPConfigureFuncDef={"drvOmronEIPConfigure", 9, drvOmronEIPConfigureArgs};

static void drvOmronEIPConfigureCallFunc(const iocshArgBuf *args)
{
  drvOmronEIPConfigure(args[0].sval, args[1].sval, args[2].ival, args[3].ival, args[4].ival, args[5].ival, args[6].sval, args[7].ival, args[8].sval);
}

static void drvOmronEIPRegister(void)
{
  iocshRegister(&drvOmronEIPConfigureFuncDef, drvOmronEIPConfigureCallFunc);
}

epicsExportRegistrar(drvOmronEIPRegister);

} // extern "C"
