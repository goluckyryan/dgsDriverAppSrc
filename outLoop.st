//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		outLoop.st
// Description: The next loop outside of inLoop.st.  :)
//				Checks digitizer data for alignment and performs any extra
//				processing that the sender may need.
//				Name to be changed after initial laughs.
//--------------------------------------------------------------------------------

// Must provide CRATE=1,2,3,...,10,11,12
program  outLoop ("CRATE=CRATE")

// Documentation at https://www-csr.bessy.de/control/SoftDist/sequencer/Compiling.html
//  option +d;	// Turn on run-time debug messages.


option -r;	// Generated code is not reentrant. This is the default.
option +s;	//added 20200612 for thread safety checking.

%% #include "devGVME.h"

%% #include <stdio.h>
%% #include <epicsMutex.h>
%% #include <epicsMessageQueue.h>
%% #include <epicsEvent.h>
%% #include <freeList.h>
%% #include <taskLib.h>

%% #include "DGS_DEFS.h"
%% #include "readDigFIFO.h"
%% #include "readTrigFIFO.h"
%% #include "QueueManagement.h"
%% #include "outLoopSupport.h" 


//DECLMON(<variable type>,<variable name>,<PV name>) monitors a given PV so that the variable here
//is a shadow copy of the PV.  Updates, whenever the PV is changed.
#define DECLMON(t,n,s)          \
        t n;                    \
        assign n to #s;         \
        monitor n;

//DECL(<variable type>,<variable name>,<PV name>) is supposed to push a local variable into a PV.
#define DECL(t,n,s)             \
        t n;                    \
        assign n to #s;

#define DECLARRAY(t,n,s,l)      \
        t n[l];                 \
        assign n to #s;

//Online_CS_StartStop and Online_CS_SaveData PVs are hosted by the Soft IOC
//running on the host Linux box that runs the EDM screens.
DECLMON	(short,	sendEnable,		Online_CS_SaveData)
DECLMON	(short,	AcqRun,			Online_CS_StartStop)		//Online_CS_StartStop is the master run/stop control from the GUI.


// DAQC{CRATE}:inLoop_Running is a semaphore sent to the outLoop machine to let it know that inLoop is running.
// The PV is defined in the daqCrate.template database loaded by each VME IOC.
DECLMON	(short,	Running,		DAQC{CRATE}:inLoop_Running)		//inLoop Handshake PV.

//These PVs are defined in the daqCrate.template database loaded by each VME IOC.
DECL	(float,	old_NumBuffers_Free,	DAQC{CRATE}_CV_BuffersAvail)
DECL	(float,	old_NumBuffers_Send,	DAQC{CRATE}_CV_NumSendBuffers)

/* Board data lost or discarded in MB */
/* Repurposed for error count reporting */
DECL	(float,	DataLost_Board0,	DAQC{CRATE}_CV_OutLoop0)
DECL	(float,	DataLost_Board1,	DAQC{CRATE}_CV_OutLoop1)
DECL	(float,	DataLost_Board2,	DAQC{CRATE}_CV_OutLoop2)
DECL	(float,	DataLost_Board3,	DAQC{CRATE}_CV_OutLoop3)
DECL	(float,	DataLost_Board4,	DAQC{CRATE}_CV_OutLoop4)
DECL	(float,	DataLost_Board5,	DAQC{CRATE}_CV_OutLoop5)
DECL	(float,	DataLost_Board6,	DAQC{CRATE}_CV_OutLoop6)

/* Board read rates in KB/s */
DECL	(float,	DataRate_Board0,	DAQC{CRATE}_OL_DataRate0)
DECL	(float,	DataRate_Board1,	DAQC{CRATE}_OL_DataRate1)
DECL	(float,	DataRate_Board2,	DAQC{CRATE}_OL_DataRate2)
DECL	(float,	DataRate_Board3,	DAQC{CRATE}_OL_DataRate3)
DECL	(float,	DataRate_Board4,	DAQC{CRATE}_OL_DataRate4)
DECL	(float,	DataRate_Board5,	DAQC{CRATE}_OL_DataRate5)
DECL	(float,	DataRate_Board6,	DAQC{CRATE}_OL_DataRate6)

/* Board total data in MB */
DECL	(float,	DataTotal_Board0,	DAQC{CRATE}_OL_Data0)
DECL	(float,	DataTotal_Board1,	DAQC{CRATE}_OL_Data1)
DECL	(float,	DataTotal_Board2,	DAQC{CRATE}_OL_Data2)
DECL	(float,	DataTotal_Board3,	DAQC{CRATE}_OL_Data3)
DECL	(float,	DataTotal_Board4,	DAQC{CRATE}_OL_Data4)
DECL	(float,	DataTotal_Board5,	DAQC{CRATE}_OL_Data5)
DECL	(float,	DataTotal_Board6,	DAQC{CRATE}_OL_Data6)

