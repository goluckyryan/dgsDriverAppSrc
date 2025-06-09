//==============================================================================
//	asynDebugDriver is supposed to be a minimal implementation of access to 
//	a VME slot, providing peek/poke access to the registers of any generic
//	module that might be present in the slot but no more.
//
//	At least that's what we've been told.
//==============================================================================

#ifdef vxWorks
	#include <sysLib.h>
	#include <vxWorks.h>
	#include <tickLib.h>
	#include <taskLib.h>
	#include <semLib.h>
	#include <rebootLib.h>
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
#include <asynStandardInterfaces.h>

#include "asynDebugDriver.h"
#include <string.h>

#include "DGS_DEFS.h"
#include "devGVME.h"


#include <unistd.h>


#include "vmeDriverMutex.h"

extern struct daqDevPvt *daqDevPvt_list[65536];
extern int daqDevPvt_index;

int asyn_debug_level = 0;
int inloop_debug_level = 0;
int outloop_debug_level = 0;
int sender_debug_level = 0;

int printevery = 1024;

int prog_flip_endian=1;


int asyn_sleepusec=0;

asynDebugDriver *pasynDebugDriver;		//this pointer to a driver instance is used throughout this file

/******************************************************************
 *	asynDebugConfig() is typically called by the .cmd file (boot script)
 *	but can also be called from the console.  It creates the driver
 *  instance for the selected card.
 *
 *	Usually the boot script will have a line
 * dbLoadRecords("db/asynDebug.template","P=VME04:,R=DBG:,PORT=DBG,ADDR=0,TIMEOUT=1")
 *  to load the EPICS database process variables for a debugging instance.
 *
 *  Then, later in the boot script a line like
 * asynDebugConfig("DBG",0)
 *
 *	calls this function to create a debug instance.
 *
 *	the global variable pasynDebugDriver is a pointer to the driver instance
 *	and is used throughout this file.
 *******************************************************************/
extern "C" int asynDebugConfig(const char *portName, int card_number)
{
	initVmeDrvMutex();	
	pasynDebugDriver = new asynDebugDriver(portName, card_number);
    return(asynSuccess);
}

/******************************************************************
 *	This is the actual constructor for an asynDebugDriver instance.
 *
 *  Clearly there's a bunch of EPICS background arcana behind this.
 *  I have no idea where asynPortDriver is defined or what the magic
 *  constants used therein mean.  Some clues are provided at
 *  https://epics.anl.gov/modules/soft/asyn/R4-12/asynPortDriver.html
 *******************************************************************/

