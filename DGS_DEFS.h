
/*
 * DGS_DEFS.h : Specific definitions for the DGS implementation
 * Added 20171004 by J. T. Anderson
 */

#ifndef _DGS_DEFS_H
#define _DGS_DEFS_H

//==============================
//---     Include Files     --- 
//==============================
#include <epicsMutex.h>  // for GVME structure
//==============================
//---        Defines        --- 
//==============================
//---------------------------------------------------
//	FROM DGS_DEFS
//---------------------------------------------------
#ifdef MV5500
	/******* original settings at which we get 15Mbytes/sec ****/  /*  512*1024 is length of 524,288, or 0x80000 */
	/* or VME errors will occur when attempting to transfer.                                                         */

	/* The physical FIFO of the digitizer is 262144 X 32 (256K longwords) in size. */
	/* That's 512K 16-bit words, or 1Meg bytes */

	// Note that trigger FIFOs will be a different size and thus the MAX_RAW_XFER_SIZE will have to be different for triggers.

	// Transfer length specifications must be in BYTES because that's the units used by the DMA functions of the MVME5500.

	//20230414: attempting to transfer 1Mbyte appears to give DMA errors.  Will keep buffer size as is but do DMA in chunks
	//if 

	#define RAW_BUF_SIZE	(1024 * 1024)			/* bytes */	//changed from (512 * 1024) 20230412 JTA

	//20250607: We think the original definition of MAX_DIG_RAW_XFER_SIZE, as defined, is NOT correct in terms of
	//its original explanation of 512kbytes being max you can DMA.  We think it's smaller.
	//redefined as actual size of FIFO, in bytes.
//	#define MAX_DIG_RAW_XFER_SIZE	(512 * 1024)		/* bytes */	//explicitly defined as "the max you can DMA".

	#define MAX_DIG_RAW_XFER_SIZE	(1024 * 1024)		/* bytes */	
	#define MAX_DIG_XFER_SIZE_IN_LONGWORDS (MAX_DIG_RAW_XFER_SIZE/4)  /* unsigned ints are, we believe, 32 bits */

	#define TRIG_MON_FIFO_SIZE (4 * 1024)				/* bytes when read as 32-bit longwords (data only in lower 16 of 32-bit word) */
	#define TRIG_CHAN_FIFO_SIZE (4 * 1024)				/* bytes when read as 32-bit longwords (data only in lower 16 of 32-bit word) */
	#define MTRIG_MON7_FIFO_SIZE (4 * 65536)			/* bytes when read as 32-bit longwords (data only in lower 16 of 32-bit word) */

	#define RAW_Q_SIZE	 200	//changed from 400 to 200 20230412 JTA

	#define MAX_TRIG_RAW_XFER_SIZE (4 * 65536)			/* bytes when read as 32-bit longwords (data only in lower 16 of 32-bit word) */
	#define MAX_TRIG_XFER_SIZE_IN_LONGWORDS (MAX_TRIG_RAW_XFER_SIZE/4)  /* unsigned ints are, we believe, 32 bits */

	#define DMA_CHUNK_SIZE_IN_BYTES 0x10000			/*what we believe max DMA size actually is, 20250607 */

#endif

#define READOUT_USE_DMA  	/* use dma for fifo reads? */

#define DIGITIZER_IS_DGS
#undef DIGITIZER_IS_GRETINA

// For profiling counters:
#define NO_PROFILING
#define PROFILE_TICK_FREQUENCY 33333333.333	//Hz
#define NUM_PROFILE_COUNTERS 9
#define PROF_OL_CHECK_AND_MOVE_BUFFERS 0
#define PROF_OL_GET_TRACE 1
#define PROF_IL_CHECK_AND_READ_DIG 2
#define PROF_IL_XFER_DIG_FIFO_DATA 3
#define PROF_MS_GET_RECEIVER_REQUEST 4
#define PROF_MS_SEND_SERVER_RESPONCE 5
#define PROF_MS_SEND_DATA_BUFFER 6
#define PROF_IL_CHECK_AND_READ_TRIG 7
#define PROF_IL_XFER_TRIG_FIFO_DATA 8


//---------------------------------------------------
//	FROM inLoopSupport
//---------------------------------------------------
// various #defines to control operation of inLoop.

