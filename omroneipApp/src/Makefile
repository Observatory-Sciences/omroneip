# Makefile

TOP = ../..
include $(TOP)/configure/CONFIG

# Point the build system to libplctag's header and shared library files
# The name "plctag" must be used instead of "libplctag" or there will be problems!
#==================================================

$(info $$LIBPLCTAG_LIB is [${LIBPLCTAG_LIB}])

SRC_DIRS += $(LIBPLCTAG_LIB)
PROD_LIBS += plctag
LIB_LIBS += plctag
plctag_DIR = $(LIBPLCTAG_BUILD)

#==================================================
# Build an IOC support library

LIBRARY_IOC += omroneip
DBD += omroneipApp.dbd omroneip.dbd
INC += drvOmroneip.h omronUtilities.h
LIB_SRCS += drvOmroneip.cpp omronUtilities.cpp
LIB_LIBS += asyn $(EPICS_BASE_IOC_LIBS)

#=============================
# Build an IOC application which includes the omroneip library that we just defined

PROD_IOC += omroneipApp
omroneipApp_DBD += base.dbd asyn.dbd omroneip.dbd
# omroneipApp_registerRecordDeviceDriver.cpp will be created from omroneipApp.dbd
omroneipApp_SRCS_DEFAULT += omroneipApp_registerRecordDeviceDriver.cpp omroneipMain.cpp
PROD_LIBS += omroneip asyn $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
