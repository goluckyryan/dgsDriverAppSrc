/***************************************************************************************************/
//	
//		readTrigFIFO.c
//
//	Fifo manipulation functions specific and unique to the trigger modules of DGS, DFMA, etc.
//	Extracted from readFIFO.c 20180924 & forward, J.T. Anderson
//	Edited and made specific to trigger 20220713  J.T. Anderson
//
//	Function list for this file is below.  There are sections of this file:  ACTIVE routines
//	that are actually used by DAQ code and DEBUG routines that exist solely for manual activation from
//	the VxWorks command line.
//
//	==========================  ACTIVE functions =============================================
//
//	int transferTrigFifoData(int bdnum, int numlongwords, int FifoNum, int QueueUsageFlag, int *NumBytesTransferred)
//		reads data from board 'bdnum', of length 'numlongwords', from fifo FifoNum, and puts data read into the queue.
//
//	==========================  DEBUG functions =============================================
//
//	void dbgReadTrigFifo(int i, int words) dumps 'words' number of words from the FIFO associated with
//											trigger board 'i'.
//
/***************************************************************************************************/

#include <vxWorks.h>
#include <stdio.h>
#include <assert.h>
#include <taskLib.h>
#include <tickLib.h>
#include <sysLib.h>
#include <logLib.h>
#include <freeList.h>

#include <epicsMutex.h>
#include <epicsEvent.h>
#include "DGS_DEFS.h"

#include "readTrigFIFO.h"
#include "devGVME.h"

#include "QueueManagement.h"
#include "profile.h"

#ifdef READOUT_USE_DMA
	#include <cacheLib.h>
#endif

#ifdef MV5500
	epicsEventId DMASem;
#endif

extern int FBufferCount;		//defined in inLoopSupport.c
unsigned int TrigBitBucket[MAX_TRIG_RAW_XFER_SIZE];  //for reads that go nowhere (queue usage disabled)

/*****************************************************************************************************************/
//=======================  ACTIVE FUNCTIONS associated with inLoop state machine ================================
/****************************************************************************************************************/


/****************************************************************************
//
//	transferMTrigFifoData() is specific to the ANL Master Trigger module.

 *Does xfer of fifo data. attemts dma if possible.
 *Data gets stored in one of the Queue buffers. It gets buffer from free queue
 * does dma xfer, and then puits on the full queue so sender/sorter can get it.
 *
 *
 * brdnum is logics board number, 0,1,2,3 not the slot. digs will be 0,1,2,3
 *trug router will be 0,1,2,3
 *fifo address is the vme address of fifo to read.
 * if numlongwords==-1, then we will dump ONE BUIFFER LENGTH.
 * otherwise datasize will be number of 32bit words to dump
 * if numlongwords <512, no DMA done.
 * if numlongwords == 0 then no fifo read, but we send a buffer.
 ****************************************************************************/
const int FIFO_READ_ADDRESS[16] =
{
0x0160,		//MON FIFO 1	0
0x0164,		//MON FIFO 2	1
0x0168,		//MON FIFO 3	2
0x016C,		//MON FIFO 4	3
0x0170,		//MON FIFO 5	4
0x0174,		//MON FIFO 6	5
//JTA 20250512 try reading from address 0x1000 for Mon Fifo 7 now (firmware change)
//JTA 20250528 moved MON FIFO 7 to 0x5000& forward.
//0x0178,		//MON FIFO 7	6
0x5000,		//MON FIFO 7	6
0x017C,		//MON FIFO 8	7
0x0180,		//CHAN FIFO 1	8
0x0184,		//CHAN FIFO 2	9
0x0188,		//CHAN FIFO 3	10
0x018C,		//CHAN FIFO 4	11
0x0190,		//CHAN FIFO 5	12
0x0194,		//CHAN FIFO 6	13
0x0198,		//CHAN FIFO 7	14
0x019C		//CHAN FIFO 8	15
};
int transferTrigFifoData(int bdnum, long numlongwords, int FifoNum, int QueueUsageFlag, long *NumBytesTransferred)
{
	struct daqBoard *bd;	//structure of board information
	int datasize;			//amount of data to transfer

	int queue_request_stat;	//for return value from queue request function
	rawEvt *rawBuf;			//pointer to queue message buffer
	unsigned int *uintBuf;  //pointer to data read from FIFO
	
	int dmaStat = OK;
	unsigned int request_data_in_bytes = 0x10000;
	unsigned int remain_data_in_bytes = 0;

	volatile unsigned int *Read_address;	//the address from which data will be pulled  - must be volatile to ensure VME occurs when accessed


	bd = &daqBoards[bdnum];
    start_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);

