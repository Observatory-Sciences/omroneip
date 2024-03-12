< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, plcName, )
drvOmronEIPConfigure("omronDriver", "OmronNJ")

#drvOmronEIPConfigPoller(asynPortName, pollerName, updateRate)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 0.1)
drvOmronEIPConfigPoller("omronDriver", "mediumPoller", 2)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 10)

dbLoadRecords("db/testStatusChannels.db", "P=${P}, PORT=omronDriver, POLLER=fastPoller")
dbLoadRecords("db/testHeatingZones.db", "P=${P}, PORT=omronDriver, POLLER=mediumPoller")
dbLoadTemplate("iocBoot/iocTest/testISIS.substitutions")

#dbLoadRecords("db/test.db", "P=${P}, PORT=omronDriver, POLLER1=fastPoller, POLLER2=slowPoller")
#dbLoadRecords("db/testReadNoPacking.db", "P=${P}, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testReadPacking.db", "P=${P}, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testWeirdINP.db", "P=${P}, PORT=omronDriver")

#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}, PORT=omronDriver")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}2, PORT=omronDriver")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}3, PORT=omronDriver")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}4, PORT=omronDriver")
#dbLoadRecords("db/testWriteDataTypes.db", "P=${P}5, PORT=omronDriver")

#dbLoadRecords("db/testReadDataTypes.db", "P=${P}, I=1, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}2, I=2, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}3, I=3, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}4, I=4, PORT=omronDriver, POLLER=testPoller")
#dbLoadRecords("db/testReadDataTypes.db", "P=${P}5, I=5, PORT=omronDriver, POLLER=testPoller")

#dbLoadRecords("db/asynRecord.db","P=${P},R=asyn,PORT=omronDriver,ADDR=0,IMAX=100,OMAX=100")

iocInit()

