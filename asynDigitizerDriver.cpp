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

#include "asynDigitizerDriver.h"
#include <string.h>



#include <unistd.h>

#include "vmeDriverMutex.h"

#include "DGS_DEFS.h"
#include "devGVME.h"



/*thread run counter*/
 //int dig_rcnt=0;

extern struct daqDevPvt *daqDevPvt_list[65536];
extern int daqDevPvt_index;

int asyn_debug_level_d = 0;
int printevery_d = 1024;

int prog_flip_endian_d=1;
int asyn_sleepusec_d=0;

asynDigitizerDriver *maddog_d;

int recLenGDig = 25;
int asynd_wrp=0;

int total_parameters = 0;		//added 20240215 jta

/******************************************************************
 *
 *	This function is called by the VxWorks bootstrap (.cmd) file.
 *	It performs intialization of a presumed ANL digitizer board in the slot
 *	and generates a new asynDigitizerDriver instance.
 *
 *******************************************************************/
extern "C" int asynDigitizerConfig(const char *portName, int card_number, int slot) //JTA: removed 'clocksource' 20190114
{
	asynDigitizerDriver *pasynDigitizerDriver;
	devAsynDigCardInit(card_number, slot);	//perform card setup
	pasynDigitizerDriver = new asynDigitizerDriver(portName, card_number);  //create new driver instance
	maddog_d = pasynDigitizerDriver;	//'maddog_d' is believed to be some Tim Madden debug variable	
	pasynDigitizerDriver = NULL;		//why you have to null a local variable before returning is beyond me.
return(asynSuccess);
}

/******************************************************************
 * This is the initializer for a digitizer that is called by the 
 * console/boot level function asynDigitizerConfig().  It checks for
 * whether a board is present or not, and then updates some entries
 * in the daqBoard array if it believes a board to be present.
 *******************************************************************/
