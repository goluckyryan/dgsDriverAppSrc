//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		QueueManagement.c
// Description: Contains any function that initializes, adjust or manages the queues.
//--------------------------------------------------------------------------------

#include <msgQLib.h>
#include <stdio.h>
#include <taskLib.h>
#include <string.h>
#include <epicsEvent.h>
#include <cacheLib.h>

#include "devGVME.h"

#include "QueueManagement.h"


#ifdef READOUT_USE_DMA	
	#ifdef MV5500
		epicsEventId DMASem;
		/* following prototypes in universe.c */
		STATUS sysVmeDmaInit (void);
	#endif
#endif

// Message Queue Handles
MSG_Q_ID qFree;
MSG_Q_ID qWritten;
MSG_Q_ID qSender;


// Array of buffers - For maintenance purposes only.
rawEvt *bufferlist[RAW_Q_SIZE];

// Functions for local use only.
int newEventBuffer(rawEvt **_rawBufAddr);

//array of buffers- we put all buffers on this array just for debugging
rawEvt *rawevent_bufferlist[RAW_Q_SIZE];


/*
 * This needs to be called once, probably from startup file before sequencers
 */

int setupFIFOReader()
{
	static int InitComplete = 0;
	rawEvt *rawBufAddr;
	int stat;
	int i;
	int bufstat;
	
	printf("initReaderQueues \n");
	
	#ifdef READOUT_USE_DMA
	#ifdef MV5500
	// http://read.pudn.com/downloads62/sourcecode/unix_linux/network/214452/ucosvg4/driver/universe.c__.htm
		sysVmeDmaInit();		// [universe.c] Initialize the Universe's DMA engine. Returns type STATUS, which just means OK or ERROR, and per source, always returns OK.

		stat = sysVmeDmaCnfgSet((DCTL_VDW_32 | DCTL_VCT_BLK), DCTL_VAS_A32, DCTL_PGM_DATA, 0);
		if(stat != OK) printf("\n\n\n  ****************************\n   DMA SETUP ERROR \n****************************\n\n\n\n\n");
		DMASem = epicsEventCreate(epicsEventFull);
		if (!DMASem) {
			printf("ERROR, initReaderQueues(): unable to create DMA semaphore\n");
			return -1;
		}
	#endif
	#endif
	msgQLibInit();	//#MERGED_FROM_CON6
	// Check if Queues have already been initialized.
	if (InitComplete == 1) {
		msgQDelete(qFree);
		msgQDelete(qWritten);
		msgQDelete(qSender);
	}
	
	// Create free buffer queue.
	qFree = msgQCreate(RAW_Q_SIZE, sizeof(rawEvt *), MSG_Q_FIFO);
	if (qFree == 0) {
		printf("ERROR, initReaderQueues(): unable to create free buffer queue\n");
		return -1;
	}
	
	// Create written buffer queues.  One per board.
	qWritten = msgQCreate(RAW_Q_SIZE, sizeof(rawEvt *), MSG_Q_FIFO);
	if (qWritten == 0) {
		printf("ERROR, initReaderQueues(): unable to create written buffer queue\n");
		return -1;
	}
	
	// Create the sending queue.
	qSender = msgQCreate(RAW_Q_SIZE, sizeof(rawEvt *), MSG_Q_FIFO);
	if (qSender == 0) {
		printf("ERROR, initReaderQueues(): unable to create sending buffer queue\n");
		return -1;
	}
	
	// Check if Queues have already been initalized.
	if (InitComplete == 0) {
		// Generate the buffers.
		for (i = 0; i < RAW_Q_SIZE; i++) {
			bufstat =  newEventBuffer(&rawBufAddr);
			if (bufstat) {
				printf("ERROR, initReaderQueues(): unable to create buffer #%d\n", i);
				return -1;
			}
			bufferlist[i] = rawBufAddr;
		}
	}
	
	for (i = 0; i < RAW_Q_SIZE; i++) {
		stat = putFreeBuf(bufferlist[i]);
		
		if (stat != Success) {
			printf("ERROR, initReaderQueues(): failed to add buffer #%d to free Q\n", i);
			return -1;
		}
		
		rawBufAddr->owner = OWNER_Q_FREE;
	}
	
	InitComplete = 1;
	
	return 0;
}

