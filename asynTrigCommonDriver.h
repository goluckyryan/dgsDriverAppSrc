#include <epicsTypes.h>
#include <epicsMessageQueue.h>
#include <epicsTime.h>
#include "asynPortDriver.h"

void asynTrigCommonDriver_Task(void *drvPvt);



class epicsShareFunc asynTrigCommonDriver : public asynPortDriver{
public:
    asynTrigCommonDriver(
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


	int getIntParam(int param);

		void simTask(void);	

	int viOut32 (int slot, int adr_space, int reg_adr, int data);
	int viIn32 (int slot, int adr_space, int reg_adr, int *data);

		
	  char driverName[512];	
		
//
// This is where we define parameters
// They are all in the subclasses for router/master, and in include files

int run_counter;


// 
// end paramaters
//

// num of oarams defined in here... above, see?
enum {num_common_params = 1};


int card_number;


enum {VI_A32_SPACE=0};
 
    
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
  
    
    int flipEndian(int val);

   
    };
    
