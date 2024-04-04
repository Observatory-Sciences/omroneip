< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", "0")

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 0.5) #This can be lowered to 0.1
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 10)

#drvOmronEIPStructDefine(driverPortName, pathToFile)
drvOmronEIPStructDefine("omronDriver", "iocBoot/iocTest/structDefs.csv")

#asynSetTraceFile omronDriver 0 asynTrace.out
asynSetTraceMask omronDriver 0 0x00FF

#dbLoadRecords("db/testStatusChannels.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")
#dbLoadRecords("db/testHeatingZones.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadTemplate("iocBoot/iocTest/testISIS.substitutions")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}, I=1, PORT=omronDriver")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}, I=1, PORT=omronDriver, POLLER=mediumPoller")

#dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=1, R=1:, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=2, R=2:, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=3, R=3:, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=4, R=4:, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadRecords("db/testEfficientRead.db", "P=${P}, I=5, R=5:, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=1, R=1:, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=2, R=2:, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=3, R=3:, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=4, R=4:, PORT=omronDriver, POLLER=mediumPoller")
dbLoadRecords("db/testInefficientRead.db", "P=${P}, I=5, R=5:, PORT=omronDriver, POLLER=mediumPoller")

#dbLoadRecords("db/test.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")
#dbLoadRecords("db/testReadNoPacking.db", "P=${P}, PORT=omronDriver, POLLER=slowPoller") #doesnt work properly atm
#dbLoadRecords("db/testReadPacking.db", "P=${P}, PORT=omronDriver, POLLER=slowPoller") #doesnt work properly atm
#dbLoadRecords("db/testWeirdINP.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller") #doesnt work properly atm

#dbLoadRecords("db/asynRecord.db","P=${P},R=asyn,PORT=omronDriver,ADDR=0,IMAX=100,OMAX=100")

iocInit()