// Make a new raw event buffer
int newEventBuffer(rawEvt **_rawBufAddr) 
{
	static int uid = 0;
	rawEvt *rawBufAddr;
	
	rawBufAddr=*_rawBufAddr;
//	printf("newEventBuffer \n");	//#MERGED_FROM_CON6
	
	//allocate a rawEvt size piece of memory, return in error if not available.
	//This is the actual event buffer.
	rawBufAddr = (rawEvt *)calloc(sizeof(rawEvt),1);
	if (!rawBufAddr) {
		printf("ERROR, newEventBuffer(): Out of memory.  Cannot allocate rawEvt\n");
		return -1;
	}
	
	#ifdef READOUT_USE_DMA
		rawBufAddr->data = (unsigned int *) cacheDmaMalloc(RAW_BUF_SIZE + 256);
		if (!rawBufAddr->data) {
			printf("ERROR, newEventBuffer(): Out of memory.  Cannot allocate rawEvt->data\n");
			return -1;
		}
		
		// 256 byte alignment required by CES rio3
		if ((unsigned int)rawBufAddr->data % 256) {
			rawBufAddr->data += (256 - ((unsigned int)rawBufAddr->data % 256))/4;
		}
		
		// Set all buffer data to 0.
		bzero((char *)rawBufAddr->data, RAW_BUF_SIZE);
	#else
		rawBufAddr->data = (unsigned int *) calloc(RAW_BUF_SIZE, 1);  //this allocates the memory for the buffer from the heap
		if (!rawBufAddr->data) {
			printf("ERROR, newEventBuffer(): Out of memory.  Cannot allocate rawEvt->data\n");
			return -1;
		}
	#endif
	
	rawBufAddr->owner = OWNER_UNDEF;
	
	uid++;

	rawBufAddr->id = uid;					// Assign a unique id.  This value never changes.
	rawBufAddr->datapcrosscheck = rawBufAddr->data;	// Store a copy of the data pointer.  This values should never be referenced, it is for cross checking only.

	*_rawBufAddr=rawBufAddr;
	return 0;
}

// The plan here is to pass a pointer to a rawEvt, meaning a pointer to a structure that
// itself has a pointer to a "buffer" of data, and put the rawEvt into the Free queue.
// The QUEUE only holds the rawEvt structures, not the data that the rawEvt structures point to.

void	bufDiag(rawEvt *rawBuf, char* caller, int check_length, int check_AAAA, int print_info)
{
	#ifdef PRINT_BUFFER_ERRORS
	// TODO: add thread protection
	if (rawBuf == NULL) {
		printf("\nBUF_ERR_NULL: FN:%s", caller);
	}
	else if (rawBuf->datapcrosscheck != rawBuf->data) {
		printf("\nBUF_ERR_DP: FN:%s ID:%d BRD:%d LEN:%d DP:%08X DPCC:%08X", caller, rawBuf->id, rawBuf->board, rawBuf->len, (unsigned int)(rawBuf->data), (unsigned int)(rawBuf->datapcrosscheck));
	}
	else if (check_length && (rawBuf->len < 16)) {
		printf("\nBUF_ERR_LEN: FN:%s ID:%d BRD:%d LEN:%d DP:%08X DPCC:%08X", caller, rawBuf->id, rawBuf->board, rawBuf->len, (unsigned int)(rawBuf->data), (unsigned int)(rawBuf->datapcrosscheck));
	}
	else if (check_AAAA ){
        switch(rawBuf->board_type) 
				{
				case BrdType_ANL_MDIG:
				case BrdType_ANL_SDIG:
				case BrdType_MAJORANA_MDIG:
				case BrdType_MAJORANA_SDIG:
				case BrdType_LBNL_DIG:
		         	if( rawBuf->data[0] != 0xAAAAAAAA)  {
						printf("\nBUF_ERR_AA: FN:%s ID:%d BRD:%d LEN:%d DP:%08X DATA0:%08X", caller, rawBuf->id, rawBuf->board, rawBuf->len, (unsigned int)(rawBuf->data), rawBuf->data[0]);
					}
                    break;
				case BrdType_DGS_MTRIG:	
					if( (rawBuf->data[0] != 0x0000AAAA) && ((rawBuf->data[3] & 0xFFFF0000) == 0)  ){
						printf("\nBUF_ERR_0A: FN:%s ID:%d BRD:%d, BRD Type:%d, LEN:%d DP:%08X DATA0:%08X", caller, rawBuf->id, rawBuf->board, rawBuf->board_type, rawBuf->len, (unsigned int)(rawBuf->data), rawBuf->data[0]);
					}
                    break;
				default:
					break;
				}
	}	
	else if (check_AAAA && ((rawBuf->board < 0) || (rawBuf->board >= GVME_MAX_CARDS))) {
		printf("\nBUF_ERR_BRD: FN:%s ID:%d BRD:%d LEN:%d DP:%08X DATA0:%08X", caller, rawBuf->id, rawBuf->board, rawBuf->len, (unsigned int)(rawBuf->data), rawBuf->data[0]);	
	}	
	else if (print_info) {
		printf("\nBUF_OK: FN:%s ID:%d BRD:%d LEN:%d DP:%08X DATA0:%08X", caller, rawBuf->id,rawBuf->board, rawBuf->len, (unsigned int)(rawBuf->data), rawBuf->data[0]);	
	}
	#endif
	
	return;
}

