//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		outLoopSupport.c
// Description: Data processing functions.
// ToDo: 		Clean up pass...
//--------------------------------------------------------------------------------
#include <vxWorks.h>
#include <stdio.h>
#include <string.h>
#include <timers.h>
#include <taskHookLib.h>
#include <epicsMutex.h>
#include "DGS_DEFS.h"


#include "devGVME.h"
#include <QueueManagement.h>
#include "profile.h"

#include <outLoopSupport.h>

#define print_status_prescale		0x101	// one in this many buffers will print out partial header information.
#define print_buffer_headers_prescale	0x4001	// one in this many buffers will print out partial header information.
#define print_header_prescale 		0x101	// when printing partial header data, only output 1 in 'print_header_prescale' events.
unsigned int total_buffer_count = 0;
unsigned int print_status_prescaler_count = print_status_prescale;
unsigned int print_buffer_headers_prescaler_count = print_buffer_headers_prescale;

//#define STD_PRINTF_ERROR_HEADER_DUMP "ERROR EVNTHDR:%03X|%01X|%03X|%03X|%02X|%012llX|%01X|%01X|%02X\n", buffer_event_index, ch_id, user_def, packet_length, geo_addr, complete_timestamp, header_type, event_type, header_length
#define STD_PRINTF_ERROR_HEADER_DUMP "ERROR EVNTHDR:%03X|%01X|%03X|%03X|%02X|%08X|%01X|%01X|%02X\n", buffer_event_index, ch_id, user_def, packet_length, geo_addr, timestamp_lower, header_type, event_type, header_length

//#define STD_PRINTF_HEADER_DUMP "EVNTHDR:%03X|%01X|%03X|%03X|%02X|%012llX|%01X|%01X|%02X\n", buffer_event_index, ch_id, user_def, packet_length, geo_addr, complete_timestamp, header_type, event_type, header_length
#define STD_PRINTF_HEADER_DUMP "EVNTHDR:%03X|%01X|%03X|%03X|%02X|%08X|%01X|%01X|%02X\n", buffer_event_index, ch_id, user_def, packet_length, geo_addr, timestamp_lower, header_type, event_type, header_length

unsigned int last_timestamp[GVME_MAX_CARDS][16];  // second index is for each channel

#ifdef HISTO_ENABLE
	short NoiseHisto[GVME_MAX_CARDS][NUM_DIG_CHANNELS][2][2*MAX_HISTO_DELTA]; //70kB, trace PV is defined as a short
	unsigned short HistoEnable[GVME_MAX_CARDS][NUM_DIG_CHANNELS];
#endif

#ifdef TRACE_ENABLE
	short LookForChannel[GVME_MAX_CARDS];
//	short TraceSearchEnable;
	short ChannelTrace[GVME_MAX_CARDS][NUM_DIG_CHANNELS][MAX_TRACE_LENGTH]; //70kB, trace PV is defined as a short
	short StrippedChannelTrace[MAX_TRACE_LENGTH]; 
	short TraceLength[GVME_MAX_CARDS][NUM_DIG_CHANNELS];
#endif

// For statistics
/* Board data lost or discarded in Bytes */
unsigned long long int	DataLost[GVME_MAX_CARDS];
/* Board read rates in Bytes/s */
float DataRate[GVME_MAX_CARDS];
/* Board total data in Bytes */
unsigned long long int	DataTotal[GVME_MAX_CARDS];

unsigned int	TotalBuffers_Written;
unsigned int	TotalBuffers_Lost;

unsigned int	TotalFBuffers_Written;

unsigned int	TotalErrors[GVME_MAX_CARDS];
unsigned int	ErrorData[GVME_MAX_CARDS][4];  // second index is for each error type.
//char		FirstErrorBuffer[GVME_MAX_CARDS][RAW_BUF_SIZE];


/* Buffered send rates in Bytes/s */
float SendDataRate;

unsigned int BoardDataThisCycle[GVME_MAX_CARDS];
unsigned int TotalSendDataThisCycle;

struct timespec CurrentTimeSpec;  
float LastTime;
float CurrentTime;
float DeltaTime;

