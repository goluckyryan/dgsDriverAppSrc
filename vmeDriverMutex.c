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

#include <math.h>
#include <errno.h>

#include <epicsString.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <cantProceed.h>
/* NOTE: This is needed for interruptAccept */
#include <dbAccess.h>

#define epicsExportSharedSymbols
#include <shareLib.h>


/*
*
* this code is for a mutex shared across the asyn dig and trig drivers
* it allows locking the vme transactions so we can program the flash
* w/out any vme stuff going on. 
*/
volatile int is_vme_mutex_exist=0;
epicsMutexId vme_driver_mutex;
int disable_mutex_id=1000000;

void initVmeDrvMutex(void)
{
//this needs to be NON reentrant. 
	if (is_vme_mutex_exist==0) vme_driver_mutex=epicsMutexCreate();
	is_vme_mutex_exist=1;
}

//give caller id so we can disable mutex lock for debugging for some funtcions
void vmeMutexLock(int caller_id)
{
  if (disable_mutex_id!=caller_id) epicsMutexLock(vme_driver_mutex);
}


void vmeMutexUnLock(int caller_id)
{ 
	if (disable_mutex_id!=caller_id) epicsMutexUnlock(vme_driver_mutex);
}



/*
*
* this code is for a mutex shared across the digitizer and trigger readouts
* it allows locking the vme transactions so we can suck data
* w/out any EPICS-related vme stuff going on. 
*/
volatile int is_readout_mutex_exist=0;
epicsMutexId readout_driver_mutex;
int disable_mutex_id2=1000001;

void initReadoutDrvMutex(void)
{
//this needs to be NON reentrant. 
	if (is_readout_mutex_exist==0) readout_driver_mutex=epicsMutexCreate();
	is_readout_mutex_exist=1;
}

//give caller id so we can disable mutex lock for debugging for some funtcions
void readoutMutexLock(int caller_id)
{
  if (disable_mutex_id2!=caller_id) epicsMutexLock(readout_driver_mutex);
}


void readoutMutexUnLock(int caller_id)
{ 
	if (disable_mutex_id2!=caller_id) epicsMutexUnlock(readout_driver_mutex);
}


