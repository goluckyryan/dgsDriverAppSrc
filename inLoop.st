/****************************************************************************/
/*	inLoop is the state machine that polls the boards in the crate and		*/
/*	sucks data out of them.  This machine pulls buffers off of the QFree	*/
/*	queue, fills them with data from ONE board in the VME crate, and then	*/
/*	stuffs the buffer onto the QWritten queue.								*/
/*																			*/
/*	A JTA/MBO "we don't remember who wrote what line" production, 2019-2020 */
/*																			*/
/*	On 20200612, much of the inline code moved to inLoopSupport.c.			*/
/*																			*/
/*  July 2022, another big whack to implement readout of trigger modules	*/
/*	and increase number of boards per crate from 4 to 6.					*/
/****************************************************************************/


// The CN is the crate number
program  inLoop ("CRATE=CRATE, B0=B0, B1=B1, B2=B2, B3=B3, B4=B4, B5=B5, B6=B6")


//option +r;		//means this state machine is re-entrant so that you can have multiple copies running.
					//in the rewrite of 20190109, we plan on having only one machine per crate, rather than
					//the one-per-board paradigm of the original design, so we change this to -r.
option -r;
option +s;			//added 20200612 for thread safety checking.

/***************************************************
*	Found a manual at www.slac.stanford.edu/grp/ssrl/spear/epics/site/seq/Manual.pdf
* that explains the various "options" for version 2.0.99 of SNL (our version is much older).
*
*	+/-a controls PV gets. -a is synchronous(wait for completion), and is the default.
*	+/-c controls PV connections.  +c is wait for all pvs to connect before starting, and is the default.
*	+/-d controls run-time debug messages.  -d (no) is the default.
*	+/-e controls "event flag mode".  +e (new mode) is the default.
*	+/-l controls "line markers".  + means that compilation errors will have line numbers relevant to the SNL source file; - is the default.
*	+/-m controls generation of a main() procedure in the SNL program.  -m is the default.
*	+/-r controls whether state machine is re-entrant code, allowing multiple copies on one IOC.  -r is default.
*	+/-w controls SNc warning messages, + is the default.

* This manual also states "Normally, the state programs are not run until all process variables are connected". This can be overridden
* by use of the -c option.

* The manual also says you can have one entry{} and one exit{} item defined outside state blocks, that are the global entry
* and exit code that occurs at machine start and machine end.

************************************************************/

/****************************************************
 *
 *  From the manual:
Because the SNL does not support the full C language, C code may be escaped in the program. The escaped code
is not compiled by snc, instead it is literally copied to the generated C code. There are two escape methods:
1.  Any code between %% and the next newline character is escaped. Example:
	%% for (i=0; i < NVAL; i++)
2.  Any code between %{and}% is escaped. Example:
	%{
	extern float smooth();
	extern LOGICAL accelerator_mode;
	}%

Note that text appearing on the same line after %{ and before }% also belongs to the literal code block.

   C preprocessor directives inside a %{...}% don't seem to work as expected.  #ifdefs always disable even if the thing is defined.

    Oddly enough you'd *think* that you need to include DGS_DEFS.h, but it doesn't seem to matter.  Apparently because QueueManagement.h
    and profile.h #include it for you.
***************************************************/
%% #include "readDigFIFO.h"
%% #include "readTrigFIFO.h"
%% #include "inLoopSupport.h"
%% #include <stdio.h>
%% #include <epicsMutex.h>
%% #include <epicsMessageQueue.h>
%% #include <epicsEvent.h>
%% #include <freeList.h>
%% #include <taskLib.h>
%% #include "QueueManagement.h"
%% #include "devGVME.h"



// The following #defines make psuedo-functions used for monitoring or assignment operations
// between integeditrnal variables and EPICS process variables. 


//DECLMON(<variable type>,<variable name>,<PV name>) monitors a given PV so that the variable here
//is a shadow copy of the PV.  Updates, presumably, whenever the PV is changed.
#define DECLMON(t,n,s)          \
        t n;                    \
        assign n to #s;         \
        monitor n;

//DECL(<variable type>,<variable name>,<PV name>) is supposed to push a local variable into a PV.
#define DECL(t,n,s)             \
        t n;                    \
        assign n to #s;

//Array version of DECL.
#define DECLARRAY(t,n,s,l)      \
        t n[l];                 \
        assign n to #s;

#define DECLEVENT(t,n,s)        \
        t n;                    \
        assign n to #s;         \
        monitor n;              \
        evflag n##Event;        \
        sync n n##Event;        \
        t n##Save;



DECLMON	(short,	AcqRun,			Online_CS_StartStop)		//Online_CS_StartStop is the master run/stop control.
															//This PV is declared in the dgsSupport.db database
															//that is used by dgsSoftIOC, a Soft IOC running on 
															//the boot host Linux machine.

// DAQC{CRATE}:inLoop_Running is a semaphore sent to the outLoop machine to let it know that inLoop is running.
// The PV is defined in the daqCrate.template database loaded by each VME IOC.
DECL(short,	InloopIsRunning,		DAQC{CRATE}:inLoop_Running)		//inLoop Handshake PV.

// DAQC{CRATE}_CV_InLoop1 through InLoop4 are PVs that display on the VME Summary screen.
// These PVs are defined in the daqCrate.template database loaded by each VME IOC.
//
DECL(float ,MBytesPerSec, DAQC{CRATE}_CV_InLoop1)
DECL(long ,Local_FBufferCount, DAQC{CRATE}_CV_InLoop2)
DECL(short ,NumXfers, DAQC{CRATE}_CV_InLoop3)
DECL(long ,CurrentTransferResult, DAQC{CRATE}_CV_InLoop4)

