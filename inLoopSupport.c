//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		John Anderson
// File:		inLoopSupport.c
// Description: Support functions for the inLoop state machine.
//--------------------------------------------------------------------------------
#ifdef vxWorks
#include <vxWorks.h>
#include <stdio.h>
#endif

#include <string.h>
#include <timers.h>
#include <taskHookLib.h>
#include <epicsMutex.h>
#include "DGS_DEFS.h"

#include <math.h>

#include "devGVME.h"
#include <QueueManagement.h>
#include "profile.h"


#include "readDigFIFO.h"
#include "readTrigFIFO.h"
#include <inLoopSupport.h>

// MstrLogicReg, FIFOStatusReg and RawDataLengthReg are arrays of pointers to int
// (pointers to 32-bit numbers) that are used to hold the addresses in VMEA32/D32 space
// that directly access registers of interest in digitzer modules.
int *MstrLogicReg[10];			//there is only one master logic status register per board.
int *FIFOStatusReg[10];			//there is only one FIFO status register per board.
int *RawDataLengthReg[10][10];	//20230410: the user can program a unique readout length per channel per board.
int *PulsedControlReg[10];		//there is only one pulsed control register per board in the digitizer.
int *FIFO_RESET_REG[10];		//address of the fifo reset register per board

unsigned long DigitizerCalcEventSize[10][10];	//since the raw data length per channel per board can vary, then there must be a unique calc size per channel per board.
unsigned long MaxEventsToRead[10];
unsigned long MinEventsToRead[10];
unsigned long DigitizerFull[10];
unsigned long DigitizerAlmostFull[10];
unsigned long DigitizerEmpty[10];
unsigned long DigitizerFifoDepth[10];
unsigned long DigCalcNumEventsAvail[10];
unsigned long MinimumCalcEventSize[10];		//added 20230410; is smallest value of DigitizerCalcEventSize in each board
unsigned long MaximumCalcEventSize[10];		//added 20230410; is largest value of DigitizerCalcEventSize in each board

int TriggerFull[10];
int TriggerEmpty[10];
int TriggerFifoDepth[10];

FILE *IL_FILE = NULL;
FILE *OL_FILE = NULL;
FILE *MS_FILE = NULL;


int FBufferCount;

// Digitizer fixed addresses we use
// should come from the spreadsheet
int DIG_MSTR_LOGIC_REG = (0x500/4);		//address offset of Master Logic Status register
int DIG_PROGRAMMING_DONE_REG = (0x004/4);	//address offset of ProgrammingDone (FIFO depth/status) register
int DIG_RAW_DATA_WINDOW_REG[10] = {(0x140/4), (0x144/4), (0x148/4), (0x14C/4), (0x150/4), (0x154/4), (0x158/4), (0x15C/4), (0x160/4), (0x164/4)};
int DIG_PULSED_CTRL_REG = (0x40C/4);	//address offset of digitizer pulsed control register
int DIG_USR_PKG_DATA_REG = (0x024/4);
int DIG_FIFO = (0x1000/4);

//master trigger fixed addresses we use
//This really should come from the spreadsheet via a #included c source file
int MTRG_MON_FIFO_STATE_REG = (0x1B4/4);	//address offset of MON7_FIFO_STATE register (master trigger)
int MTRG_CHAN_FIFO_STATE_REG = (0x01A4/4);		//address offset of trigger's CHAN_FIFO_STATE register
int MTRG_FIFO_RESET_REG = (0x8F0/4);	//address offset of trigger pulsed control register used for FIFO resets
int MTRG_USR_PKG_DATA_REG = (0x214/4);
int MTRG_FIFO = (0x5000/4);
int MTRG_MON7_LATCHED_DEPTH = (0x01AC/4);		//this is the at-event-boundaries latched counter
int MTRG_MON7_LIVE_DEPTH = (0x0154/4);		//this is the LIVE depth counter
// int MTRG_TRIG_MASK_REG = (0x0850/4);		//address offset of trigger's TRIG_MASK register // Removed MBO 20250904: We don't want to modify this register in the IOC.
int RTRG_FIFO_RESET_REG = (0x8F0/4);	//address offset of trigger pulsed control register used for FIFO resets