//==========================================
// take a rawEvt structure out of the queue of free buffers
// Only to be used by the board reader.
//=========================================
int	getFreeBuf(rawEvt **rawBuf)	// the get requires passing in the rawBuf by reference, hence ** instead of *.
{
	int numRecv;
	
	numRecv = msgQReceive(qFree, (char *) rawBuf, sizeof(rawEvt*), NO_WAIT);
	if (numRecv != sizeof(rawEvt*)) {	//equates roughly to "did not transfer the amount asked for"
		return(NoBufferAvail);	
	}
	
	(*rawBuf)->data[0] = 0x87654321;
	(*rawBuf)->owner = OWNER_INLOOP;	// Mark the owner of that buffer as the board reader.

	bufDiag(*rawBuf, "getFBuf", 0, 0, 0); // dont check length, dont check for AAA, dont print unless an error is detected.

	return (Success);
}

//==========================================
// Functions that take a rawEvt structure out of the queue of written buffers
//=========================================
int getWrittenBuf(rawEvt **rawBuf)	// the get requires passing in the rawBuf by reference, hence ** instead of *.q
{
	int numRecv;
	
	numRecv = msgQReceive(qWritten, (char *) rawBuf, sizeof(rawEvt*), NO_WAIT);
	if (numRecv != sizeof(rawEvt*)) { 	//equates roughly to "did not transfer the amount asked for"
		return(NoBufferAvail);	
	}

	(*rawBuf)->owner = OWNER_OUTLOOP;	// Mark the owner of that buffer as the board reader.

	bufDiag(*rawBuf, "getWBuf", 1, 1, 0); // check length, check for AAA, dont print unless an error is detected.

	return (Success);
}

//==========================================
// Functions that take a rawEvt structure out of the queue of written buffers
//=========================================
int getSenderBuf(rawEvt **rawBuf)	// the get requires passing in the rawBuf by reference, hence ** instead of *.q
{
	int numRecv;
	
	numRecv = msgQReceive(qSender, (char *) rawBuf, sizeof(rawEvt*), NO_WAIT);
	if (numRecv != sizeof(rawEvt*)) { 	//equates roughly to "did not transfer the amount asked for"
		return(NoBufferAvail);	
	}

	(*rawBuf)->owner = OWNER_SENDER;	// Mark the owner of that buffer as the board reader.

	bufDiag(*rawBuf, "getSBuf", 1, 1, 0); // check length, check for AAA, dont print unless an error is detected.

	return (Success);
}