// DAC{CRATE}_BoardType0 through 6 come from the daqCrate.template database loaded by each IOC.
//These get filled when inLoop starts and are driven out by inLoop to be displayed on the GUI screens.
DECL(short ,BoardType0, DAQC{CRATE}_BoardType0)
DECL(short ,BoardType1, DAQC{CRATE}_BoardType1)
DECL(short ,BoardType2, DAQC{CRATE}_BoardType2)
DECL(short ,BoardType3, DAQC{CRATE}_BoardType3)
DECL(short ,BoardType4, DAQC{CRATE}_BoardType4)
DECL(short ,BoardType5, DAQC{CRATE}_BoardType5)
DECL(short ,BoardType6, DAQC{CRATE}_BoardType6)


// The following DECLMONs set up monitors for the board-by-board readout enables.
//
//	Example portion of IOC startup:
//
//	dbLoadRecords("db/daqSegment2.template",      "P=VME10:,R=MTRG:")
//	dbLoadRecords("db/daqSegment2.template",      "P=VME10:,R=MDIG1:")
//	dbLoadRecords("db/daqSegment2.template",      "P=VME10:,R=SDIG1:")

//
//	Within daqsegment2.template, the PV of interest is defined as
//
//	record(bo, "$(CRATE):$(BOARD):CS_Ena") 
//	{
//	  field(DESC, "Software board enable")
//	  field(PINI, "YES")
//	  field(DTYP, "Raw Soft Channel")
//	  field(DOL, "1")
//	  field(ZNAM, "Disable")
//	  field(ONAM, "Enable")
//	}
//
//	So the combination above will generate PVs named VME10:MTRG:CS_Ena, VME10:MDIG1:CS_Ena and VME10:SDIG1:CS_Ena
//
//	This will vary crate to crate so this requires the startup script of the IOC to invoke inLoop with 
//	additional substitution variables so that these names may be mapped into an array of internal variables.
//	This is done by using DECLMON statements to map the PV names to a list of individual internal variable names,
//	and then the inLoop code calls a startup function SetupBoardAddresses() that maps the individual internal variable
//  names to an array that we use in the inLoop scan loop (daqBoards[BoardNumber].EnabledForReadout).
//
//	The invocation of inLoop will be formatted something like
//
//	seq &inLoop,"CRATE=10,B0=MDIG1,B1=SDIG1,B2=X,B3=X,B4=MTRG,B5=X,B6=X"

DECLMON	(short,	B0En, VME{CRATE}:{B0}:CS_Ena)
DECLMON	(short,	B1En, VME{CRATE}:{B1}:CS_Ena)
DECLMON	(short,	B2En, VME{CRATE}:{B2}:CS_Ena)
DECLMON	(short,	B3En, VME{CRATE}:{B3}:CS_Ena)
DECLMON	(short,	B4En, VME{CRATE}:{B4}:CS_Ena)
DECLMON	(short,	B5En, VME{CRATE}:{B5}:CS_Ena)
DECLMON	(short,	B6En, VME{CRATE}:{B6}:CS_Ena)

DECLMON	(short,	CRATENUM, DAQC{CRATE}_CV_CRATENUM)

// The 'X' case (e.g. VME10:X:CS_Ena) is a DUMMY process variable defined in daqCrate.template because
// daqCrate.template is only loaded once per crate.  daqSegment2 is loaded multiple times, once per
// board in the crate.


// These DECLMONs provide monitoring of a PV per board saying which FIFO to read, that also comes from daqSegment2.
// Only matters for triggers. FifoNum values are 1-16 where 1-7 are the Mon FIFOs and 8-16 are the Chan FIFOs.  
// The inLoop code ignores the value if the board is a digitizer because there's only one FIFO to read.
DECLMON	(short,	B0FifoNum, VME{CRATE}:{B0}:FifoNum)
DECLMON	(short,	B1FifoNum, VME{CRATE}:{B1}:FifoNum)
DECLMON	(short,	B2FifoNum, VME{CRATE}:{B2}:FifoNum)
DECLMON	(short,	B3FifoNum, VME{CRATE}:{B3}:FifoNum)
DECLMON	(short,	B4FifoNum, VME{CRATE}:{B4}:FifoNum)
DECLMON	(short,	B5FifoNum, VME{CRATE}:{B5}:FifoNum)
DECLMON	(short,	B6FifoNum, VME{CRATE}:{B6}:FifoNum)

// The 'X' case (e.g. VME10:X:FifoNum) is a DUMMY process variable defined in daqCrate.template because
// daqCrate.template is only loaded once per crate.  daqSegment2 is loaded multiple times, once per
// board in the crate. 



//================================================================
//	Variable declarations (just like C)
//
//	NOTE: These variables are PRIVATE to inLoop.  Any passing of
//	variables to routines located in files outside of inLoop has to
//	be done using PVs or equivalent semaphores.
//================================================================
int SendNextEmptyCount[10];			//scaling counter for SendNextEmpty
%% int SendNextEmptyPrescale[10] = {4,4,4,4,4,4,4,4,4,4};	//scaling value for SendNextEmpty
int SendNextEmpty[10];				//flag value, incremented at (update timer rate/SendNextEmptyPrescale), to send one set of empty-check messages every n times the stats are updated.

int TestDigEmpty;				//return value of data transfer during draining state, used to catch readout errors.
int LocalFBufferCount;

%%unsigned long long int TotalBytesTransferred;		//Accumulator to count all data transferred across all boards in crate.

float UpdateDelay;				//delay, in seconds, between statistics PV updates.
float ScanDelay;				// MBO 20200616: Added delay, in seconds, between digitizer scan cycles.  This will be updated by a function in inLoopSupport.c
float SecondCounter;			//counter that counts by UpdateDelay each UpdateDelay loop, to get approximate total run time in seconds.

