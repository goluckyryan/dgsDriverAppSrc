/* record addressing:  */

/* structure of a VME io channel     <from base, here for illustration>
struct vmeio {
        short   card;
        short   signal;
        char    *parm;
};

Card = 0..GVME_MAX_CARDS - 1, which card it is.
signal = offset (of first channel in multi-channel register case)
parm = channel bitfield number*/

#ifndef _DEV_GVME_H
#define _DEV_GVME_H

//==============================
//---     Include Files     --- 
//==============================
#include <epicsMutex.h>
//==============================
//---        Defines        --- 
//==============================

//==============================
//---         Enums         --- 
//==============================
//==============================
//---   Stucts and Unions   --- 
//==============================
//dawregister moved to dgs defs 20200611

//daqDevPvt moved to dgs defs 20200611

//daqboard moved to dgs defs 20200611
//==============================
//---        Externs        --- 
//==============================
///   extern struct daqBoard daqBoards[GVME_MAX_CARDS];     //moved to DGS_DEFS 20200611
extern char BoardTypeNames[16][30];

//additional global vars for outloop.st to communicate PV values to outloopsupport.c
extern unsigned short OL_Hdr_Chk_En;
extern unsigned short OL_TS_Chk_En;
extern unsigned short OL_Deep_Chk_En;
extern unsigned short OL_Hdr_Summ_En;
extern unsigned int OL_Hdr_Summ_PS;
extern unsigned int OL_Hdr_Summ_Evt_PS;


//==============================
//---       Prototypes      --- 
//==============================
#ifdef __cplusplus
extern "C" int devGVMECardInit(int cardno, int slot);  //JTA: removed 'clocksource' 20190114
extern "C" void InitializeDaqBoardStructure(void);
extern "C" void VMEWrite32(int bdnum, int regaddr,	unsigned int data);
extern "C" unsigned int VMERead32(int bdnum, int regaddr);
extern "C" void VerifyFlashD32 ( int bdnum, int address_control, int StopOnErrorCount_flag, char flashfname[100]);

#else
void InitializeDaqBoardStructure(void);
 int devGVMECardInit(int cardno, int slot);  //JTA: removed 'clocksource' 20190114
void VMEWrite32(int bdnum, int regaddr,	unsigned int data);
unsigned int VMERead32(int bdnum, int regaddr);
void VerifyFlashD32 ( int bdnum, int address_control, int StopOnErrorCount_flag, char flashfname[100]);

#endif


///// struct daqRegister *devGVMEMainRegisterInit(int cardno, unsigned short offset);

#endif //ifndef _DEV_GVME_H
