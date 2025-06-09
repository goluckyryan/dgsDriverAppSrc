
#ifndef _READ_DIG_FIFO_H
#define _READ_DIG_FIFO_H

//==============================
//---     Include Files     --- 
//==============================
//==============================
//---        Defines        --- 
//==============================
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
int transferDigFifoData(int bdnum, long numwords,  int QueueUsageFlag, long *NumBytesTransferred);
int DigitizerTypeFHeader(int mode, int BoardNumber, int QueueUsageFlag);
void dbgReadDigFifo(int board, int numwords, int mode);



#endif //ifndef _READ_DIG_FIFO_H
