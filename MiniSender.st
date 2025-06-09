program  MiniSender ("CRATE=CRATE") 


//option +r;		//means this state machine is re-entrant so that you can have multiple copies running.
			//in the rewrite of 20190109, we plan on having only one machine per crate, rather than
			//the one-per-board paradigm of the original design, so we change this to -r.
option -r;
option +s;		//added 20200612 for thread safety checking.

#define DECLEVENT(t,n,s)        \
        t n;                    \
        assign n to #s;         \
        monitor n;              \
        evflag n##Event;        \
        sync n n##Event;

#define DECLMON(t,n,s)          \
        t n;                    \
        assign n to #s;         \
        monitor n;

#define DECL(t,n,s)             \
        t n;                    \
        assign n to #s;

#define DECLARRAY(t,n,s,l)      \
        t n[l];                 \
        assign n to #s;


//Online_CS_StartStop and Online_CS_SaveData PVs are hosted by the Soft IOC
//running on the host Linux box that runs the EDM screens.

//jta 20230926 changed RunStopButton to DECLMON instead of DECLEVENT
DECLMON(short,RunStopButton,Online_CS_StartStop)
DECLMON(short,Save_NoSave_Button,Online_CS_SaveData)


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

Note that text appearing on the same line after %{ and before }% also belongs to the literal code block .
***************************************************/

DECL(short,SenderRunning, DAQC{CRATE}_CV_SenderRunning)






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

%% #include "profile.h"
%% #include "QueueManagement.h"
%% #include "outLoopSupport.h" 
%% #include "SendReceiveSupport.h"



int RequestMsgStatus;	//0 = success; various negative = failures
int NumBufsAvailable;		//how many data buffers are on the QSend queue
int ConnectionAccepted;		//how many data buffers are on the QSend queue





ss ReceiveRequest
	{
/*************************************************************/	
	state init 
		{
		entry
			{
			taskPrioritySet(taskIdSelf(), 190);
			SenderRunning = 0;
			}
		when ((RunStopButton == 1) && (Save_NoSave_Button == 1))		//wait for run to start and for save/nosave to be in save mode
			{
			printf("MiniSender: Started\n");
			RequestMsgStatus = 1;		//initialize to no-message status
			ConnectionAccepted = 0;
			InitRequestSocket();	//in SendReceiveSupport.c; socket number is held in variable defined there too.
			} state DelayAfterStart
		when (delay(1))
			{
			FlushAllBuffers();
			} state init
		}
/*************************************************************/	

	state DelayAfterStart
		{
		when (delay(2))
			{
			printf("MiniSender: delay after start complete\n");
			} state WaitForConnection
		}
/*************************************************************/	
	state WaitForConnection 
		{
		option -e;		//do the entry every time
		entry
			{
			ConnectionAccepted = AcceptConnection();
			}
		when ((RunStopButton == 0) || (Save_NoSave_Button == 0))		//wait for run to start and for save/nosave to be in save mode
			{
			} state cleanup
		when (ConnectionAccepted > 0)		//if connect is happy then do requests
			{
			if(sender_debug_level >= 1) printf("MiniSender: Connection accepted.\n");
			FlushAllBuffers();	//Try to start clean.
			} state HandleRequests
		when (delay(0.05))		//wait for run to start
			{
			} state WaitForConnection
		}

/*************************************************************/	
	state HandleRequests
		{
		option -e;		//do the entry every time
		entry 
			{
			SenderRunning = 1;
			start_profile_counter(4);	//#define PROF_MS_GET_RECEIVER_REQUEST 4
			RequestMsgStatus = getReceiverRequest();	//check to see if receiver has sent a request (see SendreceiveSupport.c)
			stop_profile_counter(4);	//#define PROF_MS_GET_RECEIVER_REQUEST 4
			if (RequestMsgStatus < 0)
				{
				// State machine language does not process switch statements, so escaped as inline c code.
				%{
				if(sender_debug_level >= 2) printf("ReceiveRequest : ERROR :");
				switch(RequestMsgStatus)
					{
					case -1 : if(sender_debug_level >= 2) printf("memory allocation: cannot malloc buffer\n"); break;
					case -2 :
					case -3 : 
					case -4 : if(sender_debug_level >= 2) printf("socket error\n"); break;
					case -5 : if(sender_debug_level >= 2) printf("message size error\n"); break;
					}
				}%
				}
			}
		//under normal conditions (message received), process it.
		when (RequestMsgStatus == 0)
			{
			} state ProcessRequest
		// No receive message is to be expected now, especially at startup.
		when (RequestMsgStatus == 1)
			{
			} state HandleRequests
		when (!RunStopButton)		//at run stop go back to INIT
			{
			} state cleanup
		}	//end state HandleRequests


/*************************************************************/	

/*************************************************************/	
//
//	ProcessRequest calls sendServerResponse() to provide the initial reply
//	to the receiver.  sendServerResponse() will look at the status of the 
//	queue of buffers of digitizer/trigger data and modulate its response 
//	appropriately to report how much data is available.
//
//	sendServerResponse will return immediately after sending the response header
//	so that this machine can be actively polling for when the next receiver 
//	request message shows up.
/*************************************************************/	
	state ProcessRequest
		{
		entry
			{
			//sendServerResponse() returns the number of buffers on the Qsend queue.
			// IF NONZERO, WorkingDescriptor points to a buffer THAT HAS ALREADY BEEN FETCHED FROM THE QUEUE
			// AND THE LENGTH OF THAT DATA BUFFER HAS BEEN REPOSRTED TO THE RECEIVER.
			start_profile_counter(5);	//#define PROF_MS_SEND_SERVER_RESPONCE 5
			NumBufsAvailable = sendServerResponse();
			stop_profile_counter(5);	//#define PROF_MS_SEND_SERVER_RESPONCE 5
			}
		when (NumBufsAvailable == 0)
			{
			} state HandleRequests
		when (NumBufsAvailable > 0)
			{
			start_profile_counter(6);	//#define PROF_MS_SEND_DATA_BUFFER 6
			sendDataBuffer();
			stop_profile_counter(6);	//#define PROF_MS_SEND_DATA_BUFFER 6
			} state HandleRequests
		}

/*************************************************************/	
	state cleanup
		{
		when()
			{
			if(sender_debug_level >= 1) printf("MiniSender: Cleaning up.\n");
			while (getSenderBufCount())
				{
				FlushAllBuffers();
				};
			CloseAllSockets();
			} state init
		}
	}	//end ss ReceiveRequest