void ResetStats(void)
{
	static int run_once = 0;
	int i,j;
	#if defined(HISTO_ENABLE) || defined (TRACE_ENABLE)
		int k;
	#endif
	CurrentTimeSpec.tv_sec = 0;
	CurrentTimeSpec.tv_nsec = 0;
	clock_settime(CLOCK_REALTIME, &CurrentTimeSpec);
	clock_gettime(CLOCK_REALTIME, &CurrentTimeSpec);
	CurrentTime = (float)(CurrentTimeSpec.tv_sec) + (double)(CurrentTimeSpec.tv_nsec) / 1000000000.0;

	LastTime = CurrentTime;
	DeltaTime = 0;

	SendDataRate = 0.0;
	TotalSendDataThisCycle = 0;

	TotalBuffers_Written = 0;
	TotalBuffers_Lost = 0;
	
	TotalFBuffers_Written = 0;	

	for(i=0;i<GVME_MAX_CARDS;i++) {
		for(j=0;j<16;j++)
			last_timestamp[i][j] = 0;
		
		#ifdef HISTO_ENABLE
			for(j=0;j<NUM_DIG_CHANNELS;j++){
				for(k=0;k<2*MAX_HISTO_DELTA;k++)
				{
					NoiseHisto[i][j][0][k] = 0x8000;
					NoiseHisto[i][j][1][k] = 0x8000;
				}
				HistoEnable[i][j] = 1;
			}
		#endif
		#ifdef TRACE_ENABLE
			LookForChannel[i] = 0x03FF;
			for(j=0;j<NUM_DIG_CHANNELS;j++) {
				TraceLength[i][j] = 0;
				for(k=0;k<MAX_TRACE_LENGTH;k++)
					ChannelTrace[i][j][k] = 0x8000;
			}
		#endif
				
		DataLost[i] = 0;
		DataRate[i] = 0.0;
		DataTotal[i] = 0;
		BoardDataThisCycle[i] = 0;
		TotalErrors[i] = 0;
		ErrorData[i][0] = 0;
		ErrorData[i][1] = 0;
		ErrorData[i][2] = 0;
		ErrorData[i][3] = 0;
//		for(j=0;j<RAW_BUF_SIZE;j++)
//			{
//			FirstErrorBuffer[0][j] = 0;
//			FirstErrorBuffer[1][j] = 0;
//			FirstErrorBuffer[2][j] = 0;
//			FirstErrorBuffer[3][j] = 0;
//			}
	}
		

	total_buffer_count = 0;
	print_status_prescaler_count = print_status_prescale;
	print_buffer_headers_prescaler_count = print_buffer_headers_prescale;


	init_profile_counters(PROFILE_TICK_FREQUENCY);
	if (!run_once) {
		i = taskSwitchHookAdd((FUNCPTR)(profile_counter_task_switch_hook));
		if (i == ERROR)
			printf("Could not add profiler task switch hook.");
		else
			run_once = 1;
	}
	init_profile_counter(PROF_OL_CHECK_AND_MOVE_BUFFERS, "outLoop - CheckAndMoveBuffers", 1);
	init_profile_counter(PROF_OL_GET_TRACE, "outLoop - GetTrace", 1);
	init_profile_counter(PROF_IL_CHECK_AND_READ_DIG, "inLoop - CheckAndReadDigitizer", 1);
	init_profile_counter(PROF_IL_XFER_DIG_FIFO_DATA, "inLoop - transferDigFifoData", 1);
	init_profile_counter(PROF_MS_GET_RECEIVER_REQUEST, "MiniSender - getReceiverRequest", 1);
	init_profile_counter(PROF_MS_SEND_SERVER_RESPONCE, "MiniSender - sendServerResponse", 1);
	init_profile_counter(PROF_MS_SEND_DATA_BUFFER, "MiniSender - sendDataBuffer", 1);
	run_profile_counters();
}

int GetTrace(short* trace, int board, int channel)
{
int jta;		//counter used only in this routine
int ModLength;
	start_profile_counter(PROF_OL_GET_TRACE);
	#ifdef HISTO_ENABLE
		static unsigned short trace_toggle = 0;
	#endif

	if ((board >= GVME_MAX_CARDS) || (board < 0) || (channel >= NUM_DIG_CHANNELS) || (channel < 0) || (trace == 0))
	{
		stop_profile_counter(PROF_OL_GET_TRACE);
		return 0;
	}
	#ifdef HISTO_ENABLE
		memcpy(trace, NoiseHisto[board][channel][trace_toggle], 2*MAX_HISTO_DELTA*sizeof(short));
		trace_toggle = !trace_toggle;
		stop_profile_counter(PROF_OL_GET_TRACE);
		return 2*MAX_HISTO_DELTA;
	#else
		#ifdef TRACE_ENABLE
			//strip 'horns' (timing/downsampling marks)
			//excess/unused variable StrippedChannelTrace not used, go directly to trace to try to save a little CPU.
			ModLength = TraceLength[board][channel] * 2;
			for(jta=0;jta<ModLength;jta++)
				trace[jta] = ChannelTrace[board][channel][jta] & 0x3FFF;

//			memcpy(trace, ChannelTrace[board][channel], TraceLength[board][channel] * 2);
//			memcpy(trace, StrippedChannelTrace, TraceLength[board][channel] * 2);
			stop_profile_counter(PROF_OL_GET_TRACE);
			return TraceLength[board][channel];
		#else
			stop_profile_counter(PROF_OL_GET_TRACE);
			return 0;
		#endif
	#endif
}

