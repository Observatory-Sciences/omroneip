< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)


#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 2, 3.5)

#asynSetTraceFile omronDriver 0 asynTrace.out
#asynSetTraceMask omronDriver 0 0x00FF #everything
asynSetTraceMask omronDriver 0 0x0021 #Warning and Error

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2, 1)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/testStructDefs.csv")

dbLoadRecords("db/testTime.db", "P=${P}, I=1, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testTimeStruct.db", "P=${P}, I=1, PORT=omronDriver, POLLER=mediumPoller")

iocInit()
