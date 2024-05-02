< envPaths
epicsEnvSet("P", "omronEIP:")
epicsEnvSet("EPICS_CAS_INTF_ADDR_LIST", "127.0.0.1")
epicsEnvSet("EPICS_CA_ADDR_LIST", "127.0.0.1")

cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug) 
drvOmronEIPConfigure("omronDriver", "127.0.0.1", "18,127.0.0.1", $(PLC), "1")

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 1) #This can be lowered to 0.1

#asynSetTraceFile omronDriver 0 asynTrace.out
#asynSetTraceMask omronDriver 0 0x00FF

dbLoadRecords("db/testInt.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")


iocInit()

dbl()

casr 6
