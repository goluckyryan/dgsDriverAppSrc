
#ifndef _READ_TRIG_FIFO_H
#define _READ_TRIG_FIFO_H

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
int transferTrigFifoData(int bdnum, long numwords, int FifoNum, int QueueUsageFlag, long *NumBytesTransferred);

int TriggerTypeFHeader(int mode, int FifoNum, int BoardNumber, int QueueUsageFlag);
///   int WrapTriggerFIFOEvent(rawEvt *buf_to_process, unsigned int MON7_FILL_CTL_Value, int bdnum, rawEvt *processed_buf);

#endif //ifndef _READ_TRIG_FIFO_H