int BoardNumber;				//which VME slot is in play throughout the identification and scan loops.

int NumBoardsEnabled;			//return value from setup, total count of boards that were enabled for readout by user.

#ifdef INLOOP_PROCESS_TRIGGER		// MBO 20200601: remove warning regarding ununsed variable.
	/*|*/	int TriggerFifoDepth;
  int TriggerCalcEventSize;
  float TriggerCalcNumEventsAvail;
#endif

//int inLoop_TID;
//int inLoopPriority_LOW = 129;
//int inLoopPriority_NORMAL = 149;
//int inLoopPriority_HIGH = 149;

char *TypeOfBoard;		//mostly deprecated character string for text type of board, but preserved in case necessary for trigger portions.

int globQueueUsageFlag;  //set this to 1 to enable use of queues.

int SendNxtEmptyidx;

// int LowPriorityChangeFlag;
// int HighPriorityChangeFlag;

int FIFO_index[7];	//20220719 : put in to map the B0FifoNum - B6FifoNum into an array

/// global entry function to tell user machine has at least started.
entry 
	{
	printf("inLoop Global Entry.  About to connect PVs.\n");
	printf("inLoop Global Entry.  Monitoring FIFO #s %d,%d,%d,%d,%d,%d,%d\n",
		B0FifoNum,B1FifoNum,B2FifoNum,B3FifoNum,B4FifoNum,B5FifoNum,B6FifoNum);
	}


/// global exit function

exit
	{
	printf("inLoop Global Exit.\n");
	}







/// in the state machine below, C escapes are all pushed to column 1 to more clearly mark escaped code.

ss InLoopStats {
	state INIT 
		{
		entry 
			{
			printf("InLoopStats INIT entry \n");
			LocalFBufferCount = 0;
			MBytesPerSec = 0.0;
			SecondCounter = 0.0;
//inline C code is marked by %{...}%.
%{
			for(SendNxtEmptyidx=0;SendNxtEmptyidx<10;SendNxtEmptyidx++)
				{
				SendNextEmpty[SendNxtEmptyidx] = 1;
				SendNextEmptyCount[SendNxtEmptyidx] = 0;
				}
}%
			UpdateDelay = 0.5;
			}
		when(AcqRun)
			{
			printf("InLoopStats now running \n");
			} state UPDATE
		}

	state RUN
		{
		when(!AcqRun) {
			} state HOLD
		when(delay(UpdateDelay)) 
			{
			} state UPDATE
		}

	state UPDATE 
		{
		 
		when() 
			{
//inline C code is marked by %{...}%.
%{
			for(SendNxtEmptyidx=0;SendNxtEmptyidx<10;SendNxtEmptyidx++)
				{
				SendNextEmptyCount[SendNxtEmptyidx] = SendNextEmptyCount[SendNxtEmptyidx] + 1;
				if (SendNextEmptyCount[SendNxtEmptyidx] >= SendNextEmptyPrescale[SendNxtEmptyidx])			
					{
					SendNextEmpty[SendNxtEmptyidx] = 1;
					SendNextEmptyCount[SendNxtEmptyidx] = 0;
					}
				}
}%
			SecondCounter = SecondCounter + UpdateDelay;
			//use of casting must be escaped to raw C transliteration or compile will fail.
			// MBO 202006024: MBytesPerSec has been in use as a total data count.  Changing / 1000 to / (1024 * 1024)
			//single statement inline C code is marked by %%.  Appears to be required if using floating point or casting.
%%			MBytesPerSec = (float)(TotalBytesTransferred / (unsigned long long int)(1024 * 1024));
			pvPut(MBytesPerSec);	//inLoop1
%%			Local_FBufferCount = FBufferCount;
			pvPut(Local_FBufferCount);	//inLoop2
			pvPut(NumXfers);	//inloop3
			} state RUN
		}

	state HOLD
		{
		when(AcqRun)
			{
			MBytesPerSec = 0.0;
			SecondCounter = 0.0;
			} state UPDATE
		}
}

/*
	The InLoop state machine, as rewritten 20190110, is intended to scan an entire crate of VME boards.
	At initialization the machine checks each slot to determine which are populated, and which slots
	have what kind of boards.  The existence of an external array daqBoards[bdnum] is expected.  The
	structure itself is defined in DGS_DEFS.h.
*/

