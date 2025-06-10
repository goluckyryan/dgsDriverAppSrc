#include <sysLib.h>
#include <vxWorks.h>
#include <stdio.h>
#include <stdlib.h>
#include <tickLib.h>
#include <taskLib.h>
#include <semLib.h>

#include "devLib.h"
#include "alarm.h"
#include "dbDefs.h"
#include "dbAccess.h"
#include "recGbl.h"
#include "recSup.h"
#include "devSup.h"
#include "longinRecord.h"
#include "longoutRecord.h"
#include "aoRecord.h"
#include "aiRecord.h"
#include "biRecord.h"
#include "mbboRecord.h"
#include "mbbiDirectRecord.h"
#include "boRecord.h"
#include "epicsMutex.h"
#include "iocsh.h"
#include "epicsExport.h"
#include "time.h"

#include "devGVME.h"
#include "DGS_DEFS.h"
#include "string.h"

extern epicsMutexId vme_driver_mutex;

//added 20190114 by JTA
char BoardTypeNames[16][30] = {
   //The indices are derived from values defined for the firmware.
	"No Board Present",			//0
	"GRETINA Router Trigger",	//1		--the '1' comes from bits 11:8 of the code_revision register
	"GRETINA Master Trigger",	//2		--the '2' comes from bits 11:8 of the code_revision register
	"LBNL Digitizer",			//3		--arbitrary placeholder assigned by JTA
	"DGS Master Trigger",		//4		--the '4' comes from bits 11:8 of the code_revision register
	"Unknown",					//5
	"DGS Router Trigger",		//6		--the '6' comes from bits 11:8 of the code_revision register
	"Unknown",					//7
	"MyRIAD",					//8		--arbitrary placeholder assigned by JTA
	"Unknown",					//9
	"Unknown",					//10
	"Unknown",					//11
	"ANL Master Digitizer",		//0xC : 12	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
	"ANL Slave Digitizer",		//0xD : 13	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
	"Majorana Master Digitizer",//14		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
	"Majorana Slave Digitizer",	//15		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
	};






struct daqBoard daqBoards[GVME_MAX_CARDS];

//additional global vars for outloop.st to communicate PV values to outloopsupport.c
unsigned short OL_Hdr_Chk_En = 1;
unsigned short OL_TS_Chk_En = 1;
unsigned short OL_Deep_Chk_En = 1;
unsigned short OL_Hdr_Summ_En = 0;
unsigned int OL_Hdr_Summ_PS = 0x1000;
unsigned int OL_Hdr_Summ_Evt_PS = 0x100;


/******************************************************************
 *  InitializeDaqBoardStructure() intializes all the VME data structures.
 ******************************************************************/
void InitializeDaqBoardStructure(void)
{
int JTA_cardno = 0;
int JTA_regno = 0;


// JTA: 20200616: there needs to be a for-sure intialization of daqBoards here, 'cause there is no
// compile-time initialization of the daqBoards[] structure.

	for(JTA_cardno=0;JTA_cardno < GVME_MAX_CARDS; JTA_cardno++)		//was <=, changed to < 20230921.  Never seen before 'cause somehow this was never called by anything....
		{
		for(JTA_regno = 0; JTA_regno < GVME_NUM_REGISTERS; JTA_regno++)
			{
			daqBoards[JTA_cardno].vmeRegisters[JTA_regno].addr = NULL;
			daqBoards[JTA_cardno].vmeRegisters[JTA_regno].sem = 0;
			daqBoards[JTA_cardno].vmeRegisters[JTA_regno].tick = 0;
			daqBoards[JTA_cardno].vmeRegisters[JTA_regno].copy = 0;
			daqBoards[JTA_cardno].vmeRegisters[JTA_regno].dibs = 0;
			}
			daqBoards[JTA_cardno].base32 = NULL;
			daqBoards[JTA_cardno].FIFO = NULL;
			daqBoards[JTA_cardno].vmever = 0;
			daqBoards[JTA_cardno].rev = 0;
			daqBoards[JTA_cardno].subrev = 0;
			daqBoards[JTA_cardno].mainOK = 0;
			daqBoards[JTA_cardno].board = 0;
			daqBoards[JTA_cardno].EnabledForReadout = 0;
			daqBoards[JTA_cardno].router = 0;
			daqBoards[JTA_cardno].board_type = 0;

		}
}



/******************************************************************
 * devGVMECardInit performs initialization of a digitizer board.
 * This is specific to a digitizer, and may have junk specific to only GRETINA digitizers in it.
 *
 * Modified starting 20171016 to remove GRETINA-specific bits and to adjust coding
 * to match operation of DGS firmware.
 *******************************************************************/
