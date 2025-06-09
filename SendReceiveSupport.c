#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <assert.h>
#include <taskLib.h>
#include <tickLib.h>
#include <sysLib.h>
#include <logLib.h>
#include <freeList.h>
#include <epicsMutex.h>
#include <epicsEvent.h>

#include <cacheLib.h>

#include <msgQLib.h>

#include <hostLib.h>
#include <sockLib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sockLib.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ioLib.h>


#include <errno.h>

#include <inetLib.h>
#include <semLib.h>
#include <timers.h>

#include <SendReceiveSupport.h>
#include <QueueManagement.h>
#include "DGS_DEFS.h"







//--------------------
//	Define a union (buffer/structure) to hold any message from the receiver.
//	This buffer is continuously overwritten as new messages arrive.
//--------------------
ReqMsg ReceiverMessage;
ReqMsg *RequestFromReceiver;

//--------------------
//	Define a structure to hold the buffer descriptor for each item pulled off the QSend queue.
//--------------------
rawEvt WorkingBuffer;
rawEvt *WorkingDescriptor;


static int SocketForRequests = -1;

//MBO 20200611: super hacky hack, promoted to global just to see if this gets the sender sending.
int ReadWriteSocket = -1;		//Socket address assigned by accept()

//MBO 20200617: New Function. Lets try to be generous with the socket options
void setsocketoption(int sock)
{
	int rcvbuf = 65536;
	int sndbuf = 65536;
	int tcp_nodelay = 1;
//see comment below.	int tcp_maxseg = 1400;

	//  The optval is expected to be an int passed as an char*.
	if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)(&rcvbuf), sizeof(rcvbuf)))
		printf("could not set SO_RCVBUF");
	if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)(&sndbuf), sizeof(sndbuf)))
		printf("could not set SO_SNDBUF");
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)(&tcp_nodelay), sizeof(tcp_nodelay)))
		printf("can't set TCP_NODELAY\n");
		
	//20220921:  JTA:  VxWorks documentation says that, after a call to socket() but before the 
	//connection is established, you can REDUCE the TCP message size from the default value set 
	//by TCP_MSS_DFLT through a call to setsockopt, but you cannot make it BIGGER.  Default
	//value of TCP_MSS_DFLT per manual is 512.  If the connection, when received, has an MSS
	//option, the MSS will be modified depending upon the value received and may be set as high
	//as the MTU.
	
	//Based upon this, I don't see this call as useful and since it throws an error message4
	//confusing the reader will comment it out.

#if 0
	if (setsockopt(sock, IPPROTO_TCP, TCP_MAXSEG, (char*)(&tcp_maxseg), sizeof(tcp_maxseg)))
		printf("can't set TCP_MAXSEG\n");
#endif

	return;
}	

//===============================================================================
//	InitRequestSocket() opens up a socket for the reception of request packets
//	from the receiver.  Once opened, a sockaddr_in structure

//	struct sockaddr_in {
//		u_short sin_family; /* address family AF_INET = 2 */
//		u_short sin_port; /* protocol port# */
//		u_long sin_addr; /* IP address */
//		char sin_zero[8]; /* unused (set to zero) */
//	}
//
//	is filled with the socket address information.
//===============================================================================
#define SERVER_PORT 9001		//magic define copied from original psNet.h.

