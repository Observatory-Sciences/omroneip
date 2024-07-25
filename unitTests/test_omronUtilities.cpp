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
        const char* structDefsFile = "./unitTests/structDefs.csv";
        int libplctagDebugLevel = 0;
        double timezoneOffset = 0;
        drvOmronEIPWrapper * testDriver;
        omronUtilitiesWrapper * testUtilities;
        std::string dummy_port;
        drvInfoMap keyWords;
        std::string tagConnectionString_ = "protocol=ab-eip&gateway=" + (std::string)gateway + "&path=" + (std::string)path + "&plc=" + (std::string)plcType;
        omronUtilitiesTestFixture()
        {
            std::cout << "-----------Starting " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            dummy_port = ("omronDriver");
            uniqueAsynPortName(dummy_port);
            initKeywords(keyWords);
            testDriver = new drvOmronEIPWrapper(dummy_port.c_str(),path,gateway,plcType,libplctagDebugLevel,timezoneOffset);
            testUtilities = new omronUtilitiesWrapper(testDriver);
            testDriver->wrap_createPoller(dummy_port.c_str(),"testPoller",1,0);
            testDriver->wrap_setAsynTrace(0x0021);
        }
        ~omronUtilitiesTestFixture()
        {
            std::cout<<"-----------Cleaning up after " << boost::unit_test::framework::current_test_case().p_name << "-----------" << std::endl;
            delete testDriver;
            delete testUtilities;
        }

        void uniqueAsynPortName(std::string& name)
        {
            // Asyn doesnt shut down properly when we destroy our driver, this means that the asyn port is still registered when we make the next
            // instance of the driver. Therefor we must create a unique port name.
            static unsigned long counter = 0;
            std::stringstream ss;
            ss << name << "_" << counter;
            name = ss.str();
            counter++;
        }

        void initKeywords(drvInfoMap &keyWords){
            keyWords = {
                {"pollerName", "none"}, // optional
                {"tagName", "none"},  
                {"dataType", "none"}, 
                {"startIndex", "1"},   
                {"sliceSize", "1"},     
                {"offset", "0"},       
                {"tagExtras", "none"},
                {"strCapacity", "0"}, // only needed for getting strings from UDTs  
                {"optimisationFlag", "not requested"}, // stores the status of optimisation, ("not requested", "attempt optimisation","dont optimise","optimisation failed","optimised","master")
                {"stringValid", "true"}, // set to false if errors are detected which aborts creation of tag and asyn parameter, return early if false
                {"offsetReadSize", "0"},
                {"readAsString", "0"}, // currently just used to optionally output the TIME dtypes as user friendly strings in the local timezone
                {"optimise", "0"} // if 0 then we use the offset to look within a datatype, if 1 then we use it to get a datatype from within an array/UDT
            };
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

/* drvInfoParser tests */

BOOST_AUTO_TEST_CASE(test_negative_drvInfoParser_Empty)
{
    std::string drvInfo = "";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    free(newDrvUser);
}

//Specifying slice size when not accessing an array
BOOST_AUTO_TEST_CASE(test_negative_drvInfoParser_BadSlice)
{
    std::string drvInfo = "@testPoller lwordArray LWORD 10 none none";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_negative_drvInfoParser_BadPoller)
{
    std::string drvInfo = "@badPoller lwordArray[1] LWORD 10 none none";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(newDrvUser->pollerName,"none");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_negative_drvInfoParser_BadOffset)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 1 -1 none";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(newDrvUser->tagOffset, 0);
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoParser_AllExtras)
{
    std::string drvInfo = "@testPoller testString STRING none none &allow_packing=0&str_is_zero_terminated=1&str_is_fixed_length=1"
                            "&str_is_counted=0&str_count_word_bytes=0&str_pad_to_multiple_bytes=2&str_max_capacity=100&optimise=1"
                                "&offset_read_size=5";
    std::cout << "Test string: " << drvInfo << std::endl;
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

BOOST_AUTO_TEST_CASE(test_drvInfoParser_DisablePacking)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none &allow_packing=0";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(keyWords.at("tagExtras"), "&allow_packing=0");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoParser_DefaultExtras)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none none";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(keyWords.at("tagExtras"), "&allow_packing=1");
    free(newDrvUser);
}