#define INLOOP_MIN_BOARD_NUMBER 0		//the lowest board number (NOT SLOT) to scan
#define INLOOP_MAX_BOARD_NUMBER 6		//the highest board number (NOT SLOT) to scan

#define INLOOP_PROCESS_TRIGGER	/* turn on processing of trigger boards */
#define INLOOP_CHECK_PARTIALS	/* if defined, inLoopSupport will check for total data not being exact integer of calculated event size */

/* MBO 20200616: maximum scan loop delay in seconds. This is the scan loop delay to apply
 *		 when (RAW_Q_SIZE - getFreeBufCount()) == SENDER_BUF_BYPASS_THRESHOLD 
 *		 it adjsuts up or down in linear correlation to (RAW_Q_SIZE - getFreeBufCount())
 */
#define SCAN_LOOP_MINIMUM_DELAY 0.001		/* This is the minimum delay value for the scan loop */
//#define SCAN_LOOP_MAXIMUM_DELAY 0.050		/* This setting provides a range of 15.2MB/s to 7.2MB/s */
//#define SCAN_LOOP_MAXIMUM_DELAY 0.075		/* Good for 1 digitizer.  This setting provides a range of 15.2MB/s to TBD (>6MB/s) */
#define SCAN_LOOP_MAXIMUM_DELAY 0.300		/* Good for 4 digitizer.  This setting provides a range of 15.2MB/s to TBD (>6MB/s) */
#define SCAN_LOOP_THROTTLE_FREE_THRESHOLD 40	/* This is the maximum throttle free buffer usage limit (i.e. 15.2MB/s) */

// 202230918: #defines modified to either ENABLE (first set) or DISABLE (2nd set)

#define INLOOP_GENERATE_EMPTY_TYPEF		//if defined, 'Type F' headers for digitizer/trigger empty are generated.
#define INLOOP_GENERATE_ERROR_TYPEF		//if defined, 'Type F' headers for digitizer/trigger error conditions are generated.
#define INLOOP_GENERATE_EOD_TYPEF		//if defined, 'Type F' headers for digitizer/trigger End Of Data are generated at the end of a run.
#undef DISABLE_ALL_TYPE_F_RESPONSE		//subroutines that generate "Type F" headers are NOT reduced to empty functions.

// #undef INLOOP_GENERATE_EMPTY_TYPEF		//if defined, 'Type F' headers for digitizer/trigger empty are NOT generated.
// #undef INLOOP_GENERATE_ERROR_TYPEF		//if defined, 'Type F' headers for digitizer/trigger error conditions are NOT generated.
// #undef INLOOP_GENERATE_EOD_TYPEF		//if defined, 'Type F' headers for digitizer/trigger End Of Data are NOT generated at the end of a run.
// #define DISABLE_ALL_TYPE_F_RESPONSE		//subroutines that generate "Type F" headers ARE reduced to empty functions.

//---------------------------------------------------
//	FROM outLoopSupport
//---------------------------------------------------
#define SENDER_BUF_BYPASS_THRESHOLD (int)(RAW_Q_SIZE * 0.500)			/* Maximum depth of sender queue before outLoop bypasses onto Free */ /* changed from 150/400 to scale with # bufs 20230412 JTA) */
// MBO 20230416: The related code is currently unused (commented out) in outLoopSupport.
//#define BUF_RATE_WARNING_THRESHOLD (int)(RAW_Q_SIZE * 0.5)			/* Trigger a buffer rate warning at this threshold in terms of buffers/sec */ /* changed from 200/400 to scale with # bufs 20230412 JTA) */
#define DATA_RATE_TIME_CONSTANT 1			/* time constant of the data rate averageing in seconds. */
//#define BUFFER_CHECK_LATENCY_ERROR_THRESHOLD 0.2	/* Trigger a latency error message if the delay between state machine cycles is 4x the nominal value */
//#define BUFFER_CHECK_LATENCY_WARNING_THRESHOLD 0.1	/* Trigger a latency warning message if the delay between state machine cycles is 2x the nominal value */
#define NUM_DIG_CHANNELS 10						/* Number of dig channels. */
#define DIG_WORD_SIZE 4							/* Number of bytes per digitizer word, as implied by the packet length. */
#define MIN_HEADER_LENGTH 4						/* Minimum size, in digitizer words, of a digitizer header. */
#define MAX_PACKET_LENGTH 510					/* Maximum value, in digitizer words, of the reported packet length. */
#define MAX_TRACE_LENGTH 1024					/* Maximum length, in samples, of a waveform trace. */
//#define MAX_EVENTS_TO_CHECK_PER_BUFFER 2048		/* Maximum number of event to check per buffer.  Prevents throughput restriction at very small event sizes. */
// The above works fine for headers only at 15.2MB/s, however, for the first ethernet test lets reduce this just to be safe.
#define MAX_EVENTS_TO_CHECK_PER_BUFFER 128	/* Maximum number of event to check per buffer.  Prevents throughput restriction at very small event sizes. */
#define OUTLOOP_NUM_ERR_PER_BOARD 1	/* Sets # of detail errors per board, if not defined limit is 1*/
#define OUTLOOP_TRY_REALIGNMENT				/* if defined outLoop will suck data after an error until it resynchronizes or falls off end of buffer */

