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

///  void asynDigReport(const char *cmd);    //removed JTA 20250421: redundant code.

/// int asynDigitizerConfig(const char *portName,int card_number,int slot,int clock_source);
int asynDigitizerConfig(const char *portName,int card_number,int slot);

/** Code for iocsh registration */
static const iocshArg asynDigitizerConfigArg0 = {"Port name", iocshArgString};
static const iocshArg asynDigitizerConfigArg1 = {"card_number", iocshArgInt};
static const iocshArg asynDigitizerConfigArg2 = {"slot", iocshArgInt};
// static const iocshArg asynDigitizerConfigArg3 = {"clock_source", iocshArgInt}; //removed JTA 20250421

static const iocshArg * const asynDigitizerConfigArgs[] =  {&asynDigitizerConfigArg0,
                                                          &asynDigitizerConfigArg1,
                                                          &asynDigitizerConfigArg2};
//                                                          &asynDigitizerConfigArg3};	 //removed JTA 20250421:
							
								
static const iocshFuncDef configasynDigitizer = {"asynDigitizerConfig", 3, asynDigitizerConfigArgs};	 //chgd 4 to 3 JTA 20250421

static void configasynDigitizerCallFunc(const iocshArgBuf *args)
{
    printf("calling configasynDigitizerCallFunc\n");
//    asynDigitizerConfig(args[0].sval, args[1].ival,args[2].ival,args[3].ival);	//JTA20250421
    asynDigitizerConfig(args[0].sval, args[1].ival,args[2].ival);
}
								
//removed 20250421 JTA: redundant code.
// static const iocshArg asynDigReportArg0 = {"Command", iocshArgString};
// static const iocshArg * const asynDigReportArgs[] =  {&asynDigitizerConfigArg0};
// static const iocshFuncDef configaasynDigReport = {"maddog", 1, asynDigReportArgs};								

// static void asynDigReportCallFunc(const iocshArgBuf *args)
// {
// 	printf("calling asynDigReportCallFunc\n" );
//     asynDigReport(args[0].sval);
// }

static void asynDigitizerRegister(void)
{
	printf("Running asynDigitizerRegister\n");
    iocshRegister(&configasynDigitizer, configasynDigitizerCallFunc);
//    iocshRegister(&configaasynDigReport, asynDigReportCallFunc);		//removed 20250421 JTA: redundant code.
}

epicsExportRegistrar(asynDigitizerRegister);