int devGVMECardInit(int cardno, int slot)   //JTA: removed 'clocksource' 20190114
//int devGVMECardInit(int cardno, int slot, int clocksource) 
{
	int retval;
	int base;
	volatile unsigned int *newbase;
	unsigned int vmever;
	int i, space;
	char *TypeOfBoard;
	
	base = slot << 20;  //In VME64x bits 20 and higher are used to form the base VME address of the card
	TypeOfBoard = NULL;
	
	if (cardno < 0 || cardno >= GVME_MAX_CARDS) 
		{
		printf("devGVMECardInit: Card number %d is illegal\n", cardno);
		return -1;
		}
  
// Purpose of this ifdef is to set the address in local IOC memory that gets mapped to VME accesses.
// Apparently RIO3 processor boards have a different offset than others, but I'll bet there are other
// variants not covered here.
#ifdef RIO3
   space = 0x0b;
#else
   space = 0x0a;
#endif

	//sysBusToLocalAdrs() not in any of the DGS or GRETINA source - part of VxWorks syslib:
	//
	//	sysBusToLocalAdrs( ) - convert a bus address to a local address
	//
	//    STATUS sysBusToLocalAdrs
	//    	(
	//    	int    adrsSpace,         /* bus address space in which busAdrs resides */
	//    	char * busAdrs,           /* bus address to convert */
	//    	char * *pLocalAdrs        /* where to return local address */
	//    )

   
	retval = sysBusToLocalAdrs(space, (char *)base,(char **)&newbase);

	if (	!retval 		//if no error from sysBusToLocalAdrs
		&& (daqBoards[cardno].base32 == 0 || daqBoards[cardno].base32 == newbase)  //and existing value is null or matches
		) 

		daqBoards[cardno].base32 = newbase;  //then assign

	else  //but if above if fails
		{
		if (retval)  //tell user on syslib error
			printf("devGVMECardInit: Error %d returned from sysBusToLocalAdrs base 0x%x\n", retval, base);
		else  		//or attempted re-use of device
			printf("devGVMECardInit: card number %d already has address 0x%lx not 0x%lx\n",
					cardno, (unsigned long)daqBoards[cardno].base32, (unsigned long)newbase);

		daqBoards[cardno].base32 = 0;	/* checked by init_record */  
		return -1;  //and return some error value
		}
		
	//If the bus mapping works, then do a test read to see if the module responds
			
	// JTA: This is a read of address 0x920 in the VME FPGA.
	// In the ANL version of the VME FPGA, the data read from this address is defined as
	//		data_out(31 downto 16) <= code_revision(15 downto 0);
	//		data_out(15 downto 12) <= "0000";
	//		data_out(11 downto  0) <= serial_number_in;	
	//
   retval = devReadProbe(4, newbase + 0x248, &vmever);	// VME FPGA version register; 0x248 is 0x920 >> 2
   														// as function assumes address is the longword offset
   														// and not the true VME address that is the byte offset
	if (retval) 
		{
		printf("devGVMECardInit: cannot read VME FPGA at base address 0x%x 0x%x\n", base, (unsigned int)newbase);
		daqBoards[cardno].base32 = 0;	/* checked by flash operations */
		return -1;
		} 

	// JTA : the decoding here extracts bits 31:24 of the register,
	// but as noted above in the ANL version of the VME FPGA the 
	// code revision is a sixteen bit value in bits 31:16.
	//
	//   vmever = (vmever >> 24) & 0xff;		cut 20171016 JTA
	//
	vmever = (vmever >> 16) & 0xffff;	//modified to match DGS firmware 20171016 JTA
	printf("devGVMECardInit: VME FPGA version 0x%x\n", vmever);

	//JTA: address 0x900 in the ANL VME FPGA is the FPGA_CTL register.
	//I believe this assignment is intended to set newbase to the first
	//register in the VME FPGA as later accesses are relative to this address.
	newbase = newbase + 0x900/4;		//assign newbase to beginning of VME FPGA register map
	daqBoards[cardno].board = slot; 
	daqBoards[cardno].vmever = vmever; 
    

	// Set up some mystery semaphore logic for every register in the VME FPGA.  Doesn't appear
	// to set up anything for the main FPGA or other registers.
	//
	// GVME_NUM_REGISTERS is a magic constant #defined as 0x24 in devGVME.h.
	// as noted above, newbase is the address in longwords, so newbase++ is not unreasonable here.
	// 
	// As written, this would set up semaphores for addresses 0x900, 0x904, ... , 0x98C.
	// That's consistent with covering the usual range of VME FPGA register addresses.
	for (i = 0; i < GVME_NUM_REGISTERS; i++, newbase++)
		{
		daqBoards[cardno].vmeRegisters[i].addr = newbase;
		/* multiple write/board semaphore: only used in case of mbbo and bo records.*/
		daqBoards[cardno].vmeRegisters[i].sem = epicsMutexCreate();
		if (daqBoards[cardno].vmeRegisters[i].sem == 0) 
			{
			printf("devGVMECardInit cannot allocate semaphore\n");
			return -1;
			}
		}

return 0;
}

//add a global var for the last value read by a call to VMERead32...
unsigned int VMERead32TempVal;