//==============================================================================
//	SetupBoardAddresses() initializes a set of arrays that hold addresses of 
//	interest for control of digitizers and triggers.
//==============================================================================
extern int SetupBoardAddresses(int CRATENUM, int MaxBoardNum, int B0_SW_en, int B1_SW_en, int B2_SW_en, int B3_SW_en, int B4_SW_en, int B5_SW_en, int B6_SW_en)		
//mod 20200616 to add per-board enables.
//increased to range 0-6 20220713
{
int BoardNumber;
int NumBoardsEnabled = 0;


#ifdef SM_PRINT_TO_FILE
	if (IL_FILE == NULL)
		{
		sprintf(FileNameStr,"%02d_inLoop.txt",CRATENUM);
		IL_FILE = fopen(FileNameStr, "w");
		fprintf(IL_FILE, "SetupBoardAddresses: file open\n");
		}

	if (OL_FILE == NULL)
		{
		sprintf(FileNameStr,"%02d_outLoop.txt",CRATENUM);
		OL_FILE = fopen(FileNameStr, "w");
		fprintf(OL_FILE, "SetupBoardAddresses: file open\n");
		}

	if (MS_FILE == NULL)
		{
		sprintf(FileNameStr,"%02d_MiniSender.txt",CRATENUM);
		MS_FILE = fopen(FileNameStr, "w");
		fprintf(MS_FILE, "SetupBoardAddresses: file open\n");
		}
#endif

	if(inloop_debug_level >= 1) printf("inLoopSupport: SetupBoardAddresses\n");
	for(BoardNumber = INLOOP_MIN_BOARD_NUMBER; BoardNumber < INLOOP_MAX_BOARD_NUMBER; BoardNumber++)
		if(daqBoards[BoardNumber].mainOK)	//only initialize boards we know exist and are triggers or digitizers.
			{
			//updated 20220713 to handle both triggers and digitizers
			//	"No Board Present",			//0
			//	"GRETINA Router Trigger",	//1		--the '1' comes from bits 11:8 of the code_revision register
			//	"GRETINA Master Trigger",	//2		--the '2' comes from bits 11:8 of the code_revision register
			//	"LBNL Digitizer",			//3		--arbitrary placeholder assigned by JTA
			//	"DGS Master Trigger",		//4		--the '4' comes from bits 11:8 of the code_revision register
			//	"Unknown",					//5
			//	"DGS Router Trigger",		//6		--the '6' comes from bits 11:8 of the code_revision register
			//	"Unknown",					//7
			//	"MyRIAD",					//8		--arbitrary placeholder assigned by JTA
			//	"Unknown",					//9
			//	"Unknown",					//10
			//	"Unknown",					//11
			//	"ANL Master Digitizer",		//0xC : 12	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
			//	"ANL Slave Digitizer",		//0xD : 13	 - low 16 bits of code_revision should be 4XYZ (4:digitizer X:master/slave Y:major rev Z:minor rev)
			//	"Majorana Master Digitizer",//14		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
			//	"Majorana Slave Digitizer",	//15		- Majorana digitizers read FXYZ (F: Majorana digitizer, X:master/slave Y:major rev Z:minor rev)
			switch(daqBoards[BoardNumber].board_type)
				{
				case BrdType_ANL_MDIG:	//	"ANL Master Digitizer",		//0xC : 12
				case BrdType_ANL_SDIG:	//	"ANL Slave Digitizer",		//0xD : 13
					MstrLogicReg[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + DIG_MSTR_LOGIC_REG);		//address offset of Master Logic Status register
					FIFOStatusReg[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + DIG_PROGRAMMING_DONE_REG);	//address offset of ProgrammingDone (FIFO depth/status) register

					RawDataLengthReg[BoardNumber][0] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[0]);	//address offset of digitizer raw data window register, channel 0.
					RawDataLengthReg[BoardNumber][1] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[1]);	//channel 1.
					RawDataLengthReg[BoardNumber][2] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[2]);	//channel 2.
					RawDataLengthReg[BoardNumber][3] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[3]);	//channel 3.
					RawDataLengthReg[BoardNumber][4] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[4]);	//channel 4.
					RawDataLengthReg[BoardNumber][5] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[5]);	//channel 5.
					RawDataLengthReg[BoardNumber][6] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[6]);	//channel 6.
					RawDataLengthReg[BoardNumber][7] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[7]);	//channel 7.
					RawDataLengthReg[BoardNumber][8] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[8]);	//channel 8.
					RawDataLengthReg[BoardNumber][9] = (int *)(daqBoards[BoardNumber].base32 + DIG_RAW_DATA_WINDOW_REG[9]);	//channel 9.

					PulsedControlReg[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + DIG_PULSED_CTRL_REG);
					//Digitizer FIFOs start at address 0x1000
					daqBoards[BoardNumber].FIFO = (int *) (daqBoards[BoardNumber].base32 + DIG_FIFO);
					daqBoards[BoardNumber].DigUsrPkgData = *( (int *)(daqBoards[BoardNumber].base32 + DIG_USR_PKG_DATA_REG) );
					DigitizerFull[BoardNumber] = 0;		//initialize all full flags to "not full"
					DigitizerEmpty[BoardNumber] = 1;		//initialize all empty flags to "empty"
					//turn off MasterLogicEnable, clear the FIFO.
					ClearDigMstrLogicEnable(BoardNumber);
					ClearDigFIFO(BoardNumber);
					break;
				case BrdType_DGS_MTRIG:		//	"DGS Master Trigger",		//4	
					MstrLogicReg[BoardNumber] = NULL;	//there is no MasterLogicRegister in a trigger module
					FIFOStatusReg[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON_FIFO_STATE_REG);
					RawDataLengthReg[BoardNumber][0] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][1] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][2] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][3] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][4] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][5] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][6] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][7] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][8] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][9] = NULL;	//there is no Raw Data Window register in a trigger module
					FIFO_RESET_REG[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + MTRG_FIFO_RESET_REG);
					//Trigger MON7 FIFOs start at address 0x5000
					daqBoards[BoardNumber].FIFO = (int *) (daqBoards[BoardNumber].base32 + MTRG_FIFO);
					daqBoards[BoardNumber].TrigUsrPkgData = *( (int *)(daqBoards[BoardNumber].base32 + MTRG_USR_PKG_DATA_REG ) );
					DigitizerFull[BoardNumber] = 0;		//initialize all full flags to "not full"
					DigitizerEmpty[BoardNumber] = 1;		//initialize all empty flags to "empty"
					//turn on Software Veto, clear the FIFO.
					// SetTrigSoftwareVeto(BoardNumber); // Removed MBO 20250904: We don't want to do this.
					//ClearTrigFIFO(BoardNumber, FIFO_index[BoardNumber]); // comment out by Ryan, since FIFO clear will be done in inLoop:INITIAL_FIFO_CLEAR
					break;
				case BrdType_DGS_RTRIG:		//	"DGS Router Trigger",		//6	
					MstrLogicReg[BoardNumber] = NULL;	//there is no MasterLogicRegister in a trigger module
					FIFOStatusReg[BoardNumber] = NULL;	//for now, until router is further ready, say there is no status register
					RawDataLengthReg[BoardNumber][0] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][1] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][2] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][3] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][4] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][5] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][6] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][7] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][8] = NULL;	//there is no Raw Data Window register in a trigger module
					RawDataLengthReg[BoardNumber][9] = NULL;	//there is no Raw Data Window register in a trigger module
					FIFO_RESET_REG[BoardNumber] = (int *)(daqBoards[BoardNumber].base32 + RTRG_FIFO_RESET_REG);
					//as of 20220713, a Router doesn't have a User Package Data register.
					daqBoards[BoardNumber].TrigUsrPkgData = 0;
					DigitizerFull[BoardNumber] = 0;		//initialize all full flags to "not full"
					DigitizerEmpty[BoardNumber] = 1;		//initialize all empty flags to "empty"
					break;
				default:		//any other kind of board
					MstrLogicReg[BoardNumber] = NULL;	//NULL means "do not use"
					FIFOStatusReg[BoardNumber] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][0] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][1] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][2] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][3] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][4] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][5] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][6] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][7] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][8] = NULL;	//NULL means "do not use"
					RawDataLengthReg[BoardNumber][9] = NULL;	//NULL means "do not use"
					PulsedControlReg[BoardNumber] = NULL;	//NULL means "do not use"
					daqBoards[BoardNumber].TrigUsrPkgData = 0;
					DigitizerFull[BoardNumber] = 0;		//initialize all full flags to "not full"
					DigitizerEmpty[BoardNumber] = 1;		//initialize all empty flags to "empty"
					break;

				}  //end switch(daqBoards[BoardNumber].board_type)

			switch(BoardNumber)
				{
				case 0 : daqBoards[BoardNumber].EnabledForReadout = B0_SW_en; break;
				case 1 : daqBoards[BoardNumber].EnabledForReadout = B1_SW_en; break;
				case 2 : daqBoards[BoardNumber].EnabledForReadout = B2_SW_en; break;
				case 3 : daqBoards[BoardNumber].EnabledForReadout = B3_SW_en; break;
				case 4 : daqBoards[BoardNumber].EnabledForReadout = B4_SW_en; break;
				case 5 : daqBoards[BoardNumber].EnabledForReadout = B5_SW_en; break;
				case 6 : daqBoards[BoardNumber].EnabledForReadout = B6_SW_en; break;
				case 7 : daqBoards[BoardNumber].EnabledForReadout = 0; break;
				default : break;
				}		//end switch(BoardNumber) // Removed MBO 20250904
			if(daqBoards[BoardNumber].EnabledForReadout == 1) NumBoardsEnabled++;
			if(inloop_debug_level >= 2) printf("\n====== Board %d has mainOK set.  Readout enable is %d\n",BoardNumber,daqBoards[BoardNumber].EnabledForReadout);
			}
		else
			{
			MstrLogicReg[BoardNumber] = NULL;
			FIFOStatusReg[BoardNumber] = NULL;
			RawDataLengthReg[BoardNumber][0] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][1] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][2] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][3] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][4] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][5] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][6] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][7] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][8] = NULL;	//NULL means "do not use"
			RawDataLengthReg[BoardNumber][9] = NULL;	//NULL means "do not use"
			DigitizerFull[BoardNumber] = 1;		//initialize all full flags of unused slots to "full"
			DigitizerEmpty[BoardNumber] = 1;		//initialize all empty flags of unused slots to "empty"
			daqBoards[BoardNumber].EnabledForReadout = 0;	//mark board as "do not read out"
			if(inloop_debug_level >= 2) printf("Board %d has mainOK NOT set.  Readout disabled.\n",BoardNumber);
			}
	return(NumBoardsEnabled);
}