DECL	(float,	NumBuffers_Free,	DAQC{CRATE}_OL_NumFreeBuffers)	// Number of free buffers.
DECL	(float,	NumBuffers_Written,	DAQC{CRATE}_OL_NumWrittenBuffers)	// Number of written buffers
DECL	(float,	NumBuffers_Send,	DAQC{CRATE}_OL_NumSendBuffers)	// Number of send buffers.
DECL	(float,	TotalBuffers_Written,	DAQC{CRATE}_OL_TotalBufsWritten)	// Total Number of written buffers.
DECL	(float,	TotalFBuffers_Written,	DAQC{CRATE}_OL_TotalFBufsWritten)	// Total Number of written buffers.
DECL	(float,	TotalBuffers_Lost,	DAQC{CRATE}_OL_TotalBufsLost)	// Total Number of loast buffers.
DECL	(float,	LostBuffer_Percent,	DAQC{CRATE}_OL_BufLostPerecnt)	// Lost Buffer Precentage.

DECL	(float, SendDataRate,		DAQC{CRATE}_CV_SendRate)


DECLMON	(int,	traceBoard,		DAQC{CRATE}_CS_TraceBd)
DECLMON	(int,	traceChan,		DAQC{CRATE}_CS_TraceChan)
DECLMON	(int,	traceHorns,		DAQC{CRATE}_CS_TraceHorns)
DECL	(int,	traceLen,		DAQC{CRATE}_CV_TraceLen)
DECLARRAY(short,traceData,		DAQC{CRATE}_CV_Trace, 	1024) //MAX_TRACE_LENGTH


DECLMON	(unsigned short,outLoopHeaderCheckEnable,DAQC{CRATE}_OL_HeaderCheckEnable)	// Default: 1 (for now) // Switch to enable event header checking
DECLMON	(unsigned short,outLoopTimestampCheckEnable,DAQC{CRATE}_OL_TimestampCheckEnable)	// Default: 1 (for now) // Switch to enable timestamp checking
DECLMON	(unsigned short,outLoopDeepCheckEnable,DAQC{CRATE}_OL_DeepCheckEnable)	// Default: 1 (for now) // Switch to enable any other checking we may think up.
DECLMON	(unsigned short,outLoopHeaderSummaryEnable,DAQC{CRATE}_OL_HeaderSummaryEnable)	// Default: 0  (off) // Switch to enable allow periodic dumps of headers to console (could also define a value of 0 below to mean disabled.)
DECLMON	(unsigned int,outLoopHeaderSummaryPrescale,DAQC{CRATE}_OL_HeaderSummaryPrescale)	// Default: 0x1000 //
DECLMON	(unsigned int,outLoopHeaderSummaryEventPrescale,DAQC{CRATE}_OL_HeaderSummaryEventPrescale)	// Default: 0x100 //



int msgFilter;
unsigned short refreshTrace;
unsigned short refreshPvDataVars;
unsigned short pvRefreshReady;
unsigned short outLoopTraceMonToggle;

int send_bufs;
int written_bufs;

// holding variable for PV input
/* Board data lost or discarded in KB */
/* Repurposed for error count reporting */
unsigned int DataLost_Board0_Raw;
unsigned int DataLost_Board1_Raw;
unsigned int DataLost_Board2_Raw;
unsigned int DataLost_Board3_Raw;
unsigned int DataLost_Board4_Raw;
unsigned int DataLost_Board5_Raw;
unsigned int DataLost_Board6_Raw;

/* Board read rates in Bytes/s */
int DataRate_Board0_Raw;
int DataRate_Board1_Raw;
int DataRate_Board2_Raw;
int DataRate_Board3_Raw;
int DataRate_Board4_Raw;
int DataRate_Board5_Raw;
int DataRate_Board6_Raw;

/* Board total data in KB */
unsigned int DataTotal_Board0_Raw;
unsigned int DataTotal_Board1_Raw;
unsigned int DataTotal_Board2_Raw;
unsigned int DataTotal_Board3_Raw;
unsigned int DataTotal_Board4_Raw;
unsigned int DataTotal_Board5_Raw;
unsigned int DataTotal_Board6_Raw;


unsigned int TotalBuffers_Written_Raw;
unsigned int TotalFBuffers_Written_Raw;
unsigned int TotalBuffers_Lost_Raw;

