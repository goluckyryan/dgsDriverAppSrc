TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================

# Build an IOC support library
USR_CFLAGS_vxWorks-ppc604_long += -DMV5500
USR_CFLAGS_vxWorks-ppc604 += -DRIO3 \
        -I/remote/devel/vxWorks/Tornado2.2/target/config/rio3





HOST_OPT = NO

DBEXPAND = msi

#=============================
# build an ioc application
#=============================

# JTA: I interpret this to mean that everything that is compiled into devDGSDriverSupport by inclusion
# below will get built into a library for inclusion in a bigger compile.
LIBRARY_IOC_vxWorks += devDGSDriverSupport

#
#	Enumerate source files in this build.
#
#
#	the "Params" files are understood to be written by the code generating spreadsheets
#	that 
#
#	The asynXXXXXXX.cpp files used #include to pull in the asynXXXX.h files.
#	The asynXXXX.h files then #include the 'params.h' files.  The asynXXXX.cpp
#	files do a #include of the matching 'params.c' file to build a subroutine with
#	a replaceable number of calls to EPICS functions.

#-rw-rw-r--   1 dgs      dgs        44622 May 26 12:21 asynDigParams.c
#-rw-rw-r--   1 dgs      dgs        85711 May 26 12:21 asynMTrigParams.c
#-rw-rw-r--   1 dgs      dgs        37701 May 26 12:21 asynRTrigParams.c
# -rw-rw-r--   1 dgs      dgs        12709 May 26 12:21 asynDigParams.h
# -rw-rw-r--   1 dgs      dgs        24907 May 26 12:21 asynMTrigParams.h
# -rw-rw-r--   1 dgs      dgs         9176 May 26 12:21 asynRTrigParams.h


devDGSDriverSupport_SRCS += drvAsynDebug.c
devDGSDriverSupport_SRCS += drvAsynDigitizer.c
devDGSDriverSupport_SRCS += drvAsynTrigMaster.c
devDGSDriverSupport_SRCS += drvAsynTrigRouter.c
### devDGSDriverSupport_SRCS += equalSub.c   <=== JTA 20250421: removed
devDGSDriverSupport_SRCS += QueueManagement.c
devDGSDriverSupport_SRCS += readDigFIFO.c
devDGSDriverSupport_SRCS += readTrigFIFO.c
###  devDGSDriverSupport_SRCS += restoreSub.c  <=== MBO 20220729: Removed
devDGSDriverSupport_SRCS += vmeDriverMutex.c
devDGSDriverSupport_SRCS += devGVME.c
### devDGSDriverSupport_SRCS += devGData.c   <=== JTA 20250421: removed
devDGSDriverSupport_SRCS += inLoopSupport.c
devDGSDriverSupport_SRCS += outLoopSupport.c
devDGSDriverSupport_SRCS += profile.c
devDGSDriverSupport_SRCS += SendReceiveSupport.c
###  devDGSDriverSupport_SRCS += FlashMaintenance.c	  <=== JTA 20231005: removed
devDGSDriverSupport_SRCS += asynDebugDriver.cpp
devDGSDriverSupport_SRCS += asynDigitizerDriver.cpp
devDGSDriverSupport_SRCS += asynTrigCommonDriver.cpp 
devDGSDriverSupport_SRCS += asynTrigMasterDriver.cpp
devDGSDriverSupport_SRCS += asynTrigRouterDriver.cpp
devDGSDriverSupport_SRCS += inLoop.st
devDGSDriverSupport_SRCS += outLoop.st
devDGSDriverSupport_SRCS += MiniSender.st

#=============================
# Parse dbd files
#=============================

PROD_IOC_vxWorks = dgsDriver

# dgsDriver.dbd will be created and installed
DBD += dgsDriver.dbd


#   #not local  dgsDriver_DBD += asyn.dbd
dgsDriver_DBD += dgsDriverSupport.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
dgsDriver_SRCS += dgsDriver_registerRecordDeviceDriver.cpp

# NOTE: To build SNL programs, SNCSEQ must be defined
# in the <top>/configure/RELEASE file

# ifneq ($(SNCSEQ),)
    # This builds sncExample as a component of gretDAQ
    # gretDAQ_DBD += sncExample.dbd


    # gretDAQ_SRCS += sncExample.stt
    # gretDAQ_LIBS += seq pv
#
    ## The following builds sncProgram as a standalone application
    # PROD_HOST += sncProgram
    # sncProgram_SNCFLAGS += +m
    # sncProgram_SRCS += sncProgram.st
    # sncProgram_LIBS += seq pv
    # sncProgram_LIBS += $(EPICS_BASE_HOST_LIBS)
# endif

# gretDAQ_LIBS += $(EPICS_BASE_IOC_LIBS)

dgsDrivers_LIBS += asyn devDGSDriverSupport

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

