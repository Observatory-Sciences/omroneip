/* This is the "main" file for the boost test framework which allows tests to span multiple files*/
/* BOOST_TEST_MODULE tells boost to generate the main() function */
/* If boost is linked dynamically, then #define BOOST_USE_STATIC_LINK*/

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE "omroneip Tests"
#include <boost/test/unit_test.hpp>