/* Buffered send rate in Bytes/s */
int SendDataRate_Raw;

ss outLoop {

	//	INIT : where we begin.  Waits for PV "Online_CS_StartStop" (AcqRun) to be set to 1/true.
	state INIT {
		entry {
			taskPrioritySet(taskIdSelf(), 190);
			printf("outLoop: Idle.\n");
			printf("outLoop: Number of free buffers: %d\n", getFreeBufCount ());
			msgFilter = 0;
			pvRefreshReady = 0;
		}
		when (AcqRun) {
			 ResetStats();
		} state CHECK_FOR_DATA
		when (delay(0.5)) {
			if (refreshTrace) {
				traceLen = GetTrace(traceData, traceBoard, traceChan);
				pvPut(traceLen);
				pvPut(traceData);
				refreshTrace = 0;
			} 
			if (refreshPvDataVars) {
				//UpdateDataRates();
					
				if (traceBoard <= 3) {
					/* Repurposed for error count reporting */
					DataLost_Board0_Raw = GetErrorCount(0);
					DataLost_Board1_Raw = GetErrorCount(1);
					DataLost_Board2_Raw = GetErrorCount(2);
					DataLost_Board3_Raw = GetErrorCount(3);
					DataLost_Board4_Raw = GetErrorCount(4);
					DataLost_Board5_Raw = GetErrorCount(5);
					DataLost_Board6_Raw = GetErrorCount(6);
				}
				else {
					// for diagnostics
					DataLost_Board0_Raw = GetErrorData(traceBoard-4,0);
					DataLost_Board1_Raw = GetErrorData(traceBoard-4,1);
					DataLost_Board2_Raw = GetErrorData(traceBoard-4,2);
					DataLost_Board3_Raw = GetErrorData(traceBoard-4,3);
					DataLost_Board4_Raw = GetErrorData(traceBoard-4,4);
					DataLost_Board5_Raw = GetErrorData(traceBoard-4,5);
					DataLost_Board6_Raw = GetErrorData(traceBoard-4,6);
				}
				
				/* Board read rates in Bytes/s */
				DataRate_Board0_Raw = 0;
				DataRate_Board1_Raw = 0;
				DataRate_Board2_Raw = 0;
				DataRate_Board3_Raw = 0;
				DataRate_Board4_Raw = 0;
				DataRate_Board5_Raw = 0;
				DataRate_Board6_Raw = 0;
				
				/* Board total data in KB */
				DataTotal_Board0_Raw = GetDataTotal(0);
				DataTotal_Board1_Raw = GetDataTotal(1);
				DataTotal_Board2_Raw = GetDataTotal(2);
				DataTotal_Board3_Raw = GetDataTotal(3);
				DataTotal_Board4_Raw = GetDataTotal(4);
				DataTotal_Board5_Raw = GetDataTotal(5);
				DataTotal_Board6_Raw = GetDataTotal(6);
				
				TotalBuffers_Written_Raw = GetTotalBuffers_Written();
				TotalFBuffers_Written_Raw = GetTotalFBuffers_Written();
				TotalBuffers_Lost_Raw = GetTotalBuffers_Lost();
				
				SendDataRate_Raw = 0;
				
				refreshPvDataVars = 0;
				pvRefreshReady = 1;
			}
		} state INIT
		exit {
			if(outloop_debug_level >= 1) printf("outLoop: Running.\n");
			if(outloop_debug_level >= 2) printf("outLoop: Number of free buffers: %d\n", getFreeBufCount ());
		}
	} //end INIT

	state PROCESS_DATA {
		when() {
            //printf("####### outLoop:PROCESS_DATA, written_bufs:%d, send_bufs:%d, sendEnable:%d\n", written_bufs, send_bufs, sendEnable);
			CheckAndMoveBuffers(written_bufs, send_bufs, sendEnable);
			send_bufs += written_bufs;
			written_bufs = 0;
		} state CHECK_FOR_DATA
	} // end PROCESS_DATA


	state CHECK_FOR_DATA {
		option -e;
		entry {
            //printf("####### outLoop:CHECK_FOR_DATA\n");
			if (refreshTrace) {
				traceLen = GetTrace(traceData, traceBoard, traceChan);
				pvPut(traceLen);
				pvPut(traceData);
				refreshTrace = 0;
			} 
			if (refreshPvDataVars) {
				UpdateDataRates();
				

				if (traceBoard <= 3) {
					/* Board data lost or discarded in KB */
					/* Repurposed for error count reporting */
					DataLost_Board0_Raw = GetErrorCount(0);
					DataLost_Board1_Raw = GetErrorCount(1);
					DataLost_Board2_Raw = GetErrorCount(2);
					DataLost_Board3_Raw = GetErrorCount(3);
					DataLost_Board4_Raw = GetErrorCount(4);
					DataLost_Board5_Raw = GetErrorCount(5);
					DataLost_Board6_Raw = GetErrorCount(6);
				}
				else {
					// for diagnostics
					DataLost_Board0_Raw = GetErrorData(traceBoard-4,0);
					DataLost_Board1_Raw = GetErrorData(traceBoard-4,1);
					DataLost_Board2_Raw = GetErrorData(traceBoard-4,2);
					DataLost_Board3_Raw = GetErrorData(traceBoard-4,3);
					DataLost_Board4_Raw = GetErrorData(traceBoard-4,4);
					DataLost_Board5_Raw = GetErrorData(traceBoard-4,5);
					DataLost_Board6_Raw = GetErrorData(traceBoard-4,6);
				}
				
				/* Board read rates in Bytes/s */
				DataRate_Board0_Raw = GetDataRate(0);
				DataRate_Board1_Raw = GetDataRate(1);
				DataRate_Board2_Raw = GetDataRate(2);
				DataRate_Board3_Raw = GetDataRate(3);
				DataRate_Board4_Raw = GetDataRate(4);
				DataRate_Board5_Raw = GetDataRate(5);
				DataRate_Board6_Raw = GetDataRate(6);
				
				/* Board total data in KB */
				DataTotal_Board0_Raw = GetDataTotal(0);
				DataTotal_Board1_Raw = GetDataTotal(1);
				DataTotal_Board2_Raw = GetDataTotal(2);
				DataTotal_Board3_Raw = GetDataTotal(3);
				DataTotal_Board4_Raw = GetDataTotal(4);
				DataTotal_Board5_Raw = GetDataTotal(5);
				DataTotal_Board6_Raw = GetDataTotal(6);
				
				TotalBuffers_Written_Raw = GetTotalBuffers_Written();
				TotalFBuffers_Written_Raw = GetTotalFBuffers_Written();
				TotalBuffers_Lost_Raw = GetTotalBuffers_Lost();

				SendDataRate_Raw = GetSendDataRate();
				
				refreshPvDataVars = 0;
				pvRefreshReady = 1;
			}
		}
		
		when (!AcqRun && !Running) {
		} state CHECK_FOR_EMPTY_WRITTEN_Q

		when (written_bufs > 0) {
			//printf("outLoop:CHECK_FOR_DATA, written_buf : %d\n", written_bufs);
		} state PROCESS_DATA
		
		when(delay(0.05)) {
			// if the run has stopped but inloop is still running or visa-versa, report it.
			if (AcqRun != Running) {
				// but not too frequently
				if (msgFilter == 0) {
					msgFilter = 100;
					if (!AcqRun) {
						if(outloop_debug_level >= 2) printf("outLoop: Run stopped, inLoop is running...\n");
					}
					else {
						if(outloop_debug_level >= 2) printf("outLoop: Run started, inLoop not running...\n");
					}
				}
				msgFilter--;
			}
			// Update buffer counts
			written_bufs = getWrittenBufCount();
			send_bufs = getSenderBufCount();
		} state CHECK_FOR_DATA
	} // end CHECK_FOR_DATA


	state CHECK_FOR_EMPTY_WRITTEN_Q {
		entry {
			// Update buffer counts
			written_bufs = getWrittenBufCount();
			send_bufs = getSenderBufCount();
			printf("outLoop: Flushing buffers...\n");
			printf("outLoop: Number of written buffers: %d\n", getWrittenBufCount());
		}
		
		when (written_bufs > 0) {
		} state PROCESS_DATA
		
		when() {
		} state INIT
		
		exit {
			printf("outLoop: Done.\n");
		}
	} // end CHECK_FOR_EMPTY_WRITTEN_Q

}