BOOST_AUTO_TEST_CASE(test_drvInfoParser_DefaultExtrasString)
{
    std::string drvInfo = "@testPoller testString STRING none none none";
    std::cout << "Test string: " << drvInfo << std::endl;
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

BOOST_AUTO_TEST_CASE(test_drvInfoParser_ArraySlice)
{
    std::string drvInfo = "@testPoller lwordArray[1] LWORD 10 none none";
    std::cout << "Test string: " << drvInfo << std::endl;
    const auto [stringValid, newDrvUser, keyWords] = parser(drvInfo);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(newDrvUser->sliceSize, 10);
    free(newDrvUser);
}

/* checkValidName tests */

BOOST_AUTO_TEST_CASE(test_negative_checkValidName_NegativeInt)
{
    std::string str = "lwordArray[-2]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(startIndex, "1");
    BOOST_CHECK_EQUAL(indexable, true);
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidName_LetterInBrackets)
{
    std::string str = "lwordArray[x]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(startIndex, "1");
    BOOST_CHECK_EQUAL(indexable, true);
}


BOOST_AUTO_TEST_CASE(test_negative_checkValidName_NoClosingBracket)
{
    std::string str = "lwordArray[4";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
    BOOST_CHECK_EQUAL(startIndex, "1");
    BOOST_CHECK_EQUAL(indexable, true);
}

BOOST_AUTO_TEST_CASE(test_checkValidName_NoOpeningBracket)
{
    std::string str = "lwordArray4]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(startIndex, "1");
    BOOST_CHECK_EQUAL(indexable, false);
}

BOOST_AUTO_TEST_CASE(test_checkValidName_Float)
{
    std::string str = "lwordArray[5.234]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"true");
    //Normally the driver converts to int and should deal with floats like this, it may be better to report 
    BOOST_CHECK_EQUAL(std::stoi(startIndex), 5);
    BOOST_CHECK_EQUAL(indexable, true);
}

BOOST_AUTO_TEST_CASE(test_checkValidName_ValidInt)
{
    std::string str = "lwordArray[5]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, startIndex, indexable] = testUtilities->wrap_checkValidName(str);
    BOOST_CHECK_EQUAL(stringValid,"true");
    BOOST_CHECK_EQUAL(startIndex, "5");
    BOOST_CHECK_EQUAL(indexable, true);
}

/* checkValidDtype tests */

BOOST_AUTO_TEST_CASE(test_checkValidDtype_ValidDtype)
{
    std::string str = "TIME";
    std::cout << "Test string: " << str << std::endl;
    std::string stringValid = testUtilities->wrap_checkValidDtype(str);
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidDtype_InValidDtype)
{
    std::string str = "PENCIL";
    std::cout << "Test string: " << str << std::endl;
    std::string stringValid = testUtilities->wrap_checkValidDtype(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidDtype_InValidDtype2)
{
    std::string str = "-100.2";
    std::cout << "Test string: " << str << std::endl;
    std::string stringValid = testUtilities->wrap_checkValidDtype(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidDtype_InValidDtype3)
{
    std::string str = "int";
    std::cout << "Test string: " << str << std::endl;
    std::string stringValid = testUtilities->wrap_checkValidDtype(str);
    BOOST_CHECK_EQUAL(stringValid,"false");
}

/* checkValidSliceSize tests */

BOOST_AUTO_TEST_CASE(test_checkValidSliceSize_ValidSize)
{
    std::string str = "123";
    bool indexable = true;
    std::string dtype = "REAL";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"123");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidSliceSize_NotIndexable)
{
    std::string str = "123";
    bool indexable = false;
    std::string dtype = "REAL";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidSliceSize_NonIndexableDtype)
{
    std::string str = "123";
    bool indexable = true;
    std::string dtype = "LINT";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_checkValidSliceSize_NonSliceableDtype)
{
    std::string str = "1";
    bool indexable = true;
    std::string dtype = "LINT";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidSliceSize_NonSliceableDtype2)
{
    std::string str = "1";
    bool indexable = false;
    std::string dtype = "LINT";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidSliceSize_NonIntIndexable)
{
    std::string str = "pineapple";
    bool indexable = true;
    std::string dtype = "REAL";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_checkValidSliceSize_NonIntNonIndexable)
{
    std::string str = "pineapple";
    bool indexable = false;
    std::string dtype = "REAL";
    std::cout << "Test string: " << str << " Test indexable: " << indexable << " Test dtype: " << dtype << std::endl;
    const auto [stringValid, sliceSize] = testUtilities->wrap_checkValidSliceSize(str,indexable,dtype);
    BOOST_CHECK_EQUAL(sliceSize,"1");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

/* checkValidOffsets tests */

BOOST_AUTO_TEST_CASE(test_checkValidOffset_Valid)
{
    std::string str = "553";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"553");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidOffset_ValidBig)
{
    std::string str = "55123123";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"55123123");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidOffset_NestedStruct)
{
    std::string str = "UDT800_Zone[1][42][1]";
    std::cout << "Test string: " << str << std::endl;
    testDriver->wrap_setAsynTrace(0x00FF);
    testDriver->loadStructFile(dummy_port.c_str(), structDefsFile);
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"192");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidOffset_NestedStruct2)
{
    std::string str = "sPSU[14][5]";
    std::cout << "Test string: " << str << std::endl;
    testDriver->wrap_setAsynTrace(0x00FF);
    testDriver->loadStructFile(dummy_port.c_str(), structDefsFile);
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"192");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidOffset_TooBig)
{
    std::string str = "2345321424325235";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"0");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidOffset_Negative)
{
    std::string str = "-54";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"0");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidOffset_String)
{
    std::string str = "chicken pizza";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"0");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

