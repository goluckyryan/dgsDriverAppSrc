/***************************************************************************************************/
//	
//		readDigFIFO.c
//
//	Fifo manipulation functions specific and unique to the digitizer modules of DGS, DFMA, etc.
//	Extracted from readFIFO.c 20180924 & forward, J.T. Anderson
//
//	Function list for this file is below.  There are sections of this file:  ACTIVE routines
//	that are actually used by DAQ code and DEBUG routines that exist solely for manual activation from
//	the VxWorks command line.
//
//	==========================  ACTIVE functions =============================================
//
//	int transferDigFifoData(int bdnum, int numwords)	reads 'numwords' from the digitizer fifo and stuffs that
//														into some buffer.
//
//	==========================  DEBUG functions =============================================
//
//	void dbgReadDigFifo(int i, int words) dumps 'words' number of words from the FIFO associated with
//											digitzer board 'i'.
//
/***************************************************************************************************/

#ifdef linux
	#include "readFIFO_linux.c"
#else
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


	#include "readDigFIFO.h"
	#include "devGVME.h"

	#include "QueueManagement.h"
	#include "profile.h"

	#ifdef READOUT_USE_DMA
		#include <cacheLib.h>
	#endif

	#ifdef MV5500
		epicsEventId DMASem;
	#endif

#endif

extern int FBufferCount;		//defined in inLoopSupport.c
unsigned int BitBucket[MAX_DIG_RAW_XFER_SIZE];  //for reads that go nowhere (queue usage disabled)


/*****************************************************************************************************************/
//=======================  ACTIVE FUNCTIONS associated with inLoop state machine ================================
/****************************************************************************************************************/


/****************************************************************************
//
//	transferDigFifoData() is specific to the ANL digitizer.

 *Does xfer of fifo data. attemts dma if possible.
 *Data gets stored in one of the Queue buffers. It gets buffer from free queue
 * does dma xfer, and then puits on the full queue so sender/sorter can get it.
 *
 *
 * brdnum is logics board number, 0,1,2,3 not the slot. digs will be 0,1,2,3

 *fifo address is the vme address of fifo to read.

* Numwords is amount of data desired (longwords)
 ****************************************************************************/
