//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		outLoopSupport.h
// Description: Data processing functions.
//--------------------------------------------------------------------------------
#ifndef _OUT_LOOP_SUPPORT_H
#define _OUT_LOOP_SUPPORT_H

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
void ResetStats(void);
int GetTrace(short* trace, int board, int channel);
void CheckAndMoveBuffers(int written_bufs, int send_bufs, short sendEnable);

/* Call this to update rates. */
void UpdateDataRates(void);
/* Board data lost or discarded in KBytes */
unsigned int	GetDataLost(unsigned short board);
/* Board read rates in Bytes/s */
int		GetDataRate(unsigned short board);
/* Board total data in KByte */
unsigned int	GetDataTotal(unsigned short board);
unsigned int	GetErrorCount(unsigned short board);
unsigned int	GetErrorData(unsigned short board, unsigned short data_index);
unsigned int	GetTotalBuffers_Written(void);
unsigned int	GetTotalBuffers_Lost(void);
unsigned int	GetTotalFBuffers_Written(void);

/* Board read rates in Bytes/s */
int		GetSendDataRate(void);


//float GetTotalBufsLostPercentage(void);

#endif // #ifndef _OUT_LOOP_SUPPORT_H
