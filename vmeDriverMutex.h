
#ifndef _VMEDRIVERMUTEX_H
#define _VMEDRIVERMUTEX_H

/*
*
* this code is for a mutex shared across the asyn dig and trig drivers
* it allows locking the vme transactions so we can program the flash
* w/out any vme stuff going on. 
*/

extern volatile int is_vme_mutex_exist;
extern epicsMutexId vme_driver_mutex;
extern void initVmeDrvMutex(void);
void vmeMutexLock(int caller_id);
void vmeMutexUnLock(int caller_id);

extern volatile int is_readout_mutex_exist;
extern epicsMutexId readout_driver_mutex;
extern void initReadoutDrvMutex(void);
extern void readoutMutexLock(int caller_id);
extern void readoutMutexUnLock(int caller_id);
#endif