int transferDigFifoData(int bdnum, long numlongwords, int QueueUsageFlag, long *NumBytesTransferred){
	struct daqBoard *bd;	//structure of board information
	long DMA_length_in_bytes;		//amount of data to transfer in each DMA
	long datasize_in_bytes;			//total number of bytes we want
	long data_remaining_in_bytes;
	int queue_request_stat;	//for return value from queue request function
	rawEvt *rawBuf;			//pointer to queue message buffer
	unsigned int *uintBuf;  //pointer to data read from FIFO

	int dmaStat = OK;

	start_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);

	bd = &daqBoards[bdnum];

	//datasize_in_bytes is in bytes, num of btytes to xfer from vme fifo
	//numwords, the passed-in value, is in 32-bit longwords (int)
	switch (numlongwords){
		// a value of zero is nonsensical and causes a return in error.
		case 0 :
			if(inloop_debug_level >= 0) printf("\ntransferDigFifo : illegal transfer length of 0 requested.  Aborting transfer.\n");
			return(-2);
		    //if neither "do max" nor zero, constrain to no bigger than MAX_DIG_RAW_XFER_SIZE
		default:
			datasize_in_bytes = numlongwords * 4;	//numwords is the # of 32-bit words (from register), convert to BYTES
			if (datasize_in_bytes > MAX_DIG_RAW_XFER_SIZE) {
				if(inloop_debug_level >= 1) printf("\ntransferDigFifo : datasize_in_bytes %ld (bytes) > max transfer, will do multiple DMAs of %d each\n", datasize_in_bytes,MAX_DIG_RAW_XFER_SIZE);
				DMA_length_in_bytes = MAX_DIG_RAW_XFER_SIZE;
			}else{
				DMA_length_in_bytes = datasize_in_bytes;
			}
			break;
	} // end switch(numwords)
	if(inloop_debug_level >= 2) printf("numlongwords %ld , datasize_in_bytes %ld (bytes) datasize_in_bytes/4 %ld (longs)\n",numlongwords,datasize_in_bytes,datasize_in_bytes/4);
	//initialize the return value to what we're going to ask for....might change later in this routine if transfer error.
	*NumBytesTransferred = datasize_in_bytes;

	//================================================
	//	Buffer logic
	//
	//	If the user has said NOT to use queue system (QueueUsageFlag == 0) data read is transferred
	//	to the BitBucket array that gets overwritten each time you use it.
	//
	//	If the queue system is on, then get a buffer from the qFree queue and read the data into that
	//	buffer.  Note that what you get from the queue is a buffer DESCRIPTOR structure (rawBuf); the
	//	rawBuf structure has a POINTER to the actual data buffer.
	//================================================
	/* msgQReceive receives a message from the qFree queue; copies message to rawBuf; return value is # of bytes received, or a magic value ERROR. */
	rawBuf = NULL;		//suppress warning, initialize pointer.
	if(QueueUsageFlag == 1){ // 1 means yes, use the queue system
		queue_request_stat = getFreeBuf(&rawBuf);	//fetch record descriptor from queue
		//handle queueing error
		if (queue_request_stat != Success){
			if(inloop_debug_level >= 0) printf("Xfr Dig FIFO : Starved for raw buffers- throwing away data\n");
			*NumBytesTransferred = 0;

			stop_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);
			return(queue_request_stat);	//inLoop will keep pounding until one frees up.
		}
		//here we assume we have a buffer. else we would have returned!
		uintBuf = (unsigned int *) rawBuf->data;	//copy the pointer to the data buffer, recast to unsigned int for consistency with BitBucket definition
		rawBuf->board =  bd->board;  //copy board # from daqBoards[] system structure into rawBuf for identification
		rawBuf->board_type =  bd->board_type;  //copy board type from daqBoards[] system structure into rawBuf for identification  Added 20220801
		rawBuf->data_type =  0;  //Digitizers, by definition, have a data type of 0.  Added 20220801
		rawBuf->len = datasize_in_bytes; //this is the full data size derived from reading the digitizer fifo depth, in bytes.
	}else{	//user said not to use queues      
		rawBuf = NULL;
		uintBuf = &BitBucket[0];
		if(inloop_debug_level >= 2) printf("Queues disabled, dumping to BitBucket\n");
	}
	//we dma directly to the buffer if DMA is enabled.

#ifdef MV5500
		//DMA from fifo of digitizer to uintBuf.
		//The possible returns here are
		//  OK
		//	ERROR (driver not initialized, or invalid argument)
		//	DGCS_LERR	(PCI bus error)
		//	DGCS_VERR	(VME error)
		//	DGCS_P_ERR	(protocol error)
		// sysVmeDmaV2LCopy (UCHAR  *localVmeAddr,UCHAR *localAddr, UINT32 nbytes);
		if( inloop_debug_level >= 1) printf("About to take DMA semaphore\n");
		dmaStat = sysVmeDmaV2LCopy((unsigned char *)bd->FIFO,(unsigned char *)uintBuf, DMA_length_in_bytes);  //the actual read....