void CheckAndMoveBuffers(int written_bufs, int send_bufs, short sendEnable)
{
	rawEvt *rawBuf = NULL;
	static unsigned short emergency_data_dump;
	unsigned int* raw_header = 0;
	unsigned int print_header_prescaler_count = print_header_prescale;
	unsigned int offset = 0;
	static unsigned int board_num = 0;
	unsigned int local_buffer_count = 0;
	unsigned int buffer_event_index = 0;
	int fatal_buffer_error = 0;		
	#ifdef HISTO_ENABLE
	unsigned int buffer_histo_event_count = 0;
	static short delta[2] = {0,0};
	#endif
	static unsigned int ch_id = 0;
	static unsigned int user_def = 0;
	static unsigned int packet_length = 0;
	static unsigned int geo_addr = 0;
	static unsigned int header_type = 0;
	static unsigned int event_type = 0;
	static unsigned int header_length = 0;
	static unsigned int timestamp_lower = 0;
	static unsigned int timestamp_upper = 0;
//	static unsigned long long complete_timestamp = 0;
	static unsigned int timestamp_check = 0;			//bits 47:16 of timestamp, used to compare against last_timestamp.
	static int BufLengthInLongwords = 0;
	int stat = 0;
	unsigned int *dptr = NULL;
	int ErrorReportLimit = 0;
	
#ifdef OUTLOOP_NUM_ERR_PER_BOARD
	ErrorReportLimit = OUTLOOP_NUM_ERR_PER_BOARD;
#else
	ErrorReportLimit = 1;
#endif
	
	start_profile_counter(PROF_OL_CHECK_AND_MOVE_BUFFERS);

	TotalBuffers_Written += written_bufs;
	emergency_data_dump = 0;
	if (sendEnable && ((send_bufs+written_bufs) > SENDER_BUF_BYPASS_THRESHOLD))
		{
		TotalBuffers_Lost += written_bufs;
//		if(outloop_debug_level >= 0) printf("EVENT_CHECK_ERROR: ALERT! Send buffer limit reached! Data buffers lost: %d (%d.%02dpct.)\n", TotalBuffers_Lost, (TotalBuffers_Lost*100)/TotalBuffers_Written, (((TotalBuffers_Lost*10000)/TotalBuffers_Written)%100));
// TODO: Generate Error message by prescale it's output frequency.
//		if(outloop_debug_level >= 2) printf("EVENT_CHECK_ERROR: ALERT! Send buffer limit reached! Data buffers lost: %d (%d.%02dpct.)\n", TotalBuffers_Lost, (TotalBuffers_Lost*100)/TotalBuffers_Written, (((TotalBuffers_Lost*10000)/TotalBuffers_Written)%100));
		sendEnable = 0;	// override send_enable
		emergency_data_dump = 1;
		} // end if (sendEnable && ((send_bufs+written_bufs) > SENDER_BUF_BYPASS_THRESHOLD))

//	if ((DeltaTime < 10)  && ((float)(written_bufs) / DeltaTime > BUF_RATE_WARNING_THRESHOLD))
//	{
//		// The timer need to be restarted every minute which is nominally indicated by a DeltaTime of more than 1e6. 
//		if(outloop_debug_level >= 1) printf("\nEVENT_CHECK_WARNING: High buffer rate. %.1f buffers per second.\n", (float)(written_bufs) / DeltaTime);
//	}

#ifdef TRACE_ENABLE
	for(board_num=0;board_num<GVME_MAX_CARDS;board_num++) 
		{
		LookForChannel[board_num] = 0x03FF;
		} //end for(board_num=0;board_num<GVME_MAX_CARDS;board_num++) 
#endif
//20230330 : you now have global variables available for if statements, driven by PVs.
//unsigned short OL_Hdr_Chk_En;
//unsigned short OL_TS_Chk_En;
//unsigned short OL_Deep_Chk_En;
//unsigned short OL_Hdr_Summ_En;
//unsigned int OL_Hdr_Summ_PS;
//unsigned int OL_Hdr_Summ_Evt_PS;		
//----------------------------------
//	Main processing loop start.
//----------------------------------
	while (local_buffer_count < written_bufs) 
		{
		total_buffer_count++;
		local_buffer_count++;
//		#ifdef TRACE_ENABLE
//			TraceSearchEnable = 1;
//		#endif
		#ifdef HISTO_ENABLE
			buffer_histo_event_count = 0;
		#endif
		stat = getWrittenBuf(&rawBuf);		//pull next available buffer
		//if no buffer available, issue error message and exit.
		if (stat != Success) 
			{
			if(outloop_debug_level >= 0) printf("ECE[X,XXXX,0xXXXXX]: Cant get a buffer.\n");
			stop_profile_counter(PROF_OL_CHECK_AND_MOVE_BUFFERS);
			return;
			} //end if (stat != Success)
		//otherwise, start processing the buffer.
		//first thing we do is check for obvious errors.

		//Error check #1: check that board number is not corrupt
		board_num = rawBuf->board;		
		if ((board_num >= GVME_MAX_CARDS) || (board_num < 0)) 
			{
			if(outloop_debug_level >= 0) printf("ECE[%d,XXXX,0xXXXXX]: Board number out of range.\n", board_num);
			//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
			fatal_buffer_error = 1;
			goto MOVE_BUFFER_AFTER_CHECK;
			} //end if ((board_num >= GVME_MAX_CARDS) || (board_num < 0)) 
		//JTA: 20230926: addition of goto obviates need for else case for if ((board_num >= GVME_MAX_CARDS) || (board_num < 0)) 
			
		//Error check #2: check that length of buffer is not zero
		if (rawBuf->len == 0)  
			{
			TotalErrors[board_num]++;
			if (ErrorData[board_num][0] == 0) 
				{
				ErrorData[board_num][0] = 1;
				ErrorData[board_num][1] = 0;
				ErrorData[board_num][2] = 0;
				ErrorData[board_num][3] = 0;
				} //end if (ErrorData[board_num][0] == 0) 
			if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale) 
				{
				if(outloop_debug_level >= 2) printf("\nEVNTHDR:buffer_event_index, ch_id, user_def, packet_length, geo_addr, timestamp_upper, header_type, event_type, header_length\n");
				//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
				fatal_buffer_error = 1;
				goto MOVE_BUFFER_AFTER_CHECK;
				} //end if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale) 
			} // end if (rawBuf->len == 0)  
		//------------------------------------------------------------------------------------------	
		//with obvious errors out of the way, assume buffer is usable, and begin real processing.
		//Still within the big while (local_buffer_count < written_bufs) loop here.
		//------------------------------------------------------------------------------------------	

		print_header_prescaler_count = print_header_prescale;
	
		//per comment in readDigFIFO.c, rawBuf->len is the length of the buffer, IN BYTES, because that's the argument size
		//the DMA transfer functions want.  But all digitizer calculations, AND rawBuf->data, is in LONGWORDS.
		offset = 0;		//offset into data buffer, in LONGWORDS (usable as rawBuf->data[offset])
		dptr = rawBuf->data;	//dptr starts at the start of the data buffer.
		buffer_event_index = 0;
		BufLengthInLongwords = rawBuf->len / DIG_WORD_SIZE;	//DIG_WORD_SIZE is the constant 4, as a digitizer word is four bytes long.
	
		//if there were too many buffers in the send queue, emergency_data_dump would be set.  that's a simple error condition to test,
		//so is less complicated to test for it set and exit than wrap a lot of code in an if() on it not set.
		if (emergency_data_dump == 1) 
			{
			//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
			fatal_buffer_error = 1;
			goto MOVE_BUFFER_AFTER_CHECK;
			} //end if (emergency_data_dump == 1) 

		while (offset < BufLengthInLongwords) 
			{
			#ifdef disable_all_checking
				break;
			#endif
	
			if ((board_num < 0) || (board_num >= GVME_MAX_CARDS))	// No error msg here as the QueueManagement will detect this condition.
				break;
			if (buffer_event_index >= MAX_EVENTS_TO_CHECK_PER_BUFFER)	// Limit the time spent checking buffers.
				break;
			//JTA: 20220801: 	Since only digitizers have data with the 0xAAAAAAAA header, there must be an if() or switch() here to 
			//					only perform this check on data from digitizers.
			switch(rawBuf->board_type) 
				{
				case BrdType_ANL_MDIG:
				case BrdType_ANL_SDIG:
				case BrdType_MAJORANA_MDIG:
				case BrdType_MAJORANA_SDIG:
				case BrdType_LBNL_DIG:
					//check for the 0xAAAAAAAA header.
					if (*dptr != 0xAAAAAAAA) 
						{
						TotalErrors[board_num]++;
						if (ErrorData[board_num][0] == 0) 
							{
							ErrorData[board_num][0] = 2;
							ErrorData[board_num][1] = offset;
							ErrorData[board_num][2] = BufLengthInLongwords;
							ErrorData[board_num][3] = *dptr;
							DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
							DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);
							}	//end of if (ErrorData[board_num][0] == 0) 
						
						if (TotalErrors[board_num] <= ErrorReportLimit)
							{
							if(outloop_debug_level >= 0) printf("OutLoopSupport: CheckAndMoveBuffers: bnum:%d btyp:%d evtidx:%04d offset:0x%05X Alignment error. BLEN:0x%05X DATA:0x%08X\n", 
											board_num, rawBuf->board_type, buffer_event_index, offset, BufLengthInLongwords, *dptr);
							} //end of if (TotalErrors[board_num] <= ErrorReportLimit)
							
		#ifdef OUTLOOP_TRY_REALIGNMENT
						if(outloop_debug_level >= 0) printf("OutLoopSupport: CheckAndMoveBuffers:  ECE[%d,%04d,0x%05X]: Attempting realignment...\n", board_num, buffer_event_index, offset);
					
						//realignment consists of sucking data, until either end of buffer or 0xAAAAAAAA is found.
						while ((*dptr != 0xAAAAAAAA) && (offset < BufLengthInLongwords)) 
							{
							offset ++;
							dptr ++;
							}	//end while ((*dptr != 0xAAAAAAAA) && (offset < BufLengthInLongwords))  
							
						//at exit here, either we have a newly found 0xAAAAAAAA or we've reached the end of the road.
						if (offset >= BufLengthInLongwords) 
							{	//end of the road
							if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Alignment failed.\n", board_num, buffer_event_index, offset);
							//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
							fatal_buffer_error = 1;
							goto MOVE_BUFFER_AFTER_CHECK;
							}  //end if (offset >= BufLengthInLongwords)  
	
						//Alignment is successful, continue checking.
						if(outloop_debug_level >= 0) printf("OutLoopSupport: CheckAndMoveBuffers:  ECE[%d,%04d,0x%05X]: Found Next Header.\n", board_num, buffer_event_index, offset);
		#else
						//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
						fatal_buffer_error = 1;
						goto MOVE_BUFFER_AFTER_CHECK;
		#endif
						}	//end of if (*dptr != 0xAAAAAAAA) ;still within switch(rawBuf->board_type); within while (offset < BufLengthInLongwords)

					//so no matter what at this point we must be at 0xAAAAAAAA.
					//dptr is pointing to the 0xAAAAAAAA.  offset is pointing to the 0xAAAAAAAA.
		
					if ((offset + MIN_HEADER_LENGTH) > BufLengthInLongwords) 
						{   // 16 bytes is the minimum header length supported by this routine.
						TotalErrors[board_num]++;
						if (ErrorData[board_num][0] == 0) 
							{
							ErrorData[board_num][0] = 3;
							ErrorData[board_num][1] = offset;
							ErrorData[board_num][2] = BufLengthInLongwords;
							ErrorData[board_num][3] = *dptr;
							DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
							DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);	
							} // end if (ErrorData[board_num][0] == 0)
						if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Header cutoff. BLEN:0x%05X\n", board_num, buffer_event_index, offset, BufLengthInLongwords);
						//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
						fatal_buffer_error = 1;
						goto MOVE_BUFFER_AFTER_CHECK;
						}	//end of if ((offset + MIN_HEADER_LENGTH) > BufLengthInLongwords)

//Notice : still within switch(rawBuf->board_type) that is within while (offset < BufLengthInLongwords), that started about 100 lines back from here.
					
					//-------------------------------
					//At this point all error checks are complete, so peform decoding of the header.
					//-------------------------------
					raw_header = dptr;	//set raw_header to the beginning of each digitizer event, per loop.  dptr updates as each event is processed to beginning of next 	event.
			
					//************ strip out header bits **************/
					//digitizer format
					//word	|31|30|29|28|27|26|25|24|23|22|21|20|19|18|17|16|15|14|13|12|11|10|09|08|07|06|05|04|03|02|01|00
					//0		|                                     FIXED 0xAAAAAAAA                                          |
					//1		|Geo Addr      |   PACKET LENGTH             |    USER PACKET DATA                  |CHANNEL ID |
					//2		|                          LEADING EDGE DISCRIMINATOR TIMESTAMP[31:0]                           |
					//3		| HEADER LENGTH   | EV TYP |00|TS|IN|  HDR TYP  | LEADING EDGE DISCRIMINATOR TIMESTAMP[47:32]   |
					ch_id			= (raw_header[1] & 0x0000000F) >> 0;					// Word 1: 3..0
					user_def 		= (raw_header[1] & 0x0000FFF0) >> 4;					// Word 1: 15..4
					packet_length	= (raw_header[1] & 0x07FF0000) >> 16;					// Word 1: 26..16
					geo_addr		= (raw_header[1] & 0xF8000000) >> 27;					// Word 1: 31..27
					timestamp_lower = (raw_header[2] & 0xFFFFFFFF) >> 0;					// Word 2: 31..0
					timestamp_upper = (raw_header[3] & 0x0000FFFF) >> 0;					// Word 3: 15..0
					header_type		= (raw_header[3] & 0x000F0000) >> 16;					// Word 3: 19..16
					event_type		= (raw_header[3] & 0x03800000) >> 23;					// Word 3: 25..23
					header_length	= (raw_header[3] & 0xFC000000) >> 26;					// Word 3: 31..26

					offset += packet_length + 1;		// +1 to account for the packet length not counting the 0xAAAAAAAA word.
		
					//--------------------------------------------------
					// Check validity of the data within the header				
					//--------------------------------------------------

					//Check #1: verify that packet length as reported in header does not go past the end of the buffer.
					if (offset > BufLengthInLongwords) 
						{
						TotalErrors[board_num]++;
						if (ErrorData[board_num][0] == 0) 
							{
							ErrorData[board_num][0] = 4;
							ErrorData[board_num][1] = offset;
							ErrorData[board_num][2] = BufLengthInLongwords;
							ErrorData[board_num][3] = packet_length;
							DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
							DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);	
							} // end if (ErrorData[board_num][0] == 0) 
						if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Event cutoff. BLEN:0x%05X\n", board_num, buffer_event_index, offset, BufLengthInLongwords);
						// You may not get a column header line for this.
						if(outloop_debug_level >= 0) printf(STD_PRINTF_ERROR_HEADER_DUMP);
						//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
						fatal_buffer_error = 1;
						goto MOVE_BUFFER_AFTER_CHECK;
						} //end if (offset > BufLengthInLongwords) 
					
					//Check #2: verify that the reported packet length is not larger than the maximum packet length.
					if (packet_length > MAX_PACKET_LENGTH) 
						{
						TotalErrors[board_num]++;
						if (ErrorData[board_num][0] == 0) 
							{
							ErrorData[board_num][0] = 5;
							ErrorData[board_num][1] = offset;
							ErrorData[board_num][2] = BufLengthInLongwords;
							ErrorData[board_num][3] = packet_length;
							DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
							DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);
							} // end if (ErrorData[board_num][0] == 0) 
						if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Event length error. ELEN:0x%03X\n", board_num, buffer_event_index, offset, packet_length);
						// You may not get a column header line for this.
						if(outloop_debug_level >= 0) printf(STD_PRINTF_ERROR_HEADER_DUMP);
						//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
						fatal_buffer_error = 1;
						goto MOVE_BUFFER_AFTER_CHECK;
						} //end if (packet_length > MAX_PACKET_LENGTH) 
					
					//Check #3: Check to see if this is a type F header, and if so, whether it is malformed.
					if (header_type	== 0xF) 
						{
						TotalFBuffers_Written++;
						// check channel number reported in header.  0xD, 0xE and 0xF are ok, other values are not.
						if ((ch_id != 0xD) && (ch_id != 0xE) && (ch_id != 0xF)) 
							{
							TotalErrors[board_num]++;
							if (ErrorData[board_num][0] == 0) 
								{
								ErrorData[board_num][0] = 6;
								ErrorData[board_num][1] = offset;
								ErrorData[board_num][2] = ch_id;
								ErrorData[board_num][3] = packet_length;
								DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
								DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);	
								} // end if (ErrorData[board_num][0] == 0) 
							if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Type F Channel ID out of range. CHID:%X\n", board_num, buffer_event_index, offset, ch_id);
							// You may not get a column header line for this.
							if(outloop_debug_level >= 0) printf(STD_PRINTF_ERROR_HEADER_DUMP);
							//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
							fatal_buffer_error = 1;
							goto MOVE_BUFFER_AFTER_CHECK;
							} //end if ((ch_id != 0xD) && (ch_id != 0xE) && (ch_id != 0xF)) 
						} //end main clause of if (header_type	== 0xF) 
					else 	//the 'else' here means it is NOT a type F, so is presumably normal data.
						{
						//Check #4: If not type F header, then check channel number of the header.
						// but if header is not type F, then channel # must be 0 to 9.
						if (ch_id > 9) 
							{
							TotalErrors[board_num]++;
							if (ErrorData[board_num][0] == 0) 
								{
								ErrorData[board_num][0] = 7;
								ErrorData[board_num][1] = offset;
								ErrorData[board_num][2] = ch_id;
								ErrorData[board_num][3] = header_type;
								DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
								DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);
								}// end if (ErrorData[board_num][0] == 0) 
							if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Channel ID out of range. CHID:%X\n", board_num, buffer_event_index, offset, ch_id);
							// You may not get a column header line for this.
							if(outloop_debug_level >= 0) printf(STD_PRINTF_ERROR_HEADER_DUMP);
							//JTA: 20230926: for any fatal buffer error, set the flag and jump to the end of the routine, so that we still count statistics.
							fatal_buffer_error = 1;
							goto MOVE_BUFFER_AFTER_CHECK;
							} // end if (ch_id > 9) 

