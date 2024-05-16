import testSetup
import os
import unittest
import epics
import time, inspect
import argparse
import numpy as np

def options():
    parser = argparse.ArgumentParser()
    parser.add_argument("-s", "--simulator", default=os.environ.get("HOME")+"/libplctag-2.5.5/build/bin_dist/ab_server", help="path to libplctag PLC simulator")
    parser.add_argument("-o", "--omroneip", default=os.getcwd(), help="path to the top directory of the omroneip driver")
    parser.add_argument("-p", "--plc", default="Omron", help="Choose either 'Omron' or 'ControlLogix'")
    args = parser.parse_args()
    return args

class TestDriver(unittest.TestCase):
    #Each test uses the same IOC, driver instance and libplctag simulator
    def setUp(self, simulatorPath, omroneipPath, plc):
        print("------------------Setting up tests-----------------------\n")
        self.omroneipPath = omroneipPath
        self.plc = plc
        self.testOmronEIP = testSetup.TestSetup(simulatorPath, omroneipPath, plc)
        self.errorList = []
        print("\n")

    def tearDown(self):
        epics.ca.flush_io()
        epics.ca.clear_cache()
        epics.ca.finalize_libca()
        try: self.assertEqual([], self.errorList)
        except:
            for error in self.errorList:
                print(error)
            print(str(len(self.errorList))+" test(s) failed")
            exit(1)
        else:
            print("All tests passed!")

    def test_test_setup(self):
        #Testing simulator and ioc startup and shutdown
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]'])
        self.testOmronEIP.startIOC("testInt.iocsh")
        time.sleep(1)
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        self.testOmronEIP.ioc.check_output()
        output = self.testOmronEIP.ioc.outs
        print("IOC stdout:\n"+output)
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)

    def test_int16(self):
        #Test reading and writing int16 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]'])
        self.testOmronEIP.startIOC("testInt.iocsh")
        writeVal = 5
        self.testOmronEIP.writePV("omronEIP:writeInt16",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt16")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

    def test_negative_int16(self):
        #Test reading and writing a negative int16 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]'])
        self.testOmronEIP.startIOC("testInt.iocsh")
        writeVal = -22
        readVal = 0
        self.testOmronEIP.writePV("omronEIP:writeInt16",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt16")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")
            
    def test_int32(self):
        #Test reading and writing int32 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestDINT:DINT[1,1]'])
        self.testOmronEIP.startIOC("testDInt.iocsh")
        writeVal = 99999
        self.testOmronEIP.writePV("omronEIP:writeInt32",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt32")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

    def test_int64(self):
        #Test reading and writing int64 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestLINT:LINT[1,1]'])
        self.testOmronEIP.startIOC("testLInt.iocsh")
        writeVal = -200000000001
        self.testOmronEIP.writePV("omronEIP:writeInt64",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt64")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")
    
    def test_float32(self):
        #Test reading and writing float32 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestREAL:REAL[1,1]'])
        self.testOmronEIP.startIOC("testReal.iocsh")
        writeVal = np.float32(-12.580238505)
        self.testOmronEIP.writePV("omronEIP:writeFloat32",writeVal)
        time.sleep(1)
        readVal = np.float32(self.testOmronEIP.readPV("omronEIP:readFloat32"))
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

    def test_float64(self):
        #Test reading and writing float64 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestLREAL:LREAL[1,1]'])
        self.testOmronEIP.startIOC("testLReal.iocsh")
        writeVal = 1.23456789e-302
        self.testOmronEIP.writePV("omronEIP:writeFloat64", float(writeVal))
        time.sleep(1)
        readVal = float(self.testOmronEIP.readPV("omronEIP:readFloat64"))
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

    def test_string(self):
        #Test reading and writing a string to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestSTRING:STRING[1,1]'])
        self.testOmronEIP.startIOC("testString.iocsh")
        writeVal = "hello!"
        self.testOmronEIP.writePV("omronEIP:writeString",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readString")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        output = self.testOmronEIP.ioc.errs
        print("IOC stderr:\n"+output)
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: 
            self.errorList.append(str(e))
            print("---------------------"+inspect.stack()[0][3]+" fail :( ---------------------\n")
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

if __name__ == '__main__':
    args = options()
    newTest = TestDriver()
    TestDriver.setUp(newTest, args.simulator, args.omroneip, args.plc)
    TestDriver.test_test_setup(newTest)
    TestDriver.test_int16(newTest)
    TestDriver.test_negative_int16(newTest)
    TestDriver.test_int32(newTest)
    TestDriver.test_int64(newTest)
    TestDriver.test_float32(newTest)
    TestDriver.test_float64(newTest)
    TestDriver.test_string(newTest)
    TestDriver.tearDown(newTest)