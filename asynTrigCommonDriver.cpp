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
#include <asynStandardInterfaces.h>

#include "asynTrigCommonDriver.h"
#include <string.h>

#include "DGS_DEFS.h"
#include "devGVME.h"

#include <unistd.h>

#include "vmeDriverMutex.h"
 struct daqDevPvt *daqDevPvt_list[65536];
 int daqDevPvt_index;

//int asyntrig_debug_level = 0;
//int asyntrig_printevery = 1024;

//int prog_flip_endian=1;


//int asyn_sleepusec=0;

//asynTrigCommonDriver *asyntrigcommon_mine;

int asyntrig_trace=-1;

/******************************************************************
 *
 *
 *
 *******************************************************************/
void asynTrigCommonDriver_Task(void *drvPvt)
{
	asynTrigCommonDriver *pPvt = (asynTrigCommonDriver *)drvPvt;
	pPvt->simTask();
}

/******************************************************************
 *
 *
 *
 *******************************************************************/
void asynTrigCommonDriver::simTask(void)
{
	/* read back vme regs on board, update params and pvs. */
	int k; 
	int stat;
	int trig_rcnt;
	/* Loop forever */    
	trig_rcnt=0;    
	while (1) 
		{
		// getDoubleParam(P_UpdateTime, &updateTime);
		epicsThreadSleep(1.0);		//changed from 2.0 to 1.0 JTA 20220916
		trig_rcnt++;
		setUIntDigitalParam( run_counter	     ,trig_rcnt,0xffffffff);
		/* step thru all params mapped to vme space, read vme
		 then update params and update pvs*/
		epicsMutexLock(vme_driver_mutex);	 
		for (k=0; k<param_address_cnt;k++)
			{
			viIn32 (this->card_number, 0, address_list[k].address, &stat);
			setUIntDigitalParam(address_list[k].param_num,stat,0xffffffff);
			}
		epicsMutexUnlock(vme_driver_mutex);
		// so pvs can see changes in param list.
		callParamCallbacks();
		}
}
/******************************************************************
 *
 *
 *
 *******************************************************************/
int asynTrigCommonDriver::getIntParam(int param)
{
	int val;
	getIntegerParam(param,&val);
	return(val);
}
/******************************************************************
 *
 *
 *
 *******************************************************************/
asynTrigCommonDriver::asynTrigCommonDriver(const char *portName, int card_number) :
	asynPortDriver(
		portName, 
		1, 
		1024,   // changed from 256 by mpc 10/29/15 per tm
         	asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask |
//			asynInt8ArrayMask |asynDrvUserMask | asynUInt32DigitalMask,
//         	asynInt32Mask | asynFloat64Mask | asynOctetMask |asynInt8ArrayMask| asynGenericPointerMask | asynUInt32DigitalMask,
			asynDrvUserMask | asynUInt32DigitalMask,
         	asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask | asynUInt32DigitalMask,
		0,
	 	1, 
		100, 
		-1)
{
asynStatus status;

	printf("Constructing new asynTrigCommonDriver\n");
 	this->card_number = card_number;
	address_list =  new int_int[1024];   // changed from 256 by mpc 10/30/15
	param_address_cnt=0;
	createParam("run_counter", asynParamUInt32Digital,&run_counter);
	setUIntDigitalParam(run_counter ,1,0xffffffff);
	
	 /* Create the thread that reads vme regs in  background */
    status = (asynStatus)(epicsThreadCreate("asynTrigCommonDriver_Task",
                          epicsThreadPriorityMedium,
                          epicsThreadGetStackSize(epicsThreadStackMedium),
                          (EPICSTHREADFUNC)::asynTrigCommonDriver_Task,
                          this) == NULL);

}
/******************************************************************
 *
 *
 *
 *******************************************************************/
asynStatus asynTrigCommonDriver::readUInt32Digital(asynUser *pasynUser, epicsUInt32 *value, epicsUInt32 mask)
{
	int function = pasynUser->reason;
	int stat = 0;
	int shift_flag;
	unsigned long shift = 0;
	unsigned long numbits;
	double cmaskd;
	unsigned long cmask;
	asynStatus status = asynSuccess;
	
	if (asyntrig_trace==function) printf("asynTrigCommonDriver::readUInt32Digital  mask %d  \n",mask);
	
	// for longin out recs,
	//format of mask is 0xaaaa_nb_sl, where mm is num bits. nn is shift left. 
	// otherwise it is raw mask. raw masks never have a's in them.
	shift_flag = 0;
	if ((mask&0xffff0000) == 0xaaaa0000)//then it is a long rec and we must shift val.
		{
		numbits = (mask&0x0000ff00)/256;
		shift = mask&0x000000ff;
		cmaskd= pow(2.0,(double)numbits)-1.0;
		cmaskd = cmaskd * pow(2.0,(double)shift);
		cmask = (unsigned long)cmaskd;
		mask=cmask;
		shift_flag=1;
		if (asyntrig_trace==function) printf(" cmask %lx numbits %ld shift %lod mask %d\n",cmask,numbits,shift,mask);
		}
	int address = findAddress(function);
	#ifndef linux 
		//if (address!=-1)
		//{
		//	viIn32 (this->card_number, 0, address, &stat);
		//}
	#endif	
	//	setUIntDigitalParam(function,stat,0xffffffff);
	//call base function...
	asynPortDriver::readUInt32Digital( pasynUser,  value,mask);
	
	if (shift_flag) *value=(*value)>>shift;
	if (asyntrig_trace==function)
		{
	printf("asynTrigCommonDriver::readUInt32Digital  function=%d address=0x%x mask=0x%x value=%d 0x%x\n",
		function,
		address,
		mask,
		stat,stat);
		}
		return(status);

}