//Notice : still within switch(rawBuf->board_type) that is within while (offset < BufLengthInLongwords), that started about 200 lines back from here.

							//still within the else case of if (header_type	== 0xF)....
							//full 48-bit timestamp stored in 64-bit unsigned int.
							//complete_timestamp = ((unsigned long long)(timestamp_upper)) << 32;
							//complete_timestamp |= (unsigned long long)(timestamp_lower);
				
							// timestamp check
							timestamp_check = (timestamp_upper << 16) | (timestamp_lower >> 16);		//bits 47:16 as 32-bit unsigned int
							if (last_timestamp[board_num][ch_id] > timestamp_check) 
								{
								TotalErrors[board_num]++;
								if (ErrorData[board_num][0] == 0) 
									{
									ErrorData[board_num][0] = 8;
									ErrorData[board_num][1] = offset;
									ErrorData[board_num][2] = last_timestamp[board_num][ch_id];
									ErrorData[board_num][3] = timestamp_check;
									DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
									DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);
									} //end if (ErrorData[board_num][0] == 0)
								if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Timestamp out of order. CHID:%X LAST:%08X CUR:%08X \n", board_num, buffer_event_index, offset, ch_id, last_timestamp[board_num][ch_id], timestamp_check);
								// You may not get a column header line for this.
								if(outloop_debug_level >= 0) printf(STD_PRINTF_ERROR_HEADER_DUMP);
								break;
								} //end if (last_timestamp[board_num][ch_id] > timestamp_check) 
								
							last_timestamp[board_num][ch_id] = timestamp_upper;
							
		#ifdef TRACE_ENABLE
							//	if (TraceSearchEnable) {
							if ((LookForChannel[board_num] & (1 << ch_id)) != 0) 
								{
								LookForChannel[board_num] = LookForChannel[board_num] & (~(1 << ch_id));
					//			if (LookForChannel[board_num] == 0)
					//				TraceSearchEnable = 0;		// Only snag one trace (at most) per buffer to limit processing time.
								TraceLength[board_num][ch_id] = (packet_length-14)*2;
								memcpy(ChannelTrace[board_num][ch_id], dptr+15, TraceLength[board_num][ch_id]*2);
								} // end if ((LookForChannel[board_num] & (1 << ch_id)) != 0)
					//	}
		#endif
								
		#ifdef HISTO_ENABLE
							if (buffer_histo_event_count < MAX_EVENTS_TO_HISTO_PER_BUFFER) 
								{
								if (HistoEnable[board_num][ch_id]) 
									{
									buffer_histo_event_count++;
									delta[0] = (signed short)(raw_header[15] & 0x3FFF) - (signed short)((raw_header[15] >> 16) & 0x3FFF);
									if ((delta[0] < MAX_HISTO_DELTA) && (delta[0] > -MAX_HISTO_DELTA)) 
										{
										NoiseHisto[board_num][ch_id][0][delta[0]+MAX_HISTO_DELTA]++;
										if (NoiseHisto[board_num][ch_id][0][delta[0]+MAX_HISTO_DELTA] == 0x7FFF)
											HistoEnable[board_num][ch_id] = 0;
										} // end if ((delta[0] < MAX_HISTO_DELTA) && (delta[0] > -MAX_HISTO_DELTA))
									delta[1] = (signed short)(raw_header[15] & 0x3FFF) - (signed short)((raw_header[16] >> 16) & 0x3FFF);
									if ((delta[1] < MAX_HISTO_DELTA) && (delta[1] > -MAX_HISTO_DELTA)) 
										{
										NoiseHisto[board_num][ch_id][1][delta[1]+MAX_HISTO_DELTA]++;
										if (NoiseHisto[board_num][ch_id][1][delta[1]+MAX_HISTO_DELTA] == 0x7FFF)
											HistoEnable[board_num][ch_id] = 0;
										} // end if ((delta[1] < MAX_HISTO_DELTA) && (delta[1] > -MAX_HISTO_DELTA)) 
									} // end if (HistoEnable[board_num][ch_id]) 
								} // end if (buffer_histo_event_count < MAX_EVENTS_TO_HISTO_PER_BUFFER) 
		#endif

						}  // end else of if (header_type == 0xF), that started about 100 lines back....