asynDebugDriver::asynDebugDriver(const char *portName, int card_number) :
	asynPortDriver(portName, 
		1,		/* maxAddr */
		2048,	/* number of driver parameters */
		//Bit mask defining the asyn interfaces that this driver supports. The bit mask values are defined in asynPortDriver.h, e.g. asynInt32Mask. 
//		asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask |asynInt8ArrayMask |asynDrvUserMask,
		asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask | asynDrvUserMask,
		//Bit mask definining the asyn interfaces that can generate interrupts (callbacks). The bit mask values are defined in asynPortDriver.h, e.g. asynInt8ArrayMask. 
//		asynInt32Mask | asynFloat64Mask | asynOctetMask |asynInt8ArrayMask| asynGenericPointerMask,
		asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask,
		0, 		/* asynFlags.  This driver does not block and it is not multi-device, so flag is 0 */ //Flags when creating the asyn port driver; includes ASYN_CANBLOCK and ASYN_MULTIDEVICE. 
		1, 		/* Autoconnect */
		100, 	/* Default priority */
		-1		/* Default stack size*/
		)
{
	asynStatus status;
	epicsThreadId EpicsStat;
	printf("Constructing new asynDebugDriver\n");

 	this->card_number = card_number;

	//I would assume that you can create as many parameters as you want here, up to the 
	//maximum number of parameters listed above.
	//
	//I have absolutely no clue where all these variables whose addresses are passed
	//to createParam are defined, but would guess they're in one of the innumerable
	//.h files listed at the top of the file. 


	//the dbg_ variables would be used for peek-poke access to the module.
	createParam("dbg_address", asynParamInt32,   &dbg_address);
	createParam("dbg_long_address", asynParamInt32,   &dbg_long_address);
	createParam("dbg_value", asynParamInt32,   &dbg_value);
	createParam("dbg_value_read", asynParamInt32,   &dbg_value_read);
	createParam("dbg_write_addr", asynParamInt32,   &dbg_write_addr);
	createParam("dbg_read_addr", asynParamInt32,   &dbg_read_addr);
	createParam("dbg_write_long_addr", asynParamInt32,   &dbg_write_long_addr);
	createParam("dbg_read_long_addr", asynParamInt32,   &dbg_read_long_addr);
	createParam("dbg_card_number", asynParamInt32,   &dbg_card_number);

	printf("Started asynDebug Driver\n");
	
	setIntegerParam(dbg_address,0);
	setIntegerParam(dbg_long_address,0);
	setIntegerParam(dbg_value,0);
	setIntegerParam(dbg_value_read,0);
	setIntegerParam(dbg_write_addr,0);
	setIntegerParam(dbg_read_addr,0);
	setIntegerParam(dbg_write_long_addr,0);
	setIntegerParam(dbg_read_long_addr,0);
	setIntegerParam(dbg_card_number,0);	
	
	strcpy(driverName,"asynDebugDriver");

	/* Create the a background thread to monitor PV changes and react thereto */
//epicsThreadCreate returns the type epicsThreadId;
//that's defined as a pointer to a structure : typedef struct epicsThreadOSD *epicsThreadId
// where epicsThreadOSD is a structure
//typedef struct epicsThreadOSD {
//    ELLNODE node;
//    HANDLE handle;
//    EPICSTHREADFUNC funptr;
//    void * parm;
//    char * pName;
//    DWORD id;
//    unsigned epicsPriority;
//    char isSuspended;
//}
	EpicsStat = epicsThreadCreate("asynDebugDriver_Task",epicsThreadPriorityMedium,epicsThreadGetStackSize(epicsThreadStackMedium),(EPICSTHREADFUNC)::asynDebugDriver_Task,this);
	//So presumably EpicsStat will be NULL if the thread was not successfully created.

	//	asynStatus is an enum {asynSuccess,asynTimeout,asynOverflow,asynError,asynDisconnected,asynDisabled}
    if (EpicsStat == NULL) status = asynError; else status = asynSuccess;
}

/********************************************************************
 * EPICS thread function started when asynDebugDriver is constructed
********************************************************************/
void asynDebugDriver_Task(void *drvPvt)
	{
	asynDebugDriver *pPvt = (asynDebugDriver *)drvPvt;
	pPvt->CommandHandlerTask();
	}


/******************************************************************
 *	writeInt32() method.
 *******************************************************************/
asynStatus asynDebugDriver::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
	int function = pasynUser->reason;		//or put another way, which PV got you here (technically the integer of the asyn 'parameter' of the PV)

	asynStatus status = asynSuccess;
	int offset;
	int data;
	int *addr = NULL;
	int cardnum;
    /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
     * status at the end, but that's OK */
    status = setIntegerParam(function, value);		//sets PV to value sent by user

	//when the method is called, variable "function" is initialized with the 
	//type of writeInt32 we want to do:

	if (function == dbg_write_addr)
		{
		getIntegerParam(dbg_card_number,&cardnum);	//get card, offset and data
		getIntegerParam(dbg_address,&offset);
		getIntegerParam(dbg_value,&data);
		epicsMutexLock(vme_driver_mutex);	//set the lock
		viOut32 (cardnum, 0, offset, data);	//do the write
		epicsMutexUnlock(vme_driver_mutex);	//release the lock
	
		//if debugging on, dump info to console
		if (asyn_debug_level>0)
			printf("Write Register  card %d  addr_offset 0x%x  base 0x%x  addr %0x data@addr 0x%d",
			cardnum,
			offset,
			(unsigned int)daqBoards[cardnum].base32,
			(unsigned int)addr,
			data);
		}
	//hitting the 'READ' button in the debug EDM screen calls this
	else if (function == dbg_read_addr)
		{
		getIntegerParam(dbg_card_number,&cardnum);
		getIntegerParam(dbg_address,&offset);

		//this actually reads the register
		epicsMutexLock(vme_driver_mutex);
		viIn32 (cardnum, 0, offset,&data);
		epicsMutexUnlock(vme_driver_mutex);

		setIntegerParam(dbg_value_read,data);
	
			if (asyn_debug_level>0)
				printf("Read Register  card %d  addr_offset 0x%x  base 0x%x  addr %0x data@addr 0x%04X\n",
				cardnum,
				offset,
				(unsigned int)daqBoards[cardnum].base32,
				(unsigned int)addr,
				data);
		}
  

    /* Do callbacks so higher layers see any changes */
    callParamCallbacks();

    return status;
}