//==============================================================================
//	ClearDigMstrLogicEnable() uses direct memory addressing
//	to clear the Master Logic Enable of a digitizer module
//==============================================================================
extern void ClearDigMstrLogicEnable(int BoardNumber)
{
int tempval;
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigMstrLogicEnable: clearing Master Logic Enable of board #%d\n",BoardNumber);		
	tempval = *MstrLogicReg[BoardNumber];	//read from 'master logic status' register
	*MstrLogicReg[BoardNumber] = (tempval & 0xFFFFFFFE);	//CLEAR bit 0 disable the board
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigMstrLogicEnable: Master logic status after write = 0x%08X\n",*MstrLogicReg[BoardNumber]);
}

//==============================================================================
//	SetDigMstrLogicEnable() sets the Master Logic Enable register of
//	and ANL digitizer board into the data-taking condition.
//
// JTA: 20220179: 	modified SetDigMstrLogicEnable so that when the state machine enters state ENABLE_DIGITIZERS,
//					a write of 0x00000001 is done to address 0x040C of every digitizer.  This sets bit 0
//					of the "pulsed channel control" register, A.K.A. the "load delays" bit.  Writing this
//					bit (it is self-clearing, don't need to reset it in software) causes the digitizer to
//					re-initialize all data pipelines and load any new updated delay values prior to taking data.
//					Thus if the user changed any acquisition parameters, they automatically load when the user
//					hits the Run/Stop button to go from Stop to Run.

//==============================================================================
extern void SetDigMstrLogicEnable(int BoardNumber)
{
int tempval;	//temporary holder for data read from VME
	if(inloop_debug_level >= 2) printf("inLoop: SetDigMstrLogicEnable: Set Master Logic Enable of %s module #%d\n",BoardTypeNames[daqBoards[BoardNumber].board_type], BoardNumber);
	// 20220719: add call to InitializeDigPipeline every time a digitizer is enabled.
	InitializeDigPipeline(BoardNumber);
	taskDelay(1);
	tempval = *MstrLogicReg[BoardNumber];	//read from 'master logic status' register
	tempval = (tempval | 0x00000001);	//SET bit 0, re-enable the board
	*MstrLogicReg[BoardNumber] = tempval;	//write to register, setting bit 0
	if(inloop_debug_level >= 2) printf("inLoop:ENBL: tempval: %08X  Enable = 0x%08X\n",tempval,*MstrLogicReg[BoardNumber]);//read from 'master logic status' register

}

//==============================================================================
//	InitializeDigPipeline performs a write to the Pulsed Control register
//	of an ANL-firmware digitizer to set the LOAD_DELAYS bit.  This causes
//	the data processing pipelines to re-initialize.
//	The pulsed control register is at address 0x040C, and the bit is bit 0.
//==============================================================================
extern void InitializeDigPipeline(int BoardNumber)
{
	if(inloop_debug_level >= 2) printf("inLoop: InitializeDigPipeline: reset channels of %s module #%d\n",BoardTypeNames[daqBoards[BoardNumber].board_type], BoardNumber);
	*PulsedControlReg[BoardNumber] = 1;	//write to register, setting bit 0.  The pulsed-control is a write-only register so no read-mod-write required.
	taskDelay(5);
}

//==============================================================================
//	ClearDigFIFO() uses direct memory addressing
//	to clear the FIFO of a digitizer module.
//==============================================================================
extern void ClearDigFIFO(int BoardNumber)
{
int tempval;
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigFIFO: Reset FIFO of %s module #%d\n",BoardTypeNames[daqBoards[BoardNumber].board_type], BoardNumber);
	//clear the FIFO of this board (programming done register, address 0x004)
	tempval = *FIFOStatusReg[BoardNumber];
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigFIFO: Initial Value of ProgrammingDone = 0x%08X\n",tempval);
	*FIFOStatusReg[BoardNumber] = tempval | 0x08000000;	//set bit 27
	taskDelay(10);
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigFIFO: Reset Value of ProgrammingDone = 0x%08X\n",*FIFOStatusReg[BoardNumber]);
	*FIFOStatusReg[BoardNumber] = tempval;		//then put the register back where it was before
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigFIFO: ProgrammingDone after write = 0x%08X\n",*FIFOStatusReg[BoardNumber]);
	taskDelay(10);
	if(inloop_debug_level >= 2) printf("inLoop:ClearDigFIFO: 2nd read: ProgrammingDone after write = 0x%08X\n",*FIFOStatusReg[BoardNumber]);
}

//==============================================================================
//	ResetAndReEnableDig() does a sequence of clearing the master logic
//	enable, resetting the FIFO, and re-enabling the board as a general-purpose
//	recovery mechanism.
//==============================================================================
extern void ResetAndReEnableDig(int BoardNumber)
{
	ClearDigMstrLogicEnable(BoardNumber);
	ClearDigFIFO(BoardNumber);
	SetDigMstrLogicEnable(BoardNumber);	// 20220719: call to InitializeDigPipeline is now buried inside SetDigMstrLogicEnable()
//	InitializeDigPipeline(BoardNumber);
}