extern "C" int devAsynDigCardInit(int cardno, int slot) {
	volatile unsigned int *newbase;
	unsigned int fifo;
	int retval;


	//create vme mutex
	initVmeDrvMutex();

	//the devGVMECardInit() call does partial setup of the daqBoards[] structure that is defined 
	//in asynDebugDriver sets up SOME of the structure, but not all:
	//struct daqBoard {
	//	ihtEntry *registers;  <== this gets filled for some GRETINA-specific 'hash table' function
	//	struct daqRegister vmeRegisters[GVME_NUM_REGISTERS];  <== this gets filled with the VME32 addresses of the "canonical" VME registers (for flash access)
	//	volatile unsigned int *base32;	<== this gets filled with the address in VME A32 space of the board
	//	volatile unsigned int *FIFO;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.
	//	unsigned short vmever;	<== this gets filled with a revision value read from the VME FPGA of the board
	//	unsigned int rev;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.
	//	unsigned int subrev;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.
	//	unsigned short mainOK;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.
	//	unsigned short board;	<== this gets filled with the slot of the board
	//	unsigned short router;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.
	//	unsigned short board_type;  <== NOT INIITIALIZED BY devGVMECardInit(); initialized here.

	//devGVMECardInit extracted from GRETINA code and moved to end of this file May2020 by JTA.

	if (devGVMECardInit(cardno, slot) != 0) return -1;	//JTA: removed 'clocksource' 20190114
							//devGVMECardInit() function returns 0 on success, -1 if there are any errors.

	newbase = daqBoards[cardno].base32;	//newbase is the base address, in VME32 space, of the board.
	daqBoards[cardno].router = 0;  //by definition a digitizer is not a router...

	//At this point you believe a board is in the slot, and because you called
	//devAsynDigCardInit() you think it should be a digitizer.  So, initialize
	//the board as if it were a digitizer. 
  
	daqBoards[cardno].FIFO = newbase + 0x1000 / sizeof(int);		//loads daqBoards[].FIFO with address of FIFO buffer

	//Reads the 'vmever' value and uses that to select which VME address it reads to get the rev & subrev.
	//'vmever' is tcreateParamhe value read from address 0x920 of the board, done in the previous call to devGVMECardInit().
	//That function is in the gretVME tree of source, not part of the dgsDriverApp.
	//
	//	In an ANL digitizer, address 0x920 reads back
	//		data_out(31 downto 16) <= code_revision(15 downto 0);
	//		data_out(15 downto 12) <= "0000";
	//		data_out(11 downto  0) <= serial_number_in;  
	//
	//	Modifications made by JTA to the devGVMECardInit() function put the full 16-bit revision
	//  into daqBoards[].vmever.  In the ANL digitizer VME FPGA you have the following additional data:
	//
	//	Address 0x924 is a 32-bit 'code revision', but in the ANL digitizer there's only 16 bits of info anyway. 
	//	The expectation here is that you'd read 0x0F13, where the 'F' means "VME FPGA" and the '13' is the VME FPGA
	//	code revision.
	//	Address 0x928 is a 32-bit 'code date'.  Current value as of 20191014 is '20150120' for an ANL digitizer.

	//The VME FPGA revision information is arguably useful, but what you really want is the identification of what *kind* of
	//digitizer this is - master/slave, LBL vs. ANL vs. Majorana, etc.  For that you need to look at the Main FPGA 
	//code_revision information.

	//	Main FPGA addresses of ANL digitizers are 0x600 (code revision) and 0x604 (code date).  From the firmware source
	//  the format is
	//
	//  regin_code_revision <= X"00004" & X"D" & cCODE_VERSION_MAJOR & cCODE_VERSION_MINOR when(SLAVE_MODE = TRUE) else X"00004" & X"C" & cCODE_VERSION_MAJOR & cCODE_VERSION_MINOR;
	//
	// where the 'D' means "ANL slave digitizer" and the 'C' means "ANL master digitizer".
	//
	//	ANL-firmware Majorana digitizer have the format of 
	// 
	//  regin_code_revision <= X"0000F" & X"D" & cCODE_VERSION_MAJOR & cCODE_VERSION_MINOR when(SLAVE_MODE = TRUE) else X"0000F" & X"C" & cCODE_VERSION_MAJOR & cCODE_VERSION_MINOR;
	//
	// For comparison, ANL master triggers read back a 16-bit value for their code revision, and it is at address 0x15C.
	
	//devReadProbe is presumably some kind of VME read with error return?
	//                 word size    address      ptr to value storage
	retval = devReadProbe(4, newbase + 0x1000/4, &fifo);
	//Tim Madden's idea here is that if you can read from address 0x1000, this must be an ANL digitizer.
	if (retval) //retval <> 0 is, presumably, a bus error...
		{
		printf("cannot read Main FPGA at base address 0x%x\n",(unsigned int)newbase);
		daqBoards[cardno].mainOK = 0;	/* init_record reads this */
		}
	else daqBoards[cardno].mainOK = 1;	/* init_record reads this */
	
	daqBoards[cardno].rev = *(daqBoards[cardno].base32 + (0x600/4));	//read from VME - ANL digitizer "code revision"
	daqBoards[cardno].subrev = *(daqBoards[cardno].base32 + (0x604/4));  //ANL digitizer "code date"
	
	// so the proper check of "is this a digitizer" presumably is as below.
	// TODO : put in identification of LBL digitizers

	switch ((daqBoards[cardno].rev & 0x0000FF00) >> 8)
		{
		case 0x4C : 	//ANL (DGS) Master Digitizer
			daqBoards[cardno].board_type = BrdType_ANL_MDIG; break;
		case 0x4D : 	//ANL (DGS) Slave Digitizer
			daqBoards[cardno].board_type = BrdType_ANL_SDIG; break;
		case 0xFC : 	//ANL (Majorana) Master Digitizer
			daqBoards[cardno].board_type = BrdType_MAJORANA_MDIG; break;
		case 0xFD : 	//ANL (Majorana) Slave Digitizer
			daqBoards[cardno].board_type = BrdType_MAJORANA_SDIG; break;
		default:	//not a digitizer type we understand
			daqBoards[cardno].board_type = 0; break;
		}
	
	printf("devAsynDigCardInit: Digitizer module #%d at slot %d identifies as type %s\n", cardno, slot, BoardTypeNames[daqBoards[cardno].board_type]);

	//---------------------------------------------------------------------------
	// JTA: removed a bunch of GRETINA-specific clock crap, formerly here, that
	// was inappropriately copied by Tim.
	//---------------------------------------------------------------------------
	
	
	return OK;

}


