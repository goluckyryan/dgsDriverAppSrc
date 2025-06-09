//   inLoopSupport.h
#ifndef _IN_LOOP_SUPPORT_H
#define _IN_LOOP_SUPPORT_H

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
int SetupBoardAddresses(int CRATENUM, int MaxBoardNum, int B0_SW_en, int B1_SW_en, int B2_SW_en, int B3_SW_en, int B4_SW_en, int B5_SW_en, int B6_SW_en);
void ClearDigMstrLogicEnable(int BoardNumber);
void SetDigMstrLogicEnable(int BoardNumber);
void ClearDigFIFO(int BoardNumber);
void CalcDigMaxEventsPerRead(int BoardNumber);
void EnableModule(int BoardNumber);
long CheckAndReadDigitizer(int BoardNumber, int SendNextEmpty, int globQueueUsageFlag);
long CheckAndReadTrigger(int BoardNumber, int FifoNum, int SendNextEmpty, int globQueueUsageFlag);
void SendEndOfRun(int BoardNumber, int globQueueUsageFlag);
float UpdateScanDelay(void /*float current_delay*/);
void InitializeDigPipeline(int BoardNumber);
void ResetAndReEnableDig(int BoardNumber);
extern void DumpInLoopArrays(void);
extern void CloseDumpFiles(void);

extern void ClearTrigFIFO(int BoardNumber, int FIFO_index);
extern void SetTrigSoftwareVeto(int BoardNumber);
extern void ClearTrigSoftwareVeto(int BoardNumber);


#endif // #ifndef _IN_LOOP_SUPPORT_H
