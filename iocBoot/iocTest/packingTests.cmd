#testReadPacking.db and testReadNoPacking.db both read the same amount of data. However the packing case should read much faster, and you should
#see that the time taken reported by the poller is much smaller ~half as long.
< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)


#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", 2, 0)

#asynSetTraceFile omronDriver 0 asynTrace.out
#asynSetTraceMask omronDriver 0 0x00FF #everything
asynSetTraceMask omronDriver 0 0x0031 #Warning and Error

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate, spreadRequests)
#I have disabled spreadRequests here to properly see the performance difference
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 1, 0)

#dbLoadRecords("db/testReadPacking.db", "P=${P}, I=1, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testReadNoPacking.db", "P=${P}, I=1, PORT=omronDriver, POLLER=fastPoller")

iocInit()