//==============================================================================
//	CalcDigMaxEventsPerRead() determines the maximum number of events
//	that can be read per read loop based upon digitizer parameters and
//	the size of a readout buffer.
//==============================================================================
void CalcDigMaxEventsPerRead(int BoardNumber)
{
int ChannelNumber;

	MinimumCalcEventSize[BoardNumber] = 99999999;
	MaximumCalcEventSize[BoardNumber] = 0;

	//Read here the settings of the digitizer (raw window length) for each channel
	for(ChannelNumber=0;ChannelNumber<10;ChannelNumber++)
		{
		DigitizerCalcEventSize[BoardNumber][ChannelNumber]  = *RawDataLengthReg[BoardNumber][ChannelNumber];	//read from reg_raw_data_window[0]
		if(inloop_debug_level >= 2) printf("Raw Data Window readback (event size) for digitizer board #%d, channel %d, is %lu 16-bit words\n",BoardNumber,ChannelNumber,DigitizerCalcEventSize[BoardNumber][ChannelNumber]);
		DigitizerCalcEventSize[BoardNumber][ChannelNumber]  = DigitizerCalcEventSize[BoardNumber][ChannelNumber]  >> 1;	//value read is # of ADC samples, so divide by 2 to get longwords
		if(inloop_debug_level >= 2) printf("Calc event size for digitizer board #%d, channel %d, after shift is %lu 32-bit words\n",BoardNumber,ChannelNumber,DigitizerCalcEventSize[BoardNumber][ChannelNumber]);
		DigitizerCalcEventSize[BoardNumber][ChannelNumber]  += 1;	//add one for the 0xAAAAAAAA. 
		if(inloop_debug_level >= 2) printf("Calculated event size for digitizer board #%d, channel %d, is %lu 32-bit words\n",BoardNumber,ChannelNumber,DigitizerCalcEventSize[BoardNumber][ChannelNumber]);
		if(DigitizerCalcEventSize[BoardNumber][ChannelNumber] < MinimumCalcEventSize[BoardNumber])
			{
			MinimumCalcEventSize[BoardNumber] = DigitizerCalcEventSize[BoardNumber][ChannelNumber];
			}
		if(DigitizerCalcEventSize[BoardNumber][ChannelNumber] > MaximumCalcEventSize[BoardNumber])
			{
			MaximumCalcEventSize[BoardNumber] = DigitizerCalcEventSize[BoardNumber][ChannelNumber];
			}
		}
	//MAX_RAW_XFER_SIZE is the maximum size of a buffer, in BYTES.
	//DigitizerFifoDepth[BoardNumber][ChannelNumber] is in longwords, so DigitizerFifoDepth[BoardNumber][ChannelNumber]*4 is in units of BYTES.
	MaxEventsToRead[BoardNumber] = MAX_DIG_RAW_XFER_SIZE / (MinimumCalcEventSize[BoardNumber]*4);		//calculate max # of events we can read at a time without blowing up buffer.
	MinEventsToRead[BoardNumber] = MAX_DIG_RAW_XFER_SIZE / (MaximumCalcEventSize[BoardNumber]*4);		//calculate minimum # of events that could be in a full FIFO.
}

//==============================================================================
// EnableModule() looks up the kind of board that is referenced by BoardNumber,
// and performs the appropriate operation to enable the board to take data and
// store data into its internal FIFO.
//==============================================================================
extern void EnableModule(int BoardNumber)
{
	if(inloop_debug_level >= 2) printf("inLoop: EnableModule\n");
	if (daqBoards[BoardNumber].mainOK == 1)		//if slot occupied
		{
		//If board is a digitizer, enable it.  Otherwise do nothing.
		switch (daqBoards[BoardNumber].board_type)
			{
			case BrdType_NO_BOARD:
			case BrdType_GRETINA_RTRIG:
			case BrdType_GRETINA_MTRIG:
			case BrdType_MYRIAD:
				break;

			case BrdType_DGS_MTRIG:
			//	ClearTrigSoftwareVeto(BoardNumber); // Removed MBO 20250904: We don't want to do this.
				break;			//trigger modules don't need any explicit enable, but here is where you'd do it if needed.

			case BrdType_DGS_RTRIG:
				break;			//trigger modules don't need any explicit enable, but here is where you'd do it if needed.

			case BrdType_LBNL_DIG:
			case BrdType_ANL_MDIG:
			case BrdType_ANL_SDIG:
			case BrdType_MAJORANA_MDIG:
			case BrdType_MAJORANA_SDIG:
				SetDigMstrLogicEnable(BoardNumber);
				break;

			default:
				break;
			}  //end switch()
		} //end if (daqBoards[BoardNumber].mainOK == 1)
}


