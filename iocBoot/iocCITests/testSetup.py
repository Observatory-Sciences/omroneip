import unittest
import epics
from os import environ, chdir
import subprocess
import argparse

#Startup the IOC and the driver and the libplctag simulator

class TestSetup:
    def __init__(self, simulatorPath, iocPath, plc):
        self.simulatorPath = simulatorPath
        self.iocPath = iocPath
        self.plc = plc
        environ["EPICS_CA_ADDR_LIST"] = "127.0.0.1"
        environ["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
        #environ["PATH"] = "/home/runner/.cache/base-R3.15.9" + environ.get("PATH")
        self.EPICS_HOST_ARCH = environ.get("EPICS_HOST_ARCH")
        self.EPICS_BASE = environ.get("EPICS_BASE")
        print("Epics base is: " +  environ.get("IOC_TOP"))
        print("IOC top is: " + self.EPICS_BASE)
        self.IOC_EXECUTABLE = (f"{self.iocPath}/bin/linux-x86_64/omroneipApp")
        self.IOC_CMD = (f"{self.iocPath}/iocBoot/iocCITests/testInt.cmd")
        environ["EPICS_DB_INCLUDE_PATH"] = (f"{self.iocPath}/omroneipApp/db")
        environ["LD_LIBRARY_PATH"] = self.EPICS_BASE + "/lib/" + self.EPICS_HOST_ARCH

    def startSimulator(self, simulatorArgs):
        print("Setting up libplctag simulator!")
        print(simulatorArgs)
        args = [self.simulatorPath]
        args.extend(simulatorArgs)
        self.simulatorProc = subprocess.Popen(args=args, shell=False, stdout=subprocess.PIPE)

    def closeSimulator(self):
        print("Closing PLC server simulator!")
        self.simulatorProc.kill()
        self.simulatorProc.wait(timeout=5)

    def startIOC(self):
        print("Setting up test IOC!")
        if (self.plc == "Omron"):
            environ["PLC"] = "omron-njnx"
        elif (self.plc == "ControlLogix"):
            environ["PLC"] = "ControlLogix"
        else:
            print("Invalid PLC name supplied!")
        chdir(self.iocPath + "/iocBoot/iocCITests/")
        self.iocProc = subprocess.Popen([self.IOC_EXECUTABLE, self.IOC_CMD], shell=False)

    def closeIOC(self):
        print("Closing IOC")
        self.iocProc.kill()

    def readPV(self, pvName):
        val = epics.caget(pvName, timeout=2)
        if (val == "None"):
            val = 0
        print(f"Read value={val} from simulator")
        return val

    def writePV(self, pvName, val):
        print(f"Writing value={val} to simulator")
        epics.caput(pvName, val, wait=True, timeout=2)