//************************************************************************************
//	Verify readout depth is valid for the fifo that is selected.
//************************************************************************************
	//datasize is in bytes, num of btytes to xfer from vme fifo
	//numlongwords, the passed-in value, is in 32-bit longwords (int)
	if(inloop_debug_level >= 1) printf("TransferTrigFIFO: Fifo %d, numlongwords %ld Qmode %d\n",FifoNum,numlongwords,QueueUsageFlag);
	switch (numlongwords)
		{
		//if the user passes in -1, this means "read a whole buffers's worth".
		case -1:
			if (FifoNum == 6)	//FifoNum values are 1-16 where 1-7 are the Mon FIFOs and 8-16 are the Chan FIFOs.
				datasize=MAX_TRIG_RAW_XFER_SIZE;	//defined in DGS_DEFS.h, and is in BYTES
			else
				datasize=TRIG_MON_FIFO_SIZE;	//defined in DGS_DEFS.h, and is in BYTES
			break;
		// a value of zero is nonsensical and causes a return in error.
		case 0 :
			printf("\ntransferTrigFifo : illegal transfer length of 0 requested.  Aborting transfer.\n");
			return(-2);
			//if neither "do max" nor zero, constrain to no bigger than MAX_TRIG_RAW_XFER_SIZE
		default:
	   		datasize=numlongwords * 4;	//numlongwords is the # of 32-bit words (from register), convert to BYTES
			if (FifoNum == 6)	//trigger monitor fifo 7 is variable length, others are fixed.
				{
				if (datasize>MAX_TRIG_RAW_XFER_SIZE) 
					{
					if(inloop_debug_level >= 1) printf("\ntransferTrigFifo : datasize %d (bytes) > max transfer, chopped to %d\n", datasize,MAX_TRIG_RAW_XFER_SIZE);
					datasize=MAX_TRIG_RAW_XFER_SIZE;
					}
				}
			else	//other fifos have a different compare size
				{
				if (datasize>TRIG_MON_FIFO_SIZE) 
					{
					if(inloop_debug_level >= 1) printf("\ntransferTrigFifo : datasize %d (bytes) > max transfer, chopped to %d\n", datasize,TRIG_MON_FIFO_SIZE);
					datasize=TRIG_MON_FIFO_SIZE;
					}
				}
			break;
		} // end switch(numlongwords)
	if(inloop_debug_level >= 1) printf("transferTrigFifoData:numlongwords = %ld (longs), datasize = %d (bytes), datasize/4 = %d (longs)\n",numlongwords,datasize,datasize/4);
	//initialize the return value to what we're going to ask for....might change later in this routine if transfer error.
	*NumBytesTransferred = datasize;

