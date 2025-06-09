#include <epicsTypes.h>
#include <epicsMessageQueue.h>
#include <epicsTime.h>
#include "asynPortDriver.h"

extern "C" int devGVMECardInit(int cardno, int slot);//JTA: removed 'clocksource' 20190114


extern "C" int asynDigitizerConfig(
	const char *portName, 
	int card_number,
	int slot);
//	int clock_source); //JTA: removed 'clocksource' 20190114


extern "C" int devAsynDigCardInit(int cardno, int slot);  //JTA: removed 'clocksource' 20190114
extern "C" void asynDigReport(char *cmd);

void asynDigitizerDriver_Task(void *drvPvt);

class epicsShareFunc asynDigitizerDriver : public asynPortDriver{
public:
    asynDigitizerDriver(
	const char *portName, 
	int card_number);
	
	asynStatus readUInt32Digital(
		asynUser *pasynUser, 
		epicsUInt32 *value, 
		epicsUInt32 mask);
		
     	asynStatus writeUInt32Digital(
		asynUser *pasynUser, 
		epicsUInt32 value, 
		epicsUInt32 mask);


	//asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);

	//asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
	
	void simTask(void);
	int getIntParam(int param);
		
		

	int viOut32 (int slot, int adr_space, int reg_adr, int data);
	int viIn32 (int slot, int adr_space, int reg_adr, int *data);
	
	int flipEndian(int val);
		
	  char driverName[512];	
int run_counter;

// true to schedule hitting pulse chan control reg.
volatile bool is_hit_pcc;
// time counter- increment every 2 sec when task runs.
volatile int pcc_time_counter;
		
//
// This is where we define parameters
//




//there are now two separate params files, one for the digitizer main,
//one for the digitizer VME.  20240221  JTA	
#include "asynDigParams.h"
#include "asynDigParamsVME.h"

//apparently the above cannot work, so I am forced to manually merge the two generated params files together.
//  #include "MergedasynDigParams.h"
// 
// end parameters
//

int card_number;



//128*1024 is 131072


char msgx[512];
    
class int_int
{
public:
 int param_num;
 int address; 
};
    int_int *address_list;
    int param_address_cnt;
    
    void setAddress(int param, int address);
   
    
    int findAddress(int param);
  
    
    
    
    
 };
    