/******************************************************************
 *
 *
 *
 *******************************************************************/
asynStatus asynTrigCommonDriver::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value,	epicsUInt32 mask)
{
	int function = pasynUser->reason;
	asynStatus status = asynSuccess;
	int address;
	int is_long;
	unsigned long numbits;
	unsigned long shift;
	double cmaskd;
	unsigned long cmask;
	epicsUInt32 value2;

	/* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
	 * status at the end, but that's OK */
	if (asyntrig_trace==function) printf("asynTrigCommonDriver::writeUInt32Digital val %d %0x mask %d %0x \n",value,value, mask, mask);
	   
	if (function==-1)
		{
		//spacial case parameters such as ao,ai
		} 
	else
		{	
		// for longin out recs,
		//format of mask is 0xaaaa_nb_sl, where mm is num bits. nn is shift left. 
		// otherwise it is raw mask. raw masks never have a's in them.
		is_long = 0;
		if ((mask&0xffff0000) == 0xaaaa0000)//then it is a long rec and we must shift val.
			{
			numbits = (mask&0x0000ff00)/256;
			shift = mask&0x000000ff;
			cmaskd= pow(2.0,(double)numbits)-1.0;
			cmaskd = cmaskd * pow(2.0,(double)shift);
			cmask = (unsigned long)cmaskd;
			value=value<<shift;
			is_long = 1;
			mask=cmask;

			
//			if (asyntrig_trace==function) printf(" cmask %lx numbits %ld shift %ld mask %d\n",cmask,numbits,shift,mask);
			}
			//		if ( asynd_wrp==1)
			//		printf("asynTrigCommonDriver::writeUInt32Digital function %d, value 0x%x, mask 0x%x long=%d\n",
			//			function,
			//			value,
			//			mask,
			//			is_long);
				 	
			status = setUIntDigitalParam(function, value,mask);
			getUIntDigitalParam(function, &value2, 0xffffffff);
			address = findAddress(function);
			if (address!=-1)
				{
//				printf("asynTrigCommonDriver::writeUInt32Digital:  address 0x%04x value %lx cmask %lx numbits %ld shift %ld mask %d\n",address, value2, cmask,numbits,shift,mask);
				epicsMutexLock(vme_driver_mutex);
				viOut32(this->card_number, 0, address, value2);
				epicsMutexUnlock(vme_driver_mutex);
				}
			}
	/* Do callbacks so higher layers see any changes */
	callParamCallbacks();
	return status;
}

/******************************************************************
 *e8200000
 *
 *
 *******************************************************************/

int asynTrigCommonDriver::viOut32 (int slot, int adr_space, int reg_adr, int data)
{
	int *addr;
	static int prcnt=0;
	
	
	
	addr = (int*)(daqBoards[slot].base32 + reg_adr/4) ;
	//addr = (int*)(0xe8200000+ reg_adr/4) ;
	
#ifdef vxWorks

	
		*addr = data;
	
#endif
	
//	if (asyntrig_debug_level>1 )
//	{
//		prcnt=0;
//		printf("wr slot=%d regadr=%x addr=%x data=%x \n",
//			slot,
//			reg_adr,
//			addr,
//			data);
//	}
	prcnt++;
	
	
	
	return(0);
	
}


/******************************************************************
 *
 *
 *
 *******************************************************************/


int asynTrigCommonDriver::viIn32 (int slot, int adr_space, int reg_adr, int *data)
{
int *addr;
static int prcnt = 0;

	#ifdef vxWorks
	


	addr = (int*)(daqBoards[slot].base32 + reg_adr/4) ;
	//addr = (int*)(0xe8200000 + reg_adr/4) ;
	
	
	
	
		*data = *addr;
	
//	if (asyntrig_debug_level>1 )
//	{
//		prcnt=0;
//		printf("rd slot=%d regadr=%x addr=%x data=%x \n",
//			slot,
//			reg_adr,
//			addr,
//			*data);
//	}
	prcnt++;
			
	
	#endif
	return(0);
	
}



int asynTrigCommonDriver::flipEndian(int val)
{

  int byte;
  int newval;
  
  byte=val&0xff;
  newval= byte<<24;
  
  byte=(val>>8)&0xff;
  newval= newval| (byte<<16);
  
  byte=(val>>16)&0xff;
  newval= newval| (byte<<8);

  byte=(val>>24)&0xff;
  newval= newval| byte;

  return(newval);
}
	
   void asynTrigCommonDriver::setAddress(int param, int address)
    {
    	address_list[param_address_cnt].param_num = param;
    	address_list[param_address_cnt].address = address;
	param_address_cnt++;
	
	
    };
    
    int asynTrigCommonDriver::findAddress(int param)
    {
    	int k; 
	k=0;
	while (k<param_address_cnt)
	{
		if (address_list[k].param_num==param)
		  return(address_list[k].address);
		  
		 k++;
	}
	return(-1);
    }
