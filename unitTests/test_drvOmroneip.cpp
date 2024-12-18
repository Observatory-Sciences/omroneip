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
            static unsigned long counter = 5000;
            std::stringstream ss;
            ss << name << "_" << counter;
            name = ss.str();
            counter++;
        }
        
};

BOOST_FIXTURE_TEST_SUITE(optimisationTests, drvOmroneipTestFixture)
// These tests require a PLC programmed with an array of structures called aPSU which contains 140 structures
// Each structure should be 232 bytes big
// These tests sometimes seg fault as I dont properly create the asynUser.

BOOST_AUTO_TEST_CASE(test_optimiseTags_arrayOptSimple)
{
    // We create fake drvUseres and add to tagMap_
    // Asyn parameter does not get made correctly, but it doesnt matter for our purposes
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    drvInfo = "@testPoller aPSU[1] REAL 10 0 &optimise=1";
    testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    drvInfo = "@testPoller aPSU[1] REAL 10 100 &optimise=1";
    testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    asynStatus status = testDriver->wrap_optimiseTags();
    BOOST_CHECK_EQUAL(status, asynSuccess);
}

BOOST_AUTO_TEST_CASE(test_optimiseTags_arrayOptBig)
{
    // We create fake drvUseres and add to tagMap_
    // Asyn parameter does not get made correctly, but it doesnt matter for our purposes
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    for (size_t i = 1; i<=136; i++)
    {
        //We use i to offset just so that the tags are all unique, as otherwise this would stop duplicate tags being created
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
        //A second tag reading the same struct to make optimisation work
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i+1)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    }
    asynStatus status = testDriver->wrap_optimiseTags();

    //Test offsets are correct
    for (size_t i = 1; i<=8; i++)
    {
        omronDrvUser_t* drvUser = testDriver->getDrvUser(2*(i-1));
        size_t expectedOffset = i+224*(i-1);
        BOOST_CHECK_EQUAL(drvUser->tagOffset, expectedOffset);
        if (i==1){
            BOOST_CHECK_EQUAL(drvUser->optimisationFlag, "master");
            BOOST_CHECK_EQUAL(drvUser->readFlag, true);
        }
        else {
            BOOST_CHECK_EQUAL(drvUser->optimisationFlag, "optimised");
            BOOST_CHECK_EQUAL(drvUser->readFlag, false);
        }
    }
    BOOST_CHECK_EQUAL(status, asynSuccess);
}

BOOST_AUTO_TEST_CASE(test_optimiseTags_arrayOptIrregular)
{
    //Rather than getting every element, we get one in four elements, but these should still be packed into slices of size 8
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    for (size_t i = 1; i<=136; i+=4)
    {
        //We use i to offset just so that the tags are all unique, as otherwise this would stop duplicate tags being created
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
        //A second tag reading the same struct to make optimisation work
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i+1)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    }
    asynStatus status = testDriver->wrap_optimiseTags();

    //Test offsets are correct
    omronDrvUser_t* drvUser = testDriver->getDrvUser(0);
    size_t expectedOffset = 1;
    BOOST_CHECK_EQUAL(drvUser->tagOffset, expectedOffset);
    BOOST_CHECK_EQUAL(drvUser->optimisationFlag, "master");
    BOOST_CHECK_EQUAL(drvUser->readFlag, true);
    drvUser = testDriver->getDrvUser(2);
    expectedOffset = 901;
    BOOST_CHECK_EQUAL(drvUser->tagOffset, expectedOffset);
    BOOST_CHECK_EQUAL(drvUser->optimisationFlag, "optimised");
    BOOST_CHECK_EQUAL(drvUser->readFlag, false);
    
    BOOST_CHECK_EQUAL(status, asynSuccess);
}

BOOST_AUTO_TEST_CASE(test_optimiseTags_unoptomisableArray)
{
    // We read 1 in 9 elements, the driver still reads the array slices, but it is not efficient
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    for (size_t i = 1; i<=136; i+=9)
    {
        //We use i to offset just so that the tags are all unique, as otherwise this would stop duplicate tags being created
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
        //A second tag reading the same struct to make optimisation work
        drvInfo = "@testPoller aPSU["+std::to_string(i)+"] REAL 10 "+std::to_string(i+1)+" &optimise=1";
        testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    }
    asynStatus status = testDriver->wrap_optimiseTags();

    BOOST_CHECK_EQUAL(status, asynSuccess);
}

BOOST_AUTO_TEST_CASE(test_negative_optimiseTags_oneTagOnly)
{
    // We need two tags to read from the structure for it to be worthwhile, so this optimisation will fail
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    drvInfo = "@testPoller aPSU[1] REAL 10 none &optimise=1";
    testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    asynStatus status = testDriver->wrap_optimiseTags();
    BOOST_CHECK_EQUAL(status, asynError);
}

BOOST_AUTO_TEST_CASE(test_optimiseTags_noOptimisations)
{
    // We have two optimisable tags, but the user has not requisted optimisation
    testDriver->wrap_setAsynTrace(0x0031);
    asynUser *pAsynUser = (asynUser *)calloc(1, sizeof(asynUser));
    std::string drvInfo;
    drvInfo = "@testPoller aPSU[1] REAL 1 1 &optimise=0";
    testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    drvInfo = "@testPoller aPSU[1] REAL 1 2 &optimise=0";
    testDriver->wrap_drvUserCreate(pAsynUser, drvInfo.c_str());
    asynStatus status = testDriver->wrap_optimiseTags();
    BOOST_CHECK_EQUAL(status, asynSuccess);
}

BOOST_AUTO_TEST_SUITE_END()