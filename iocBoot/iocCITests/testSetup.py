import unittest
import epics
from os import environ, chdir, getcwd
import subprocess
import argparse
import time

#Startup the IOC and the driver and the libplctag simulator

class TestSetup:
    def __init__(self, simulatorPath, omroneip, plc):
        self.simulatorPath = simulatorPath
        self.omroneipPath = omroneip
        self.plc = plc
        self.ENV = environ
        self.EPICS_HOST_ARCH = environ.get("EPICS_HOST_ARCH")
        self.EPICS_BASE = environ.get("EPICS_BASE")
        self.IOC_TOP = environ.get("IOC_TOP")
        self.IOC_EXECUTABLE = (f"{self.omroneipPath}/bin/linux-x86_64/omroneipApp")
        self.IOC_CMD = (f"{self.omroneipPath}/iocBoot/iocCITests/testInt.cmd")
        if self.EPICS_BASE not in self.ENV["PATH"]:
            self.ENV["PATH"] = f"{self.EPICS_BASE}/bin/{self.EPICS_HOST_ARCH}:{self.ENV['PATH']}"
            print("Added EPICS_BASE to path, path is now: " + self.ENV["PATH"])
        else:
            print("Path is: " + self.ENV["PATH"])
        if self.EPICS_BASE!=None:
            print("Epics base is: " + self.EPICS_BASE )
        else:
            print("Could not find EPICS base!")
        if self.IOC_TOP != None:
            print("IOC top is: " + self.IOC_TOP)
        else:
            self.IOC_TOP = self.omroneipPath + "/iocBoot/iocCITests"
            print("New IOC top is: " + self.IOC_TOP)
        print("Libplctag simulator at: " + self.simulatorPath)

        self.ENV["EPICS_CA_ADDR_LIST"] = "127.0.0.1:5064"
        self.ENV["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
        self.ENV["EPICS_DB_INCLUDE_PATH"] = (f"{self.omroneipPath}/omroneipApp/db")
        self.ENV["LD_LIBRARY_PATH"] = self.EPICS_BASE + "/lib/" + self.EPICS_HOST_ARCH

    def startSimulator(self, simulatorArgs):
        print("Setting up libplctag simulator with parameters: " + ', '.join(simulatorArgs))
        args = [self.simulatorPath]
        args.extend(simulatorArgs)
        self.simulatorProc = subprocess.Popen(args=args, shell=False, stdout=subprocess.PIPE, env=self.ENV)

    def closeSimulator(self):
        print("Closing PLC server simulator!")
        self.simulatorProc.kill()
        self.simulatorProc.wait(timeout=5)

    def startIOC(self):
        print("Setting up test IOC!", flush=True)
        epics.ca.initialize_libca()
        if (self.plc == "Omron"):
            self.ENV["PLC"] = "omron-njnx"
        elif (self.plc == "ControlLogix"):
            self.ENV["PLC"] = "ControlLogix"
        else:
            print("Invalid PLC name supplied!")
        chdir(self.IOC_TOP)
        self.iocProc = subprocess.Popen([self.IOC_EXECUTABLE, self.IOC_CMD], text=True, shell=False, env=self.ENV)
        time.sleep(3)
        print("")

    def closeIOC(self):
        print("Closing IOC")
        epics.ca.flush_io()
        epics.ca.clear_cache()
        epics.ca.finalize_libca()
        self.iocProc.kill()
        self.iocProc.wait(timeout=5)

    def readPV(self, pvName):
        pv = epics.PV(pvName)
        status = pv.wait_for_connection(5)
        if (status == True):
            val = pv.get()
            print(f"Read value={val} from simulator")
        else:
            print("Error, could not find PV "+pvName)
            val = None
        return val

    def writePV(self, pvName, val):
        pv = epics.PV(pvName, connection_timeout=5)
        status = pv.wait_for_connection(5)
        if (status == True):
            print(f"Writing value={val} to simulator")
            pv.put(val, wait=True)
        else:
            print("Error, could not find PV "+pvName)


