#include <epicsTypes.h>
#include <epicsMessageQueue.h>
#include <epicsTime.h>
//#include "asynPortDriver.h"
#include "asynTrigCommonDriver.h"

class epicsShareFunc asynTrigMasterDriver : public asynTrigCommonDriver
{
public:
    asynTrigMasterDriver(const char *portName, int card_number);

//
// This is where we define parameters
//
#include "asynMTrigParams.h"
// 
// end paramaters
//

};
    