BOOST_AUTO_TEST_CASE(test_negative_checkValidOffset_BadStructIndex)
{
    std::string str = "sPSU[14[5]";
    std::cout << "Test string: " << str << std::endl;
    const auto [stringValid, offset] = testUtilities->wrap_checkValidOffset(str);
    BOOST_CHECK_EQUAL(offset,"0");
    BOOST_CHECK_EQUAL(stringValid,"false");
}

/* checkValidExtras tests */

BOOST_AUTO_TEST_CASE(test_checkValidExtras_Empty)
{
    std::string str = "";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "REAL";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&allow_packing=1");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_DisallowPacking)
{
    std::string str = "&allow_packing=0";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "REAL";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&allow_packing=0");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_NoAnd)
{
    std::string str = "allow_packing=0";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "REAL";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&allow_packing=0");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_InvalidAttr)
{
    //We dont check whether the attribute or value is valid, we let libplctag do this when the tag is created
    std::string str = "&iamimaginary=fish";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "REAL";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&iamimaginary=fish&allow_packing=1");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_ReadAsString)
{
    std::string str = "&read_as_string=1";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "TIME";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&allow_packing=1");
    BOOST_CHECK_EQUAL(keyWords.at("readAsString"),"1");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_ReadAsString2)
{
    //We request this extra for a "REAL" datatype which is not valid
    std::string str = "&read_as_string=1";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "REAL";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(extrasString,"&read_as_string=1&allow_packing=1");
    BOOST_CHECK_EQUAL(keyWords.at("readAsString"),"0");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_CASE(test_checkValidExtras_AllExtras)
{
    std::string str = "&allow_packing=0&str_is_zero_terminated=1&str_is_fixed_length=1"
                            "&str_is_counted=0&str_count_word_bytes=0&str_pad_to_multiple_bytes=2&str_max_capacity=100&optimise=1"
                                "&offset_read_size=5";
    std::cout << "Test string: " << str << std::endl;
    keyWords.at("dataType") = "STRING";
    const auto [stringValid, extrasString] = testUtilities->wrap_checkValidExtras(str,keyWords);
    BOOST_CHECK_EQUAL(keyWords.at("strCapacity"),"100");
    BOOST_CHECK_EQUAL(keyWords.at("optimise"),"1");
    BOOST_CHECK_EQUAL(keyWords.at("offsetReadSize"),"5");
    BOOST_CHECK_EQUAL(stringValid,"true");
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(importStructDefsTests, omronUtilitiesTestFixture)

BOOST_AUTO_TEST_CASE(test_importFile)
{
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()