//==============================================================================
//	CheckAndReadDigitizer() tests a digitizer to see if there is data available.
//	If so, the data is read out.  If not, a Type F header may be manufactured
//	to alert the back end of the DAQ that the digitizer was checked, but that 
//	there was no data.  This is controlled by a flag variable that limits the
//	number of Type F headers noting empty that may be sent.
//
//	A control flag variable SendNextEmpty is passed in by inLoop.st, so that
//	the state machine may control the rate of empty messages to the back end.
//
//	If the digitizer's FIFO Full flag is set, this spawns a Type F error message.
//	IF the digitizer's FIFO Empty flag is set, but the FIFO depth count is not zero,
//		this also spawns a type F error message.
//
//	Function returns the number of bytes transferred, if transfer occurred.
//		return of 0 means no data, but no error; digitizer was empty.
//		return of -1 means full error.
//		return of -2 means empty/depth mismatch.
//		return of -3 means DMA error.
//		return of -4 means function logical error.
//		return of -5 means FIFO synch error; board not empty, but amount of data in FIFO < 1 event.
//
//	When CheckAndReadDigitizer is called, we have no idea how many events from whichever channels
//  may be in the FIFO, so we can only check data length based upon the minimum and maximum event
//	sizes known to be in this particular board.
//==============================================================================
extern long CheckAndReadDigitizer(int BoardNumber, int SendNextEmpty, int globQueueUsageFlag)
{
int tempval;	//temporary holder for data read from VME
long NumBytesTransferred;
int TransferFifoStatus;		//for status return of DigitizerTypeFHEader()




	if(inloop_debug_level >= 2) printf("inLoopSupport: CheckAndReadDigitizer\n");

	start_profile_counter(PROF_IL_CHECK_AND_READ_DIG);

	// Status of the FIFO of the digitizer is all contained in the 'programming done' register:
	//
	//regin_programming_done(18 downto 0) <= external_fifo_depth(18 downto 0);
	//regin_programming_done(19) <= FIFO_PROG_FLAG;	--added 20160520	
	//regin_programming_done(20) <= ext_fifo_empty(0);
	//regin_programming_done(21) <= ext_fifo_empty(1);
	//regin_programming_done(22) <= ext_fifo_almost_empty when((ext_fifo_empty = "00") and (ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
	//regin_programming_done(23) <= ext_fifo_half_full when((ext_fifo_almost_full = '0') and (ext_fifo_full = "00")) else '0';
	//regin_programming_done(24) <= ext_fifo_almost_full when(ext_fifo_full = "00") else '0';
	//regin_programming_done(25) <= ext_fifo_full(0);
	//regin_programming_done(26) <= ext_fifo_full(1);			
		
	tempval = *FIFOStatusReg[BoardNumber];	//read from 'programming done'
	if(tempval & ALL_FULL_MASK) DigitizerFull[BoardNumber] = 1; else DigitizerFull[BoardNumber] = 0;	//extract full flags
	if (tempval & ALMOST_FULL_MASK) DigitizerAlmostFull[BoardNumber] = 1; else DigitizerAlmostFull[BoardNumber] = 0;	//extract full flags
	if((tempval & EMPTY_MASK) == EMPTY_MASK) DigitizerEmpty[BoardNumber] = 1; else DigitizerEmpty[BoardNumber] = 0;	//extract empty flags
	if(inloop_debug_level >= 2) printf("inLoopSupport: FIFO flags: Full = %ld  Empty = %ld ProgFull = %d\n",DigitizerFull[BoardNumber],DigitizerEmpty[BoardNumber],(int)((tempval & 0x00080000)>> 19));
	DigitizerFifoDepth[BoardNumber] = (tempval & 0x7FFFF);   //bits 18:0 is the depth as provided by the firmware (in longwords).
	if(inloop_debug_level >= 2) printf("inLoopSupport: FIFO Depth %ld 32-bit words\n",DigitizerFifoDepth[BoardNumber]);
//*********************************************************************************/
//	These lines excised 20220727 by JTA as they appear to introduce a bug.
//  The comment dated 20200709 is believed to reference this if statement.
//      MBO 20220729: 20200709 comment in reference to "if (DigitizerFifoDepth[BoardNumber] < 0)"
//                    but as elaborated below, should is now irrelevant.

//	if ((DigitizerFifoDepth[BoardNumber] & 0x60000) != 0)
//		DigitizerFifoDepth[BoardNumber]  = -1 * (~(DigitizerFifoDepth[BoardNumber]  & 0x1FFFF));
/*********************************************************************************/


	//=======================================
	//Check for digitizer full error.  this should be impossible, means firmware error.
	//=======================================
	if (DigitizerFull[BoardNumber]) 
		{
		if(inloop_debug_level >= 0) printf("ERROR : FIFO OVERFLOW (firmware error) in board #%d\n",BoardNumber);
		TransferFifoStatus = DigitizerTypeFHeader(2, BoardNumber, globQueueUsageFlag);     //send error header
		//reset the FIFO by a) disabling the board  b) clearing the fifo  c) re-enabling the board.
		ResetAndReEnableDig(BoardNumber);
		if(inloop_debug_level >= 0) printf("FIFO RESET after overflow in board #%d\n",BoardNumber);
		stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
		return(-1);
		} //end if (DigitizerFull[BoardNumber])

	//=======================================
	//Check for digitizer almost full.  If this occurs it means throttle should be in use, and
	//it also means the data at the end of the FIFO won't be on an event boundary, so you have to reset.
	//=======================================
	if (DigitizerAlmostFull[BoardNumber]) 
		{
		//I_ErrorPrintf("ERROR : FIFO MAXED OUT (almost full set) in board #%d.  You should be using throttle.\n",BoardNumber);
		if(inloop_debug_level >= 1) printf("WARNING : Digitizer FIFO is full (almost full set) in board #%d.  You should be using throttle.\n",BoardNumber);
		TransferFifoStatus = DigitizerTypeFHeader(2, BoardNumber, globQueueUsageFlag);     //send error header
		} //end if (DigitizerFull[BoardNumber])

	//=======================================
	//if no overflow, then check if FIFO is asserting empty flags,
	//but depth of FIFO is not zero.  This would be an underflow error.
	//=======================================
	if (DigitizerEmpty[BoardNumber])
		{
		if(inloop_debug_level >= 0) printf("FIFO EMPTY in board #%d\n",BoardNumber);
		if (DigitizerFifoDepth[BoardNumber] != 0)
			{	
			if(inloop_debug_level >= 0) printf("InLoop:ERROR:FIFO underflow in board #%d. FIFO Depth counter claims:#%lX\n",BoardNumber, DigitizerFifoDepth[BoardNumber]);
			TransferFifoStatus = DigitizerTypeFHeader(2, BoardNumber, globQueueUsageFlag);     //send error header
			//reset the FIFO by a) disabling the board  b) clearing the fifo  c) re-enabling the board.
			ResetAndReEnableDig(BoardNumber);
			if(inloop_debug_level >= 0) printf("FIFO RESET after underflow in board #%d\n",BoardNumber);
			stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
			return(-2);
			}
		//=======================================
		//	Else case is the true empty - flags were set and counter was zero.
		//=======================================
		else
			{
			//	To reduce load on Ethernet, SendNextEmpty is a flag that causes
			//	reporting of true empty to once every 'n' cycles of inLoop.
			if (SendNextEmpty)
				{
				TransferFifoStatus = DigitizerTypeFHeader(0, BoardNumber, globQueueUsageFlag);		//send informational message that digitizer was empty
				}
			stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
			return(0);
			}
		}  //end If (DigitizerEmpty[BoardNumber])

	// MBO 20230416: It's completely legal and expected to encounter the dig FIFO not empty with the DigitizerFifoDepth still equal to 0.
	//		Note that this condition means than the arrivial of an event that can be read is imminent, and will be avilable
	//		on the next read pass.  As such may not be necessary to fire off the "true empty" F header report, as we know it's not
	//		not truly empty, and IF the digitizer is working properly that event will be sent on the next scan cycle.
	//		However, I will send the 'F' anyway, as this also could be an indication of a digitizer firmware problem.
	if (DigitizerFifoDepth[BoardNumber] == 0)
		{
		// NOT AN ERROR, but I want to see it during verification testing.
		if(inloop_debug_level >= 1) printf("No data ready, but FIFO not EMPTY in board #%d\n",BoardNumber);
		if (SendNextEmpty)
			{
			TransferFifoStatus = DigitizerTypeFHeader(0, BoardNumber, globQueueUsageFlag);		//send informational message that digitizer was empty
			}
		stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
		return(0);
		}

	//=======================================
	// if neither empty nor overflowed, read data.  As of 20230412 the buffer is big enough to eat an entire FIFO, so checks
	// that used to be here and looping construct to read multiple times into multiple buffers are commented out.
	//=======================================
	if(inloop_debug_level >= 2) printf("inLoop: FIFO of module #%d has %lu words in it\n",BoardNumber, DigitizerFifoDepth[BoardNumber]);

//	if(inloop_debug_level >= 2) printf("inLoop: longwords available (%lu) > max buffer size (%u); read limited to %d bytes\n",DigitizerFifoDepth[BoardNumber],MAX_DIG_RAW_XFER_SIZE,MAX_DIG_RAW_XFER_SIZE);
	TransferFifoStatus = transferDigFifoData(BoardNumber, DigitizerFifoDepth[BoardNumber], globQueueUsageFlag, &NumBytesTransferred);

	if (TransferFifoStatus == Success) 
		{
		if(inloop_debug_level >= 2) printf("inLoop: readout of FIFO complete\n"); 
		stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
		return(NumBytesTransferred);
		}
	else if (TransferFifoStatus == NoBufferAvail)
		{
		if(inloop_debug_level >= 0) printf("inLoop: read of FIFO returned NoBufferAvail");
		stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
		return(-3);
		}		
	else 
		{
		if(inloop_debug_level >= 0) printf("inLoop: DMAError: you're screwed\n");
		stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
		return(-3);
		}

	stop_profile_counter(PROF_IL_CHECK_AND_READ_DIG);
	return(-4);		//should be impossible to get here.
}