//************************************************************************************
//	Set up pointers for where the data goes depending upon whether user said to 
//	use queues or not.
//************************************************************************************

	//================================================
	//	Buffer logic
	//
	//	If the user has said NOT to use queue system (QueueUsageFlag == 0) data read is transferred
	//	to the TrigBitBucket array that gets overwritten each time you use it.
	//
	//	If the queue system is on, then get a buffer from the qFree queue and read the data into that
	//	buffer.  Note that what you get from the queue is a buffer DESCRIPTOR structure (rawBuf); the
	//	rawBuf structure has a POINTER to the actual data buffer.
	//================================================
	/* msgQReceive receives a message from the qFree queue; copies message to rawBuf; return value is # of bytes received, or a magic value ERROR. */
	rawBuf = NULL;		//suppress warning, initialize pointer.
	if(QueueUsageFlag == 1) // 1 means yes, use the queue system
		{
		if(inloop_debug_level >= 2) printf("readTrigFIFO:transferTrigFifoData, QueueUsageFlag = 1\n");
		queue_request_stat = getFreeBuf(&rawBuf);	//fetch record descriptor from queue
		//handle queueing error
		if (queue_request_stat != Success)
			{
			if(inloop_debug_level >= 0) printf("transferTrigFifoData: Starved for raw buffers- throwing away data\n");
			*NumBytesTransferred = 0;

			stop_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);
			return(queue_request_stat);	//inLoop will keep pounding until one frees up.
			}
		//here we assume we have a buffer. else we would have returned!
		uintBuf = (unsigned int *) rawBuf->data;	//copy the pointer to the data buffer, recast to unsigned int for consistency with TrigBitBucket definition
		rawBuf->board =  bd->board;  //copy board # from daqBoards[] system structure into rawBuf for identification
		rawBuf->board_type =  bd->board_type;  //copy board type from daqBoards[] system structure into rawBuf for identification
		rawBuf->data_type =  FifoNum;  //Data type encodes which FIFO it came from, so outLoop can scan specifically for fifo type.  Added 20220801
		rawBuf->len = datasize/4; //Queues, unfortunately, count BYTES, but DMA wants 32-bit longword counts...

		if(inloop_debug_level >= 2) 
			printf("readTrigFIFO:transferTrigFifoData, board (%d), board_type (%d), data_type (%d), len (%d)\n", rawBuf->board, rawBuf->board_type, rawBuf->data_type, rawBuf->len);

		}
	else	//user said not to use queues
		{
		rawBuf = NULL;
		uintBuf = &TrigBitBucket[0];
		if(inloop_debug_level >= 1) printf("transferTrigFifoData:Queues disabled, dumping to TrigBitBucket\n");
		}

//************************************************************************************
//	Suck data using DMA mode.  Chunked into multiple transfers if more than DMA
//	can do in one big block. 
//************************************************************************************

	//First figure out what address we are reading from.
	Read_address = bd->base32 + (FIFO_READ_ADDRESS[FifoNum]/4); // Ryan 20250429, change FIFO_READ_ADDRESS index from 16 to 6  //JTA:20250512: fifo num is passed arg

	//ensure readout is chunked into multiple DMA blocks if more data is available than
	//can be correctly transferred by DMA
	remain_data_in_bytes = datasize;
    do
		{
		//JTA:20250607: empirical testing shows that readouts with length > 0x10000 bytes occur with specified length
		//over the VME bus, and the data is read from the board, but we see the DMA return an error and only transfer
		//0x10000 bytes to the buffer.
		if( remain_data_in_bytes > DMA_CHUNK_SIZE_IN_BYTES ) 	
			{
			request_data_in_bytes = DMA_CHUNK_SIZE_IN_BYTES;	
			}
		else
			{
			request_data_in_bytes = remain_data_in_bytes;
			}
		remain_data_in_bytes = remain_data_in_bytes - request_data_in_bytes;
		if(inloop_debug_level >= 1) printf("datasize : %d| request %d , remain %d\n",datasize, request_data_in_bytes, remain_data_in_bytes);
		//actual DMA transfer.

		dmaStat = sysVmeDmaV2LCopy((unsigned char *)Read_address,(unsigned char *)uintBuf, request_data_in_bytes);

			//The possible returns here are
			//  OK
			//	ERROR (driver not initialized, or invalid argument)
			//	DGCS_LERR	(PCI bus error)
			//	DGCS_VERR	(VME error)
			//	DGCS_P_ERR	(protocol error)

		if (dmaStat != OK) 
			{
			printf("transferTrigFifoData:DMA Error: transfer returned %d (xfer 1)\n", dmaStat);
			*NumBytesTransferred = 0;
			//if DMA failed, presumably we here return the message back to the queue.
			//In the hopes the transfer will be tried again somewhere?
			if(QueueUsageFlag == 1)
				{
				queue_request_stat = putFreeBuf(rawBuf);
				//It's possible the queue overflows at this point, so there should be a test
				if(queue_request_stat != Success) 
					{
					stop_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);
					return(queue_request_stat);
					}
				}	//if(QueueUsageFlag == 1)
			stop_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);
			return(DMAError);
	   		} //end if (dmaStat != OK) 
		else 
			{
			if(inloop_debug_level >= 1) printf("transferTrigFifoData: success : stat %d datasize %d Bytes\n",dmaStat,datasize);
			} //end else clause if (dmaStat != OK) 

		// All trigger readouts should start with 0x0000AAAA.  Check this here before passing to queue.
		if (*uintBuf == 0x0000AAAA)
			{
			if(inloop_debug_level >= 1) printf("\ntransferTrigFifoData:  data start correct\n");
			}
		else
			{
			printf("\ntransferTrigFifoData: data start ERROR: expect 0x0000AAAA, got %08X\n", *uintBuf);
			}

		uintBuf = uintBuf + (request_data_in_bytes/4);		//move the pointer by the number of unsigned ints (32-bit objects in VxWorks).
		}while(remain_data_in_bytes > 0 );		//end of do loop starting at line 222

	//
	// at this point the data is transferred from trigger's fifo and is in rawBuf->data if queueing is enabled.
	// Change the state of the buffer to OWNER_Q_WRITTEN, and then enter the buffer 
	// into the qWritten queue.
	if(QueueUsageFlag == 1)
		{
		rawBuf->board = bdnum;

		rawBuf->len = datasize;		//push in length of buffer, in bytes.
		queue_request_stat = putWrittenBuf(rawBuf);
		//It's possible the queue overflows at this point, so there should be a test
		if(queue_request_stat != Success) 
			{
		   	stop_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);
			return(queue_request_stat);
			}
		}

	stop_profile_counter(PROF_IL_XFER_TRIG_FIFO_DATA);
	return(Success);   //if we haven't exited yet, declare success...

}