int InitRequestSocket() 
	{
	struct sockaddr_in our_addr;
	char hname[80];
	int on = TRUE;

	
	if(sender_debug_level >= 1) printf("InitRequestSocket: Init start.\n");
	if (SocketForRequests > 0) close(SocketForRequests);		//if socket already in place, close it.

	// 20200602:JTA: this call to socket() creates a stream type socket, internet domain.  Does not specify
	// any protocol details.
	SocketForRequests = socket(AF_INET, SOCK_STREAM, 0);
	if (SocketForRequests == -1) 
		{
		if(sender_debug_level >= 0) printf("InitRequestSocket: socket call failed in InitRequestSocket\n");
		return -1;
		}

	// MBO: Make it non-blocking so the machine can still react to a run stop
	// ioctl expects pointer to be passed as an int
 	ioctl(SocketForRequests, FIONBIO, (int)(&on));

	bzero((char *)&our_addr,sizeof(struct sockaddr_in));	
	our_addr.sin_family = AF_INET;
	our_addr.sin_port = htons(SERVER_PORT);
	
	// From https://www.ee.ryerson.ca/~courses/ee8205/Data-Sheets/Tornado-VxWorks/vxworks/ref/hostLib.html :
	// use of gethostname() and hostGetByName() require that the library be initialized by
	// a call to hostTblInit().  This is done automatically if the configuration macro
	// INCLUDE_NET_INIT is defined.
 
	if  (gethostname(hname,79))  //gethostname( ) - get the symbolic name of this machine
		{
		if(sender_debug_level >= 0) printf("InitRequestSocket: our host name (%s) unavailable in request grabber\n",hname);
		return -2;
		}
	else
		{
		if(sender_debug_level >= 2) printf("InitRequestSocket: host name %s available.\n",hname);
		}

	our_addr.sin_addr.s_addr = hostGetByName(hname);	//hostGetByName( ) - look up a host in the host table by its name

	if (our_addr.sin_addr.s_addr == -1) 
		{
		if(sender_debug_level >= 0) printf("InitRequestSocket: Error setting up our receiving address in request grabber\n");
	
		
		return -3;
		}
	else
		{
		if(sender_debug_level >= 2) printf("InitRequestSocket: receiving address set up OK\n");
		if(sender_debug_level >= 2) printf("InitRequestSocket: Family : %d\n",our_addr.sin_family);
		if(sender_debug_level >= 2) printf("InitRequestSocket: Port : %d\n",our_addr.sin_port);
		if(sender_debug_level >= 2) printf("InitRequestSocket: Address : %ld\n",our_addr.sin_addr.s_addr);
		}

	setsocketoption(SocketForRequests);

	//Once you create a socket, you must then bind it to the network address just loaded into structure our_addr.
	if (-1 == bind(SocketForRequests, (struct sockaddr *)&our_addr,sizeof(struct sockaddr_in)))  
		{
		if(sender_debug_level >= 0) printf("InitRequestSocket: request grabber unable to bind our address\n");
		return -4;
		}
	else
		{
		if(sender_debug_level >= 2) printf("InitRequestSocket: receiving address successfully bound OK\n");
		}

	//Once a socket is created and bound to an address, one must then call listen() to enable connections to that socket.
	//The 2nd argument to listen() is the maximum number of unaccepted connections that can be pending at any time.
	if (-1 == listen(SocketForRequests, 10)) 
		{
		if(sender_debug_level >= 0) printf("InitRequestSocket: request grabber unable to listen our address\n");
		return -5;
		}
	else
		{
		if(sender_debug_level >= 2) printf("InitRequestSocket: able to listen OK\n");
		}
	//If the listen() call succeeds, then connections are actually accepted by later calls to accept(<socket>, <*sockaddr>, <addrlen>).
	if(sender_debug_level >= 1) printf("InitRequestSocket: hname and port: %s %d\n", hname, SERVER_PORT);

	//set up our buffers and pointers.
	RequestFromReceiver = &ReceiverMessage;
	WorkingDescriptor = &WorkingBuffer;
	return 0;
}


int AcceptConnection() 
	{
	struct sockaddr AcceptedSocket;
		
	if(sender_debug_level >= 1) printf("MiniSender: AcceptConnection: About to call accept() with socket %d\n",SocketForRequests);		//added 20220921
	ReadWriteSocket = accept(SocketForRequests, &AcceptedSocket, 0);	//wait for a message (SocketForRequests is global to this file)
	if(sender_debug_level >= 1) printf("MiniSender: AcceptConnection: Response from accept() is %d\n",ReadWriteSocket);		//added 20220921
	
	if (ReadWriteSocket == ERROR) //MBO: 20200611 changed from -1 to ERROR  //if error, exit
		{
		// Throw a message for anything other than "would block"
		if(sender_debug_level >= 0) printf("AcceptConnection: accept failed %s\n", strerror(errno));
		if (errno != EWOULDBLOCK)
			{
			if(sender_debug_level >= 1) printf("AcceptConnection: re-initializing socket, returning -1\n");
			//jta: 20230919: suggest it would be useful to forcibly close the socket here.
			//this can be done by just calling InitRequestSocket() again.
			InitRequestSocket();
			return -1;
			}
		else
			{
			if(sender_debug_level >= 1) printf("AcceptConnection: error was EWOULDBLOCK, will try again, returning 0\n");
			//jta: 20230919: suggest it would be useful to forcibly close the socket here.
			//this can be done by just calling InitRequestSocket() again.
			InitRequestSocket();
			return 0;
			}
		}  //end if (ReadWriteSocket == ERROR) 
	if(sender_debug_level >= 1) printf("AcceptConnection: socket accept ok, status = %d, ERROR val is %d, returning 1\n",ReadWriteSocket,ERROR);

	return 1;
	}

