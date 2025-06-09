#include <epicsTypes.h>
#include <epicsMessageQueue.h>
#include <epicsTime.h>
#include "asynPortDriver.h"



extern "C" int asynDebugConfig(
	const char *portName, 
	int card_number);

void asynDebugDriver_Task(void *drvPvt);

class epicsShareFunc asynDebugDriver : public asynPortDriver{
public:
    asynDebugDriver(const char *portName, int card_number);
	asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
	asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
	void CommandHandlerTask(void);
	int getIntParam(int param);
//	asynStatus writeInt8Array(asynUser *pasynUser,epicsInt8 *value,size_t nElements);
//	asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value, size_t nElements, size_t *nIn);
	int viOut32 (int slot, int adr_space, int reg_adr, int data);
	int viIn32 (int slot, int adr_space, int reg_adr, int *data);
	void resetRead(void);
	void resetWrite(void);
	int read(char*outbuf,int length);
	int write(char*inbuf,int length);
	int programFlash (int VME_slot, int fpga_flash_baseaddress);
	int eraseFlash  (int VME_slot, int fpga_flash_baseaddress);
	int verifyFlash (int VME_slot, int fpga_flash_baseaddress);  
	int readFlash (int VME_slot, int fpga_flash_baseaddress);  
	void ConfigureFlash ( int VME_slot, int fpga_flash_baseaddress);
	int flipEndian(int val);
	//to read as a stream from fpga_prog_data
	int read(int*outbuf,int length);
	//to write as a stream to fpga_prog_data2
	int write(int*inbuf, int length);
	
	char driverName[512];	
		
	//
	// This is where we define parameters
	//

	//here we have debug registers so we can write any address. we can write address in card, say 0x804, or
	//we can write long address, where we write not address offset rel. to card, but address in crate
	int dbg_address;
	int dbg_long_address;
	int dbg_value;
	int dbg_value_read;
	int dbg_write_addr;
	int dbg_read_addr;    
	int dbg_write_long_addr;
	int dbg_read_long_addr;
	int dbg_card_number;

//============================
//	fpga params expunged 202420221 JTA
//============================
#if 0
	// here are params for programming fpga
	int fpga_program;
	int fpga_board;
	int fpga_erase;
	int fpga_verify;
	int fpga_read;
	int fpga_data;
	int fpga_datarb;
	//added 20190411
	int fpga_flash_baseaddress;	//values are 0 (use base/default flash area) and 1 (use test flash area)
	//end add
	//1 to send large file in small blacks to waveform
	int fpga_startsend;
	int fpga_blocksize;

	int fpga_updatewave;
	int fpga_updatewave2;


	int fpga_runcounter;
	int rebootcrate;

//  real trig module params
	int fpga_fail_flashdata;
	int fpga_verifyok;
	int fpga_data_len;
	int fpga_fail_addr;
	int fpga_fail_filedata;
#endif 




	int fpga_message;


// 
// end parameters
//

int card_number;

enum {num_params = 8};		//updated 20240221 jta

enum {fpga_prog_size=10000000};

//128*1024 is 131072

enum 
{
	FLASH_BLOCK_SIZE =131072,
	 FLASH_BLOCKS= 128,
	FLASH_BUFFER_BYTES=32,
	VI_A32_SPACE=0
};
	

char fpga_prog_data[fpga_prog_size];
char fpga_prog_data2[fpga_prog_size];
int rd_addr;
int wr_addr;

char msgx[512];

    
 
    
    
    };
    
