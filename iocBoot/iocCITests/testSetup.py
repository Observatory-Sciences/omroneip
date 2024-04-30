import unittest
import epics
from run_iocsh import IOC
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
        self.EPICS_HOST_ARCH = environ.get("EPICS_HOST_ARCH")
        self.IOC_EXECUTABLE = (f"{self.iocPath}/bin/{self.EPICS_HOST_ARCH}/omroneipApp")
        self.IOC_CMD = (f"{self.iocPath}/iocBoot/iocCITests/testInt.cmd")

    def setupSimulator(self, simulatorArgs):
        print("Setting up libplctag simulator!")
        print(simulatorArgs)
        args = [self.simulatorPath]
        args.extend(simulatorArgs)
        self.simulatorProc = subprocess.Popen(args=args, shell=False, stdout=subprocess.PIPE)

    def closeSimulator(self):
        print("Closing PLC server simulator!")
        self.simulatorProc.terminate()
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
        subprocess.run([self.IOC_EXECUTABLE, self.IOC_CMD])