//===============================================================================
//	getReceiverRequest uses the union ReqMsg ReceiverMessage to get a message from 
//	the receiver.  Upon return, if no error is reported, 
//	the "type' has been filled with the data sent by the receiver software to
//	us here in the ReceiveRequest machine, using the ReqMsg.RawMsg access method.
//	Any decoding of the message may use the ReqMsg.Fields.type and/or
//	the ReqMsg.Fields.time structure fields.
//
//	You have to call InitRequestSocket() once, first, before calling this function
//	or there won't be a socket to use.
//===============================================================================
int getReceiverRequest() 
	{
	int insize;
	int DataStillDesired = 0;
	int BytesReceived = 0;

	if(sender_debug_level >= 2) printf("getReceiverRequest: called\n");

	if (ReadWriteSocket == ERROR) //MBO: 20200611 changed from -1 to ERROR  //if error, exit
		{
		printf("getReceiverRequest: accept failed %s\n", strerror(errno));
		return(-2);
		}

	// If message available, receive it.
	// from the manuals, read() may be not a native function but a rename for backwards compatibility.
	// In the socklib.h file there's a listen(), an accept(), a send(), but no read().
	//		However there is a recv(), recvfrom() and an recvmsg().  The recv() looks closest.
	//	The arg list for recv is
	//	int recv
    //		(
    //		int    s,      /* socket to receive data from */
    //		char * buf,    /* buffer to write data to */
    //		int    bufLen, /* length of buffer */
    //		int    flags   /* flags to underlying protocols */
    //		)
	//
	//	Flags would normally be 0 unless you want to send OutOfBand(OOB) data or peek without removing the data from the socket.
	
//	insize = read(buf->clnt, buf->request, sizeof(struct RequestMsg));
	insize = recv(ReadWriteSocket, RequestFromReceiver->RawMsg, sizeof(ReqMsg),0);
	if (insize <= 0) //If error, exit  (#defined value ERROR is -1; #defined value OK is 0)
		{
		 //MBO 20200621: This is now non-blocking, so check why it failed. 
		if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
			{
			// This is ok, just try again.
			taskDelay(1);
			return 1;
			}
		else
			{
			// Throw a message for anything other than "would block"
			if(sender_debug_level >= 0) printf("getReceiverRequest: recv failed with: %s\n", strerror(errno));
			return -3;
			}
		}
	if(sender_debug_level >= 2) printf("getReceiverRequest : message recv returned ok\n");
	
	//If message successfully received, process it.
	BytesReceived = insize;
	//If the length of the received message is less than the size of the ReceiverRequestPacket structure,
	//try reading some more until you have gotten that much.
	while (BytesReceived < sizeof(ReqMsg)) 
		{
		DataStillDesired = sizeof(ReqMsg) - BytesReceived;	//calculate what you still need
		if(sender_debug_level >= 2) printf("getReceiverRequest : need %d more bytes\n", DataStillDesired);  //MBO 20200611: added additional info print.
		//ask for that much; buf->request is char[28], send pointer to index after last data
		insize = recv(ReadWriteSocket, &(RequestFromReceiver->RawMsg[BytesReceived]), DataStillDesired,0);
		BytesReceived = BytesReceived + insize;
		if (insize <= 0) 
			{
			printf("requestgrabber failed to read request\n");
			close (ReadWriteSocket);
			return(-4);
			}
		if (BytesReceived > sizeof(ReqMsg)) 
			{
			printf("requestgrabber overran buffer\n");
			close (ReadWriteSocket);
			return(-5);
			}
		}  //End while (BytesReceived < sizeof(....
	//MBO 20200611: lines below tabbed back 1 indent <-
	if(sender_debug_level >= 2) printf("getReceiverRequest returning, got %d bytes; type is %d\n", BytesReceived, RequestFromReceiver->type);

//jta: 20230919: Why do we not close the socket if we are happy?


	return (0);	//success!
}