/****************************************************************************
 *	TriggerTypeFHeader() is called whenever there is no data in a digitizer
 *	or else the run is ending.  Either way, the function grabs a buffer off of
 *  the 'free' queue, stuffs in some data based upon the arguments provided and/or
 *  reads of the module, and then returns with a status value from the
 *  TransferFIFOReturnVals enum defined in DGS_DEFS.h.
 *
 * Arguments:
 *		mode is 0 if this is an 'update' header, 1 if this is an 'end of run' header.
 *		BoardNumber is the board number (packed in header data); from this we get the VME addresses of the board.
 *		mode of 2 means "error report"

****************************************************************************/

//Helper function for TriggerTypeFHeader
int PushTrigTypeFToQueue(rawEvt *rawBuf, int BoardNumber, int QueueUsageFlag)
{
#ifdef DISABLE_ALL_TYPE_F_RESPONSE
	if(inloop_debug_level >= 3) printf("PushTrigTypeFToQueue called, but F response disabled\n");
	return(Success);
#else
	FBufferCount++;		//increment count of F buffers pushed to queue

	if(QueueUsageFlag == 1)
		{
		if(inloop_debug_level >= 3) printf("Pushing Trigger Type F; F buffer count now %d\n",FBufferCount );
		rawBuf->board = BoardNumber;
		rawBuf->len = 16;		//push in length of buffer, in bytes.  For type F this is one type F header (4 32-bit words == 16 bytes)
		return(putWrittenBuf(rawBuf));
//		DumpRawEvt (rawBuf, "TriggerTypeF", 4,0);
		}
	else return(Success);   //Success defined in the enum BufReturnVals, in DGS_DEFS.h
#endif
}