//#define disable_all_checking	// disables all event data checking if defined.
//#define HISTO_ENABLE
#define TRACE_ENABLE

#ifdef HISTO_ENABLE
	#define MAX_EVENTS_TO_HISTO_PER_BUFFER 256	/* Maximum historgram points to snag per buffer.  Limits processing overhead */
	#define MAX_HISTO_DELTA 256					/* Maximum delta between 2 point to histogram.  Maximum mus tbe less than MAX_TRACE_LENGTH / 2 */
#endif
//---------------------------------------------------
//	FROM readDigFIFO
//---------------------------------------------------
/***
	These #defines are presumably hardcoded compare values used for the status
	of the DIGITIZER fifo and have nothing to do with the status of the
	TRIGGER fifo.  

Per the firmware:
regin_programming_done(19) <= FIFO_PROG_FLAG;	--added 20160520
regin_programming_done(20) <= ext_fifo_empty(0);
regin_programming_done(21) <= ext_fifo_empty(1);
regin_programming_done(22) <= ext_fifo_almost_empty when((ext_fifo_empty = "00") and (ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
regin_programming_done(23) <= ext_fifo_half_full when((ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
regin_programming_done(24) <= ext_fifo_almost_full when(ext_fifo_full = "00") else '0';
regin_programming_done(25) <= ext_fifo_full(0);
regin_programming_done(26) <= ext_fifo_full(1);

regin_programming_done(18 downto 0) <= external_fifo_depth;

***/
//regin_programming_done(19) <= FIFO_PROG_FLAG;	--added 20160520
#define PROG_FULL_MASK       0x00800000

//regin_programming_done(20) <= ext_fifo_empty(0);
//regin_programming_done(21) <= ext_fifo_empty(1);
#define EMPTY_MASK 	        0x00300000

//regin_programming_done(22) <= ext_fifo_almost_empty when((ext_fifo_empty = "00") and (ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
#define ALMOST_EMPTY_MASK 	0x00400000

//regin_programming_done(23) <= ext_fifo_half_full when((ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
#define HALF_FULL_MASK 	0x00800000

//regin_programming_done(24) <= ext_fifo_almost_full when(ext_fifo_full = "00") else '0';
#define ALMOST_FULL_MASK 	0x01000000

//regin_programming_done(25) <= ext_fifo_full(0);
//regin_programming_done(26) <= ext_fifo_full(1);
#define ALL_FULL_MASK 	0x06000000

#define MIN_READ_INTERVAL	10

//---------------------------------------------------
//	FROM SendReceiveSupport
//---------------------------------------------------
#define CLIENT_REQUEST_EVENTS 1
#define SERVER_NORMAL_RETURN 2
#define SERVER_SENDER_OFF 3
#define SERVER_SUMMARY 4
#define INSUFF_DATA 5

// ported from devGVME
#define GVME_NUM_REGISTERS 0x24
#define GVME_MAX_CARDS 7		//20230921:  there are SEVEN slots in each VME crate