//==============================================================================
//	ClearTrigFIFO() uses direct memory addressing
//	to clear the FIFO of a trigger module.  The 
//	second argument is the "fifo number".  The mapping within 
//	the firmware is
//
//	bits 07:00 clear Monitor FIFO (n), respectively (bit 3 clears MON_FIFO3)
//	bits 15:08 clear Channel FIFO (n), respectively (bit 12 clears CHAN_FIFO7)
//
//	Argument FIFO_index is a value from 0 to 15.
//==============================================================================
extern void ClearTrigFIFO(int BoardNumber, int FIFO_index)
{
int *p;
int MON_FIFO_STAT, CHAN_FIFO_STAT;

	if(inloop_debug_level >= 2) printf("inLoop: ClearTrigFIFO: Reset FIFO %d of %s module #%d\n",FIFO_index,BoardTypeNames[daqBoards[BoardNumber].board_type], BoardNumber);
	//read full/empty status of FIFOs
	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON_FIFO_STATE_REG);
	MON_FIFO_STAT = *p;
	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_CHAN_FIFO_STATE_REG);
	CHAN_FIFO_STAT = *p;
	if(inloop_debug_level >= 2) printf("inLoop: ClearTrigFIFO: before clear Mon FIFO stat %04X  Chan FIFO stat %04X\n",MON_FIFO_STAT, CHAN_FIFO_STAT);

	//clear the FIFO of this board (pulsed control register at address 0x08F0)
	*FIFO_RESET_REG[BoardNumber] = (1 << FIFO_index);	//set the reset bit for the selected FIFO in the trigger
	*FIFO_RESET_REG[BoardNumber] = (0);	//and then clear the bit to release the reset

	//read full/empty status of FIFOs
	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON_FIFO_STATE_REG);
	MON_FIFO_STAT = *p;
	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_CHAN_FIFO_STATE_REG);
	CHAN_FIFO_STAT = *p;
	if(inloop_debug_level >= 2) printf("inLoop: ClearTrigFIFO: after clear Mon FIFO stat %04X  Chan FIFO stat %04X\n",MON_FIFO_STAT, CHAN_FIFO_STAT);
	
	taskDelay(5);
}



//==============================================================================
//	CheckAndReadTrigger() tests a trigger to see if there is data available.
//	If so, the data is read out.  If not, a Type F header may be manufactured
//	to alert the back end of the DAQ that the trigger was checked, but that 
//	there was no data.  This is controlled by a flag variable that limits the
//	number of Type F headers noting empty that may be sent.
//
//	A control flag variable SendNextEmpty is passed in by inLoop.st, so that
//	the state machine may control the rate of empty messages to the back end.
//
//	If the trigger's FIFO Full flag is set, this spawns a Type F error message.
//	IF the trigger's FIFO Empty flag is set, but the FIFO depth count is not zero,
//		this also spawns a type F error message.
//
//	Function returns the number of bytes transferred, if transfer occurred.
//		return of 0 means no data, but no error; trigger fifo was empty.
//		return of -1 means full error.
//		return of -2 means empty/depth mismatch.
//		return of -3 means DMA error.
//		return of -4 means function logical error.
//		return of -5 means FIFO synch error; board not empty, but amount of data in FIFO < 1 event.
//
//	Trigger module FIFO details
//
//		The MON7_FIFO_STATE register at address 0x01B4 provides 8 bits of status.
//			7 : underflow
//			6 : overflow
//			5 : full
//			4 : almost full
//			3 : over prog full threshold
//			2 : under prog empty threshold
//			1 : almost empty
//			0 : empty
//
//		The MON7_FIFO_DEPTH is at address 0x0154.  This is a 16 bit number.  (this is the LIVE depth)
//		The LATCHED depth (modulo event size) is at 0x01AC.
//
//	The full/empty status of all FIFOs is found in the MON_FIFO_STATE (0x01A0) and
//	CHAN_FIFO_STATE (0x01A4) registers.  Each FIFO has a pair of bits:
//
//	15	14	13	12	11	10	09	08	07	06	05	04	03	02	01	00
//  F7 	E7	F6	E6	F5	E5	F4	E4	F3	E3	F2	E2	F1	E1	F0	E0
//
//	The MON FIFOs read out from addresses 0x0160 - 0x017C (MON FIFO 1 to 8)
//	The CHAN FIFOs read out from addresses 0x0180 - 0x019C (MON FIFO 1 to 8)

