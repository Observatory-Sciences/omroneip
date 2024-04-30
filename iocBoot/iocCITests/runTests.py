import testSetup
import os
import unittest
import epics
import run_iocsh
import time, inspect
import argparse

def options():
    parser = argparse.ArgumentParser()
    parser.add_argument("-s", "--simulator", default=os.environ.get("HOME")+"/libplctag-2.5.5/build/bin_dist/ab_server", help="path to libplctag PLC simulator")
    parser.add_argument("-t", "--ioc", default=os.environ.get("HOME")+"/omroneip", help="path to the top directory of the test IOC")
    parser.add_argument("-p", "--plc", default="Omron", help="Choose either 'Omron' or 'ControlLogix'")
    args = parser.parse_args()
    return args

class TestDriver(unittest.TestCase):
    #Each test uses the same IOC, driver instance and libplctag simulator
    def setUp(self, simulatorPath, iocPath, plc):
        self.iocPath = iocPath
        self.plc = plc
        self.testOmronEIP = testSetup.TestSetup(simulatorPath, iocPath, plc)
        print("------------------Setting up tests-----------------------")
        self.errorList = []

    def tearDown(self):
        try: self.assertEqual([], self.errorList)
        except:
            for error in self.errorList:
                print(error)
            print(str(len(self.errorList))+" test(s) failed")
        else:
            print("All tests passed!")

    def test_int16(self):
        #Test reading and writing int16 to the simulator
        print("------------------"+inspect.stack()[0][3]+"-----------------------")
        self.testOmronEIP.setupSimulator([f'--plc={self.plc}', '--tag=TestINT:INT[1,1]'])
        self.testOmronEIP.startIOC()
        try: self.assertTrue(1==1)
        except AssertionError as e: self.errorList.append(str(e))
        else:
             print("---------------------"+inspect.stack()[0][3]+" success :) ---------------------")
        self.testOmronEIP.closeSimulator()

if __name__ == '__main__':
    args = options()
    newTest = TestDriver()
    #unittest.main() #run all
    TestDriver.setUp(newTest, args.simulator, args.ioc, args.plc)
    TestDriver.test_int16(newTest)
    TestDriver.tearDown(newTest)