/* state set newTrace just sets the flag periodically */
ss outLoopTraceMon {
	state run {
		entry {
			outLoopTraceMonToggle = 1;
		}
		when (delay(0.5)) {
			%{
			//every cycle copy the check control variables to global variables that 
			//outloopsupport can use
			OL_Hdr_Chk_En = outLoopHeaderCheckEnable;
			OL_TS_Chk_En = outLoopTimestampCheckEnable;
			OL_Deep_Chk_En = outLoopDeepCheckEnable;
			OL_Hdr_Summ_En = outLoopHeaderSummaryEnable;
			OL_Hdr_Summ_PS = outLoopHeaderSummaryPrescale;
			OL_Hdr_Summ_Evt_PS = outLoopHeaderSummaryEventPrescale;
			}%

			if (outLoopTraceMonToggle)
			{
				if (pvRefreshReady) {
					/* Convert from Bytes to MB */
					/* Repurposed for error count reporting */
					DataLost_Board0 = DataLost_Board0_Raw; // / 1024.0;
					DataLost_Board1 = DataLost_Board1_Raw; // / 1024.0;
					DataLost_Board2 = DataLost_Board2_Raw; // / 1024.0;
					DataLost_Board3 = DataLost_Board3_Raw; // / 1024.0;
					DataLost_Board4 = DataLost_Board4_Raw; // / 1024.0;
					DataLost_Board5 = DataLost_Board5_Raw; // / 1024.0;
					DataLost_Board6 = DataLost_Board6_Raw; // / 1024.0;
					
					/* Convert from Bytes/s to KB/s */
					DataRate_Board0 = DataRate_Board0_Raw / 1000.0;
					DataRate_Board1 = DataRate_Board1_Raw / 1000.0;
					DataRate_Board2 = DataRate_Board2_Raw / 1000.0;
					DataRate_Board3 = DataRate_Board3_Raw / 1000.0;
					DataRate_Board4 = DataRate_Board4_Raw / 1000.0;
					DataRate_Board5 = DataRate_Board5_Raw / 1000.0;
					DataRate_Board6 = DataRate_Board6_Raw / 1000.0;
					
					/* Convert from KB to MB */
					DataTotal_Board0 = DataTotal_Board0_Raw / 1024.0;
					DataTotal_Board1 = DataTotal_Board1_Raw / 1024.0;
					DataTotal_Board2 = DataTotal_Board2_Raw / 1024.0;
					DataTotal_Board3 = DataTotal_Board3_Raw / 1024.0;
					DataTotal_Board4 = DataTotal_Board4_Raw / 1024.0;
					DataTotal_Board5 = DataTotal_Board5_Raw / 1024.0;
					DataTotal_Board6 = DataTotal_Board6_Raw / 1024.0;
					
					/* No conversion required here (asides to being a float) */
					TotalBuffers_Written = TotalBuffers_Written_Raw;
					TotalFBuffers_Written = TotalFBuffers_Written_Raw;
					TotalBuffers_Lost = TotalBuffers_Lost_Raw;
					LostBuffer_Percent = (TotalBuffers_Lost / TotalBuffers_Written) * (100.0);			

					/* Convert from Bytes/s to KB/s */
					SendDataRate = SendDataRate_Raw / 1000.0;			
	
					/* Board data lost or discarded in MB */
					/* Repurposed for error count reporting */
					pvPut(DataLost_Board0);
					pvPut(DataLost_Board1);
					pvPut(DataLost_Board2);
					pvPut(DataLost_Board3);
					pvPut(DataLost_Board4);
					pvPut(DataLost_Board5);
					pvPut(DataLost_Board6);
	
					/* Board read rates in KB/s */
					pvPut(DataRate_Board0);
					pvPut(DataRate_Board1);
					pvPut(DataRate_Board2);
					pvPut(DataRate_Board3);	
					pvPut(DataRate_Board4);
					pvPut(DataRate_Board5);
					pvPut(DataRate_Board6);	
	
					/* Board total data in MB */
					pvPut(DataTotal_Board0);
					pvPut(DataTotal_Board1);
					pvPut(DataTotal_Board2);
					pvPut(DataTotal_Board3);
					pvPut(DataTotal_Board4);
					pvPut(DataTotal_Board5);
					pvPut(DataTotal_Board6);
	
					pvPut(TotalBuffers_Written);
					pvPut(TotalFBuffers_Written);
					pvPut(TotalBuffers_Lost);
					pvPut(LostBuffer_Percent);

					/* Board (buffered) send rates in KB/s */	
					pvPut(SendDataRate);

					pvRefreshReady = 0;
				}
				refreshPvDataVars = 1;
			}
			NumBuffers_Free = getFreeBufCount();
			NumBuffers_Written = getWrittenBufCount();
			NumBuffers_Send = getSenderBufCount();
			pvPut(NumBuffers_Free);
			pvPut(NumBuffers_Written);
			pvPut(NumBuffers_Send);

			// For compatibility 
			old_NumBuffers_Free = NumBuffers_Free;
			old_NumBuffers_Send = NumBuffers_Send;
			pvPut(old_NumBuffers_Free);
			pvPut(old_NumBuffers_Send);
			refreshTrace = 1;
			//outLoopTraceMonToggle = !outLoopTraceMonToggle; //uncomment to cut update rate down to 1Hz.
		} state run
	}
}