//Notice : still within switch(rawBuf->board_type) that is within while (offset < BufLengthInLongwords), that started about 250 lines back from here.

						break; // break for digitizer board types case.

				case BrdType_DGS_MTRIG:					
				case BrdType_DGS_RTRIG:
					// Just pass it forward.
					break; // break for DGS trigger board types case.

				case BrdType_MYRIAD:
					// Just pass it forward.
					break; // break for MYRIAD board types case.

				default:
					if(outloop_debug_level >= 0) printf("OutLoopSupport: Unknown board type %d with data type %d\n",rawBuf->board_type, rawBuf->data_type);
					return;
					break;
				}	//end of switch(rawBuf->board_type) that started about 280 lines back....
			
//Notice : still within while (offset < BufLengthInLongwords), that started about 270 lines back from here.


			//Jump pointers to next event, based upon packet size in header.
			//the count of the packet length is in longwords.
			dptr += packet_length + 1;
					
			if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale) 
				{
				if (print_header_prescaler_count == print_header_prescale) 
					{
					print_header_prescaler_count = 0;
					if(outloop_debug_level >= 2) printf(STD_PRINTF_HEADER_DUMP);
					} // end if (print_header_prescaler_count == print_header_prescale) 
				print_header_prescaler_count++;
				} //end if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale)
			buffer_event_index++;	
			} //end while (offset < BufLengthInLongwords), that started ~310 lines back.

