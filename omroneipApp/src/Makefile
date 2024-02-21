# Makefile

TOP = ../..
include $(TOP)/configure/CONFIG

#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================
SRC_DIRS += $(LIBPLCTAG_LIB)
USR_INCLUDES += -I$(LIBPLCTAG_LIB)
USR_DBDFLAGS += -I$(LIBPLCTAG_LIB)
PROD_LDFLAGS += -L$(LIBPLCTAG_BUILD)
PROD_SYS_LIBS += libplctag
#==================================================
# Build an IOC support library

# <name>.dbd will be created from <name>Include.dbd
DBD += omroneipApp.dbd
DBD += omroneipSupport.dbd

INC += omroneip.h
INC += drvOmroneip.h

LIBRARY_IOC += omroneip

USR_CFLAGS += -DUSE_TYPED_RSET
USR_CPPFLAGS += -DUSE_TYPED_RSET

LIB_SRCS += drvOmroneip.cpp
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