//==========================================
// Function that pushes a rawEvt into the Free queue of buffers.
//==========================================
int putFreeBuf(rawEvt *rawBuf)
{
	STATUS stat = 0;

	rawBuf->len = 0;
	rawBuf->board = -1;
	rawBuf->data[0] = 0x12345678;
	rawBuf->owner = OWNER_Q_FREE;	// Mark the owner of that buffer as Free.
	
	bufDiag(rawBuf, "putFBuf", 0, 0, 0); // dont check length, dont check for AAA, dont print unless an error is detected.	/* for checking inLoop */
	
	stat = msgQSend (qFree, (char *) &rawBuf, sizeof(rawEvt *), NO_WAIT, MSG_PRI_NORMAL);
	if (stat == ERROR) {
		#ifdef PRINT_BUFFER_ERRORS
		printf("ERROR, putFreeBuf(): failed to add buffer to free Q\n");
		#endif
		return(QueuePutError);
	}
	
	return (Success);
}


//==========================================
// Function that pushes a rawEvt into the queue of written buffers.
// Only to be used by the board reader.
//==========================================
int putWrittenBuf(rawEvt *rawBuf)
{
	STATUS stat = 0;
	
	rawBuf->owner=OWNER_Q_WRITTEN;	// Mark the owner of that message as Written.

	bufDiag(rawBuf, "putWBuf", 1, 1, 0); // check length, check for AAA, dont print unless an error is detected.

	stat = msgQSend(qWritten, (char *) &rawBuf, sizeof(rawEvt *), NO_WAIT, MSG_PRI_NORMAL);
	
	if (stat == ERROR) {
		#ifdef PRINT_BUFFER_ERRORS
		printf("ERROR, putWrittenBuf(): failed to add buffer to written Q\n");
		#endif
		return(QueuePutError);
	}
	
	return (Success);
}


//==========================================
// Function that pushes a rawEvt into the queue of sender buffers.
// Only to be used by outLoop.
//==========================================
int putSenderBuf(rawEvt *rawBuf)
{
	STATUS stat = 0;
	
	rawBuf->owner=OWNER_Q_SENDER;	// Mark the owner of that message as ready to send.

	bufDiag(rawBuf, "putSBuf", 1, 1, 0); // check length, check for AAA, dont print unless an error is detected.

	stat = msgQSend(qSender, (char *) &rawBuf, sizeof(rawEvt *), NO_WAIT, MSG_PRI_NORMAL);
	
	if (stat == ERROR) {
		#ifdef PRINT_BUFFER_ERRORS
		printf("ERROR, putSenderBuf(): failed to add buffer to written Q\n");
		#endif
		return(QueuePutError);
	}
	
	return (Success);
}

int getFreeBufCount(void)
{
	return msgQNumMsgs(qFree);
}

int getWrittenBufCount(void)
{
	return msgQNumMsgs(qWritten);
}

int getSenderBufCount(void)
{
	return msgQNumMsgs(qSender);
}

//=========================================================
//	Utility function to print out the rawEvt structure pointed to by something.
//=========================================================

void DumpRawEvt (rawEvt *p, char *CallingRoutine, int dumplength, int dumpstart)
{
	int i,dumpstop;

	if (dumpstart < 0)
		dumpstart = 0;

	printf("\n******************************\nDumpRawEvt : called by %s\n",CallingRoutine);
	printf ("id: %d board: %d len: %d  owner: %d\n",p->id, p->board,p->len,p->owner);
	
	if(dumplength > 0)
	{
		printf ("*******  Data Dump ************\n");
		if ((dumpstart + dumplength) > p->len) {
			printf("Requested length %d plus startpoint %d > data length %d\n",dumplength,dumpstart,p->len);
		}
		else {
			dumpstop = dumpstart + dumplength;
			for(i=dumpstart;i<dumpstop;i++)
				printf("Data[%04X] = %08X\n",i,p->data[i]);
		}
	}
	printf("\n******** End Dump ************\n");
	return;
}

// Additional Notes: //#MERGED_FROM_CON6
//==============================
//   Stuff they never told you about queues in school.
//==============================
//
//	When you create a queue with the call msgQCreate(RAW_Q_SIZE, sizeof(rawEvt *), MSG_Q_FIFO);
//
//	the return value is of type MSG_Q_ID.
//
//	MSG_Q_ID is defined in the file /global/devel/vxWorks/Tornado2.2/target/h/msgQLib.h as
//
//	typedef struct msg_q *MSG_Q_ID;	/* message queue ID */

