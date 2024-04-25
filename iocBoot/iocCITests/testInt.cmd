< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug) 
drvOmronEIPConfigure("omronDriver", $(GATEWAY), $(ROUTE_PATH),$(PLC), "2")

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 1) #This can be lowered to 0.1

#asynSetTraceFile omronDriver 0 asynTrace.out
asynSetTraceMask omronDriver 0 0x00FF

dbLoadRecords("db/testWriteDataTypes.db", "P=${P}, I=1, PORT=omronDriver")
dbLoadRecords("db/testReadDataTypes.db", "P=${P}, I=1, PORT=omronDriver, POLLER=fastPoller")

iocInit()

dbpf "$(P)writeInt16" "1"
epicsThreadSleep(1)
dbgf "$(P)readInt16"
dbpf "$(P)writeInt16" "2"
epicsThreadSleep(1)
dbgf "$(P)readInt16"
dbpf "$(P)writeInt16" "3"
epicsThreadSleep(1)
dbgf "$(P)readInt16"
dbpf "$(P)writeInt16" "4"
epicsThreadSleep(1)
dbgf "$(P)readInt16"
dbpf "$(P)writeInt16" "5"
epicsThreadSleep(1)
dbgf "$(P)readInt16"