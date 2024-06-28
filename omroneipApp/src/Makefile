# Makefile

TOP = ../..
include $(TOP)/configure/CONFIG

# Point the build system to libplctags header and shared library files
# The name "plctag" must be used instead of "libplctag" or there will be problems!
#==================================================

$(info $$LIBPLCTAG_LIB is [${LIBPLCTAG_LIB}])

SRC_DIRS += $(LIBPLCTAG_LIB)
PROD_LIBS += plctag
LIB_LIBS += plctag
plctag_DIR = $(LIBPLCTAG_BUILD)

#==================================================
# Build an IOC support library

# <name>.dbd will be created from <name>Include.dbd
DBD += omroneipApp.dbd
DBD += omroneipSupport.dbd

INC += drvOmroneip.h

LIBRARY_IOC += omroneip

USR_CFLAGS += -DUSE_TYPED_RSET
USR_CPPFLAGS += -DUSE_TYPED_RSET

LIB_SRCS += drvOmroneip.cpp utilityFunctions.cpp
LIB_LIBS += asyn 
LIB_LIBS += $(EPICS_BASE_IOC_LIBS)
LIB_SYS_LIBS_WIN32 += ws2_32

#=============================
# build an ioc application

PROD_IOC += omroneipApp
omroneipApp_DBD += base.dbd
omroneipApp_DBD += asyn.dbd drvAsynIPPort.dbd drvAsynSerialPort.dbd
omroneipApp_DBD += omroneipSupport.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
omroneipApp_SRCS_DEFAULT += omroneipApp_registerRecordDeviceDriver.cpp omroneipMain.cpp
omroneipApp_SRCS_vxWorks += omroneipApp_registerRecordDeviceDriver.cpp

PROD_LIBS += omroneip
PROD_LIBS += asyn
PROD_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_SYS_LIBS_solaris += socket

#
include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
