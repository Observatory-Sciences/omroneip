< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvAsynIPPortConfigure(portName, ip:port, priority, noAutoConnect, noProcessEos )
#drvAsynIPPortConfigure("plcPort", "10.2.2.57:44818", 0, 0, 0)

#drvOmronEIPConfigure(driverPortName, asynPortName, )
drvOmronEIPConfigure("driverPort", "OmronNJ")

dbLoadRecords("db/test.db", "P=${P}, PORT=driverPort, POLLER=testPoller")

#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}, PORT=driverPort")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}2, PORT=driverPort")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}3, PORT=driverPort")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}4, PORT=driverPort")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}5, PORT=driverPort")

#dbLoadRecords("db/testReadDataTypes.db", "P=${P}, PORT=driverPort, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}2, PORT=driverPort, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}3, PORT=driverPort, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}4, PORT=driverPort, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}5, PORT=driverPort, POLLER=testPoller")

#dbLoadRecords("db/testWeirdINP.db", "P=${P}, PORT=driverPort")
dbLoadRecords("db/asynRecord.db","P=${P},R=asyn,PORT=driverPort,ADDR=0,IMAX=100,OMAX=100")

iocInit()

