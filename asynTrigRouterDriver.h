#include <epicsTypes.h>
#include <epicsMessageQueue.h>
#include <epicsTime.h>
#include "asynTrigCommonDriver.h"


class epicsShareFunc asynTrigRouterDriver : public asynTrigCommonDriver
{
public:
    asynTrigRouterDriver(
	const char *portName, 
	int card_number);



		
//
// This is where we define parameters
//

#include "asynRTrigParams.h"


// 
// end paramaters
//

 
    
    
   
    };
    
