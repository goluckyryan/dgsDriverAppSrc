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
#include <asynStandardInterfaces.h>

#include "asynTrigRouterDriver.h"
#include <string.h>

#include "DGS_DEFS.h"
#include "devGVME.h"

//  #include "readFIFO.h"  should be readTrigFIFO.h, when we get there....
//  #include "cesort.h"  //temmp cutout 20200406  JTA
#include <unistd.h>

#include "vmeDriverMutex.h"


extern struct daqDevPvt *daqDevPvt_list[65536];
extern int daqDevPvt_index;



//int asyntrig_debug_level = 0;
//int asyntrig_printevery = 1024;

//int prog_flip_endian=1;


//int asyn_sleepusec=0;

//asynTrigRouterDriver *my_asyntrigdriver;

/*
 * This is to be called in the startup file for each daq board used.
 * clock source: 0 = SerDes, 1 = ref, 2 = SerDes, 3 = Ext 
 */
extern "C" int devAsynTrigRouterCardInit(int cardno, int slot) 
{
   volatile unsigned int *newbase;



   initVmeDrvMutex();
   
   
#ifndef linux
 
   int temp;
   unsigned int boardid;
   int ftype;

   printf("\n\n devAsynTrigRouterCardInit\n\n\n");
   //minReadPeriod = sysClkRateGet()/10;

   if (devGVMECardInit(cardno, slot)) 
   {
      // return -1;
   }

   newbase = daqBoards[cardno].base32;

   temp = devReadProbe(4, newbase + 0x15C/4, &boardid); /* Main FW ver */
   if (temp) 
   {
      printf("cannot read Main FPGA at base address 0x%x\n",
                                                (unsigned int)newbase);
      daqBoards[cardno].mainOK = 0;     /* checked by init_record */
     // return ERROR;
   }

   ftype = boardid & 0xf00;
   ftype >>= 8;
   printf("Trigger firmware type found: %d\n",ftype);
   
#ifdef TEST_TRIGGER_DB
   printf("Test Mode.  Using desired type for record validation.\n");
#endif

/*  Definition of Trigger board types, last update September 2012
		-- 0-proto
		-- 1-GRETINA Router
		-- 2-GRETINA Master Trigger
		-- 3-GRETINA Data Generator
		-- 4-DGS Master Trigger
		-- 5-DSSD Master Trigger
		-- 6-DGS Router
		-- 7-DSSD Router
		-- 8-DGS Data Generator
		-- 9-DSSD Data Generator
		-- A-Digitizer Tester
		-- B-MyRIAD Trigger expansion module
		-- C-DGS Digitizer
		-- D-DSSD Digitizer
		-- E still unused
		-- F-VME FPGA
	-- bits 7:4 is major code revision ordinal
	-- bits 3:0 is minor code revision ordinal
*/

// Code at driver level should not care what version board is, that should only affect
// GUI display to user.
	daqBoards[cardno].board_type = BrdType_DGS_RTRIG;


   switch (ftype) {
		//1-GRETINA Router 6-DGS Router 7-DSSD Router
      case 1:
		printf("ERROR: Module #%d at slot %d identifies as type GRETINA Router Trigger\n",cardno,slot);
        daqBoards[cardno].router = 1;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
        return ERROR;
		break;
      case 6:		//dgs router
		printf("Module #%d at slot %d identifies as type DGS Router Trigger\n",cardno,slot);
        daqBoards[cardno].router = 1;
		daqBoards[cardno].mainOK = 1;     /* checked by init_record */
		break;
      case 7:		//dssd router
		printf("ERROR: Module #%d at slot %d identifies as type DSSD Router Trigger\n",cardno,slot);
        daqBoards[cardno].router = 1;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
        return ERROR;
        break;
		// 2-GRETINA Master Trigger  4-DGS Master Trigger  5-DSSD Master Trigger
      case 2:        /*Gretina master */
		printf("ERROR: Module #%d at slot %d identifies as type GRETINA Master Trigger\n",cardno,slot);
        daqBoards[cardno].router = 0;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
		break;
        return ERROR;
      case 4:		//dgs master
		printf("Module #%d at slot %d identifies as type DGS Master Trigger\n",cardno,slot);
        daqBoards[cardno].router = 0;
		daqBoards[cardno].mainOK = 1;     /* checked by init_record */
		break;
      case 5:		//dssd master
		printf("ERROR: Module #%d at slot %d identifies as type DSSD Master Trigger\n",cardno,slot);
        daqBoards[cardno].router = 0;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
        return ERROR;
		break;
      case 3:
        /*Data Generator- a test module for gretina- not for experiments*/
        printf("Data Generator support not implemented\n");
        daqBoards[cardno].router = 0;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
        break;
		//8-DGS Data Generator 9-DSSD Data Generator A-Digitizer Tester
		//B-MyRIAD Trigger expansion module C-DGS Digitizer D-DSSD Digitizer
		//E still unused F-VME FPGA
      default:
        printf("Reserved or undefined Trigger firmware type %d found\n", ftype);
        daqBoards[cardno].router = 0;
		daqBoards[cardno].mainOK = 0;     /* checked by init_record */
        return ERROR;
   }

	//	volatile unsigned int *FIFO;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.

   //fifo in mtrig board is at 178
   //JTA: technically, only a partial answer.  0x0178 is the address of Monitor FIFO 7
   //that is used by the DAQ to read out trigger information for event building.  However,
   //there are many other FIFOs in the trigger designs.
   daqBoards[cardno].FIFO = newbase + (0x0178 / 4);
   
   
   return OK;

// this #else is related to the #ifndef linux directive at the top of the file
//   since we always build in vxworks, whole section commented out 20200610
#else
#endif
}


/******************************************************************
 *
 *
 *
 *******************************************************************/



extern "C" int asynTrigRouterConfig1(
const char *portName, 
	int card_number,
	int slot)
{asynTrigRouterDriver *pasynTrigRouterDriver;
	
   
	devAsynTrigRouterCardInit(card_number,  slot);
	
	  pasynTrigRouterDriver
       	 = new asynTrigRouterDriver(
		portName, 
		card_number);
	
	//maddog_d = pasynTrigRouterDriver;	
    pasynTrigRouterDriver = NULL;
    return(asynSuccess);
	
	
}






/******************************************************************
 *
 *
 *
 *******************************************************************/

asynTrigRouterDriver::asynTrigRouterDriver(
	const char *portName,
	int card_number) :
		asynTrigCommonDriver(portName, card_number)
{

	printf("Constructing new asynTrigRouterDriver with # of parameters = %d\n",param_address_cnt);

	#include "asynRTrigParams.c"	

	printf(".......asynTrigRouterDriver now has # of parameters = %d after loading from file asynRTrigParams.c\n",param_address_cnt);
	
	strcpy(driverName,"asynTrigRouterDriver");
	 /* Create the thread that computes the waveforms in the background */
 
	

}

