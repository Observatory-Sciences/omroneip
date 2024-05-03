import unittest
import epics
from os import environ, chdir, getcwd
import subprocess
import argparse
import time
from run_iocsh import IOC

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
        epics.ca.initialize_libca()
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

        if (plc == "Omron"):
            self.ENV["PLC"] = "omron-njnx"
            self.ENV["PLC_GATEWAY"] = "18,127.0.0.1"
        elif (plc == "ControlLogix"):
            self.ENV["PLC"] = "ControlLogix"
            self.ENV["PLC_GATEWAY"] = "1,0"
        else:
            print("Invalid PLC name supplied!")

    def startSimulator(self, simulatorArgs):
        print("Setting up libplctag simulator with parameters: " + ', '.join(simulatorArgs))
        args = [self.simulatorPath]
        args.extend(simulatorArgs)
        if (self.plc == "ControlLogix"):
            args.append("--path=1,0")
        self.simulatorProc = subprocess.Popen(args=args, shell=False, stdout=subprocess.PIPE, env=self.ENV)
        if self.simulatorProc.returncode is not None:
            print("Simulator should be running, but is not!")

    def closeSimulator(self):
        print("Closing PLC server simulator!")
        self.simulatorProc.kill()
        self.simulatorProc.wait(timeout=5)
        if self.simulatorProc.returncode is None:
            print("Simulator failed to close!")

    def get_ioc(self):
        return IOC(ioc_executable=self.IOCSH_PATH)

    def startIOC(self, iocsh):
        print("Setting up test IOC!", flush=True)
        self.IOCSH_PATH = (f"{self.omroneipPath}/iocBoot/iocCITests/{iocsh}")
        chdir(self.IOC_TOP)
        self.ioc = self.get_ioc()
        self.ioc.start()
        time.sleep(1)
        assert self.ioc.is_running(), "Error, ioc not running!"

    def closeIOC(self):
        print("Closing IOC")
        epics.ca.flush_io()
        self.ioc.exit()
        time.sleep(1)
        assert not self.ioc.is_running(), "Error, ioc is still running!"

    def readPV(self, pvName):
        assert self.ioc.is_running(), "Error, ioc not running!"
        pv = epics.PV(pvName)
        status = pv.wait_for_connection(10)
        if (status == True):
            val = pv.get()
            print(f"Read value={val} from simulator")
        else:
            print("Error, could not find PV "+pvName)
            val = None
        return val

    def writePV(self, pvName, val):
        assert self.ioc.is_running(), "Error, ioc not running!"
        pv = epics.PV(pvName, connection_timeout=10)
        status = pv.wait_for_connection(10)
        if (status == True):
            print(f"Writing value={val} to simulator")
            pv.put(val, wait=True)
        else:
            print("Invalid PLC name supplied!")