/******************************************************************
 *
 *
 *
 *******************************************************************/
void asynDigitizerDriver_Task(void *drvPvt)
{
	asynDigitizerDriver *pPvt;
	pPvt = (asynDigitizerDriver *)drvPvt;	//convert void pointer drvPvt to pointer to asynDigitizerDriver
	pPvt->simTask();			//Then invoke the simTask method of whatever the hell drvPvt points to
	printf("asynDigitizerDriverSimTask (_RBV handler) started \n");
}

/******************************************************************
 *
 * Despite the name, this isn't some simulation task.  This apparently
 * is called every so often by a timer and is the task that updates 
 * all the status PVs.  It's also been loaded with logic that forces
 * a write to a pulsed-control register each time some other task
 * sets the global variable is_hit_pcc ("you should write to the pulsed control register").
 *
 *******************************************************************/
void asynDigitizerDriver::simTask(void)
{
	/* read back vme regs on board, update params and pvs. */
	int k; 
	int stat;
	int dig_rcnt;
	
	dig_rcnt=0;

  
 /* Loop forever */ 
 while (1) 
 	{
		// getDoubleParam(P_UpdateTime, &updateTime);
		epicsThreadSleep(2.0);		//This sets the update rate for all the PVs in the GUI.
		dig_rcnt++;
		setUIntDigitalParam(run_counter,dig_rcnt,0xffffffff);
 
		/* step thru all parameters mapped to vme space, read vme then update params and update pvs*/
		epicsMutexLock(vme_driver_mutex);
		for (k=0; k<param_address_cnt;k++)
			{
			viIn32 (this->card_number, 0, address_list[k].address, &stat);
			setUIntDigitalParam(address_list[k].param_num,stat,0xffffffff);
			}

		epicsMutexUnlock(vme_driver_mutex);
	
		// so pvs can see changes in param list.
		callParamCallbacks();
//		printf("_RBV update done for %d parameters\n",param_address_cnt);
		}	//end while(1)
}  //end of function


/******************************************************************
 *
 *
 *
 *******************************************************************/

int asynDigitizerDriver::getIntParam(int param)
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

asynDigitizerDriver::asynDigitizerDriver(const char *portName,int card_number) :
	asynPortDriver(
		portName, 
		1, 
		1024,  // changed from 256 by mpc 10/29/15 per tm
	asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask |
//		asynInt8ArrayMask |asynDrvUserMask | asynUInt32DigitalMask,
		asynDrvUserMask | asynUInt32DigitalMask,
//	asynInt32Mask | asynFloat64Mask | asynOctetMask |asynInt8ArrayMask| asynGenericPointerMask | asynUInt32DigitalMask,
	asynInt32Mask | asynFloat64Mask | asynOctetMask | asynGenericPointerMask | asynUInt32DigitalMask,
 	0,
	 	1, 
		100, 
		-1)
{
asynStatus status;

	printf("Constructing new asynDigitizerDriver\n");
	
	address_list =  new int_int[256];		//JTA 20250424: This apparently limits us to 256 paramters, or whole-register PVs, per board.
							//JTA 20250424: int_int is a class defined in the digitizer and trigger .h files
							//JTA 20250424: that is simply a struct of two ints.  Digitizer as of this date has 222.
	
	param_address_cnt=0;

 	this->card_number = card_number;

	createParam("run_counter",asynParamUInt32Digital,&run_counter);
	setUIntDigitalParam(run_counter,1,0xffffffff );

//there are now two separate params files, one for the digitizer main,
//one for the digitizer VME.  20240221  JTA

//param_addres_cnt is incremented by each call to setAddress(), a function that is called by the #included files.
//back in the day, that used to be an enum{} in the #included file, limiting us to only one #include.


	printf("Constructing new asynDigitizerDriver with # of parameters = %d\n",param_address_cnt);

	#include "asynDigParams.c"

	printf(".......asynDigitizerDriver now has # of parameters = %d after loading from file asynDigParams.c\n",param_address_cnt);

//In an effort to simplify, since we really don't think the VME FPGA will ever change again, reduced back to 
//one #included file 20250815, and put all digitizer VME registers in the digitizer spreadsheet as EPICS-only objects (no VHDL type)
#if 0
	#include "asynDigParamsVME.c"

	printf(".......asynDigitizerDriver now has # of parameters = %d after loading from file asynDigParamsVME.c\n",param_address_cnt);
#endif

	strcpy(driverName,"asynDigitizerDriver");
	 /* Create the thread that reads vme regs in  background */
	status = (asynStatus)(epicsThreadCreate("asynDigitizerDriver_Task",
											epicsThreadPriorityMedium,
											epicsThreadGetStackSize(epicsThreadStackMedium),
											(EPICSTHREADFUNC)::asynDigitizerDriver_Task,
											this) == NULL);
}


