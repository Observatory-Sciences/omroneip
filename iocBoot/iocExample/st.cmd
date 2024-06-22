# Sets up a basic ioc which reads and writes to an OmronNJ/NX PLC via ethernet/IP
# For this example to fully work, configure the gateway and route_path to that of your PLC
# You will also need the following variables in your PLC:
# A 10 element array of INT called intArray
# A LREAL called testLREAL
# A 256 byte STRING called testString

< envPaths
epicsEnvSet("P", "omronEIP:")
cd ${TOP}
dbLoadDatabase("dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

#drvOmronEIPConfigure(driverPortName, gateway, route_path, plc, libplctag_debug, timezone_offset) 
drvOmronEIPConfigure("omronDriver", "10.2.2.57", "18,10.2.2.57","omron-njnx", "0", "0")

#Warning and Error logging
#asynSetTraceMask omronDriver 0 0x0021
#Warning, error and flow logging
asynSetTraceMask omronDriver 0 0x0031

#drvOmronEIPConfigPoller(driverPortName, pollerName, updateRate)
drvOmronEIPConfigPoller("omronDriver", "fastPoller", 1)
drvOmronEIPConfigPoller("omronDriver", "slowPoller", 5)

dbLoadRecords("db/example.db", "P=${P}, R=example:, PORT=omronDriver, POLLER1=fastPoller, POLLER2=slowPoller")

iocInit()
