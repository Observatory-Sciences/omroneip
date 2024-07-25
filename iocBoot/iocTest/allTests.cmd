#This startup script creates an IOC which reads most of the test database
#As some of the test databases include erroneous user input, there will be many errors, but the valid records should read correctly
< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)


#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
#drvOmronEIPConfigure("omronDriver", "0", "1,0","ControlLogix", 2, 0)
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 0, 3.5)

#asynSetTraceFile omronDriver 0 asynTrace.out
#asynSetTraceMask omronDriver 0 0x00FF #everything
#asynSetTraceMask omronDriver 0 0x0037 #Warning, flow, error, TRACEIO_DEVICE, TRACEIO_FILTER
asynSetTraceMask omronDriver 0 0x0021 #Warning and Error
#asynSetTraceMask omronDriver 0 0x0010 #TRACE_FLOW
#asynSetTraceMask omronDriver 0 0x0008 #TRACEIO_DRIVER

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 0.1, 1)
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2, 1)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 10, 1)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/testStructDefs.csv")

dbLoadRecords("db/testTime.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testTimeStruct.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")

dbLoadRecords("db/testStrings.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")

dbLoadRecords("db/testStatusChannels.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testHeatingZones.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")
dbLoadTemplate("iocBoot/iocTest/testISIS.substitutions")

dbLoadRecords("db/testWriteDataTypes.db", "P=${P}, I=1, PORT=omronDriver")
dbLoadRecords("db/testReadDataTypes.db", "P=${P}, I=1, PORT=omronDriver, POLLER=fastPoller")

dbLoadRecords("db/testBadOptimisation.db", "P=${P}, I=2, R=2:, PORT=omronDriver, POLLER=mediumPoller")

dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=1, R=1:, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=2, R=2:, PORT=omronDriver, POLLER=fastPoller")

dbLoadRecords("db/testReadNoPacking.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller") 
dbLoadRecords("db/testReadPacking.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")

#Will create some error messages, may even seg fault although it shouldnt
dbLoadRecords("db/testWeirdINP.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")

iocInit()
