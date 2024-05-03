import testSetup
import os
import unittest
import epics
import time, inspect
import argparse

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
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]', '--debug'])
        self.testOmronEIP.startIOC()
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
        self.testOmronEIP.startIOC()
        writeVal = 5
        self.testOmronEIP.writePV("omronEIP:writeInt16",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt16")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: self.errorList.append(str(e))
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

    def test_negative_int16(self):
        #Test reading and writing a negative int16 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------", flush=True)
        self.testOmronEIP.startSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]'])
        self.testOmronEIP.startIOC()
        writeVal = -22
        readVal = 0
        self.testOmronEIP.writePV("omronEIP:writeInt16",writeVal)
        time.sleep(1)
        readVal = self.testOmronEIP.readPV("omronEIP:readInt16")
        self.testOmronEIP.closeSimulator()
        self.testOmronEIP.closeIOC()
        try: self.assertTrue(writeVal==readVal)
        except AssertionError as e: self.errorList.append(str(e))
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------\n")

if __name__ == '__main__':
    args = options()
    newTest = TestDriver()
    TestDriver.setUp(newTest, args.simulator, args.omroneip, args.plc)
    TestDriver.test_test_setup(newTest)
    TestDriver.test_int16(newTest)
    TestDriver.test_negative_int16(newTest)
    TestDriver.tearDown(newTest)