/******************************************************************
 *	readInt32() method.
 *
 * Method has no sub-function, unlike writeInt32.
 *******************************************************************/
asynStatus asynDebugDriver::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
	//call base function...
	asynPortDriver::readInt32( pasynUser,  value);
	return asynSuccess;
}
/******************************************************************
 *	CommandHandlerTask() method.
 *	The CommandHandlerTask method of asynDebugDriver enumerates	
 *	the different actions that the driver may take in response
 *	to the user setting various PVs.  The task runs continuously.
 *
 *	Every tenth of a second the thread checks the value of a number
 *  of driver parameters (linked to PVs) and if they have changed
 *  to the "active" value the action is taken.
 *******************************************************************/
void asynDebugDriver::CommandHandlerTask(void)
{
    
    while (1) 
		{
	   	epicsThreadSleep(.1);
	
		//============================================	
		//parameter checks	
		//============================================	

		}	//end while (1) 

}
/******************************************************************
	method getIntParam(int param);
 *******************************************************************/
int asynDebugDriver::getIntParam(int param)
{
	int val;
	getIntegerParam(param,&val);
	return(val);
}


/******************************************************************
	method int viOut32 (int slot, int adr_space, int reg_adr, int data);
 *******************************************************************/
int asynDebugDriver::viOut32 (int slot, int adr_space, int reg_adr, int data)
{
	int *addr = NULL;
	static int prcnt=0;
	
	#ifndef linux		
	if (daqBoards[slot].mainOK==1)
		{
		addr = (int*)(daqBoards[slot].base32 + reg_adr/4);
		*addr = data;
		}
	#endif	
	if (asyn_debug_level>1 )
		{
		prcnt=0;
		printf("wr slot=%d regadr=%x addr=%x data=%x \n",slot,reg_adr,(unsigned int)addr,data);
		}
	prcnt++;
	return(0);
}

/******************************************************************
	method int viIn32 (int slot, int adr_space, int reg_adr, int *data);
 *******************************************************************/
int asynDebugDriver::viIn32 (int slot, int adr_space, int reg_adr, int *data)
{
	int *addr = NULL;
	static int prcnt = 0;
#ifdef vxWorks
	if (daqBoards[slot].mainOK==1)
		{
		addr = (int*)(daqBoards[slot].base32 + reg_adr/4) ;
		//addr = (int*)(0xe8200000 + reg_adr/4) ;
		*data = *addr;
		}
	else *data=0xFADEFACE;

	if (asyn_debug_level>1 )
		{
		prcnt=0;
		printf("rd slot=%d regadr=%x addr=%x data=%x \n",slot,reg_adr,(unsigned int)addr,*data);
		}
	prcnt++;
#endif
	return(0);
}

/******************************************************************
	method void resetRead(void);
 *******************************************************************/
void asynDebugDriver::resetRead(void)
{
	rd_addr=0;
}
/******************************************************************
	method void resetWrite(void);
 *******************************************************************/
void asynDebugDriver::resetWrite(void)
{
	wr_addr=0;
}
/******************************************************************
	method int read(char*outbuf,int length);
// length in bytes. outbuf will be filled wth bytes,
 *******************************************************************/