//===============================================================================
//	sendServerResponse() sends a structure of type evtServerRetStruct
//	back to the receiver, as acknowledgement of the request message.
//
//	This does NOT send any VME data to the receiver.  However, it SHOULD
//	poll the QSend queue to see if there is any buffer of data available to send.
//	If the QSend queue has data available, then that buffer should be sent
//	immediately after the response message is sent.  Because the response message
//	is required to send the total length of the data to follow, the buffer has
//	to be popped off the QSend queue here if there's one to use and it's length
//	extracted from the 
//===============================================================================
int sendServerResponse()
	{
	int BytesSent;	// MBO 20200621: It's non-blocking now, so this is needed.
	int TotalBytesSent;	// MBO 20200621: It's non-blocking now, so this is needed.
	//BufDescriptor defines the next buffer of data available.  
	//If a buffer is available, this routine pulls it off of the QSend queue,
	//and then another routine sendData will put the buffer back onto the QFree queue
	//once the buffer's been sent.

	ResponseMsg ResponseMessage;
	
	int BufsAvailable = 0;
	
	BufsAvailable = getSenderBufCount();
	if(sender_debug_level >= 2) printf("sendServerResponse called\n");
	
	if (BufsAvailable == 0)		//if no buffers in QSend, send a preformatted response packet saying that there's naught to do.
		{
		// ResponseMessage.Fields is a struct of four ints (type, reclen, status, recs).
		// words have to passed through htonl() because receiver processes data as read with ntonl()
		ResponseMessage.Fields.type = htonl(INSUFF_DATA);		//one of #defn'd values CLIENT_REQUEST_EVENTS, SERVER_NORMAL_RET...etc.
//		ResponseMessage.Fields.type |= (0x00000) << 0;	// Don't care
//		ResponseMessage.Fields.type |= (0x1) << 20;		// Header Version Number - 4-bits.
//		ResponseMessage.Fields.type = htonl(ResponseMessage.Fields.type);
		
		ResponseMessage.Fields.recLen = htonl(0);		//total size, in bytes, of data that will follow from the server code (digitzer/trigger buffer)
					//The Receiver will expect "ntohl(recLen)" bytes of "payload" to follow a SERVER_SUMMARY response header.  
		ResponseMessage.Fields.status = htonl(0);		//apparently unused in GtReceiver4.
		ResponseMessage.Fields.recs = htonl(0);		//say that there are no records
		if(sender_debug_level >= 2) printf("sendServerResponse: no buffers ready, sent header type of 0x%08X using 0x%08X from #define INSUFF_DATA %d\n",ResponseMessage.Fields.type,htonl(INSUFF_DATA),INSUFF_DATA );
		if(sender_debug_level >= 2) printf("sendServerResponse: no buffers ready, sent header recLen of %d, status of 0x%08X, recs of %d\n",ResponseMessage.Fields.recLen,ResponseMessage.Fields.status,ResponseMessage.Fields.recs);
		}
	else
		{
		getSenderBuf(&WorkingDescriptor);	//get a buffer, 'cause there's at least one there.
		// ResponseMessage.Fields is a struct of four ints (type, reclen, status, recs).
		// words have to passed through htonl() because receiver processes data as read with ntonl()
		ResponseMessage.Fields.type = htonl(SERVER_SUMMARY);		//one of #defn'd values CLIENT_REQUEST_EVENTS, SERVER_NORMAL_RET...etc.
/**************************** this appears to be incompatible with current coding of dgsReceiver ********************/
		
//these lines will clobber the data set in the line above.		
//		ResponseMessage.Fields.type |= (WorkingDescriptor->board_type & 0xF) << 0;// ANL Digitizer, Master, Router.  There’s not reason I can see that one would need to differentiate between slave and master digitizers at this time, but certainly would not hurt.
//		ResponseMessage.Fields.type |= (WorkingDescriptor->board & 0xF) << 4;// Not necessary for digitizers which already contain such data in their headers, but where needed, you could use this to identify which board sent this data to the receiver, such as in the case of multiple routers.
		
//		ResponseMessage.Fields.type |= (WorkingDescriptor->data_type & 0xFF) << 8;// All codes could be unique pre board type, if desired.
//		ResponseMessage.Fields.type |= (0x0) << 16;	// reserved (4-bits)
//		ResponseMessage.Fields.type |= (0x1) << 20;	// Header Version Number - 4-bits.
//		ResponseMessage.Fields.type = htonl(ResponseMessage.Fields.type);


		ResponseMessage.Fields.recLen = htonl(WorkingDescriptor->len);		//total size, in bytes, of data that will follow from the server code (digitzer/trigger buffer)
					//The Receiver will expect "ntohl(recLen)" bytes of "payload" to follow a SERVER_SUMMARY response header.  
		ResponseMessage.Fields.status = htonl(0);		//apparently unused in GtReceiver4.
		ResponseMessage.Fields.recs = htonl(1);		//say that there is one record about to be sent.
		if(sender_debug_level >= 2) printf("sendServerResponse: %d buffer(s) availble, sent header type of 0x%08X using 0x%08X from #define SERVER_SUMMARY %d\n",BufsAvailable, ResponseMessage.Fields.type,htonl(SERVER_SUMMARY),SERVER_SUMMARY );
		if(sender_debug_level >= 2) printf("sendServerResponse: no buffers ready, sent header recLen of %d, status of 0x%08X, recs of %d\n",ResponseMessage.Fields.recLen,ResponseMessage.Fields.status,ResponseMessage.Fields.recs);
		}
		
	//whether data available or not, send the Response Message and return so that
	// the receiving monitor state machine can start polling again.
	TotalBytesSent = 0;
	do 
		{
		BytesSent = send(ReadWriteSocket, &(((char*)((void*)(ResponseMessage.RawMsg)))[TotalBytesSent]), sizeof(ResponseMsg) - TotalBytesSent, 0);	
		if (BytesSent <= 0) //If error, exit  (#defined value ERROR is -1; #defined value OK is 0)
			{
			 //MBO 20200621: This is now non-blocking, so check why it failed. 
			if ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != ENOBUFS))
				{
				// Throw a message for anything other than "would block"
				if (BufsAvailable != 0)				
					putFreeBuf(WorkingDescriptor);
				if(sender_debug_level >= 0) printf("sendServerResponse: send failed with: %s\n", strerror(errno));
				return 0;
				}
			else if(errno == ENOBUFS)
				{
				taskDelay(1);
				}
			}
		else
			TotalBytesSent += BytesSent;
		if(sender_debug_level >= 2) printf("sendServerResponse: Response message size = %d bytes, total bytes sent = %d, message total length = %d\n",BytesSent,TotalBytesSent,sizeof(ResponseMessage.RawMsg));
		} while (TotalBytesSent < sizeof(ResponseMessage.RawMsg));


	// now what I'd really like to do here is call taskSpawn() if the # of buffers available was nonzero, so
	// that we could spawn off a high priority thread to ship the data buffer as fast as possible.  However,
	// that might be done just as well by having a parallel state machine thread that looks at a flag variable.
	if(sender_debug_level >= 2) printf("sendServerResponse: response sent\n");
	return (BufsAvailable);	//if BufsAvailable is non-zero, sendDataBuffer() should be called.
}

