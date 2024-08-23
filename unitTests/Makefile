
TOP = ..

include $(TOP)/configure/CONFIG

ifeq ($(USE_BOOST),YES)

# Point the build system to libplctag's header and shared library files
# The name "plctag" must be used instead of "libplctag" or there will be problems!
#==================================================


SRC_DIRS += $(LIBPLCTAG_LIB)
PROD_LIBS += plctag
plctag_DIR = $(LIBPLCTAG_BUILD)

#=============================
# Build an IOC application which includes the omroneip library that we just defined and also boost testing framework


boost_unit_test_framework_DIR = $(BOOST_LIB)
PROD_IOC += test_omroneip
test_omroneip_SRCS += test_omronUtilities.cpp test_drvOmroneip.cpp drvOmroneipWrapper.cpp omronUtilitiesWrapper.cpp

ifeq ($(PLC_CONNECTED),YES)
test_omroneip_SRCS += test_omroneip.cpp
endif

test_omroneip_LIBS += omroneip asyn $(EPICS_BASE_IOC_LIBS)

ifdef BOOST_LIB
boost_system_DIR = $(BOOST_LIB)
boost_unit_test_framework_DIR = $(BOOST_LIB)
test_omroneip_LIBS += boost_system boost_unit_test_framework
else
	PROD_SYS_LIBS += boost_system boost_unit_test_framework
endif

ifdef BOOST_INCLUDE
	USR_INCLUDES += $(addprefix -I, $(BOOST_INCLUDE))
endif

USR_CXXFLAGS_Linux += -O0 -std=c++17

all:
	$(info Directory is ./$(T_A))
	cp structDefs.csv ./$(T_A)

endif

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