#endif

	if (dmaStat != OK) {
		if(inloop_debug_level >= 0) printf("DMA Error: transfer returned %d (xfer 1)\n", dmaStat);
		*NumBytesTransferred = 0;
		//if DMA failed, presumably we here return the message back to the queue.
		//In the hopes the transfer will be tried again somewhere?
		if(QueueUsageFlag == 1) {
			queue_request_stat = putFreeBuf(rawBuf);
			//It's possible the queue overflows at this point, so there should be a test
			if(queue_request_stat != Success) {
				stop_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);
				return(queue_request_stat);
			}
		}	//if(QueueUsageFlag == 1)
		stop_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);
		return(DMAError);
	}else{ //end if (dmaStat != OK) 
		if(inloop_debug_level >= 2) printf("DMA success : stat %d \n",dmaStat);
	} //end else clause if (dmaStat != OK) 

	// All readouts should start with 0xAAAAAAAA.  Check this here before passing to queue.
	if (*uintBuf == 0xAAAAAAAA){
		if(inloop_debug_level >= 2) printf("\nReadDigFifo : data start correct\n");
	}else{
		if(inloop_debug_level >= 0) printf("\nReadDigFifo : data start ERROR: expect 0xAAAAAAAA, got %08X\n", *uintBuf);
	}

	//20230414: Calculate whether there is a 2nd DMA required, and if so do it.

	data_remaining_in_bytes = datasize_in_bytes - DMA_length_in_bytes;	//calculate amount of data remaining
	if (data_remaining_in_bytes > 0){
		if(inloop_debug_level >= 2) printf("\nReadDigFifo : 2nd DMA required to get all data, %ld bytes remain\n\n\n\n\n\n",data_remaining_in_bytes);
		if (data_remaining_in_bytes > MAX_DIG_RAW_XFER_SIZE){
			if(inloop_debug_level >= 0) printf("DMA Error: total transfer size requires more than two DMAs\n");
			return(DMAError);
		}
		//if no error in size, do another DMA appending to the first one
		if(inloop_debug_level >= 2) printf("adjusting pointer.....\n\n\n\n\n\n");
		uintBuf = (unsigned int *) (&rawBuf->data[MAX_DIG_XFER_SIZE_IN_LONGWORDS]);
		if(inloop_debug_level >= 2) printf("2nd transfer.....\n\n\n\n\n\n");
		dmaStat = sysVmeDmaV2LCopy((unsigned char *)bd->FIFO,(unsigned char *)uintBuf, data_remaining_in_bytes);  //the actual read....
		if(inloop_debug_level >= 2) printf("\n\n\n\n\n\n");
	}

	//
	// at this point the data is transferred from digitizer's fifo and is in rawBuf->data if queueing is enabled.
	// Change the state of the buffer to OWNER_Q_WRITTEN, and then enter the buffer 
	// into the qWritten queue.
	if(QueueUsageFlag == 1) {
		rawBuf->board = bdnum;

		rawBuf->len = datasize_in_bytes;		//push in length of buffer, in bytes.
		queue_request_stat = putWrittenBuf(rawBuf);
		//DumpRawEvt (rawBuf, "ReadDigFIFO", 10,0);
		//It's possible the queue overflows at this point, so there should be a test
		if(queue_request_stat != Success) {
			stop_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);
			return(queue_request_stat);
		}
	}

	stop_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA);
	return(Success);   //if we haven't exited yet, declare success...

}

