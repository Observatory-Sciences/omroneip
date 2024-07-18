/* This is the "main" file for the boost test framework which allows tests to span multiple files*/
#include "drvOmroneipWrapper.h"
#include "omronUtilitiesWrapper.h"
#include <boost/test/unit_test.hpp>

class omronUtilitiesTestFixture
{
    public:
        const char* gateway = "18,10.2.2.57";
        const char* path = "10.2.2.57";
        const char* plcType = "omron-njnx";
        int libplctagDebugLevel = 0;
        double timezoneOffset = 0;
        drvOmronEIPWrapper * testDriver;
        omronUtilitiesWrapper * testUtilities;
        std::string tagConnectionString_ = "protocol=ab-eip&gateway=" + (std::string)gateway + "&path=" + (std::string)path + "&plc=" + (std::string)plcType;
        omronUtilitiesTestFixture()
        {
            std::cout << "-----------Starting " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            std::string dummy_port("omronDriver");
            uniqueAsynPortName(dummy_port);
            testDriver = new drvOmronEIPWrapper(dummy_port.c_str(),path,gateway,plcType,libplctagDebugLevel,timezoneOffset);
            testUtilities = new omronUtilitiesWrapper(testDriver);
            testDriver->wrap_createPoller(dummy_port.c_str(),"testPoller",1,0);
        }
        ~omronUtilitiesTestFixture()
        {
            std::cout<<"-----------Cleaning up after " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            delete testDriver;
            delete testUtilities;
        }

        void uniqueAsynPortName(std::string& name)
        {
            static unsigned long counter = 0;
            std::stringstream ss;
            ss << name << "_" << counter;
            name = ss.str();
            counter++;
        }

        std::tuple<std::string, omronDrvUser_t*, drvInfoMap> parser(std::string drvInfo)
        {
            drvInfoMap keyWords = testUtilities->wrap_drvInfoParser(drvInfo.c_str());
            omronDrvUser_t *newDrvUser = (omronDrvUser_t *)calloc(1, sizeof(omronDrvUser_t));
            asynUser *pasynUser = (asynUser *)calloc(1, sizeof(asynUser));
            pasynUser->timeout = 1;
            std::string tag = tagConnectionString_ + "&name=" + keyWords.at("tagName") +
                "&elem_count=" + keyWords.at("sliceSize") + keyWords.at("tagExtras");
            testDriver->wrap_initialiseDrvUser(newDrvUser, keyWords, 0, tag, true, pasynUser);
            std::string stringValid = keyWords.at("stringValid");
            free(pasynUser);
            return {stringValid,newDrvUser,keyWords};
        }
};

BOOST_FIXTURE_TEST_SUITE(drvInfoTests, omronUtilitiesTestFixture)

BOOST_AUTO_TEST_CASE(test_negative_drvInfoEmpty)
{
    std::string drvInfo = "";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    free(newDrvUser);
}

//Specifying slice size when not accessing an array
BOOST_AUTO_TEST_CASE(test_negative_drvInfobadSlice)
{
    std::string drvInfo = "@testPoller lwordArray LWORD 10 none none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_negative_drvInfoBadPoller)
{
    std::string drvInfo = "@badPoller lwordArray[1] LWORD 10 none none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(newDrvUser->pollerName,"none");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_negative_drvInfoBadOffset)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 1 -1 none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(newDrvUser->tagOffset, 0);
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoAllExtras)
{
    std::string drvInfo = "@testPoller testString STRING none none &allow_packing=0&str_is_zero_terminated=1&str_is_fixed_length=1"
                            "&str_is_counted=0&str_count_word_bytes=0&str_pad_to_multiple_bytes=2&str_max_capacity=100&optimise=1"
                                "&offset_read_size=5";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    std::string res = keyWords.at("tagExtras");
    std::cout << "Extras string: " << res << std::endl;
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(res.find("&allow_packing=0")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_zero_terminated=1")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_fixed_length=1")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_counted=0")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_count_word_bytes=0")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_pad_to_multiple_bytes=2")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_max_capacity=100")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&optimise=1")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&offset_read_size=5")!=res.npos, true);
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoDisablePacking)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none &allow_packing=0";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(keyWords.at("tagExtras"), "&allow_packing=0");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoDefaultExtras)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(keyWords.at("tagExtras"), "&allow_packing=1");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoDefaultExtrasString)
{
    std::string drvInfo = "@testPoller testString STRING none none none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    std::string res = keyWords.at("tagExtras");
    std::cout << "Extras string: " << res << std::endl;
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(res.find("&allow_packing=1")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_zero_terminated=0")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_fixed_length=0")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_is_counted=1")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_count_word_bytes=2")!=res.npos, true);
    BOOST_CHECK_EQUAL(res.find("&str_pad_to_multiple_bytes=0")!=res.npos, true);
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoArraySlice)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none none";
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(newDrvUser->sliceSize, 10);
    free(newDrvUser);
}

BOOST_AUTO_TEST_SUITE_END()