/******************************************************************
 *
 *	asynDigitizerDriver::readUInt32Digital
 *
 *	In the EPICS database as generated by the spreadsheets, every PV
 *	has a 'mask' value associated with it.  A straight-forward whole-
 *  register read should just return the value.  But if you have a
 *	field that is some contiguous group of bits within the register,
 *  the Python code sets the mask value to indicate which set of bits
 *  you want out of the full register read.	
 *
 *******************************************************************/

asynStatus asynDigitizerDriver::readUInt32Digital(asynUser *pasynUser,epicsUInt32 *value,epicsUInt32 mask)
{
	int is_long = 0;
	unsigned long numbits = 0;
	unsigned long shift = 0;
	unsigned long cmask = 0;
	double cmaskd = 0.0;
	
  	asynStatus status = asynSuccess;
	
	// for longin out recs,
	//format of mask is 0xaaaa_nb_sl, where mm is num bits. nn is shift left. 
	// otherwise it is raw mask. raw masks never have a's in them.
	is_long = 0;	//is_long does not mean "it is a long".  is_long means "I want a sub-field" if nonzero.
	if ((mask&0xffff0000) == 0xaaaa0000)//then it is a long rec and we must shift val.
	{
		numbits = (mask&0x0000ff00)/256;		//how many bits wide you want the sub-field to be
		cmaskd = pow(2.0,(double)numbits)-1.0;  //2^n-1 generates a bit-mask (e.g. a sub-field 5 bits long yields (2^^5 - 1), or binary 11111

		shift = mask&0x000000ff;				//position within 32-bit value where field starts
		cmaskd = cmaskd * pow(2.0,(double)shift);	//shift the mask left that many bits
		cmask = (unsigned long)cmaskd;				//convert from double to unsigned long for later use
		mask=cmask;		//re-convert from unsigned long to epicsUint32?  And isn't this bad practice to use a formal as a variable?
		is_long=1;	//is_long does not mean "it is a long".  is_long means "I want a sub-field" if nonzero.
	}
	
	
	//call base function...
	asynPortDriver::readUInt32Digital(pasynUser,value,mask);	//shouldn't you just pass cmask here instead of mask?


	//is_long does not mean "it is a long".  is_long means "I want a sub-field" if nonzero.
	if (is_long) *value=(*value)>>shift;  //so if a sub-field is asked for presumably the readUint32Digital does the mask,
											//but the result still needs to be shifted.
	return(status);

}


/******************************************************************
 *
 *
 *
 *******************************************************************/

