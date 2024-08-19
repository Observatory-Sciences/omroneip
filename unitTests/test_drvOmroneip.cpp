/* This is the "main" file for the boost test framework which allows tests to span multiple files*/
#include "drvOmroneipWrapper.h"
#include "omronUtilitiesWrapper.h"
#include <boost/test/unit_test.hpp>

class drvOmroneipTestFixture
{
    public:
        const char* gateway = "18,10.2.2.57";
        const char* path = "10.2.2.57";
        const char* plcType = "omron-njnx";
        const char* structDefsFile = "./unitTests/structDefs.csv";
        int libplctagDebugLevel = 0;
        double timezoneOffset = 0;
        drvOmronEIPWrapper * testDriver;
        omronUtilitiesWrapper * testUtilities;
        std::string dummy_port;
        std::string tagConnectionString_ = "protocol=ab-eip&gateway=" + (std::string)gateway + "&path=" + (std::string)path + "&plc=" + (std::string)plcType;
        drvOmroneipTestFixture()
        {
            std::cout << "-----------Starting " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            dummy_port = ("omronDriver");
            uniqueAsynPortName(dummy_port);
            testDriver = new drvOmronEIPWrapper(dummy_port.c_str(),path,gateway,plcType,libplctagDebugLevel,timezoneOffset);
            testUtilities = new omronUtilitiesWrapper(testDriver);
            testDriver->wrap_createPoller(dummy_port.c_str(),"testPoller",1,0);
            testDriver->wrap_setAsynTrace(0x0021);
        }
        ~drvOmroneipTestFixture()
        {
            std::cout<<"-----------Cleaning up after " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            delete testDriver;
            delete testUtilities;
        }

        void uniqueAsynPortName(std::string& name)
        {
            // Asyn doesnt shut down properly when we destroy our driver, this means that the asyn port is still registered when we make the next
            // instance of the driver. Therefor we must create a unique port name.
            static unsigned long counter = 99999999;
            std::stringstream ss;
            ss << name << "_" << counter;
            name = ss.str();
            counter++;
        }
};

BOOST_FIXTURE_TEST_SUITE(optimisationTests, drvOmroneipTestFixture)

BOOST_AUTO_TEST_CASE(test_doNothing)
{
    //testDriver->tagMap_
    BOOST_CHECK_EQUAL(10, 10);
}

BOOST_AUTO_TEST_SUITE_END()