int TriggerTypeFHeader(int mode, int FifoNum, int BoardNumber, int QueueUsageFlag)
{
	rawEvt *rawBuf;			//pointer to queue message buffer
	unsigned int TS_LSword, TS_Midword, TS_MSword;
	unsigned int *OutBufDataPtr;	//for running through buf_to_process->data
	int queue_request_stat;	//for return value from queue request function
	char *TypeOfBoard;

#ifdef DISABLE_ALL_TYPE_F_RESPONSE
	return(0);
#else
	
	/* get a raw data buffer for this board */
	/* msgQReceive receives a message from the qFree queue; copies message to rawBuf; return value is # of bytes received, or a magic value ERROR. */
	/* The length of the transfer is sizeof(int) as passed */
	rawBuf = NULL;		//suppress warning, initialize pointer.


	TypeOfBoard = &(BoardTypeNames[daqBoards[BoardNumber].board_type][0]);	//pointer to char[30]

	if(QueueUsageFlag == 1) // 1 means yes, use the queue system
		{
		queue_request_stat = getFreeBuf(&rawBuf);		//rawBuf is a pointer to a rawEvt struct, taken from the queue of pointers to rawEvt structs
		if (queue_request_stat != Success)
			{
			if(inloop_debug_level >= 0) printf("TriggerTypeFHeader : Starved for raw buffers- throwing away data\n");
			return(queue_request_stat);	//inLoop will keep pounding until one frees up.
			}
		//here we assume we have a buffer. else we would have returned!
		OutBufDataPtr = rawBuf->data;  //rawBuf->data is a pointer to an array of unsigned ints, the actual data buffer.
		if(inloop_debug_level >= 2) printf("TriggerTypeFHeader : Got buffer\n" );
		rawBuf->board_type = daqBoards[BoardNumber].board_type;
		}
	else
		{
		rawBuf = NULL;
		OutBufDataPtr = &TrigBitBucket[0];	//if not using queues, OutBufDataPtr instead points to a stand-alone buffer that gets reused over and over.
		}
	
	//========================================================================================
	//Build the 'event header'.
	//To make the data palatable for the sender/receiver/event builder, the first 4
	//words must be as follows.
	//0	FIXED 0xAAAAAAAA
	//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
	//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]
	//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]

	//The PACKET LENGTH is defined as the # of 32-bit longwords in the event, not counting
	//the 0xAAAAAAAA word.

	//The HEADER LENGTH is defined as the # of 32-bit longwords in the header, not counting
	// the 0xAAAAAAAA word.

	//  The mode argument to this function declares the details of the 1st and 3rd words
	//========================================================================================
	switch(mode)
		{	
		case 0:	//mode 0: update header (trigger was empty when polled)
#ifdef INLOOP_GENERATE_EMPTY_TYPEF
			//0	FIXED AAAAAAAA
			*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the header as defined by email communication.

			//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--0xE for Empty
			//	 |				  |					   +--as taken from the board
			//	 |				  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
			//	 +-- taken from daq_board structure (->base value)
			*OutBufDataPtr  = 
							(daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
							+ 0x00030000		//packet length aligned to bit 16
							+ ((daqBoards[BoardNumber].TrigUsrPkgData & 0xFFF) << 4)
							+ 0x0000000E;		//0xE for empty
			OutBufDataPtr++;
			//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
			//20250521: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
			*(daqBoards[BoardNumber].base32 + (0x8E0/4)) = 0x00000010;	//set bit 4 of pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
			TS_LSword = *(daqBoards[BoardNumber].base32 + (0x288/4));	//read bits 15:00 of latched timestamp
			TS_Midword = *(daqBoards[BoardNumber].base32 + (0x284/4));	//read bits 31:16 of latched timestamp
			TS_MSword = *(daqBoards[BoardNumber].base32 + (0x280/4));	//read bits 47:32 of latched timestamp
			if(inloop_debug_level >= 2) printf("TriggerTypeFHeader: FIFO of %s module #%d is EMPTY at timestamp 0x%04X%04X%04X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_Midword, TS_LSword);
			*OutBufDataPtr  = (TS_Midword << 16) + TS_LSword; OutBufDataPtr++;
	
			//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--previously read
			//	 |				  |					   +--by fiat, the value 0xF
			//	 |				  +--set to the value 0, defined as "this is information"
			//	 +-- defined as 3
			*OutBufDataPtr  = 0x0C000000	//header length of 3, event type is informational.
							+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
							+ TS_MSword;	//timestamp, bits 47:32
			OutBufDataPtr++;
	
			//After writing the type F header, do queue maintenance.
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return (PushTrigTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif
	
#ifndef INLOOP_GENERATE_EMPTY_TYPEF
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
#endif

			break;
//=========================================================================================================================================

		case 1:	//mode 1: issue header stating that trigger is empty and will not have new data (End of Data)   
#ifdef INLOOP_GENERATE_EOD_TYPEF
			//0  Fixed AAAAAAAA
			OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
			*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header
	
			//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--0xD for Run is Done
			//	 |				  |					   +--as taken from digitizer
			//	 |				  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
			//	 +-- taken from daq_board structure (->base value)
			*OutBufDataPtr  = 
							(daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
							+ 0x00030000		//packet length aligned to bit 16
							+ ((daqBoards[BoardNumber].TrigUsrPkgData & 0xFFF) << 4)
							+ 0x0000000D;		//0xD for done
			OutBufDataPtr++;
	
			//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
			//20250521: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
			*(daqBoards[BoardNumber].base32 + (0x8E0/4)) = 0x00000010;	//set bit 4 of pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
			TS_LSword = *(daqBoards[BoardNumber].base32 + (0x288/4));	//read bits 15:00 of latched timestamp
			TS_Midword = *(daqBoards[BoardNumber].base32 + (0x284/4));	//read bits 31:16 of latched timestamp
			TS_MSword = *(daqBoards[BoardNumber].base32 + (0x280/4));	//read bits 47:32 of latched timestamp
			if(inloop_debug_level >= 1) printf("TriggerTypeFHeader: EndOfData announced for %s module #%d at timestamp 0x%04X%04X%04X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_Midword, TS_LSword);
			*OutBufDataPtr  = (TS_Midword << 16) + TS_LSword; OutBufDataPtr++;
	
			//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--previously read
			//	 |				  |					   +--by fiat, the value 0xF
			//	 |				  +--set to the value 0, defined as "this is information"
			//	 +-- defined as 3
			*OutBufDataPtr  = 0x0C000000	//header length of 3, event type is informational.
							+ 0x000F0000	//mark as end-of-run header with header type 15 decimal (0xF)
							+ TS_MSword;	//timestamp, bits 47:32
			OutBufDataPtr++;
	
			//After writing the type F header, do queue maintenance.
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return (PushTrigTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif
	
#ifndef INLOOP_GENERATE_EOD_TYPEF
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
#endif

			break;
//=========================================================================================================================================


		case 2:	//mode 2: issue header stating that trigger overflowed and was forcibly cleared.
#ifdef INLOOP_GENERATE_ERROR_TYPEF
			//0  Fixed AAAAAAAA
			OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
			*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header

			//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--0xF means FIFO ERROR
			//	 |				  |					   +--package data from digitizer.
			//	 |				  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
			//	 +-- taken from daq_board structure (->base value)
			*OutBufDataPtr  = 
							(daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
							+ 0x00030000		//packet length aligned to bit 16
							+ ((daqBoards[BoardNumber].TrigUsrPkgData & 0xFFF) << 4)
   						 + 0x0000000F;		//0xF for FIFO issue
			OutBufDataPtr++;

			//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
			//20250521: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
			*(daqBoards[BoardNumber].base32 + (0x8E0/4)) = 0x00000010;	//set bit 4 of pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
			TS_LSword = *(daqBoards[BoardNumber].base32 + (0x288/4));	//read bits 15:00 of latched timestamp
			TS_Midword = *(daqBoards[BoardNumber].base32 + (0x284/4));	//read bits 31:16 of latched timestamp
			TS_MSword = *(daqBoards[BoardNumber].base32 + (0x280/4));	//read bits 47:32 of latched timestamp
			if(inloop_debug_level >= 0) printf("TriggerTypeFHeader: FIFO Overflow Error announced for %s module #%d at timestamp 0x%04X%04X%04X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF),TS_Midword, TS_LSword);
			*OutBufDataPtr  = (TS_Midword << 16) + TS_LSword; OutBufDataPtr++;
			//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--previously read
			//	 |				  |					   +--by fiat, the value 0xF
			//	 |				  +--value is 1 : overflow error
			//	 +-- defined as 3
			*OutBufDataPtr  = 0x0C800000	//header length of 3, event type 1 (overflow)
							+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
							+ TS_MSword;	//timestamp, bits 47:32
			OutBufDataPtr++;
	
			//After writing the type F header, do queue maintenance.
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return (PushTrigTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif

#ifndef INLOOP_GENERATE_ERROR_TYPEF
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
#endif

			break;
//=========================================================================================================================================

	case 3:	//mode 3: issue header stating that trigger UNDERFLOWED and was forcibly cleared.
#ifdef INLOOP_GENERATE_ERROR_TYPEF
			//0  Fixed AAAAAAAA
			OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
			*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header
	
			//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--0xF means FIFO ERROR
			//	 |				  |					   +--package data as read from digitizer
			//	 |				  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
			//	 +-- taken from daq_board structure (->base value)
			*OutBufDataPtr  = 
							(daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
							+ 0x00030000		//packet length aligned to bit 16
							+ ((daqBoards[BoardNumber].TrigUsrPkgData & 0xFFF) << 4)
							+ 0x0000000F;		//0xF for FIFO Error
			OutBufDataPtr++;
	
			//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
			//20250521: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
			*(daqBoards[BoardNumber].base32 + (0x8E0/4)) = 0x00000010;	//set bit 4 of pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
			TS_LSword = *(daqBoards[BoardNumber].base32 + (0x288/4));	//read bits 15:00 of latched timestamp
			TS_Midword = *(daqBoards[BoardNumber].base32 + (0x284/4));	//read bits 31:16 of latched timestamp
			TS_MSword = *(daqBoards[BoardNumber].base32 + (0x280/4));	//read bits 47:32 of latched timestamp
			if(inloop_debug_level >= 0) printf("TriggerTypeFHeader: FIFO Underflow Error announced for %s module #%d at timestamp 0x%04X%04X%04X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF),TS_Midword, TS_LSword);
			*OutBufDataPtr  = (TS_Midword << 16) + TS_LSword; OutBufDataPtr++;
			//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
			//	 ^				  ^					   ^				  ^
			//	 |				  |					   |				  +--previously read
			//	 |				  |					   +--by fiat, the value 0xF
			//	 |				  +--value is 2 : underflow error
			//	 +-- defined as 3
			*OutBufDataPtr  = 0x0D000000	//header length of 3, event type 2 (underflow)
							+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
							+ TS_MSword;	//timestamp, bits 47:32
			OutBufDataPtr++;
	
			//After writing the type F header, do queue maintenance.
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return (PushTrigTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif
	
#ifndef INLOOP_GENERATE_ERROR_TYPEF
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
#endif
			break;
//=========================================================================================================================================
		default:
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
			return(IncorrectModeArg);
			break;

		}	//end switch(mode)
return(-9999);
#endif
}

/***********************************************************************************/
//
//  dbgReadTrigFifo() reads 'n' words from the FIFO of a trigger and dumps data
//  to the console.  NOT USED BY DAQ.  CONSOLE DEBUG USE ONLY.  
//
// const int FIFO_READ_ADDRESS[16] =
// {
// 0x0160,		//MON FIFO 1	0
// 0x0164,		//MON FIFO 2	1
// 0x0168,		//MON FIFO 3	2
// 0x016C,		//MON FIFO 4	3
// 0x0170,		//MON FIFO 5	4
// 0x0174,		//MON FIFO 6	5
// 0x0178,		//MON FIFO 7	6
// 0x017C,		//MON FIFO 8	7
// 0x0180,		//CHAN FIFO 1	8
// 0x0184,		//CHAN FIFO 2	9
// 0x0188,		//CHAN FIFO 3	10
// 0x018C,		//CHAN FIFO 4	11
// 0x0190,		//CHAN FIFO 5	12
// 0x0194,		//CHAN FIFO 6	13
// 0x0198,		//CHAN FIFO 7	14
// 0x019C		//CHAN FIFO 8	15
// };

/***********************************************************************************/
unsigned int BitBucket[MAX_TRIG_RAW_XFER_SIZE];  //for reads that go nowhere (queue usage disabled)
void dbgReadTrigFifo(int board, int numlongwords, int mode, int FIFO_IDX)
{
    int datasize_in_bytes;			//amount of data to transfer
    int datasize_in_longwords;			//amount of data to transfer
    unsigned int *uintBuf;  //pointer to data read from FIFO
	int j;
    struct daqBoard *bd;	//structure of board information
    int dmaStat = OK;
    const int MON_FIFO7 = 6;
    int startDisplayIndex = 0;
	long fillcnt;
	int request_data_in_bytes = 0x10000;
	int remain_data_in_bytes = 0;
	int count = 0;

    bd = &daqBoards[board];

	for(fillcnt=0;fillcnt<MAX_TRIG_RAW_XFER_SIZE;fillcnt++) BitBucket[fillcnt] = 0xFFFFFFFF;

    //datasize_in_bytes is in bytes, num of btytes to xfer from vme fifo
    //constrain to no bigger than MAX_TRIG_RAW_XFER_SIZE
    if (numlongwords== 0)
		{
		if(FIFO_IDX == MON_FIFO7)
			{
			if(inloop_debug_level >= 1) printf("MON FIFO 7 Requested with length of zero\n");
//			datasize_in_longwords = *(daqBoards[board].base32 + (0x154/4));	//read from live depth of MON7 and use that.
			datasize_in_longwords = *(daqBoards[board].base32 + (0x1AC/4));	//read from latched depth of MON7 and use that.
			if(inloop_debug_level >= 1) printf("Data available = %d words\n",datasize_in_longwords);
			}
		else
			{
			if(inloop_debug_level >= 1) printf("Diagnostic FIFO requested with length==0, length of 256 longwords used\n");
			datasize_in_longwords = 256;
			}
		}
    else 
		{
		if (numlongwords > 0)
			{
	        datasize_in_longwords=numlongwords;	//use value user set
		    if (datasize_in_longwords>MAX_TRIG_RAW_XFER_SIZE) datasize_in_longwords=MAX_TRIG_RAW_XFER_SIZE;
			if(inloop_debug_level >= 1) printf("user stated length = %d, using %d after max length check\n", numlongwords, datasize_in_longwords);
			}
		else //user entered negative value, set to MAX_TRIG_RAW_XFER_SIZE
			{
			datasize_in_longwords=MAX_TRIG_RAW_XFER_SIZE;
			if(inloop_debug_level >= 1) printf("negative input, using length = %d\n", datasize_in_longwords);
			}
		}

	datasize_in_bytes = datasize_in_longwords * 4;			//convert value read (longwords) to number of BYTES to DMA


    if(inloop_debug_level >= 2) printf("numlongwords %d (longs), datasize_in_bytes %d (bytes) datasize_in_bytes/4 %d (longs)\n",numlongwords,datasize_in_bytes,datasize_in_longwords);
    uintBuf = &BitBucket[0];

	if(mode == 1)	//dma mode
		{


		remain_data_in_bytes = datasize_in_bytes;
		
        do{

			if( remain_data_in_bytes > 0x10000 ) {
				request_data_in_bytes = 0x10000;	
			}else{
				request_data_in_bytes = remain_data_in_bytes;
			}
			remain_data_in_bytes = remain_data_in_bytes - request_data_in_bytes;
		
			
			if(inloop_debug_level >= 1) printf("datasize : %d| request %d , remain %d\n",datasize_in_bytes, request_data_in_bytes, remain_data_in_bytes);

			dmaStat = sysVmeDmaV2LCopy(
					(unsigned char *)(daqBoards[board].base32 + (FIFO_READ_ADDRESS[FIFO_IDX] / 4)),
					(unsigned char *)(uintBuf + count),
					 request_data_in_bytes);  //the actual read....
			if (dmaStat != OK) {
				printf("DMA Error: transfer returned %d (xfer 1)\n", dmaStat);
			}else {
				if(inloop_debug_level >= 1) printf("DMA success : stat %d datasize_in_bytes %d\n",dmaStat,datasize_in_bytes);
			}

			count += 0x4000;

		}while(remain_data_in_bytes > 0 );

	}
	else
		{
		if(inloop_debug_level >= 1) printf("Dumping %d words to buffer, not using DMA\n",(datasize_in_longwords));
        for (j = 0; j < (datasize_in_longwords); j++) 
			{
			BitBucket[j] = *(daqBoards[board].base32 + (FIFO_READ_ADDRESS[FIFO_IDX]/4));  //longword by longword, pump mud.
			}
		}

	if (inloop_debug_level >= 2 ) 
		{
		//either way you get it, now dump the data.
		  if( datasize_in_longwords > 0x2C00 ) startDisplayIndex = 0x4000 - 100;
		for (j = startDisplayIndex; j < (datasize_in_longwords); j++) 
			{
			printf("index:%04d    data:%08X\n",j,BitBucket[j]);
			}
		}

}