//===============================================================================
//	sendDataBuffer() is passed a pointer to a buffer descriptor (rawEvt).  It 
//	calls send() to push the data to the receiver,and when done, puts the buffer
//	back on the QFree queue.
//===============================================================================
void sendDataBuffer()
	{
	int BytesSent;	// MBO 20200621: It's non-blocking now, so this is needed.
	int TotalBytesSent;	// MBO 20200621: It's non-blocking now, so this is needed.
	if(sender_debug_level >= 2) printf("sendDataBuffer called\n");

	TotalBytesSent = 0;
	do 
		{
		BytesSent = send(ReadWriteSocket, &(((char*)((void*)(WorkingDescriptor->data)))[TotalBytesSent]), WorkingDescriptor->len - TotalBytesSent, 0);	
		if (BytesSent <= 0) //If error, exit  (#defined value ERROR is -1; #defined value OK is 0)
			{
			 //MBO 20200621: This is now non-blocking, so check why it failed. 
			if ((errno != EWOULDBLOCK) && (errno != EAGAIN) && (errno != ENOBUFS))
				{
				// Throw a message for anything other than "would block"
				putFreeBuf(WorkingDescriptor);
				if(sender_debug_level >= 0) printf("sendDataBuffer: send failed with: %s\n", strerror(errno));
				return;
				}
			else if(errno == ENOBUFS)
				{
				taskDelay(1);
				}
			}
		else
			TotalBytesSent += BytesSent;
		} while (TotalBytesSent < WorkingDescriptor->len);

	putFreeBuf(WorkingDescriptor);
	}


void FlushAllBuffers()
	{
	int numBufsToMove;
	numBufsToMove = getSenderBufCount();
	while(numBufsToMove)
		{
		getSenderBuf(&WorkingDescriptor);	//get a buffer, 'cause there's at least one there.
		putFreeBuf(WorkingDescriptor);		//put it on the free queue
		if(sender_debug_level >= 1) printf("Flushing...Buffer count now %d\n",numBufsToMove);
		numBufsToMove--;
		}
	}

// MBO: spilt buffer flush and socket closing functions
void CloseAllSockets()
	{
	if (SocketForRequests > 0) close(SocketForRequests);
	if (ReadWriteSocket > 0) close(ReadWriteSocket);
	SocketForRequests = -1;
	ReadWriteSocket = -1;
	if(sender_debug_level >= 1) printf("All Sockets closed.\n");
	}