//----------------------------------
//	Remap of GammaWare function VMEWrite32() into VxWorks methodology.
//----------------------------------
void VMEWrite32(int bdnum, int regaddr,	unsigned int data)
{
int *ptr;
	epicsMutexLock(vme_driver_mutex);	//set the lock
	ptr = (int *)(daqBoards[bdnum].base32 + (regaddr)/4);
	*ptr = data;
	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
}

//----------------------------------
//	Remap of GammaWare function VMERead32() into VxWorks methodology.
//----------------------------------
unsigned int VMERead32(int bdnum, int regaddr)
{
int *ptr;
	epicsMutexLock(vme_driver_mutex);	//set the lock
	ptr = (int *)(daqBoards[bdnum].base32 + (regaddr)/4);
	VMERead32TempVal = *ptr;		//this does the actual read of the hardware by accessing *ptr.
	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
	return(VMERead32TempVal);
}


//=====================================
//  The following lines expose VMEWrite32(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires three arguments.


// Allowed values of an iocshArg are
//  iocshArgInt,iocshArgDouble,iocshArgString,iocshArgPdbbase, iocshArgArgv,iocshArgPersistentString
//=====================================
//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg VMEWriteArg0 = { "bdnum",iocshArgInt };
static const iocshArg VMEWriteArg1 = { "regddr",iocshArgInt };
static const iocshArg VMEWriteArg2 = { "data",iocshArgInt };
//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const VMEWriteArgs[3] = {&VMEWriteArg0,&VMEWriteArg1,&VMEWriteArg2};
//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is.
static const iocshFuncDef VMEWrite32FuncDef = {"VMEWrite32",3,VMEWriteArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void VMEWrite32CallFunc(const iocshArgBuf *args)
{
   VMEWrite32(args[0].ival,args[1].ival,args[2].ival);
}

//This registers the "callfunc" above as something invokable from the shell.
void VMEIOWRegistrar(void)
{
   iocshRegister(&VMEWrite32FuncDef, VMEWrite32CallFunc);
}
epicsExportRegistrar(VMEIOWRegistrar);

//=====================================
//  The following lines expose VMERead32(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires two arguments.
//
//	There appears to be no method by which a call to a function through this method
//  can actually return a value back to the IOCshell, so reads are kind of difficult.
//=====================================

//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg VMEReadArg0 = { "bdnum",iocshArgInt };
static const iocshArg VMEReadArg1 = { "regddr",iocshArgInt };

//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const VMEReadArgs[2] = {&VMEReadArg0,&VMEReadArg1};

//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is. 
static const iocshFuncDef VMERead32FuncDef = {"VMERead32",2,VMEReadArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void VMERead32CallFunc(const iocshArgBuf *args)
{
   VMERead32(args[0].ival,args[1].ival);
   printf("VMERead32 : value read is %d (0x%x)\n",VMERead32TempVal,VMERead32TempVal);
}

//This registers the "callfunc" above as something invokable from the shell.
void VMEIORRegistrar(void)
{
   iocshRegister(&VMERead32FuncDef, VMERead32CallFunc);
}
epicsExportRegistrar(VMEIORRegistrar);

//=====================================
//  The following lines expose devGVMECardInit(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires two arguments.
//
static const iocshArg devGVMECardInitArg0 = { "cardno",iocshArgInt };
static const iocshArg devGVMECardInitArg1 = { "slot",iocshArgInt };

static const iocshArg * const devGVMECardInitArgs[2] = {
       &devGVMECardInitArg0,
       &devGVMECardInitArg1};
static const iocshFuncDef devGVMECardInitFuncDef = {"devGVMECardInit",2,devGVMECardInitArgs};
static void devGVMECardInitCallFunc(const iocshArgBuf *args)
{
    devGVMECardInit(args[0].ival,args[1].ival);  
}

void devGVMERegistrar(void)
{
   iocshRegister(&devGVMECardInitFuncDef, devGVMECardInitCallFunc);
}
epicsExportRegistrar(devGVMERegistrar);

#include <ioLib.h>

// Constants
const int FLASH_BLOCK_SIZE=(128 * 1024);	 //each BLOCK of the flash is 128kByte in size
const int FLASH_BLOCKS=128;				//there are 128 blocks in the chip
const int FLASH_BUFFER_BYTES=32;

const int fpga_ctrl_reg		=	0x0900;
const int fpga_status_register 	=	0x0904;
const int vme_aux_status 	=	0x0908;
const int vme_config_control= 	0x090C;
const int fpga_gp_ctl 		=	0x0910;
//Comment from spreadsheet: Address 0x914 unused in A32D32 VME FPGA.
const int config_start 		=	0x0918;
const int config_stop 		=	0x091C;
const int fpga_version		= 	0x0920;
const int full_code_revision 	=	0x0924;
const int code_date_VME 	=	0x0928;
//Comment from spreadsheet: Address 0x92C unused in A32D32 VME FPGA.
const int vme_sandbox_a 	=	0x0930;
const int vme_sandbox_b 	=	0x0934;
const int vme_sandbox_c 	=	0x0938;
const int vme_sandbox_d 	=	0x093C;
const int code_date_2 		=	0x0940;
const int full_revision2 	=	0x0944;
const int vme_dtack_delay 	=	0x0948;
//Comment from spreadsheet: Addresses 0x94C-0x97C unused in A32D32 VME FPGA.
const int flash_address		=	0x0980;
const int flash_rd_wrt_autoinc 	=	0x0984;
const int flash_rd_wrt_no_autoinc =	0x098C;
//---------------------------------------------------------------------------------------------
// Function: 	VerifyFlash (A32/D32 version)
// Purpose:		Generic function to verify the flash memory against a file.
//---------------------------------------------------------------------------------------------
void VerifyFlash ( int bdnum, int address_control, int StopOnErrorCount_flag, char flashfname[100])
{
	int end, addr, stat, count, i;
	int message_count;
	unsigned long num_verify_error;
	char filebuf[FLASH_BUFFER_BYTES];
	int *outptr;
	int fd;
 	int address_offset;
	unsigned int flash_data_read;
	unsigned int reversed_flash_data_read;

	printf("VerifyFlash sets a mutex, neither ctrl-X nor ctrl-C will stop it\n");

	epicsMutexLock(vme_driver_mutex);	//set the lock



	if (address_control == 1) 
		address_offset = (FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE;
	else
		address_offset = 0;
	
	
 	fd = open(flashfname,  O_RDONLY, 0);
 	if (fd < 0) 
 	{
		printf("file %s : open fail with error code %d\n", flashfname,fd);	//handle bad file name error
		close(fd);
		return;		
	}

	num_verify_error = 0;
	end = address_offset + ((FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE);   //FLASH_BLOCK_SIZE is 128k (128*1024), and there are 128 FLASH_BLOCKS in the chip
												  //That is assuming BYTE addressing
	// read file in blocks, size given by FLASH_BUFFER_BYTES (const intd as 32 in old LBL code). 
	// Since file won't be an exact multiple of 32 bytes long, pad buffer at the end of the 
	// magic number of blocks with zeroes so it's all pretty.
	addr = address_offset;	//addr is counting the number of WORDS written as we write 16-bit words to the actual device
	/* Read file and compare to flash */
	count = 0;
	VMEWrite32( bdnum, flash_address, addr);// Set Flash Address to starting spot.
	// read file in blocks, size given by FLASH_BUFFER_BYTES (const intd as 32 in old LBL code). 
	// Since file won't be an exact multiple of 32 bytes long, pad buffer at the end of the 
	// magic number of blocks with zeroes so it's all pretty.
	message_count = 0;
	while (addr < end)
		{
		stat = read(fd, filebuf, FLASH_BUFFER_BYTES );
		//VxWorks only gives us num bytes read, we can't know file size.  Must assume that 0==EOF.
		if (stat != FLASH_BUFFER_BYTES) 
			{
			if (stat == 0)  		//presumably EOF
				{
				break;					
				} 
			else
				{
				if (stat < FLASH_BUFFER_BYTES) printf ("partial buffer at address %d, read %d bytes, padding 16-byte buffer with 0s\n", addr, stat);
				for (i = stat; i < FLASH_BUFFER_BYTES; i++) filebuf[i] = 0;
				count += 16;
				}
			}
		else
			{
			count += stat;
			}

		// as each buffer is read, do the read and compare against the flash.  
		outptr = (int *)filebuf;
		for (i = 0 ; i < 0x8; i++) 
			{
			// Read from Flash data with Auto Increment
			reversed_flash_data_read = VMERead32(bdnum, flash_rd_wrt_autoinc);
			//the VxWorks byte order is reversed relative to the byte order in the .bin files;
			//if the file has 0xaa995566, the board will read 0x665599aa.  So we must flip.
			//this is probably due to Linux file read byte ordering.
			flash_data_read = ((reversed_flash_data_read & 0xFF000000) >> 24);    //move bits 31:24 to bits 07:0
			flash_data_read += ((reversed_flash_data_read & 0x00FF0000) >> 8);    //move bits 23:16 to bits 15:08
			flash_data_read += ((reversed_flash_data_read & 0x0000FF00) << 8);    //move bits 15:00 to bits 23:16
			flash_data_read += ((reversed_flash_data_read & 0x000000FF) << 24);   //move bits 07:00 to bits 31:28
			// do the compare
			if (*outptr != flash_data_read) 
				{
				VMERead32(bdnum, flash_address);
				num_verify_error++;										
				printf("Data Mismatch : Addr %x Read %x expected %x\n", addr, flash_data_read,*outptr);
				}
				// return 0;						//exit on first error
			outptr++;			//advance file pointer with each read
			addr = addr + 4;	//jump address count by 4s as we're reading 32-bit values, addr count is in BYTES
			}
		message_count++;
		if (message_count > 150)
			{
			printf("Now thru addr %x\n", addr);
			message_count = 0;
			}
		if ((StopOnErrorCount_flag == 1) && (num_verify_error > 100)) break;	//exit early if too many errors
	    }	//end of WHILE over all addresses
//break at EOF jumps to here
	if (num_verify_error == 0) 
		{
		printf("Verify OK");	//tell user of success
		}
	else
		{
		printf("Verify failed, #errors: %ld\n",num_verify_error);
		}

	close(fd);
	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
	
	return;
}


//=====================================
//  The following lines expose VerifyFlash(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires four arguments.
//
//=====================================

//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg VerifyFlashArg0 = { "bdnum",iocshArgInt };
static const iocshArg VerifyFlashArg1 = { "address_control",iocshArgInt };
static const iocshArg VerifyFlashArg2 = { "StopOnErrorCount_flag",iocshArgInt };
static const iocshArg VerifyFlashArg3 = { "flashfname",iocshArgString };

//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const VerifyFlashArgs[4] = {&VerifyFlashArg0,&VerifyFlashArg1,&VerifyFlashArg2,&VerifyFlashArg3};

//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is. 
static const iocshFuncDef VerifyFlashFuncDef = {"VerifyFlash",4,VerifyFlashArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void VerifyFlashCallFunc(const iocshArgBuf *args)
{
	//before calling the function test for illegal values
#define INLOOP_MIN_BOARD_NUMBER 0		//the lowest board number (NOT SLOT) to scan
#define INLOOP_MAX_BOARD_NUMBER 6		//the highest board number (NOT SLOT) to scan
	if (args == NULL)	
		{
		printf("VerifyFlash Error: correct usage is\n\tVerifyFlash ( int bdnum, int address_control, int StopOnErrorCount_flag, char flashfname[100])\n");
		printf("\tbdnum must be between %d and %d inclusive\n",INLOOP_MIN_BOARD_NUMBER,INLOOP_MAX_BOARD_NUMBER);
		printf("\taddress_control must be 0 or 1 for lower/upper bank of flash\n");
		printf("\tStopOnErrorCount_flag must be 0 (do not stop until end of flash) or 1 (stop after 100 mismatches)\n");
		printf("\tflashfname is path to .bin file, enclosed in double-quote characters\n");
		return;
		}

	if ((args[0].ival < INLOOP_MIN_BOARD_NUMBER) || (args[0].ival > INLOOP_MAX_BOARD_NUMBER))
		{
		printf("VerifyFlash Error: 1st arg (board #) must be between %d and %d inclusive\n",INLOOP_MIN_BOARD_NUMBER,INLOOP_MAX_BOARD_NUMBER);
		return;
		}

	if ((args[1].ival < 0) || (args[0].ival > 1))
		{
		printf("VerifyFlash Error: 2nd arg (flash bank) must be 0 or 1\n");
		return;
		}

	if ((args[2].ival < 0) || (args[2].ival > 1))
		{
		printf("VerifyFlash Error: 3rd arg (error stop flag) must be 0 (no stop until end) or 1 (stop after 100 errors)\n");
		return;
		}

	if(strlen(args[3].sval) < 5)
		{
		printf("illegal file name %s\n; VerifyFlash will exit\n",args[3].sval);
		return;
		}

	VerifyFlash(args[0].ival,args[1].ival,args[2].ival,args[3].sval);
}

//This registers the "callfunc" above as something invokable from the shell.
void VerifyFlashRegistrar(void)
{
   iocshRegister(&VerifyFlashFuncDef, VerifyFlashCallFunc);
}
epicsExportRegistrar(VerifyFlashRegistrar);



//---------------------------------------------------------------------------------------------
// Function: 	EraseFlash (A32/D32 version)
// Purpose:		Erases the flash memory used for program storage in an ANL VME board.
//---------------------------------------------------------------------------------------------
void EraseFlash ( int bdnum, int address_control)
{
	int end, addr;
	unsigned int stat;
	int address_offset;
	int poll_count;


	printf("EraseFlash sets a mutex, neither ctrl-X nor ctrl-C will stop it\n");
	epicsMutexLock(vme_driver_mutex);	//set the lock

	if (address_control == 1) 
		address_offset = (FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE;
	else
		address_offset = 0;


	
	VMEWrite32(  bdnum,  flash_address,	0x0);		//set flash bus address to 0
	VMEWrite32(bdnum,fpga_status_register,0x0050);	// Clear the status register command

	VMEWrite32(  bdnum,  fpga_gp_ctl, 0x10);			//Set VPEN pin of flash high (required to erase or program)
	//
	// Erase [first quarter of] flash 
	//
	end = address_offset + ((FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE);   //FLASH_BLOCK_SIZE is 128k (128*1024), and there are 128 FLASH_BLOCKS in the chip
												  //That is assuming BYTE addressing
	// read file in blocks, size given by FLASH_BUFFER_BYTES (const intd as 32 in old LBL code). 
	// Since file won't be an exact multiple of 32 bytes long, pad buffer at the end of the 
	// magic number of blocks with zeroes so it's all pretty.
	for (addr = address_offset; addr < end; addr += FLASH_BLOCK_SIZE) 
		{
		VMEWrite32(  bdnum,  flash_address,	0x0);		//set flash bus address to 0
		VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 	0x20);		//write flash block erase command
		VMEWrite32(  bdnum,  flash_address, 	addr);		//set flash address to address of block to erase
		VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 	0xD0);		//issue erase confirm command

		//insert a small delay here before polling status
		taskDelay(10);
   	    //read flash status from 0x904.  Bit 0x80 is the 'busy' bit indicating that 
		//erase operation is in progress.  Continue polling the busy bit until the flash says it's done.
		poll_count = 0;
		do
			{
			stat = VMERead32(bdnum,  fpga_status_register); 
			poll_count++;
			taskDelay(1);
			if (poll_count > 10000)
				{
				printf("Erase ERROR: poll count on erase status > 10000\n");
				VMEWrite32(  bdnum,  flash_address, 0x0);		//set address bus of flash to 0
				VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xFF);		//switch flash back to READ ARRAY mode
				VMEWrite32(  bdnum,  fpga_gp_ctl, 0x0);	    //Set VPEN signal == 0 as erasure is done
				printf("Erase FAIL\n");
				epicsMutexUnlock(vme_driver_mutex);	//clear the lock
				return;
				}
			} while (stat & 0x80);

		printf("%d blocks erased, poll_count = %d\n", addr/FLASH_BLOCK_SIZE, poll_count);
		}

	VMEWrite32(  bdnum,  flash_address, 0x0);		//set address bus of flash to 0
	VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xFF);		//switch flash back to READ ARRAY mode
	VMEWrite32(  bdnum,  fpga_gp_ctl, 0x0);	    //Set VPEN signal == 0 as erasure is done
	printf("Erase Complete\n");

	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
	
	return;
}


//=====================================
//  The following lines expose EraseFlash(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires two arguments.
//
//=====================================

//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg EraseFlashArg0 = { "bdnum",iocshArgInt };
static const iocshArg EraseFlashArg1 = { "address_control",iocshArgInt };

//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const EraseFlashArgs[2] = {&EraseFlashArg0,&EraseFlashArg1};

//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is. 
static const iocshFuncDef EraseFlashFuncDef = {"EraseFlash",2,EraseFlashArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void EraseFlashCallFunc(const iocshArgBuf *args)
{
   EraseFlash(args[0].ival,args[1].ival);
}

//This registers the "callfunc" above as something invokable from the shell.
void EraseFlashRegistrar(void)
{
   iocshRegister(&EraseFlashFuncDef, EraseFlashCallFunc);
}
epicsExportRegistrar(EraseFlashRegistrar);


//-----------------------------------------------------------------------
// Function: 	Program Flash (A32/D32 version)
// Purpose:		Get a file name, push the data from that file into the flash ram of a VME board.
//---------------------------------------------------------------------------------------------
void ProgramFlash( int bdnum, int address_control, char flashfname[100])
{
	unsigned int stat;
	int fd;
	int end, addr, count, i;
	char filebuf[FLASH_BUFFER_BYTES];
	unsigned int *outptr;
	unsigned int tempval;
	unsigned int flip_tempval;
	int address_offset;
	int poll_count1 = 0;
	int poll_count2 = 0;
	int status_update_count = 0;
	
	epicsMutexLock(vme_driver_mutex);	//set the lock

	if(strlen(flashfname) < 5)
		{
		printf("illegal file name %s\n; ProgramFlash early exit\n",flashfname);
		return;
		}

	printf("ProgramFlash sets a mutex, neither ctrl-X nor ctrl-C will stop it\n");
 	fd = open(flashfname,  O_RDONLY,0);	//third arg of 0 required by vxWorks
 	if (fd < 0) 
	{
		printf("file open fail\n");	//handle bad file name error
		close(fd);
		return;		
	}

	if (address_control == 1) 
		address_offset = (FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE;
	else
		address_offset = 0;

	printf("Erasing flash prior to programming\n");
	EraseFlash  (bdnum, address_control); 			//Erase the flash prior to programming
	printf("Now beginning programming sequence\n");
	VMEWrite32( bdnum,  fpga_gp_ctl, 0x10);	// Set VPEN signal == 1, required to program or erase flash
	
	count = 0;
	end = address_offset + ((FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE);   //FLASH_BLOCK_SIZE is 128k (128*1024), and there are 128 FLASH_BLOCKS in the chip
												  //That is assuming BYTE addressing
	// read file in blocks, size given by FLASH_BUFFER_BYTES (const intd as 32 in old LBL code). 
	// Since file won't be an exact multiple of 32 bytes long, pad buffer at the end of the 
	// magic number of blocks with zeroes so it's all pretty.
	addr = address_offset;	//addr is counting the number of WORDS written as we write 16-bit words to the actual device

	status_update_count = 0;
	while (addr < end)
		{
		stat = read(fd, filebuf, FLASH_BUFFER_BYTES );
		//VxWorks only gives us num bytes read, we can't know file size.  Must assume that 0==EOF.
		if (stat != FLASH_BUFFER_BYTES) 
			{
			if (stat == 0)  		//presumably EOF
				{
				break;					
				} 
			else
				{
				if (stat < FLASH_BUFFER_BYTES) printf ("partial buffer at address %d, read %d bytes, padding 16-byte buffer with 0s\n", addr, stat);
				for (i = stat; i < FLASH_BUFFER_BYTES; i++) filebuf[i] = 0;
				count += 16;
				}
			}
		else
			{
			count += stat;
			}

		// as each buffer is read, shove it into the flash.  
		// send the buffer now that the flash is ready for it
		outptr = (unsigned int *)filebuf;			//create casted copy of file pointer as VME write routine wants ptr to uint
		// First tell flash that buffer is on its way
		VMEWrite32(  bdnum,  flash_address, addr);	//set address to flash
		VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xE8);		//issue WRITE BUFFER command
		VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xF);			//write #words to write minus 1 per datasheet.
		for (i = 0 ; i < 0x8; i++) 	//eight 32-bit words per buffer
	 		{
			tempval = *outptr;

			//at this point we probably have to flip the byte order around in tempval before writing.
			flip_tempval = ((tempval & 0xFF000000) >> 24);    //move bits 31:24 to bits 07:0
			flip_tempval += ((tempval & 0x00FF0000) >> 8);    //move bits 23:16 to bits 15:08
			flip_tempval += ((tempval & 0x0000FF00) << 8);    //move bits 15:00 to bits 23:16
			flip_tempval += ((tempval & 0x000000FF) << 24);   //move bits 07:00 to bits 31:28

			VMEWrite32(  bdnum,  flash_rd_wrt_autoinc, flip_tempval);		//write word from buffer to flash
			outptr++;		//advance file pointer with each write
			addr = addr + 4;	//jump address count by 4s as we're writing 32-bit values, addr count is in BYTES
	 		}
		//read flash status from 0x904.  Bit 0x80 is the 'busy' bit indicating that buffer write is in progress.
		//Continue polling the busy bit until the flash says it's done.

		//optional: insert a small delay here before polling status
		// taskDelay(5);
		poll_count1 = 0;
		do
			{
			stat = VMERead32(bdnum,  fpga_status_register); 
			poll_count1++;
//			taskDelay(1);
			if (poll_count1 > 10000)
				{
				printf("ProgramFlash ERROR: poll count on buffer write status > 10000\n");
				VMEWrite32(  bdnum,  flash_address, 0x0);		//set address bus of flash to 0
				VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xFF);		//switch flash back to READ ARRAY mode
				VMEWrite32(  bdnum,  fpga_gp_ctl, 0x0);	    //Set VPEN signal == 0 as erasure is done
				printf("ProgramFlash FAIL\n");
				epicsMutexUnlock(vme_driver_mutex);	//clear the lock
				return;
				}
			} while (stat & 0x80);

		// Issue "Program Resume" command to transfer buffer to non-volatile array
		VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xD0);
		//optional: insert a small delay here before polling status
		//taskDelay(5);
		// poll status until flash indicates copy of buffer to NV array is complete
		poll_count2 = 0;
		do
			{
			stat = VMERead32(bdnum,  fpga_status_register); 
			poll_count2++;
//			taskDelay(1);
			if (poll_count2 > 10000)
				{
				printf("ProgramFlash ERROR: poll count on NV write status > 10000\n");
				VMEWrite32(  bdnum,  flash_address, 0x0);		//set address bus of flash to 0
				VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xFF);		//switch flash back to READ ARRAY mode
				VMEWrite32(  bdnum,  fpga_gp_ctl, 0x0);	    //Set VPEN signal == 0 as erasure is done
				printf("ProgramFlash FAIL\n");
				epicsMutexUnlock(vme_driver_mutex);	//clear the lock
				return;
				}
			} while (stat & 0x80);

		// every 'n' program of block complete update the user 
		if (status_update_count == 250)
			{
			printf("Address %06X written, pollcnt 1 = %d  pollcnt 2 = %d\n", addr,poll_count1, poll_count2);
			status_update_count = 0;
			}
		else
			{
			status_update_count++;
			}
	   	}	//end of WHILE loop over all addresses.

	//final print of address after loop end
	printf("Address %06X written, pollcnt 1 = %d  pollcnt 2 = %d\n", addr,poll_count1, poll_count2);

	//after all the data is in, reset the mode of the flash chip to normal operation
	VMEWrite32(  bdnum,  flash_address, 0x0);		//reset address to zero when done
	VMEWrite32(  bdnum,  flash_rd_wrt_no_autoinc, 0xFF);		//leave flash chip in READ ARRAY mode 
	VMEWrite32(  bdnum,  fpga_gp_ctl, 0x0);	    //Set VPEN signal == 0 as programming is done

	close(fd);
	printf("Prog Complete");
	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
	return;
	
}

//=====================================
//  The following lines expose ProgramFlash(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires three arguments.
//
//=====================================

//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg ProgramFlashArg0 = { "bdnum",iocshArgInt };
static const iocshArg ProgramFlashArg1 = { "address_control",iocshArgInt };
static const iocshArg ProgramFlashArg2 = { "flashfname",iocshArgString };

//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const ProgramFlashArgs[3] = {&ProgramFlashArg0,&ProgramFlashArg1,&ProgramFlashArg2};

//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is. 
static const iocshFuncDef ProgramFlashFuncDef = {"ProgramFlash",3,ProgramFlashArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void ProgramFlashCallFunc(const iocshArgBuf *args)
{
   ProgramFlash(args[0].ival,args[1].ival,args[2].sval);
}

//This registers the "callfunc" above as something invokable from the shell.
void ProgramFlashRegistrar(void)
{
   iocshRegister(&ProgramFlashFuncDef, ProgramFlashCallFunc);
}
epicsExportRegistrar(ProgramFlashRegistrar);




//---------------------------------------------------------------------------------------------
// Function: 	ConfigureFlash (A32/D32 version)
// Purpose:		Issues a command to the VME FPGA of an ANL board to make it reconfigure the main FPGA.
//				Applicable only to boards whose VME FPGA supports reconfiguration.
//				ANL trigger boards and others derived therefrom have different bits to do this than
//				LBNL digitizer boards.
//---------------------------------------------------------------------------------------------
void ConfigureFlash ( int bdnum, int address_control)
{
	int address_offset;
	int cycle_count;
	unsigned int current_status;


	epicsMutexLock(vme_driver_mutex);	// set the lock

	if (address_control == 1) 
		address_offset = (FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE;
	else
		address_offset = 0;

	VMEWrite32( bdnum, vme_config_control, 0x2);	//acknowledge any previous configuration attempt
	VMEWrite32( bdnum, vme_config_control, 0x0);	//clear ack bit
	VMEWrite32( bdnum, vme_config_control, address_offset);
	VMEWrite32( bdnum, vme_config_control, address_offset + (FLASH_BLOCKS / 4) * FLASH_BLOCK_SIZE);
	VMEWrite32( bdnum, vme_config_control, 0x2);	//acknowledge any previous configuration attempt
	VMEWrite32( bdnum, vme_config_control, 0x0);	//clear ack bit
	VMEWrite32( bdnum, vme_config_control, 0x1);	//request reconfiguration

	cycle_count = 0;
	do
		{
		current_status = VMERead32(bdnum, fpga_status_register);
		cycle_count++;
		taskDelay(10);
		}
		while (		((current_status & 0x0002) == 0)
				&&  (cycle_count < 40)
				);

	// once the loop is done, might be nice to put some status out...
	if (cycle_count == 40)
		{
		printf("ConfigureFlash: TIMEOUT : DoneError:%d ConfigComplete:%d InitLowErr:%d InitHighErr:%d\n",
				(current_status & 0x0004) >> 2,	//bit 2 (done err)
				(current_status & 0x0002) >> 1,	//bit 1 (config complete)
				(current_status & 0x8000) >> 15,	//bit 15 (init low err)
				(current_status & 0x4000) >> 14);	//bit 14 (init hig err)
		}
	else
		{
		printf("ConfigureFlash: Config OK\n");
		}


	VMEWrite32( bdnum, vme_config_control, 0x0);	//clear request bit

	epicsMutexUnlock(vme_driver_mutex);	//clear the lock
	return;
}



//=====================================
//  The following lines expose ConfigureFlash(), defined at the
//	top of this file, as a function that can be executed from 
//	the command line of the IOCShell, and that said command line
//	invocation requires two arguments.
//
//=====================================

//an 'iocshArg' is a struct {char *name, IocshArgType type}, so these define
//what the input arguments to the function we wish to expose are.
static const iocshArg ConfigureFlashArg0 = { "bdnum",iocshArgInt };
static const iocshArg ConfigureFlashArg1 = { "address_control",iocshArgInt };

//having defined what each of the arguments to the exposed function are above,
//you then must bundle them together with a pointer to the group.
static const iocshArg * const ConfigureFlashArgs[2] = {&ConfigureFlashArg0,&ConfigureFlashArg1};

//then an iocshFuncDef structure {char *name, int nargs, iocshArg*} is made to tell 
//the IOC shell how big the stuff above is. 
static const iocshFuncDef ConfigureFlashFuncDef = {"ConfigureFlash",2,ConfigureFlashArgs};

//this is the function that actually gets called by the shell, 
//then iocshArgBuf arguments from the command line are extracted
//and passed to the real function you want.
static void ConfigureFlashCallFunc(const iocshArgBuf *args)
{
   ConfigureFlash(args[0].ival,args[1].ival);
}

//This registers the "callfunc" above as something invokable from the shell.
void ConfigureFlashRegistrar(void)
{
   iocshRegister(&ConfigureFlashFuncDef, ConfigureFlashCallFunc);
}
epicsExportRegistrar(ConfigureFlashRegistrar);
