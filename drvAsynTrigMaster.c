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








int asynTrigMasterConfig1(
	const char *portName,
	int card_number,
	int slot);



/** Code for iocsh registration */
static const iocshArg asynTrigMasterConfigArg0 = {"Port name", iocshArgString};
static const iocshArg asynTrigMasterConfigArg1 = {"card_number", iocshArgInt};
static const iocshArg asynTrigMasterConfigArg2 = {"slot", iocshArgInt};

static const iocshArg * const asynTrigMasterConfigArgs[] =  {&asynTrigMasterConfigArg0,
                                                          &asynTrigMasterConfigArg1,
                                                          &asynTrigMasterConfigArg2};
								
								
static const iocshFuncDef configasynTrigMaster = {"asynTrigMasterConfig", 3, asynTrigMasterConfigArgs};						


static void configasynTrigMasterCallFunc(const iocshArgBuf *args)
{
    printf("calling configasynTrigMasterCallFunc\n");
    asynTrigMasterConfig1(args[0].sval, args[1].ival,args[2].ival);



}
								




static void asynTrigMasterRegister(void)
{

	printf("Running asynTrigMasterRegister\n");
    iocshRegister(&configasynTrigMaster, configasynTrigMasterCallFunc);
    
}

epicsExportRegistrar(asynTrigMasterRegister);