//==============================
//---         Enums         --- 
//==============================
//---------------------------------------------------
//	FROM QueueManagement
//---------------------------------------------------
#define PRINT_BUFFER_ERRORS
typedef enum {
	OWNER_UNDEF = 0,
	OWNER_Q_FREE = 1,		//means "in the free queue"
	OWNER_INLOOP = 2,		//means "belongs to inLoop state machine"	//#MERGED_FROM_CON6
	OWNER_Q_WRITTEN = 3,	//means "in the written queue"
	OWNER_OUTLOOP = 4,		//means "belongs to the outLoop state machine"	//#MERGED_FROM_CON6
	OWNER_Q_SENDER = 5,		//means "in the written queue"
	OWNER_SENDER = 6,		//means "belongs to the sender state machine" //#MERGED_FROM_CON6
} owner_enum;
typedef enum  {
	Success,
	NoBufferAvail,				//attempt to get a queue failed
	DMAError,					//as implied
	QueuePutError,			//tried to put a buffer into a queue and failed - can look at VxWorks manual for function to decode error type.
	TrigFIFOFormatError,
	IncorrectModeArg		//user called DigitizerTypeFHeader with unknown value of mode argument.
}BufReturnVals;
//==============================
//---   Stucts and Unions   --- 
//==============================
//---------------------------------------------------
//	FROM QueueManagement
//---------------------------------------------------
typedef struct {
	unsigned int id;			// unique id for this buffer. never changes.
	unsigned int *datapcrosscheck;	// data pointer cross check.
	unsigned int board;		/* identifier of which board within the VME crate this data is related to; copied from daqBoard structure as buffer is processed */
	unsigned int len;      /* length of the data buffer this struct points to, in BYTES */
	unsigned int *data;		/* pointer to the actual data buffer this structure talks about */
	owner_enum owner; 		/* list of values from enum immediately above saying who owns this buffer*/
	unsigned short board_type;  /*  board TYPE number data is related to; copied from daqBoard structure as buffer is processed   Added 20220801 */
	unsigned short data_type;	/* 0 means "normal data", anything else is board specific.  Added 20220801 */
} rawEvt;

//---------------------------------------------------
//	FROM SendReceiveSupport
//---------------------------------------------------
//=============================================================================================
//	Structure of data from Receiver (on a linux box) to Sender (this IOC)
//=============================================================================================

// make a union to simplify access to the structure by send() and recv() functions that 
// just want a char * to work with.
typedef union  
	{
	int type;		//the message that the receiver sends us is just a single int.
	char RawMsg[4];  //MBO 20200611: Only 4 bytes.
	//char RawMsg[24];	//char buffer of same size, for recv() calls
	} ReqMsg;

//=============================================================================================
//	Structure of data from Sender (this IOC) to Receiver (on a linux box)
//=============================================================================================


//	This is the format of the response messages that the server sends to the receiver
//	when it gets a request message.
typedef struct 
	{
	int type;		/* usually SERVER_SUMMARY */ //one of #defn'd values CLIENT_REQUEST_EVENTS, SERVER_NORMAL_RET...etc.
	int recLen;		/* 32 bit words/record -DEPRECATED FOR DGS */  //total size, in bytes, of data that will follow from the server code (digitzer/trigger buffer)
					//The Receiver will expect "ntohl(recLen)" bytes of "payload" to follow a SERVER_SUMMARY response header.  
	int status;		/* 0 for succcess */ //apparently unused in GtReceiver4.  Per Torben in email of 20200609, no known use in Recevier, Merge or Sort software.
	int recs;		/* number of records */ //MBO 20200611: added. 
	// int tot_datasize	/*total data for all records- for DGS*/
	//int pad;		//MBO 20200611: This is no longer a member of evtServerRetStruct.   //apparently unused in GtReceiver4.  Per Torben in email of 20200609, 'status' and 'pad' likely exist solely to make this struct align to a 64-bit boundary.
	//long long st;		//MBO 20200611: This is no longer a member of evtServerRetStruct.  //apparently unused in GtReceiver4.  Per Torben in email of 20200609, no known use in Recevier, Merge or Sort software.
	//long long et;		//MBO 20200611: This is no longer a member of evtServerRetStruct.  //apparently unused in GtReceiver4.  Per Torben in email of 20200609, no known use in Recevier, Merge or Sort software.
	} evtServerRetStruct;

// make a union to simplify access to the structure by send() and recv() functions that 
// just want a char * to work with.
typedef union 
	{
	evtServerRetStruct Fields;
	char RawMsg[16];	//char buffer of same size, for send() calls
	} ResponseMsg;


