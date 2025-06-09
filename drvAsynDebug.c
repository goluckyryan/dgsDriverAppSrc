#ifdef vxWorks
#include <sysLib.h>
#include <vxWorks.h>
#include <tickLib.h>
#include <taskLib.h>
#include <semLib.h>
#endif

#include <stdio.h>
#include <stdlib.h>


#include "devLib.h"
#include "alarm.h"
#include "dbDefs.h"
#include "dbAccess.h"
#include "recGbl.h"
#include "recSup.h"
#include "devSup.h"
#include "mbbiDirectRecord.h"
#include "aoRecord.h"
#include "aiRecord.h"
#include "biRecord.h"
#include "mbbiRecord.h"
#include "mbboRecord.h"
#include "boRecord.h"
#include "epicsMutex.h"
#include "iocsh.h"
#include "epicsExport.h"
#include <registryFunction.h>


#include <errno.h>

#include <epicsString.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <cantProceed.h>
/* NOTE: This is needed for interruptAccept */
#include <dbAccess.h>

#define epicsExportSharedSymbols
#include <shareLib.h>



void asynGenReport(const char *cmd);

int asynDebugConfig(
	const char *portName, 
	int card_number);

/** Code for iocsh registration */
static const iocshArg asynDebugConfigArg0 = {"Port name", iocshArgString};
static const iocshArg asynDebugConfigArg1 = {"card_number", iocshArgInt};

static const iocshArg * const asynDebugConfigArgs[] =  {&asynDebugConfigArg0,
                                                          &asynDebugConfigArg1
								};
								
								
static const iocshFuncDef configasynDebug = {"asynDebugConfig", 2, asynDebugConfigArgs};						


static void configasynDebugCallFunc(const iocshArgBuf *args)
{
    printf("calling configasynDebugCallFunc\n");
    asynDebugConfig(args[0].sval, args[1].ival);



}
								

static const iocshArg asynGenReportArg0 = {"Command", iocshArgString};
static const iocshArg * const asynGenReportArgs[] =  {&asynDebugConfigArg0};

								
static const iocshFuncDef configaasynGenReport = {"debugGenReport", 1, asynGenReportArgs};								
								


static void asynGenReportCallFunc(const iocshArgBuf *args)
{

	printf("calling asynGenReportCallFunc\n" );
    asynGenReport(args[0].sval);



}



static void asynDebugRegister(void)
{

	printf("Running asynDebugRegister\n");
    iocshRegister(&configasynDebug, configasynDebugCallFunc);
    iocshRegister(&configaasynGenReport, asynGenReportCallFunc);
    
}

epicsExportRegistrar(asynDebugRegister);