asynStatus asynDigitizerDriver::writeUInt32Digital(
	asynUser *pasynUser, 
	epicsUInt32 value,
	epicsUInt32 mask)
{
 int function = pasynUser->reason;
 asynStatus status = asynSuccess;

	int address;
	int is_long;
	
	epicsUInt32 value2;

 /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
  * status at the end, but that's OK */

 	if (function==-1)
 	{
		//special case parameters such as ao,ai
	} 
	else
	{	
		// for longin out recs,
		//format of mask is 0xaaaa_nb_sl, where mm is num bits. nn is shift left. 
		// otherwise it is raw mask. raw masks never have a's in them.
		is_long = 0;
//		printf("asynDigitizerDriver::writeUInt32Digital function %d, value 0x%x, mask 0x%x long=%d\n",
//			function,
//			value,
//			mask,
//			is_long);
		if ((mask&0xffff0000) == 0xaaaa0000)//then it is a long rec and we must shift val.
		{
			unsigned long numbits = (mask&0x0000ff00)/256;
			unsigned long shift = mask&0x000000ff;
			double cmaskd= pow(2.0,(double)numbits)-1.0;
			cmaskd = cmaskd * pow(2.0,(double)shift);

			unsigned long cmask = (unsigned long)cmaskd;
			mask=cmask;
			value=value<<shift;
			is_long = 1;
		}
		
	 	status = setUIntDigitalParam(function, value,mask);	//JTA: we have to guess here that 'function' is the parameter index.
		getUIntDigitalParam(function, &value2, 0xffffffff);
		address = findAddress(function);
		if (address!=-1)
		{
			epicsMutexLock(vme_driver_mutex);
			viOut32(this->card_number, 0, address, value2);
			epicsMutexUnlock(vme_driver_mutex);
			//JTA 20220717: I would think that here one needs to test if "is_long" is set or not.
			//If (is_long == 1) that would mean that a field of a register was just written, so
			//the bit in the whole-register PV should also be set.
			//
			//If (is_long == 0) then the write was to a whole register, and thus any "field" PVs
			//associated with the whole register need to be updated with the new value of the register.
			//
			//the problem inherent to asyn is that this function only gets a pointer to whatever
			//parameter is being futzed with and there is no cross-linkage to what PVs care about the 'parameter'.
		}
	}
 /* Do callbacks so higher layers see any changes */
 callParamCallbacks();

 return status;
}



/******************************************************************
 * 0xe8200000 is the base address where vme is mapped in the MVME5500.
 *
 *
 *******************************************************************/

int asynDigitizerDriver::viOut32 (int slot, int adr_space, int reg_adr, int data)
{
	int *addr;
	static int prcnt=0;
	
	
	
	addr = (int*)(daqBoards[slot].base32 + reg_adr/4) ;
	//addr = (int*)(0xe8200000+ reg_adr/4) ;
	#ifndef linux
	*addr = data;
	#endif
	
	if (asyn_debug_level_d>1 )
	{
		prcnt=0;
		printf("wr slot=%d regadr=%x addr=%x data=%x \n",
			slot,
			reg_adr,
			(int)addr,
			data);
	}
	prcnt++;
	
	
	
	return(0);
	
}


/******************************************************************
 *
 *
 *
 *******************************************************************/


int asynDigitizerDriver::viIn32 (int slot, int adr_space, int reg_adr, int *data)
{
int *addr;
static int prcnt = 0;

	addr = (int*)(daqBoards[slot].base32 + reg_adr/4) ;
	//addr = (int*)(0xe8200000 + reg_adr/4) ;
	*data = *addr;
	
	if (asyn_debug_level_d>1 )
	{
		prcnt=0;
		printf("rd slot=%d regadr=%x addr=%x data=%x \n",
			slot,
			reg_adr,
			(int)addr,
			*data);
	}
	prcnt++;
			
	return(0);
	
}



int asynDigitizerDriver::flipEndian(int val)
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
	
void asynDigitizerDriver::setAddress(int param, int address)
 {
 	address_list[param_address_cnt].param_num = param;
 	address_list[param_address_cnt].address = address;
	param_address_cnt++;
	
	
 };
 
 int asynDigitizerDriver::findAddress(int param)
 {
 	int k; 
	k=0;
//	printf("address list searching %d parameters\n",param_address_cnt);
	while (k<param_address_cnt)
	{
		if (address_list[k].param_num==param)
		  return(address_list[k].address);
		  
		 k++;
	}
	return(-1);
 }



