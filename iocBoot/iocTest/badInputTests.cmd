< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 2, 0)

#asynSetTraceFile omronDriver 0 asynTrace.out
#asynSetTraceMask omronDriver 0 0x0021 #warn and error
asynSetTraceMask omronDriver 0 0x00FF #everything

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
drvOmronEIPConfigPoller("omronDriver", "testPoller", 2, 1)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/structDefs.csv")

dbLoadRecords("db/testWeirdINP.db", "P=${P}, PORT=omronDriver, POLLER=testPoller")

iocInit()