//========================================== STATE MACHINE ====================================================
//
//	State list:
//
//		INIT : where we begin.  Waits for PV "Online_CS_StartStop" to be in START.  Upon receipt of START,
//			   leaps to state INITIAL_FIFO_CLEAR.
//
//		INITIAL_FIFO_CLEAR: Ensures that any old data is expunged.  Goes to each slot. Action differs based upon board type.
//								Digitizer: sets Master Logic Enable OFF, clears FIFO.
//								Trigger: only clears FIFO.				
//				INITIAL_FIFO_CLEAR then transitions to state ENABLE.
//					-- un-occupied slots are skipped.
//
//		ENABLE_DIGITIZERS : scans across the crate and turns ON the master logic enable of all digitizers.
//				 Then transitions to state SCAN_FOR_DATA.
//				 20220713:  there is no "enabling" for trigger boards akin to that of the digitizers.
//							it is presumed that triggers are ALWAYS "enabled"
//
//		SCAN_FOR_DATA : Scans across all occupied slots.  For each slot, reads FIFO status register and/or
//					    FIFO depth value to see if any data is available.  One of two things occurs.
//	
//						-- if data is not available, read timestamp registers of module, generate
//						   a "type F" header, insert that into the data stream, and free buffer for processing.
//
//						-- if data is available, calculate # of words to read.  This is defined as 
//						   (event_size * # number of events) that is as close to but less than a globally set
//						   limit MAX_READOUT_SIZE.  Nominally MAX_READOUT_SIZE is based upon the size of the
//						   buffers being allocated, but can be set less than the buffer size for balancing.
//
//			State SCAN_FOR_DATA runs forever until the PV "Online_CS_StartStop" is set to STOP.  Upon the PV transition to STOP,
//			the machine transitions to state DISABLE_COLLECTION.
//
//		DISABLE_COLLECTION : Scans all the slots to turn off any more data gathering.  Action differs based on board type.
//								Digitizer : set Master Logic Enable to OFF.
//								Trigger : set FIFO WE control to "ALWAYS OFF"
//			Upon completion machine transitions to state DRAIN_REMAINING_DATA.
//
//		DRAIN_REMAINING_DATA: Scans across all occupied slots.  In each pass:
//										Reads FIFO depth register.
//											  If FIFO depth register reads back zero:
//													"type F" header is put into the data stream.
//													go to next slot, stay in DRAIN_REMAINING_DATA.
//											  If FIFO depth <> 0:
//													Reads all data that remains into buffer, still subject to the MAX_READOUT_SIZE rule.
//													go to next slot, stay in DRAIN_REMAINING_DATA.
//													go to next slot, stay in DRAIN_REMAINING_DATA.
//
ss inLoop {
//============================================================================================================
//		INIT : where we begin.  Waits for PV "Online_CS_StartStop" to be in START.  Upon receipt of START,
//			   leaps to state INITIAL_FIFO_CLEAR.
//============================================================================================================
	state INIT
		{
		entry
			{
			printf("\n############## inLoop : INIT\n");
			printf("InLoop INIT entry\n");
			taskPrioritySet(taskIdSelf(), 190);
			globQueueUsageFlag = 1;		
			InloopIsRunning = 0;
			pvPut(InloopIsRunning);		//The 'InloopIsRunning' PV is a flag between inLoop and outLoop to let outLoop know inLoop is actively scanning.
			printf("InLoop INIT entry:set 'running' flag to outLoop = 0\n");
			printf("\ninLoopInfo: globQueueUsageFlag=%d\n",globQueueUsageFlag);
								
			printf("InLoop INIT entry: inloop_debug_level=%d\n", inloop_debug_level);


			BoardNumber = 0;
%%			TotalBytesTransferred = 0;
			NumXfers=0;
%%			FBufferCount = 0;

//  high/low priority change things deprecated, but left in as comments.
//			LowPriorityChangeFlag = 0;
//			HighPriorityChangeFlag = 0;

			ScanDelay = 0.01;		// MBO 20200616: Scan throttle control.  Updated later by function in inLoopSupport.c
			printf("InLoop INIT entry:calling SetupBoardAddresses\n");
			//SetupBoardAddresses maps the enables to the daqBoard data structure, that is indexed by the assigned "card #" (not the physical SLOT, the CARD).
			
			NumBoardsEnabled = SetupBoardAddresses(CRATENUM, 6, B0En, B1En, B2En, B3En, B4En, B5En, B6En);		//mod 20200616 to add per-board enables.
			CloseDumpFiles();
			printf("InLoop:INIT:entry: %d boards enabled by user\n",NumBoardsEnabled);
// Any code between %{and}% is escaped.  C code with braces inside a state machine gets misinterpreted as state transition.
%{			
			for(BoardNumber = INLOOP_MIN_BOARD_NUMBER; BoardNumber <= INLOOP_MAX_BOARD_NUMBER; BoardNumber++)
				{
				switch(daqBoards[BoardNumber].board)	//this is the SLOT number
					{
					case 1: BoardType0 = daqBoards[BoardNumber].board_type; break;
					case 2: BoardType1 = daqBoards[BoardNumber].board_type; break;
					case 3: BoardType2 = daqBoards[BoardNumber].board_type; break;
					case 4: BoardType3 = daqBoards[BoardNumber].board_type; break;
					case 5: BoardType4 = daqBoards[BoardNumber].board_type; break;
					case 6: BoardType5 = daqBoards[BoardNumber].board_type; break;
					case 7: BoardType6 = daqBoards[BoardNumber].board_type; break;
					default: break;
					}			
				}
}%	//end of C code	

//pvput is only defined as a state language thing, not a C thing, so must put the board numbers outside a C escape.
//pvput forces the PV associated with the internal variable to be updated with the value of the internal variable.

				pvPut(BoardType0); 
				pvPut(BoardType1); 
				pvPut(BoardType2); 
				pvPut(BoardType3); 
				pvPut(BoardType4); 
				pvPut(BoardType5); 
				pvPut(BoardType6);			
			}				
		when(AcqRun) 
      		{
			InloopIsRunning = 1;
%%			TotalBytesTransferred = 0;
			pvPut(InloopIsRunning);
			printf("InLoop INIT entry:set 'running' flag to outLoop = 1\n");
	      	printf("\n*****************\nStarting ACQ State Machine\n*****************\n");
			NumBoardsEnabled = SetupBoardAddresses(CRATENUM, 6, B0En, B1En, B2En, B3En, B4En, B5En, B6En);		//mod 20200616 to add per-board enables.
			printf("InLoop:INIT:RUN: %d boards enabled by user\n",NumBoardsEnabled);
			printf("InLoop:INIT:RUN: Board enable pattern: B0en=%d B1en=%d B2en=%d B3en=%d B4en=%d B5en=%d B6en=%d\n",B0En, B1En, B2En, B3En, B4En, B5En, B6En);
			printf("InLoop:INIT:RUN: Board name pattern: 0:%s 1:%s 2:%s 3:%s 4:%s 5:%s 6:%s\n",BoardTypeNames[daqBoards[0].board_type],BoardTypeNames[daqBoards[1].board_type],BoardTypeNames[daqBoards[2].board_type],BoardTypeNames[daqBoards[3].board_type],BoardTypeNames[daqBoards[4].board_type],BoardTypeNames[daqBoards[5].board_type],BoardTypeNames[daqBoards[6].board_type]);

			} state INITIAL_FIFO_CLEAR
		when (delay(10)) 
			{
			InloopIsRunning = 0;
			pvPut(InloopIsRunning);
//			printf("\ninLoop idle.  Waiting for AcqRun\n");
			FIFO_index[0]= B0FifoNum;
			FIFO_index[1]= B1FifoNum;
			FIFO_index[2]= B2FifoNum;
			FIFO_index[3]= B3FifoNum;
			FIFO_index[4]= B4FifoNum;
			FIFO_index[5]= B5FifoNum;
			FIFO_index[6]= B6FifoNum;

// Fifo Index Cheat sheet for trigger modules
//		address		name			index
//		0x0160,		//MON FIFO 1	0
//		0x0164,		//MON FIFO 2	1
//		0x0168,		//MON FIFO 3	2
//		0x016C,		//MON FIFO 4	3
//		0x0170,		//MON FIFO 5	4
//		0x0174,		//MON FIFO 6	5
//		0x0178,		//MON FIFO 7	6		<==the usual FIFO for most applications.
//		0x017C,		//MON FIFO 8	7
//		0x0180,		//CHAN FIFO 1	8
//		0x0184,		//CHAN FIFO 2	9
//		0x0188,		//CHAN FIFO 3	10
//		0x018C,		//CHAN FIFO 4	11
//		0x0190,		//CHAN FIFO 5	12
//		0x0194,		//CHAN FIFO 6	13
//		0x0198,		//CHAN FIFO 7	14
//		0x019C		//CHAN FIFO 8	15


			} state INIT		//check the state of AcqRun every 10 seconds.
		}  //end of state INIT

//============================================================================================================
//		INITIAL_FIFO_CLEAR: Ensures that any old data is expunged.  Goes to each slot. Action differs based upon board type.
//								Digitizer: sets Master Logic Enable OFF, clears FIFO.
//								Trigger: only clears FIFO.				
//				INITIAL_FIFO_CLEAR then transitions to state ENABLE.
//					-- un-occupied slots are skipped.
//
//============================================================================================================
	state INITIAL_FIFO_CLEAR
		{
		option -e;	/* do entry every time state runs */
			entry 
				{
// Any code between %{and}% is escaped.  C code with braces inside a state machine gets misinterpreted as state transition.
%{
				if (inloop_debug_level >= 3) printf("\n############## inLoop : INITIAL_FIFO_CLEAR\n");
				for(BoardNumber = INLOOP_MIN_BOARD_NUMBER; BoardNumber <= INLOOP_MAX_BOARD_NUMBER; BoardNumber++)
					{
					if (daqBoards[BoardNumber].EnabledForReadout == 1)		//if slot occupied, and is enabled for reading
						{
						if (inloop_debug_level >= 3) printf("inLoop : INITIAL_FIFO_CLEAR: Board #%d, type %d enabled for readout, clearing FIFO\n",BoardNumber,daqBoards[BoardNumber].board_type);
						//perform the appropriate reset based upon the kind of board you think it is.
						switch (daqBoards[BoardNumber].board_type)
							{
							case BrdType_NO_BOARD:
								if (inloop_debug_level >= 3) printf("InLoop : INITIAL_FIFO_CLEAR: type No_Board\n");
								break;

							case BrdType_GRETINA_RTRIG:
							case BrdType_GRETINA_MTRIG:
							case BrdType_DGS_RTRIG:
								if (inloop_debug_level >= 3) printf("InLoop : INITIAL_FIFO_CLEAR: board type %d: %s requires no initial clear\n", daqBoards[BoardNumber].board_type, BoardTypeNames[daqBoards[BoardNumber].board_type]);
								break;

							case BrdType_DGS_MTRIG:
								//Trigger routers and trigger masters are cleared by writing to the FIFO_RESETS register at address 0x08F0.
								if (inloop_debug_level >= 3) printf("InLoop : INITIAL_FIFO_CLEAR: Call ClearTrigFIFO() for board type %d: %s\n", daqBoards[BoardNumber].board_type, BoardTypeNames[daqBoards[BoardNumber].board_type]);
								//SetTrigSoftwareVeto(BoardNumber);
								ClearTrigFIFO(BoardNumber, FIFO_index[BoardNumber]);
								break;

							case BrdType_LBNL_DIG:
							case BrdType_ANL_MDIG:
							case BrdType_ANL_SDIG:
							case BrdType_MAJORANA_MDIG:
							case BrdType_MAJORANA_SDIG:
								if (inloop_debug_level >= 3) printf("InLoop : INITIAL_FIFO_CLEAR: Call ClearDigMstrLogicEnable() then ClearDigFIFO() then CalcDigMaxEventsPerRead()\n");
								ClearDigMstrLogicEnable(BoardNumber);	
								ClearDigFIFO(BoardNumber);
								CalcDigMaxEventsPerRead(BoardNumber);
								break;

							case BrdType_MYRIAD:
								if (inloop_debug_level >= 3) printf("InLoop : INITIAL_FIFO_CLEAR: MyRIAD not yet supported\n");
								break;	//we will get to the MyRIAD after we get triggers working....

							default:
								if (inloop_debug_level >= 3) printf("InLoop : Unknown board type %d: %s\n", daqBoards[BoardNumber].board_type, BoardTypeNames[daqBoards[BoardNumber].board_type]);
								break;
							}  //end switch()
						}  //end (daqBoards[BoardNumber].EnabledForReadout == 1)	
					else
						{
						if (inloop_debug_level >= 1) printf("InLoop : INITIAL_FIFO_CLEAR: Board #%d, type %s not enabled for readout\n", BoardNumber, BoardTypeNames[daqBoards[BoardNumber].board_type]);

						}					} //end for(BoardNumber = 0; BoardN....
}%	//end of C code

 

				} //end of entry condition
		//state transition "logic"
		when(NumBoardsEnabled == 0)
			{
			printf("\n\ninLoop: Crate has no boards enabled, going IDLE\n\n");
			} state IDLE_ERROR
		when(1)
			{				
			} state ENABLE_DIGITIZERS	//upon doing the entry work, jump to state ENABLE.
		} //end of state INITIAL_FIFO_CLEAR

//============================================================================================================
//		IDLE_ERROR : wait for AcqRun to go away before returning to INIT.
//============================================================================================================
	state IDLE_ERROR
			{
			entry
				{
				printf("inLoop: IDLE_ERROR: inLoop halted until STOP\n");
				}

			when (!AcqRun)
				{
		      	printf("inLoop: Acquisition stopped, leaving IDLE_ERROR, going to INIT\n");
				} state INIT
			}

//============================================================================================================
//		ENABLE_DIGITIZERS : scans across the crate and turns ON the master logic enable of all digitizers.
//				 Then transitions to state SCAN_FOR_DATA.
//
//	the loop is by BOARD NUMBER, that is the assigned "card #" from the asynTrigRouterConfig1(<name>,<card #>,<slot>)
//	or asynDigitizerConfig(<name>,<card #>,<slot>).  The daqBoards[] array of structures is organized by "card #"
//  and stores within it the actual slot.
//
//	Trigger modules have no "enable" related to the FIFO as trigger FIFOs are always enabled.
//============================================================================================================

	state ENABLE_DIGITIZERS
		{
		entry
			{
			if (inloop_debug_level >= 3) printf("\n############## inLoop: ENABLE_DIGITIZERS: enabling modules\n");
			}
		when (1)
			{
%{ 
			if (inloop_debug_level >= 3) printf("\n############## inLoop : ENABLE_DIGITIZERS\n");
			for(BoardNumber = INLOOP_MIN_BOARD_NUMBER; BoardNumber <= INLOOP_MAX_BOARD_NUMBER; BoardNumber++) 
				{
				if (daqBoards[BoardNumber].EnabledForReadout == 1)		//if card index occupied, and is enabled for reading
					{
					EnableModule(BoardNumber);  //routine checks board type, safe for trigger or digitizer. 
					}
				}
	      	if (inloop_debug_level >= 2) printf("inLoop: nBoards enabled, FIFOs cleared, beginning readout\n");

}%
			
			} state SCAN_FOR_DATA	//upon doing the entry work, jump to state SCAN_FOR_DATA.
		} //end of state ENABLE_DIGITIZERS.

//============================================================================================================
//		SCAN_FOR_DATA : Scans across all occupied slots.  For each slot, reads FIFO status register and/or
//					    FIFO depth value to see if any data is available.  One of two things occurs.
//	
//						-- if data is not available, read timestamp registers of module, generate
//						   a "type F" header, insert that into the data stream, and free buffer for processing.
//
//						-- if data is available, calculate # of words to read.  This is defined as 
//						   (event_size * # number of events) that is as close to but less than a globally set
//						   limit MAX_READOUT_SIZE.  Nominally MAX_READOUT_SIZE is based upon the size of the
//						   buffers being allocated, but can be set less than the buffer size for balancing.
//
//			State SCAN_FOR_DATA runs forever until the PV "Online_CS_StartStop" is set to STOP.  Upon the PV transition to STOP,
//			the machine transitions to state DISABLE_COLLECTION.
//============================================================================================================

	state SCAN_FOR_DATA
	{
//		//try to build in a little flow control with the sender.
//		//if the sender has too many buffers, back off and let it have some time.
//		when(getSenderBufCount() > 100)
//			{
//			} state SCAN_PAUSE

		//similarly, if there aren't enough free buffers, also take a break.
	// 20200612: don't read any boards if there are fewer than 10 buffers available in QFree - let the digitizer FIFOs back up.  |JTA|
		when(getFreeBufCount() < 10)
			{
			if (inloop_debug_level >= 2) printf("InLoop: Forced delay because <10 free buffers available\n");
			} state SCAN_DELAY
		
		// if the acquisition is stopped, jump to state DISABLE_COLLECTION.
		when (!AcqRun)
			{
	      	if (inloop_debug_level >= 3) printf("inLoop: Acquisition stopped, leaving SCAN_FOR_DATA, going to DISABLE_COLLECTION\n");
			} state DISABLE_COLLECTION

		//otherwise, run once and then leap off to SCAN_DELAY, which after a bit will return back here.
		when ()
			{
// Any code between %{and}% is escaped.  C code with braces gets misinterpreted as state transition.
%{
			if (inloop_debug_level >= 3) printf("\n############## inLoop : SCAN_FOR_DATA\n");
			for(BoardNumber = INLOOP_MIN_BOARD_NUMBER; BoardNumber <= INLOOP_MAX_BOARD_NUMBER; BoardNumber++)
				{
				TypeOfBoard = (char *) (&(BoardTypeNames[daqBoards[BoardNumber].board_type][0]));
				if (daqBoards[BoardNumber].EnabledForReadout == 1)		//if board enabled for readout, scan it.
					{
					switch (daqBoards[BoardNumber].board_type)
						{
						case BrdType_NO_BOARD:
						case BrdType_GRETINA_RTRIG:
						case BrdType_GRETINA_MTRIG:
						case BrdType_DGS_RTRIG:
						case BrdType_MYRIAD:
							break;	//we defer MyRIAD handling until later..trigger modules other than DGS masters, too.
					
						case BrdType_DGS_MTRIG:

							CurrentTransferResult = CheckAndReadTrigger(BoardNumber, FIFO_index[BoardNumber], SendNextEmpty[BoardNumber],globQueueUsageFlag);
							if(CurrentTransferResult >= 0)
								{
								TotalBytesTransferred = TotalBytesTransferred + CurrentTransferResult;
								if (inloop_debug_level >= 1) printf("\ninLoop: Read %ld bytes from MTRG\n",CurrentTransferResult);						
								}
							else
								{
								if (inloop_debug_level >= 1) printf("\n\ninLoop trig FIFO Error: return code %ld reading board #%d\n",CurrentTransferResult,BoardNumber);
								if (inloop_debug_level >= 1) printf("\nResetting Board\n");
								ClearTrigFIFO(BoardNumber, FIFO_index[BoardNumber]);
								}

							break;	// MBO 20220729: Added missing break.


						case BrdType_LBNL_DIG:
							break;	// MBO 20220729: Added 2nd missing break.  We don't support LBNL Dig as of this writting.

						case BrdType_ANL_MDIG:
						case BrdType_ANL_SDIG:
						case BrdType_MAJORANA_MDIG:
						case BrdType_MAJORANA_SDIG:
							CurrentTransferResult = CheckAndReadDigitizer(BoardNumber, SendNextEmpty[BoardNumber],globQueueUsageFlag);
							if ((CurrentTransferResult == 0) && (SendNextEmpty[BoardNumber])) 
									{
									if (inloop_debug_level >= 1) printf("inLoop reset prescale flag for board #%d\n",BoardNumber);
									SendNextEmpty[BoardNumber] = 0;		//reset the prescale flag if you sent an empty.
									}

							if(CurrentTransferResult >= 0)
								{
								NumXfers++;
								TotalBytesTransferred = TotalBytesTransferred + CurrentTransferResult;
								if (inloop_debug_level >= 3) printf("\ninLoop: Read %ld bytes from DIG\n",CurrentTransferResult);						
								}
							else
								{
								printf("\n\ninLoop digitizer FIFO Error: return code %ld reading board #%d\n",CurrentTransferResult,BoardNumber);
								printf("\nResetting Board\n");
								ClearDigMstrLogicEnable(BoardNumber);
								ClearDigFIFO(BoardNumber);
								SetDigMstrLogicEnable(BoardNumber);
								}
						default:
							break;
						}  //end switch()
					} //end if ((getFreeBufCount() > 10) && (daqBoards[BoardNumber].EnabledForReadout == 1))
				} // end for loop over slots
}%	//end of C code escape

			} state SCAN_DELAY

	} //end state SCAN_FOR_DATA

//============================================================================================================
//		SCAN_PAUSE : delay state that waits 1 second and then returns to SCAN_FOR_DATA.
//============================================================================================================
//	state SCAN_PAUSE
//		{
//		when(delay(1))
//			{
//			} state SCAN_FOR_DATA
//		}

	state SCAN_DELAY
		{
		entry 
			{
			if (inloop_debug_level >= 3) printf("\n############## inLoop : SCAN_DELAY\n");
			if (inloop_debug_level >= 3) printf("inLoop: SCAN_DELAY: free buffer count = %d\n",getFreeBufCount());
			ScanDelay = UpdateScanDelay(/*ScanDelay*/);	// MBO 20200616: New function, take the current delay and provides a new delay based on buffer utilization.
//			if ((LowPriorityChangeFlag == 0) && (getSenderBufCount() > 100))
//				{
//				printf("inLoop: SetPriority LOW\n");
//				LowPriorityChangeFlag = 1;
//				HighPriorityChangeFlag = 0;
//				taskPrioritySet(inLoop_TID, inLoopPriority_LOW);
//				}
//			if ((HighPriorityChangeFlag == 0) && (getSenderBufCount() < 40))	// add a bit of hysteresis 
//				{
//				LowPriorityChangeFlag = 0;
//				HighPriorityChangeFlag = 1;
//				printf("inLoop: SetPriority HIGH\n");
//				taskPrioritySet(inLoop_TID, inLoopPriority_HIGH);
//				}
			}

		// if the acquisition is stopped, jump to state DISABLE_COLLECTION.
		when (!AcqRun)
			{
	      	if (inloop_debug_level >= 3) printf("inLoop: Acquisition stopped, leaving SCAN_DELAY, going to DISABLE_COLLECTION\n");
			} state DISABLE_COLLECTION

		when (delay(ScanDelay))	// MBO 20200616: Scan throttle control.  Updated by UpdateScanDelay() in inLoopSupport.c
			{
			} state SCAN_FOR_DATA
		}


//============================================================================================================
//		DISABLE_COLLECTION : Scans all the slots to turn off any more data gathering.  Action differs based on board type.
//								Digitizer : set Master Logic Enable to OFF.
//								Trigger : set FIFO WE control to "ALWAYS OFF"
//			Upon completion machine transitions to state DRAIN_REMAINING_DATA.
//============================================================================================================

	state DISABLE_COLLECTION
	{
	option -e;	/* do entry every time */
		entry 
			{
// Any code between %{and}% is escaped.  C code with braces gets misinterpreted as state transition.
%{
			if (inloop_debug_level >= 3) printf("\n############## inLoop : DISABLE_COLLECTION\n");
			for(BoardNumber = 0; BoardNumber < 7; BoardNumber++)
				if (daqBoards[BoardNumber].mainOK == 1)		//if slot occupied
					{
					//If board is a digitizer, disable it.  Otherwise do nothing.
					switch (daqBoards[BoardNumber].board_type)
						{

						case BrdType_NO_BOARD:
						case BrdType_GRETINA_RTRIG:
						case BrdType_GRETINA_MTRIG:
						case BrdType_DGS_RTRIG:
						case BrdType_MYRIAD:
                            //TODO  TAC-II : for Master Trigger, should disable trigger
							break;

						case BrdType_DGS_MTRIG:
							//SetTrigSoftwareVeto(BoardNumber);
							break;

						case BrdType_LBNL_DIG:
						case BrdType_ANL_MDIG:
						case BrdType_ANL_SDIG:
						case BrdType_MAJORANA_MDIG:
						case BrdType_MAJORANA_SDIG:
							ClearDigMstrLogicEnable(BoardNumber);
							break;
						default:
							break;
						}  //end switch()
					} //end if (daqBoards[BoardNumber].mainOK == 1)
				
}%  //end of C code
			} //end of entry
		// state transition "logic"
		when (1)
			{
			if (inloop_debug_level >= 3) printf("inLoop:  DRAIN_REMAINING_DATA\n");
			} state DRAIN_REMAINING_DATA	//after turning everything off, then drain whatever is left.
	} //end state DISABLE_COLLECTION


//============================================================================================================
//		DRAIN_REMAINING_DATA: Scans across all occupied slots.  In each pass:
//										Reads FIFO depth register.
//											  If FIFO depth register reads back zero:
//													"type F" header is put into the data stream.
//													go to next slot, stay in DRAIN_REMAINING_DATA.
//											  If FIFO depth <> 0:
//													Reads all data that remains into buffer, still subject to the MAX_READOUT_SIZE rule.
//													go to next slot, stay in DRAIN_REMAINING_DATA.
//============================================================================================================

	state DRAIN_REMAINING_DATA
		{
		option -e;	/* do entry every time */
			entry 
				{
// Any code between %{and}% is escaped.  C code with braces gets misinterpreted as state transition.
%{
				if (inloop_debug_level >= 3) printf("\n############## inLoop : DRAIN_REMAINING_DATA\n");
				for(BoardNumber = 0; BoardNumber < 7; BoardNumber++)
				    {
					TypeOfBoard = (char *) (&(BoardTypeNames[daqBoards[BoardNumber].board_type][0]));
					if (daqBoards[BoardNumber].EnabledForReadout == 1)		
						{
						switch (daqBoards[BoardNumber].board_type)
							{


							case BrdType_NO_BOARD:
							case BrdType_GRETINA_RTRIG:
							case BrdType_GRETINA_MTRIG:
							case BrdType_DGS_RTRIG:
							case BrdType_MYRIAD:
								break;	//we defer MyRIAD handling until later..trigger modules other than DGS masters, too.
						
							case BrdType_DGS_MTRIG:
								//Loop on readout until board is empty.
								do
									{
									CurrentTransferResult = CheckAndReadTrigger(BoardNumber, FIFO_index[BoardNumber], SendNextEmpty[BoardNumber],globQueueUsageFlag);
									if(CurrentTransferResult >= 0)
										{
										TotalBytesTransferred = TotalBytesTransferred + CurrentTransferResult;
										}
									else
										{
										printf("\n\ninLoop trig FIFO Error: return code %ld reading board #%d\n",CurrentTransferResult,BoardNumber);
										printf("\nResetting Board\n");
										ClearTrigFIFO(BoardNumber, FIFO_index[BoardNumber]);
										CurrentTransferResult = 0;
										}
									} while ((CurrentTransferResult != 0) && (CurrentTransferResult != -5));
									SendEndOfRun(BoardNumber, globQueueUsageFlag);
								break;

							case BrdType_LBNL_DIG:
							case BrdType_ANL_MDIG:
							case BrdType_ANL_SDIG:
							case BrdType_MAJORANA_MDIG:
							case BrdType_MAJORANA_SDIG:
								//Loop on readout until board is empty.
								do
									{
									TestDigEmpty = CheckAndReadDigitizer(BoardNumber, SendNextEmpty[BoardNumber],globQueueUsageFlag);
									//all negative returns are errors, but when flushing the specific error of -5
									//(board not empty but has less than 1 full event of data) is ignorable.
									//    Descriptions of CheckAndReadDigitizer() returns in inLoopSupport.c.
									if(TestDigEmpty >= 0)
										{
										NumXfers++;
										TotalBytesTransferred = TotalBytesTransferred + CurrentTransferResult;
										}
									else
										{
										printf("\n\ninLoop FIFO Error: return code %d reading board #%d\n",TestDigEmpty,BoardNumber);
										printf("\nFlushing and Disabling Board\n");
										ClearDigMstrLogicEnable(BoardNumber);
										ClearDigFIFO(BoardNumber);
										TestDigEmpty = 0;
										}
									} while ((TestDigEmpty != 0) && (TestDigEmpty != -5));

									SendEndOfRun(BoardNumber, globQueueUsageFlag);
								break;
							default:
								break;
							}  //end switch()
						} //end (daqBoards[BoardNumber].EnabledForReadout == 1)
					} //end for(BoardNumber = 0; Board...
}%  //end of C code
				InloopIsRunning = 0;
				pvPut(InloopIsRunning);
				} //end of entry condition
		/*********************************************
		// state transitions
		*********************************************/
		when (!InloopIsRunning) 
				{
				printf("inLoop: Draining complete..going IDLE\n");
				} state INIT
				
		} // end state DRAIN_REMAINING_DATA

} // end of state machine


