< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 2, 0)

#asynSetTraceFile omronDriver 0 asynTrace.out
asynSetTraceMask omronDriver 0 0x0021 #warn and error
#asynSetTraceMask omronDriver 0 0x00FF #everything

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 0.1, 0)
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2, 0)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 10, 0)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/structDefs.csv")

dbLoadRecords("db/testStatusChannels.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testHeatingZones.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")
dbLoadTemplate("iocBoot/iocTest/testISIS.substitutions") #uses slowPoller

iocInit()
