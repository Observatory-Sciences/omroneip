# Makefile for Asyn OmronNJ_EtherIP support
#
# Created by Phil Smith - Observatory Sciences ltd -  on Fri Jan 12 13:43:07 2024

TOP = .
include $(TOP)/configure/CONFIG

DIRS := $(DIRS) $(filter-out $(DIRS), configure)
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *[Aa]pp))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *[Ss]up))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard ioc[Bb]oot))
omroneipApp_DEPEND_DIRS = configure
iocBoot_DEPEND_DIRS = omroneipApp

include $(TOP)/configure/RULES_TOP