// Fifo Index Cheat sheet for trigger modules
//		address		name			index
//		0x0160,		//MON FIFO 1	0
//		0x0164,		//MON FIFO 2	1
//		0x0168,		//MON FIFO 3	2
//		0x016C,		//MON FIFO 4	3
//		0x0170,		//MON FIFO 5	4
//		0x0174,		//MON FIFO 6	5
//		0x0178,		//MON FIFO 7	6		<==the usual FIFO for most applications.
//		0x017C,		//MON FIFO 8	7
//		0x0180,		//CHAN FIFO 1	8
//		0x0184,		//CHAN FIFO 2	9
//		0x0188,		//CHAN FIFO 3	10
//		0x018C,		//CHAN FIFO 4	11
//		0x0190,		//CHAN FIFO 5	12
//		0x0194,		//CHAN FIFO 6	13
//		0x0198,		//CHAN FIFO 7	14
//		0x019C		//CHAN FIFO 8	15


//==============================================================================
extern long CheckAndReadTrigger(int BoardNumber, int FifoNum, int SendNextEmpty, int globQueueUsageFlag)
{
int tempval;	//temporary holder for data read from VME
int FullFlag, EmptyFlag;
int *ptr;
long NumBytesTransferred;
long NumBytesLive=0; //20250602 Ryan
int TransferFifoStatus;		//for status return of TriggerTypeFHEader()
long NumBytesToRead = 0;

	if(inloop_debug_level >= 2) printf("inLoopSupport: CheckAndReadTrigger\n");

	start_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
	if(FifoNum < 8)	//monitor fifo
		ptr = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON_FIFO_STATE_REG);
	else
	  ptr = (int *)(daqBoards[BoardNumber].base32 + MTRG_CHAN_FIFO_STATE_REG);

	tempval = *ptr;	//read full/empty status
	//now strip out bits of interest
	FullFlag = tempval & (2 << (FifoNum*2));
	if(FullFlag)  TriggerFull[BoardNumber] = 1; else TriggerFull[BoardNumber] = 0;	//extract full flags

	EmptyFlag = tempval & (1 << (FifoNum*2));
	if(EmptyFlag) TriggerEmpty[BoardNumber] = 1; else TriggerEmpty[BoardNumber] = 0;	//extract empty flags

	//=======================================
	//Check for Trigger full error, but only if it is MON_FIFO7.
	//We expect the other FIFOs to usually be full when read, so that's not an error.
	//=======================================
	if (FifoNum == 6)
		{
		if (TriggerFull[BoardNumber]) 
			{
			if(inloop_debug_level >= 2) printf("ERROR : FIFO OVERFLOW in board #%d\n",BoardNumber);
			TransferFifoStatus = TriggerTypeFHeader(2, FifoNum, BoardNumber, globQueueUsageFlag);     //send error header
			//reset the FIFO
			ClearTrigFIFO(BoardNumber, FifoNum);
			if(inloop_debug_level >= 2) printf("FIFO RESET after overflow in board #%d\n",BoardNumber);
			stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
			return(-1);
			} //end if (TriggerFull[BoardNumber])
		} //end if (FifoNum == 6)

	//=======================================
	//Check for empty.
	//=======================================
	if (TriggerEmpty[BoardNumber])
		{
			if(inloop_debug_level >= 2) printf("FIFO EMPTY in board #%d\n",BoardNumber);
			//	To reduce load on Ethernet, SendNextEmpty is a flag that causes
			//	reporting of true empty to once every 'n' cycles of inLoop.
			if (SendNextEmpty)
				{
				TransferFifoStatus = TriggerTypeFHeader(0, FifoNum, BoardNumber, globQueueUsageFlag);		//send informational message that trigger was empty
				}
			stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
			return(0);
		}  //end If (TriggerEmpty[BoardNumber])


	//=======================================
	//If the FIFO number is 6 (MON_FIFO7), read the actual depth counter.
	//For any other FIFO number, assume there are 256 words.
	//=======================================
	if(FifoNum == 6)
		{
		ptr = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON7_LATCHED_DEPTH);		//this is the at-event-boundaries latched counter
		NumBytesToRead = *ptr;	//value as returned from trigger is number of VME reads you should do
		ptr = (int *)(daqBoards[BoardNumber].base32 + MTRG_MON7_LIVE_DEPTH);		//this is the actual number of words
        NumBytesLive = *ptr;
		}
	else
		{
		NumBytesToRead = 256;	//This is the depth of most trigger FIFOs, in VME reads.
		}


	  NumBytesToRead = NumBytesToRead  * 4;	//convert VME reads to BYTES
	  NumBytesLive = NumBytesLive  * 4;	//convert VME reads to BYTES


	//=======================================
	// if neither error traps, and you get this far, suck data.
	//=======================================
	  if(inloop_debug_level >= 2) printf("inLoop: FIFO %d of module #%d: read depth (numBytesToRead) %ld Bytes, live depth %ld Bytes\n",FifoNum, BoardNumber, NumBytesToRead, NumBytesLive);

	  if (NumBytesToRead == 0) 
		{
		printf("CheckAndReadTrigger: ERROR: Depth = 0, EmptyFlag = %d  FullFlag = %d\n",TriggerEmpty[BoardNumber],TriggerFull[BoardNumber]);
		TransferFifoStatus = TriggerTypeFHeader(0, FifoNum, BoardNumber, globQueueUsageFlag);		//send informational message that trigger was empty
		stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
		return(0);
		}

		do
			{
//                           int transferTrigFifoData(int bdnum, long numlongwords, int FifoNum, int QueueUsageFlag, long *NumBytesTransferred)
			TransferFifoStatus = transferTrigFifoData(BoardNumber, NumBytesToRead/4, FifoNum, globQueueUsageFlag, &NumBytesTransferred); // Ryan 20250429, 1 word = 4 Bytes
			} while (TransferFifoStatus == NoBufferAvail);   //keep trying until a buffer is used

		if (TransferFifoStatus == Success) 
			{
			if(inloop_debug_level >= 2) printf("inLoop: readout of FIFO complete\n"); 
			stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
			return(NumBytesTransferred);
			}
		else 
			{
			if(inloop_debug_level >= 0) printf("inLoop: DMAError: you're screwed\n");
			stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
			return(-3);
			}


stop_profile_counter(PROF_IL_CHECK_AND_READ_TRIG);
return(-4);		//should be impossible to get here.
}





