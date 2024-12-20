< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)


#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 0, 0)

#asynSetTraceFile omronDriver 0 asynTrace.out
asynSetTraceMask omronDriver 0 0x00FF #everything

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 0.1, 1)
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2, 1)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 10, 1)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/structDefs.csv")

dbLoadRecords("db/testArrayOptimisations.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")


iocInit()
