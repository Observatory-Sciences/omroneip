< envPaths

dbLoadDatabase("../../dbd/omroneipApp.dbd")
omroneipApp_registerRecordDeviceDriver(pdbbase)

drvAsynIPPortConfigure("MainPort", "10.2.2.57:44818", 0, 0, 0)

drvOmronEIPConfigure("TestPort", "MainPort", 0, 1, 500, 1, 0, 100, "OmronNJ")