//Notice: at this point we are no longer under any while, do, switch or if.

//yes, this is a label for a GoTo.  Correct thing to do when you have to implement a multi-level 'break' in a
//loop/brace structure hundreds of lines long.  JTA 20230926.
MOVE_BUFFER_AFTER_CHECK :			

		BoardDataThisCycle[board_num] += rawBuf->len;
		DataTotal[board_num] += rawBuf->len;
		
		if (emergency_data_dump == 1)
			DataLost[board_num] += rawBuf->len;

		// Having been checked, place the buffer into the appropriate queue, only if the buffer had no fatal error.
		if ((fatal_buffer_error == 0) && (sendEnable > 0) )
				{
				TotalSendDataThisCycle += rawBuf->len;
				stat = putSenderBuf(rawBuf);
				}
		else 
			{
			stat = putFreeBuf(rawBuf);		//return buffer to free queue either if unable to send or if buffer had fatal error.
			}
		rawBuf = NULL;
		dptr = NULL;
	
		if (stat != Success) 
			{
			if (ErrorData[board_num][0] == 0) 
				{
				ErrorData[board_num][0] = 9;
				ErrorData[board_num][1] = offset;
				ErrorData[board_num][2] = BufLengthInLongwords;
				ErrorData[board_num][3] = buffer_event_index;
				DumpRawEvt (rawBuf, "Check & Purge:START", 5, 0);
				DumpRawEvt (rawBuf, "Check & Purge:OFFSET-20", 40, offset - 20);
				} //end if (ErrorData[board_num][0] == 0)
			if(outloop_debug_level >= 0) printf("ECE[%d,%04d,0x%05X]: Cant put buffer on queue.\n", board_num, buffer_event_index, offset);
			} // end if (stat != Success) 
		
		if (print_status_prescaler_count == print_status_prescale) 
			{
			print_status_prescaler_count = 0;
			if(outloop_debug_level >= 2) printf("BUF#: %d BOARD: %d  SLOT: %d EVENTS: %d\n", total_buffer_count, rawBuf->board, geo_addr, buffer_event_index);
			if(outloop_debug_level >= 2) printf("CT: %.3f DT: %.3f\n", CurrentTime, DeltaTime);
			print_profile_summary();
			} // end if (print_status_prescaler_count == print_status_prescale) 
		if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale) 
			{
			print_buffer_headers_prescaler_count = 0;
			} // end if (print_buffer_headers_prescaler_count == print_buffer_headers_prescale)
		print_status_prescaler_count++;
		print_buffer_headers_prescaler_count++;
	}

	stop_profile_counter(PROF_OL_CHECK_AND_MOVE_BUFFERS);
	return;
}