//	so an MSG_Q_ID ia a pointer to a structure of type "msg_q".  And this file itself has no #include statements.  Boy, that's helpful!
//
//	If you do enough find and grep you eventually determine that there's another file at
//	/global/devel/vxWorks/Tornado2.2/target/h/private/msgQLibP.h that actually defines what a msg_q is.
//	
//	That definition is
//
//	typedef struct msg_q		/* MSG_Q */
//	    {
//	    OBJ_CORE		objCore;	/* object management */
//	    Q_JOB_HEAD		msgQ;		/* message queue head */
//	    Q_JOB_HEAD		freeQ;		/* free message queue head */
//	    int			options;	/* message queue options */
//	    int			maxMsgs;	/* max number of messages in queue */
//	    int			maxMsgLength;	/* max length of message */
//	    int			sendTimeouts;	/* number of send timeouts */
//	    int			recvTimeouts;	/* number of receive timeouts */
//	    EVENTS_RSRC		events;		/* VxWorks events */
//	    } MSG_Q; 
//
//	And here we find something reasonably useful.  qFree->maxMsgs is the maximum number of messages in the queue but it
//	doesn't tell us how many are in the queue at the moment.
//
//	Going back to /global/devel/vxWorks/Tornado2.2/target/h/msgQLib.h there is another struct MSG_Q_INFO
//	and we also find there are functions that use this structure type.  The structure definition is
//
//	typedef struct			/* MSG_Q_INFO */
//	    {
//	    int     numMsgs;		/* OUT: number of messages queued */
//	    int     numTasks;		/* OUT: number of tasks waiting on msg q */
//	
//	    int     sendTimeouts;	/* OUT: count of send timeouts */
//	    int     recvTimeouts;	/* OUT: count of receive timeouts */
//	
//	    int     options;		/* OUT: options with which msg q was created */
//	    int     maxMsgs;		/* OUT: max messages that can be queued */
//	    int     maxMsgLength;	/* OUT: max byte length of each message */
//	
//	    int     taskIdListMax;	/* IN: max tasks to fill in taskIdList */
//	    int *   taskIdList;		/* PTR: array of task ids waiting on msg q */
//	
//	    int     msgListMax;		/* IN: max msgs to fill in msg lists */
//	    char ** msgPtrList;		/* PTR: array of msg ptrs queued to msg q */
//	    int *   msgLenList;		/* PTR: array of lengths of msgs */
//	
//	    } MSG_Q_INFO;
//
//	and there are functions defined in this .h file as
//
//	extern STATUS 	msgQLibInit (void);
//	extern MSG_Q_ID msgQCreate (int maxMsgs, int maxMsgLength, int options);
//	extern STATUS 	msgQDelete (MSG_Q_ID msgQId);
//	extern STATUS 	msgQSend (MSG_Q_ID msgQId, char *buffer, UINT nBytes,int timeout, int priority);
//	extern int 	msgQReceive (MSG_Q_ID msgQId, char *buffer, UINT maxNBytes,int timeout);
//	extern STATUS 	msgQInfoGet (MSG_Q_ID msgQId, MSG_Q_INFO *pInfo);
//	extern int 	msgQNumMsgs (MSG_Q_ID msgQId);
//	extern void 	msgQShowInit (void);
//	extern STATUS 	msgQShow (MSG_Q_ID msgQId, int level);
//
//	So a reasonable supposition is that if you created a queue qFree of type MSG_Q_ID, presumably then
//	if you also define a variable qFreeStat of type *MSG_Q_INFO, you can then call
//	
//	msgQInfoGet (qFree, qFreeStat);
//
//	and then the number of messages in that queue would be qFreeStat->numMsgs.
//	The list of pointers to the messages in qFree would be qFreeStat->msgPtrList, and the pointer to the lengths of those messages
//	would be qFreeStat->msgLenList.
//
//	Of course the next obvious question is what the STATUS variable type is that the different message queue functions return is.
//	From looking in other vxWorks H files we find that STATUS is a binary object that returns either the #defined value OK or the
//	#defined value ERROR.