// SendEndOfRun() is called to get the timestamp and send a Type F message announcing same.
extern void SendEndOfRun(int BoardNumber, int globQueueUsageFlag)
{
int tempval;	//temporary holder for data read from VME
int TransferFifoStatus;		//for status return of DigitizerTypeFHEader()

	if(inloop_debug_level >= 2) printf("inLoopSupport: SendEndOfRun\n");

	tempval = *FIFOStatusReg[BoardNumber];	//read from 'programming done'
	if(inloop_debug_level >= 2) printf("SendEndOfRun: after drain FIFO status = %08X\n",tempval);
	TransferFifoStatus = DigitizerTypeFHeader(1, BoardNumber, globQueueUsageFlag);		//send 'end of run' header.
}

float UpdateScanDelay(void /*float current_delay*/)
	{
	// MBO 20200616: adding scan delay feedback loop.
float new_delay;
int used_buf_count;	

	if(inloop_debug_level >= 2) printf("inLoopSupport: UpdateScanDelay\n");
	// MBO 20200616: Well, this feedback loop is more like an open loop, as it's going to ignore the current_delay unless we find a reason to use it.
	used_buf_count = RAW_Q_SIZE - getFreeBufCount();
	// MBO 20200616: Try something linear.  divideing by SENDER_BUF_BYPASS_THRESHOLD instead of RAW_Q_SIZE, since nominally SENDER_BUF_BYPASS_THRESHOLD represents the
	//		maximum buffer utilization.  Any usageless than the SCAN_LOOP_THROTTLE_FREE_THRESHOLD, will result in the minimum delay.
	new_delay = (float)(SCAN_LOOP_MAXIMUM_DELAY) * ((float)(used_buf_count) - (float)(SCAN_LOOP_THROTTLE_FREE_THRESHOLD)) / (float)(SENDER_BUF_BYPASS_THRESHOLD);
	// MBO 20200616: Check if we are below the minimum delay.  If so se the minimum delay.
	new_delay = new_delay < (float)(SCAN_LOOP_MINIMUM_DELAY) ? (float)(SCAN_LOOP_MINIMUM_DELAY) : new_delay;
	return new_delay;
	}

//===================================
//	Debugging tool
//===================================
extern void DumpInLoopArrays(void)
{
int i;
	if(inloop_debug_level >= 2) printf("\nDiagnostic dump of InLoop arrays\n");
	for(i=0;i<INLOOP_MAX_BOARD_NUMBER;i++)
		{
		if(inloop_debug_level >= 2) printf("\n\nModule index #%d\n",i);
		if(inloop_debug_level >= 2) printf("Base Address 0x%X, OK check %d Readout Enable %d, Usr Pkg %d\n",(int)daqBoards[i].base32,daqBoards[i].mainOK,daqBoards[i].EnabledForReadout,daqBoards[i].DigUsrPkgData);
		if(inloop_debug_level >= 2) printf("MstrLogicReg ptr : 0x%X, ProgDone ptr: 0x%X, RawDatWin[0] ptr: 0x%X\n",(int)MstrLogicReg[i],(int)FIFOStatusReg[i],(int)RawDataLengthReg[i][0]);
		if(inloop_debug_level >= 2) printf("CalcEvtSize(0) : %ld, MaxToRead : %ld, Full : %ld  Empty : %ld FifoDepth : 0x%lX\n",DigitizerCalcEventSize[i][0],MaxEventsToRead[i],DigitizerFull[i],DigitizerEmpty[i],DigitizerFifoDepth[i]);
		}
}

extern void CloseDumpFiles(void)
{
#ifdef SM_PRINT_TO_FILE
	if (IL_FILE != NULL)
		{
		fclose(IL_FILE);
		IL_FILE = NULL;
		}

	if (OL_FILE != NULL)
		{
		fclose(OL_FILE);
		OL_FILE = NULL;
		}

	if (MS_FILE != NULL)
		{
		fclose(MS_FILE);
		MS_FILE = NULL;
		}

	if(inloop_debug_level >= 2) printf("IL_FILE, OL_FILE and MS_FILE closed\n");
#else
	if(inloop_debug_level >= 2) printf("SM_PRINT_TO_FILE not defined, nothing to do\n");
#endif
}


//==============================================================================
//	SetTrigSoftwareVeto() uses direct memory addressing
//	to turn on the Software Veto (bit 11 of the Trigger Mask register)
//==============================================================================
// MBO 20250904: Remove the software veto from the ioc, as this code is causing
//               problems.   This is likely triggering a firmware bug, but as
//               we are running in 24~48 hours, will attempt to not trigger the
//               firmware bug by removing this logic.
//               In the long run, we do not want the IOC to be toggling enables
//               or chaning the system configuraitions.
//
//               Any software veto should be implemented in the softIOC.
//
//extern void SetTrigSoftwareVeto(int BoardNumber)
//{
//int tempaddr;
//int *p = NULL;
//unsigned int Trig_mask_val = 0;
//
//	if(inloop_debug_level >= 2) printf("inLoopSupport:SetTrigSoftwareVeto (Veto = ON)\n");
//	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_TRIG_MASK_REG);		//address offset of trigger's TRIG_MASK register
//	tempaddr = (int)p;
//	if(inloop_debug_level >= 2) printf("inLoopSupport:SetTrigSoftwareVeto : tempaddr=%08X\n", tempaddr);
//	Trig_mask_val = *p;		//read the register
//	Trig_mask_val |= 0x0800;	//force bit 11 to be set
//	*p = Trig_mask_val;		//write modified value back
//}

//==============================================================================
//	ClearTrigSoftwareVeto() uses direct memory addressing
//	to turn on the Software Veto (bit 11 of the Trigger Mask register)
//==============================================================================
// MBO 20250904: Remove the software veto from the ioc, as this code is causing
//               problems.   This is likely triggering a firmware bug, but as
//               we are running in 24~48 hours, will attempt to not trigger the
//               firmware bug by removing this logic.
//               In the long run, we do not want the IOC to be toggling enables
//               or chaning the system configuraitions.
//
//               Any software veto should be implemented in the softIOC.
//
//extern void ClearTrigSoftwareVeto(int BoardNumber)
//{
//int tempaddr;
//int *p = NULL;
//unsigned int Trig_mask_val = 0;
//
//	if(inloop_debug_level >= 2) printf("inLoopSupport:ClearTrigSoftwareVeto\n");
//	p = (int *)(daqBoards[BoardNumber].base32 + MTRG_TRIG_MASK_REG);		//address offset of trigger's TRIG_MASK register
//	tempaddr = (int)p;
//	if(inloop_debug_level >= 2) printf("inLoopSupport:ClearTrigSoftwareVeto : tempaddr=%08X\n", tempaddr);
//	Trig_mask_val = *p;		//read the register
//	Trig_mask_val &= 0xF7FF;	//force bit 11 to be clear
//	*p = Trig_mask_val;		//write modified value back
//}