void UpdateDataRates(void) {
	int j;
	clock_gettime(CLOCK_REALTIME, &CurrentTimeSpec);
	CurrentTime = (float)(CurrentTimeSpec.tv_sec) + (double)(CurrentTimeSpec.tv_nsec) / 1000000000.0;

	DeltaTime = CurrentTime - LastTime;
	
	if ( DeltaTime > 10) {
		// reboot the timer.  Required once per minute.
		CurrentTimeSpec.tv_sec = 0;
		CurrentTimeSpec.tv_nsec = 0;
		clock_settime(CLOCK_REALTIME, &CurrentTimeSpec);
		clock_gettime(CLOCK_REALTIME, &CurrentTimeSpec);
		CurrentTime = (float)(CurrentTimeSpec.tv_sec) + (double)(CurrentTimeSpec.tv_nsec) / 1000000000.0;

		LastTime = CurrentTime;
		DeltaTime = 0;

		for(j=0;j<GVME_MAX_CARDS;j++) {
			BoardDataThisCycle[j] = 0;
		}
	}
//	} else if (DeltaTime > BUFFER_CHECK_LATENCY_ERROR_THRESHOLD) {	// 4 times too long
//		if(outloop_debug_level >= 0) printf("\nEVENT_CHECK_ERROR: IOC is unresponsive.  It's been %dms since I was last called.  (50ms nominal)\n", (short)(DeltaTime*1000));
//	} else if (DeltaTime > BUFFER_CHECK_LATENCY_WARNING_THRESHOLD) {	// 2 times too long
//		if(outloop_debug_level >= 1) printf("\nEVENT_CHECK_WARNING: IOC is unresponsive.  It's been %dms since I was last called.  (50ms nominal)\n", (short)(DeltaTime*1000));
//	}

	if ( DeltaTime > 0) {
		// Only reset when we had non-zero time on the cycle.
		// The reason for this is that the data rate is only updated
		// when the DeltaTime is non-zero.  This is because there's no
		// guarantee that the reported time will increment between checks.
		// As such the data sum is carried forward when a zero delta is 
		// reported.  In this way the running average will be accurate. 
		for(j=0;j<GVME_MAX_CARDS;j++) {
			// Running average time-based
			//DataRate[j] -= DataRate[j] * (DeltaTime / (float)(DATA_RATE_TIME_CONSTANT));	// time constant of 2.5 seconds
			//DataRate[j] += BoardDataThisCycle[j] / (float)(DATA_RATE_TIME_CONSTANT);  // same as  (BoardDataThisCycle[j] / DeltaTime) * (DeltaTime / 2.5);
			// Running average measurement-based
			DataRate[j] -= DataRate[j] / 5.0;		// time constant of 2 "measuremenet"
			DataRate[j] += (BoardDataThisCycle[j] / DeltaTime) / 5.0;	// "/1000" to convert to kB

			BoardDataThisCycle[j] = 0;
		}
		SendDataRate = (float)(TotalSendDataThisCycle) / DeltaTime;
		TotalSendDataThisCycle = 0;
		LastTime = CurrentTime;
	}
	return;
}

unsigned int GetDataLost(unsigned short board) {
	// return KB
	return (unsigned int)(DataLost[board] / (unsigned long long int)(1024));
}

int GetDataRate(unsigned short board) {
	// return Bytes/s
	return (int)(DataRate[board]);
}

unsigned int GetDataTotal(unsigned short board) {
	// return KB
	return (unsigned int)(DataTotal[board] / (unsigned long long int)(1024));
}

unsigned int GetErrorCount(unsigned short board) {
	return TotalErrors[board];
}


unsigned int GetErrorData(unsigned short board, unsigned short data_index) {
	return (board < 4) ? ErrorData[board][data_index] : 0;
}

unsigned int GetTotalBuffers_Written(void) {
	return TotalBuffers_Written;
}

unsigned int GetTotalBuffers_Lost(void) {
	return TotalBuffers_Lost;
}

unsigned int GetTotalFBuffers_Written(void) {
	return TotalFBuffers_Written;
}

int GetSendDataRate(void) {
	// return Bytes/s
	return (int)(SendDataRate);
}