struct daqRegister {
   volatile unsigned int *addr;
   epicsMutexId sem;
   int tick;
   unsigned int copy;
   unsigned int dibs;
};

struct daqDevPvt {
   struct daqRegister *reg;
   unsigned int mask;
   unsigned short shft;
   unsigned short signal;
   unsigned short card;
   unsigned short chan;
};


struct daqBoard {
	//ihtEntry *registers;  //commented out 20200406  JTA
	struct daqRegister vmeRegisters[GVME_NUM_REGISTERS];
	volatile unsigned int *base32;
	volatile unsigned int *FIFO;
	unsigned short vmever;
	unsigned int rev;		//changed by JTA 20190114 from unsigned short rev;
	unsigned int subrev;		//changed by JTA 20190114 from unsigned short subrev;
	unsigned short mainOK;
	unsigned short board;
	unsigned short EnabledForReadout;	//added jta 20200611 for inLoop control, replacing other arrays that were in dgsData.
	int DigUsrPkgData;	//added 20200617 for Type F headers.
	int TrigUsrPkgData;	//added 20220713 for trigger Type F headers.
	unsigned short router;
   //added by JTA 20190114
   //Need explicit numerical identifier of what kind of board this is.
   //Tie these to strings, defined as const char BoardTypeNames[16][30]
   //The indices are derived from values defined for the firmware.
		//	"No Board Present",			//0
		//	"GRETINA Router Trigger",	//1		--the '1' comes from bits 11:8 of the code_revision register
		//	"GRETINA Master Trigger",	//2		--the '2' comes from bits 11:8 of the code_revision register
		//	"LBNL Digitizer",			//3		--arbitrary placeholder assigned by JTA
		//	"DGS Master Trigger",		//4		--the '4' comes from bits 11:8 of the code_revision register
		//	"Unknown",					//5
		//	"DGS Router Trigger",		//6		--the '6' comes from bits 11:8 of the code_revision register
		//	"Unknown",					//7
		//	"MyRIAD",					//8		--arbitrary placeholder assigned by JTA
		//	"Unknown",					//9
		//	"Unknown",					//10
		//	"Unknown",					//11
		//	"ANL Master Digitizer",		//0xC : 12	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
		//	"ANL Slave Digitizer",		//0xD : 13	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
		//	"Majorana Master Digitizer",//14		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
		//	"Majorana Slave Digitizer",	//15		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
   
   unsigned short board_type;	//numerical index of type of board this is.   Index into list of names as shown in comment.
};

//==============================
//	20220801: to simplify comparisons against board_type, make #defines here.
//==============================
#define BrdType_NO_BOARD		0
#define BrdType_GRETINA_RTRIG	1
#define BrdType_GRETINA_MTRIG	2
#define BrdType_LBNL_DIG		3
#define BrdType_DGS_MTRIG		4
#define BrdType_UNDEF_5			5
#define BrdType_DGS_RTRIG		6
#define BrdType_UNDEF_7			7
#define BrdType_MYRIAD			8
#define BrdType_UNDEF_9			9
#define BrdType_UNDEF_10		10
#define BrdType_UNDEF_11		11
#define BrdType_ANL_MDIG		12
#define BrdType_ANL_SDIG		13
#define BrdType_MAJORANA_MDIG	14
#define BrdType_MAJORANA_SDIG	15

//==============================
//---        Externs        --- 
//==============================
// from devGVME.c
extern struct daqBoard daqBoards[GVME_MAX_CARDS];

extern int FBufferCount;

extern FILE *IL_FILE;
extern FILE *OL_FILE;
extern FILE *MS_FILE;


//  from SendReceiveSupport.c
//--------------------
//	Define a union (buffer/structure) to hold any message from the receiver.
//	This buffer is continuously overwritten as new messages arrive.
//--------------------
extern ReqMsg ReceiverMessage;
extern ReqMsg *RequestFromReceiver;

//--------------------
//	Define a structure to hold the buffer descriptor for each item pulled off the QSend queue.
//--------------------
extern rawEvt WorkingBuffer;
extern rawEvt *WorkingDescriptor;

extern int asyn_debug_level;
extern int inloop_debug_level;
extern int outloop_debug_level;
extern int sender_debug_level;
//==============================
//---       Prototypes      --- 
//==============================
#endif //ifndef _DGS_DEFS_H