int asynDebugDriver::read(char*outbuf,int length)
{

	int k;

	int rval;
	int *raddr;
		
	for (k=0;k<length;k+=4)
		{
		if (rd_addr>=fpga_prog_size)
			{
			printf("asynDebugDriver::read Ran out of memory \n");
			return(0);
			}
		raddr=(int*)&fpga_prog_data[rd_addr];
		rval = *raddr;
		
		if (prog_flip_endian==1) rval=flipEndian(rval);
		raddr=(int*)&outbuf[k] ;
		*raddr =rval;
		rd_addr+=4;
		}
	return(length);
}

/******************************************************************
	method int write(char*inbuf,int length);
 *******************************************************************/
int asynDebugDriver::write(char*inbuf,int length)
{

	int k;
	int wval;
	int *waddr;
	for (k=0;k<length;k+=4)
		{
		if (wr_addr>=fpga_prog_size) return(0);
		waddr=(int*)(&inbuf[k]);
		wval=*waddr;
		if (prog_flip_endian==1) wval=flipEndian(wval);
		waddr=(int*)&fpga_prog_data2[wr_addr];
		*waddr=wval;
		wr_addr+=4;
		}
	return(length);
}


/******************************************************************
	int flipEndian(int val);
 *
 * Takes an integer (32-bit) 'val' that has bytes ABCD (bits 31:0)
 * and reorders it into DCBA, returning that as the function result.
 *
 *******************************************************************/
int asynDebugDriver::flipEndian(int val)
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


//================================================================================================
//	End of methods for asynDebugDriver
//================================================================================================



//================================================================================================
//	functions meant for use on command console
//================================================================================================

/******************************************************************
 *	asynDebugCard() is called to set up a given slot in a VME crate
 *  for access.  However, the function that is called here
 *  (found in the gretVME source file devGVME.c) is quite specific
 *  to the digitizer module.
 *******************************************************************/
extern "C" int asynDebugCard(int cardno,int slot)
{
#ifdef vxWorks
	if (devGVMECardInit(cardno, slot)) return -1;
#endif  
	daqBoards[cardno].mainOK = 1;	/* init_record reads this */
	printf("Initialization of debug access to card #%d, slot %d complete\n",cardno,slot);
	return(0);
}	

/******************************************************************
 *  asynGenReport() dumps the list of cards in the crate and what
 *  settings the gVME structure daqBoards[] has for each slot.
 *
 *******************************************************************/
extern "C" void asynGenReport(char *cmd)
{
	int k;
	char *token = strtok (cmd," ");

	printf("General report function\n");
	while (token!=NULL)
		{
		if (strcmp(token,"cards")==0)
			{
			printf("Reporting on Cards\n");
			for (k=0;k<=6;k++)
				printf("card %d base32 %x vmever %d rev %d subrev %d mainOK %d board %d router %d FIFO %x EnRead %d type %d (%s)\n ", 
			 	k,
				(unsigned int)daqBoards[k].base32,
				daqBoards[k].vmever,
				daqBoards[k].rev,
				daqBoards[k].subrev,
				daqBoards[k].mainOK,
				(unsigned int)daqBoards[k].board,
				daqBoards[k].router,
				(unsigned int)daqBoards[k].FIFO,
				daqBoards[k].EnabledForReadout,
				daqBoards[k].board_type,
				BoardTypeNames[daqBoards[k].board_type]);
			}
		
		if (strcmp(token,"regs")==0)
			{
			printf("Reporting on device ptrs,registers \n");
			for (k=0;k<daqDevPvt_index;k++)
				printf("k %d mask %x  signal %x card %d chan %d addr %x \n",
					k,
					daqDevPvt_list[k]->mask,
					daqDevPvt_list[k]->signal,
					daqDevPvt_list[k]->card,
					daqDevPvt_list[k]->chan,
					(unsigned int)daqDevPvt_list[k]->reg->addr);
			}		

		if (strcmp(token,"dbg2")==0)
			{
			printf("Enable Debug Level 2 \n");
			asyn_debug_level=2;
			}

		if (strcmp(token,"dbg1")==0)
			{
			printf("Enable Debug Level 1 \n");
			asyn_debug_level=1;
			}
				
		if (strcmp(token,"dbg0")==0)
			{
			printf("Enable Debug Level 0 \n");
			asyn_debug_level=0;
			}
		

		
	
		token = strtok (NULL," "); //search for any further tokens inside while loop
	
	}
}
