//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		QueueManagement.h
// Description: Contains any function that initializes, adjust or manages the queues.
//--------------------------------------------------------------------------------
#ifndef _QUEUE_MANAGEMENT_H
#define _QUEUE_MANAGEMENT_H

//==============================
//---     Include Files     --- 
//==============================
#include "/global/devel/vxWorks/Tornado2.2/target/config/mv5500/universe.h"
#include "DGS_DEFS.h"

//==============================
//---        Defines        --- 
//==============================
#undef GARBAGE_COLLECT

//==============================
//---         Enums         --- 
//==============================
//==============================
//---   Stucts and Unions   --- 
//==============================
//==============================
//---        Externs        --- 
//==============================
//==============================
//---       Prototypes      --- 
//==============================

//==========================================================
//	function prototypes
//==========================================================

//function prototypes copied from universe.c
STATUS sysVmeDmaInit (void);
STATUS sysVmeDmaCnfgGet (UINT32 *xferType, UINT32 *addrSpace, UINT32 *dataType,UINT32 *userType);
STATUS sysVmeDmaCnfgSet (UINT32 xferType, UINT32 addrSpace, UINT32 dataType,UINT32 userType);
STATUS sysVmeDmaStatusGet (UINT32  *transferStatus);
STATUS sysVmeDmaL2VCopy (UCHAR  *localAddr, UCHAR  *localVmeAddr, UINT32  nbytes);
STATUS sysVmeDmaV2LCopy (UCHAR  *localVmeAddr,UCHAR *localAddr, UINT32 nbytes);


int getFreeBufCount(void);
int getWrittenBufCount(void);
int getSenderBufCount(void);
int putFreeBuf(rawEvt *rawBuf);
int getFreeBuf(rawEvt **rawBuf);
int putWrittenBuf(rawEvt *rawBuf);
int getWrittenBuf(rawEvt **rawBuf);
int putSenderBuf(rawEvt *rawBuf);
int getSenderBuf(rawEvt **rawBuf);
int setupFIFOReader();
void DumpRawEvt (rawEvt *p, char *CallingRoutine, int dumplength, int dumpstart);

#endif // #ifndef _QUEUE_MANAGEMENT_H