/****************************************************************************
 *	DigitizerTypeFHeader() is called whenever there is no data in a digitizer
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

//Helper function for DigitizerTypeFHeader
int PushTypeFToQueue(rawEvt *rawBuf, int BoardNumber, int QueueUsageFlag)
{
#ifdef DISABLE_ALL_TYPE_F_RESPONSE
	if(inloop_debug_level >= 1) printf("PushTypeFToQueue called, but F response disabled\n");
	return(Success);
#else
	FBufferCount++;		//increment count of F buffers pushed to queue
	if(inloop_debug_level >= 1) printf("Pushing Digitizer Type F; F buffer count now %d\n",FBufferCount );
	if(QueueUsageFlag == 1)
		{
		rawBuf->board = BoardNumber;
		rawBuf->len = 16;		//push in length of buffer, in bytes.  For type F this is one type F header (4 32-bit words == 16 bytes)
		return(putWrittenBuf(rawBuf));
		}
	else return(Success);   //Success defined in the enum BufReturnVals, in DGS_DEFS.h
#endif
}




int DigitizerTypeFHeader(int mode, int BoardNumber, int QueueUsageFlag)
{
	rawEvt *rawBuf;			//pointer to queue message buffer
	unsigned int TS_MSword, TS_LSword;
	unsigned int *OutBufDataPtr;	//for running through buf_to_process->data
	int queue_request_stat;	//for return value from queue request function
	char *TypeOfBoard;

#ifdef DISABLE_ALL_TYPE_F_RESPONSE
	if(inloop_debug_level >= 1) printf("DigitizerTypeFHeader called, but F response disabled\n");
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
			if(inloop_debug_level >= 0) printf("DigitizerTypeFHeader : Starved for raw buffers- throwing away data\n");
			return(queue_request_stat);	//inLoop will keep pounding until one frees up.
			}
		//here we assume we have a buffer. else we would have returned!
		OutBufDataPtr = rawBuf->data;  //rawBuf->data is a pointer to an array of unsigned ints, the actual data buffer.
		if(inloop_debug_level >= 2) printf("DigitizerTypeFHeader : Got buffer\n" );
		rawBuf->board_type = daqBoards[BoardNumber].board_type;
		}
	else
		{
		rawBuf = NULL;
		OutBufDataPtr = &BitBucket[0];	//if not using queues, OutBufDataPtr instead points to a stand-alone buffer that gets reused over and over.
		}

	if(inloop_debug_level >= 1) printf("DigitizerTypeFHeader called; QueueUsageFlag=%d, mode=%d\n",QueueUsageFlag,mode);


    
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
		case 0:	//mode 0: update header (digitizer was empty when polled)
#ifdef INLOOP_GENERATE_EMPTY_TYPEF

		//0	FIXED AAAAAAAA
		*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header

		//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--0xE for Empty
		//     |                  |                       +--as taken from the board
		//     |                  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
		//     +-- taken from daq_board structure (->base value)
		*OutBufDataPtr  = (daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
				+ 0x00030000		//packet length aligned to bit 16
				+ ((daqBoards[BoardNumber].DigUsrPkgData & 0xFFF) << 4)
				+ 0x0000000E;		//0xE for empty
		OutBufDataPtr++;
		//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
		//20230414: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
		*(daqBoards[BoardNumber].base32 + (0x40C/4)) = 0x00008000;	//set bit 15 of channel pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
		TS_LSword = *(daqBoards[BoardNumber].base32 + (0x484/4));	//read bits 31:00 of latched timestamp
		TS_MSword = *(daqBoards[BoardNumber].base32 + (0x488/4));	//read bits 47:32 of latched timestamp
		if(inloop_debug_level >= 1) printf("DigitizerTypeFHeader: FIFO of %s module #%d is EMPTY at timestamp 0x%04X%08X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_LSword);
		*OutBufDataPtr  = TS_LSword; OutBufDataPtr++;

		//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--previously read
		//     |                  |                       +--by fiat, the value 0xF
		//     |                  +--set to the value 0, defined as "this is information"
		//     +-- defined as 3
		*OutBufDataPtr  = 0x0C000000	//header length of 3, event type is informational.
				+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
				+ TS_MSword;	//timestamp, bits 47:32
		OutBufDataPtr++;

		//After writing the type F header, do queue maintenance.
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return (PushTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif

#ifndef INLOOP_GENERATE_EMPTY_TYPEF
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return(putFreeBuf(rawBuf));
#endif

		break;
//=========================================================================================================================================


		case 1:	//mode 1: issue header stating that digitizer is empty and will not have new data (End of Data)   
#ifdef INLOOP_GENERATE_EOD_TYPEF
		//0  Fixed AAAAAAAA
		OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
		*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header

		//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--0xD for Run is Done
		//     |                  |                       +--as taken from digitizer
		//     |                  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
		//     +-- taken from daq_board structure (->base value)
		*OutBufDataPtr  = (daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
				+ 0x00030000		//packet length aligned to bit 16
				+ ((daqBoards[BoardNumber].DigUsrPkgData & 0xFFF) << 4)
				+ 0x0000000D;		//0xD for done
        	OutBufDataPtr++;

		//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
		//20230414: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
		*(daqBoards[BoardNumber].base32 + (0x40C/4)) = 0x00008000;	//set bit 15 of channel pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
		TS_LSword = *(daqBoards[BoardNumber].base32 + (0x484/4));	//read bits 31:00 of latched timestamp
		TS_MSword = *(daqBoards[BoardNumber].base32 + (0x488/4));	//read bits 47:32 of latched timestamp
		if(inloop_debug_level >= 0) printf("DigitizerTypeFHeader: EndOfData announced for %s module #%d at timestamp 0x%04X%08X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_LSword);
		*OutBufDataPtr  = TS_LSword; OutBufDataPtr++;

		//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--previously read
		//     |                  |                       +--by fiat, the value 0xF
		//     |                  +--set to the value 0, defined as "this is information"
		//     +-- defined as 3
		*OutBufDataPtr  = 0x0C000000	//header length of 3, event type is informational.
				+ 0x000F0000	//mark as end-of-run header with header type 15 decimal (0xF)
				+ TS_MSword;	//timestamp, bits 47:32
		OutBufDataPtr++;

		//After writing the type F header, do queue maintenance.
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return (PushTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif

#ifndef INLOOP_GENERATE_EOD_TYPEF
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return(putFreeBuf(rawBuf));
#endif
		break;

//=========================================================================================================================================

		case 2:	//mode 2: issue header stating that digitizer overflowed and was forcibly cleared.
#ifdef INLOOP_GENERATE_ERROR_TYPEF
		//0  Fixed AAAAAAAA
		OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
		*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header

		//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--0xF means FIFO ERROR
		//     |                  |                       +--package data from digitizer.
		//     |                  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
		//     +-- taken from daq_board structure (->base value)
		*OutBufDataPtr  = (daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
				+ 0x00030000		//packet length aligned to bit 16
				+ ((daqBoards[BoardNumber].DigUsrPkgData & 0xFFF) << 4)
				+ 0x0000000F;		//0xF for FIFO issue
	        OutBufDataPtr++;

		//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
		//20230414: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
		*(daqBoards[BoardNumber].base32 + (0x40C/4)) = 0x00008000;	//set bit 15 of channel pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
		TS_LSword = *(daqBoards[BoardNumber].base32 + (0x484/4));	//read bits 31:00 of latched timestamp
		TS_MSword = *(daqBoards[BoardNumber].base32 + (0x488/4));	//read bits 47:32 of latched timestamp
		if(inloop_debug_level >= 0) printf("DigitizerTypeFHeader: FIFO Overflow Error announced for %s module #%d at timestamp 0x%04X%08X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_LSword);
		*OutBufDataPtr  = TS_LSword; OutBufDataPtr++;
		//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--previously read
		//     |                  |                       +--by fiat, the value 0xF
		//     |                  +--value is 1 : overflow error
		//     +-- defined as 3
		*OutBufDataPtr  = 0x0C200000	//header length of 3, event type 1 (overflow)
				+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
				+ TS_MSword;	//timestamp, bits 47:32
		OutBufDataPtr++;

		//After writing the type F header, do queue maintenance.
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return (PushTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif

#ifndef INLOOP_GENERATE_ERROR_TYPEF
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return(putFreeBuf(rawBuf));
#endif
		break;

//=========================================================================================================================================

		case 3: 	//mode 3: issue header stating that digitizer UNDERFLOWED and was forcibly cleared.
#ifdef INLOOP_GENERATE_ERROR_TYPEF
		//0  Fixed AAAAAAAA
		OutBufDataPtr = rawBuf->data;  //I assume 'unsigned int' in VxWorks is a 32-bit object...
		*OutBufDataPtr = 0xAAAAAAAA; OutBufDataPtr++;		//begininng of the digitizer header

		//1	Geo Addr(31:27)/PACKET LENGTH(26:16)/USER PACKET DATA(15:04)/CHANNEL ID(3:0)
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--0xF means FIFO ERROR
		//     |                  |                       +--package data as read from digitizer
		//     |                  +-- Fixed value of 3 (length of this minimal header, not counting the 0xAAAAAAAA
		//     +-- taken from daq_board structure (->base value)
		*OutBufDataPtr  = (daqBoards[BoardNumber].board << 27)	//BoardNumber is value from 0 to 6, from the main for loop in inLoop.  It's the board INDEX, not the slot number. For slot # use daqBoards[BoardNumber].board.
				+ 0x00030000		//packet length aligned to bit 16
				+ ((daqBoards[BoardNumber].DigUsrPkgData & 0xFFF) << 4)
				+ 0x0000000F;		//0xF for FIFO Error
		OutBufDataPtr++;

		//2	LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]  
		//20230414: write to pulsed control to LATCH the timestamp, and read the LATCHED timestamp, not the live timestamp.
		*(daqBoards[BoardNumber].base32 + (0x40C/4)) = 0x00008000;	//set bit 15 of channel pulsed control register to latch timestamp.  Bit is self-clearing, no need to reset.
		TS_LSword = *(daqBoards[BoardNumber].base32 + (0x484/4));	//read bits 31:00 of latched timestamp
		TS_MSword = *(daqBoards[BoardNumber].base32 + (0x488/4));	//read bits 47:32 of latched timestamp
		if(inloop_debug_level >= 0) printf("DigitizerTypeFHeader: FIFO Underflow Error announced for %s module #%d at timestamp 0x%04X%08X\n",TypeOfBoard, BoardNumber, (TS_MSword & 0xFFFF), TS_LSword);
		*OutBufDataPtr  = TS_LSword; OutBufDataPtr++;
		//3	HEADER LENGTH(31:26)/EVENT TYPE(25:23)/0/0/0/HEADER TYPE(19:16)/LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]
		//     ^                  ^                       ^                  ^
		//     |                  |                       |                  +--previously read
		//     |                  |                       +--by fiat, the value 0xF
		//     |                  +--value is 2 : underflow error
		//     +-- defined as 3
		*OutBufDataPtr  = 0x0D000000	//header length of 3, event type 2 (underflow)
			+ 0x000F0000	//mark as informational header with header type 15 decimal (0xF)
			+ TS_MSword;	//timestamp, bits 47:32
		OutBufDataPtr++;

		//After writing the type F header, do queue maintenance.
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return (PushTypeFToQueue(rawBuf, BoardNumber, QueueUsageFlag));
#endif

#ifndef INLOOP_GENERATE_ERROR_TYPEF
		if(QueueUsageFlag == 1) // 1 means yes, use the queue system
			return(putFreeBuf(rawBuf));
#endif
		break;

//=========================================================================================================================================
//   catch-all case of mode not one we know.
//=========================================================================================================================================
		default:
			if(inloop_debug_level >= 0) printf("DigitizerTypeFHeader, UNKNOWN MODE\n" );
			if(QueueUsageFlag == 1) // 1 means yes, use the queue system
				return(putFreeBuf(rawBuf));
			return(IncorrectModeArg);
		break;
		} //end switch(mode)
	return(-9999);
#endif
}

/***********************************************************************************/
//
//  dbgReadDigFifo() reads 'n' words from the FIFO of a digitizer and dumps data
//  to the console.  NOT USED BY DAQ.  CONSOLE DEBUG USE ONLY.  
//
/***********************************************************************************/
void dbgReadDigFifo(int board, int numwords, int mode){
	int datasize_in_bytes;			//amount of data to transfer
	unsigned int *uintBuf;  //pointer to data read from FIFO
	int j;
	struct daqBoard *bd;	//structure of board information
	int dmaStat = OK;

	bd = &daqBoards[board];

	//datasize_in_bytes is in bytes, num of btytes to xfer from vme fifo
	//constrain to no bigger than MAX_DIG_RAW_XFER_SIZE
	if (numwords==-1){
		datasize_in_bytes = *(daqBoards[board].base32 + (0x004/4));	//read from 'programming done' to get depth of FIFO and read that.
		datasize_in_bytes = datasize_in_bytes & 0x007FFFF;	//make sure flag bits aren't included in read depth.
		datasize_in_bytes = datasize_in_bytes * 4;			//convert value read (longwords) to number of BYTES to DMA
	}else{
		datasize_in_bytes=numwords * 4;	//numwords is the # of 32-bit words (from register), convert to BYTES
	}

	if (datasize_in_bytes>MAX_DIG_RAW_XFER_SIZE) datasize_in_bytes=MAX_DIG_RAW_XFER_SIZE;
	printf("numwords %d (longs), datasize_in_bytes %d (bytes) datasize_in_bytes/4 %d (longs)\n",numwords,datasize_in_bytes,datasize_in_bytes/4);
	uintBuf = &BitBucket[0];

	if(mode == 1){	//dma mode
		dmaStat = sysVmeDmaV2LCopy((unsigned char *)bd->FIFO,(unsigned char *)uintBuf, datasize_in_bytes);  //the actual read....
		if (dmaStat != OK) {
			printf("DMA Error: transfer returned %d (xfer 1)\n", dmaStat);
		}else {
			printf("DMA success : stat %d datasize_in_bytes %d\n",dmaStat,datasize_in_bytes);
		}
	}else{
		printf("Dumping %d words to buffer, not using DMA\n",(datasize_in_bytes/4));
		for (j = 0; j < (datasize_in_bytes/4); j++) {
			*uintBuf = *((unsigned int *) bd->FIFO);  //longword by longword, pump mud.
			uintBuf++;
		}
	}

	//either way you get it, now dump the data.
	for (j = 0; j < (datasize_in_bytes/4); j++) {
		printf("index:%04d    data:%08X\n",j,BitBucket[j]);
	}


}

