//--------------------------------------------------------------------------------
// Company:		Argonne National Laboratory
// Division:	Physics
// Project:		DGS IOC
// Author:		Michael Oberling
// File:		profile.h
// Description: 
//--------------------------------------------------------------------------------
#ifndef PROFILE_H
#define PROFILE_H

//==============================
//---     Include Files     --- 
//==============================
#include <taskLib.h>
#include "DGS_DEFS.h"

//==============================
//---        Defines        --- 
//==============================
#define PAUSABLE_PROFILER
#define MAX_NAME_LENGTH 64		// maximum length of profile counter name

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
//Set this to whatever the clock/timer object returns
typedef long long int TIME;
typedef unsigned long int COUNT;

// Initialize the profiling system.
void init_profile_counters(double clock_frequency);
// Run the profiling counters
void run_profile_counters(void);

// Called at the start of a profiled block of code.
void start_profile_counter(unsigned char counter_index);

// Called at the end of a profiled block of code.
void stop_profile_counter(unsigned char counter_index);

#ifdef PAUSABLE_PROFILER
// Called to pause the profile timer.  Use this when the profiled block of code is not continuous, or has periods of sleep.
// Do not use this before calling start_profile_counter();
// Do not use this at the end of a profiled block of code.  Use stop_profile_counter() instead.
void pause_profile_counter(unsigned char counter_index);

// Called to resume the profile timer.
// Do not use this before calling pause_profile_counter();
// Do not use this at the start of a profiled block of code.  Use start_profile_counter() instead.
void resume_profile_counter(unsigned char counter_index);


void profile_counter_task_switch_hook(WIND_TCB* pOldTcb, WIND_TCB* pNewTcb);
#endif

/* Example:
	while(1)
	{
		// counter 0 will profile the entire loop.
		// counter 1 will profile the just the do_something() function.
		// counter 2 will profile the do_something_different() and the
		// do_something_interesting function, but not the sleep time.


		start_profile_counter(0);

		start_profile_counter(1);
		do_something();
		stop_profile_counter(1);

		do_something_important();

		start_profile_counter(2);
		do_something_different();
		pause_profile_counter(2);
		sleep_for_10ms();
		resume_profile_counter(2);
		do_something_interesting();
		stop_profile_counter(2);

		stop_profile_counter(0);
	}
*/

// Run this once per counter to give the counter a name and a prescale.  the prescale determins how often the profiler runs.
// This will improve performance at the cost of accuracy/runtime.  If the counters are pausable, then one must also state
// whether the counter can be automatically suspended during task switched.
//#ifndef PAUSABLE_PROFILER
void init_profile_counter(unsigned char counter_index, const char* counter_name, unsigned int profiler_prescale_value);
//#else
//void init_profile_counter(unsigned char counter_index, const char* counter_name, unsigned int profiler_prescale_value,  unsigned int can_be_suspended);
//#endif

// call this before outputting percentages.
void update_profile_counter_run_time(void);

#ifndef NO_PROFILING
TIME get_total_run_time(void);
TIME get_profile_counter_total_time(unsigned char counter_index);
TIME get_profile_counter_last_cycle_time(unsigned char counter_index);
TIME get_profile_counter_num_cycles(unsigned char counter_index);
TIME get_profile_counter_cycle_time(unsigned char counter_index, unsigned int output_scale);
COUNT get_profile_counter_percent_time(unsigned char counter_index, unsigned int percent_scale);
COUNT get_profile_counter_percent_real_time(unsigned char counter_index,  unsigned int scale);
COUNT get_profile_counter_task_switches_per_exec(unsigned char counter_index,  unsigned int scale);
double get_profile_counter_exec_second(unsigned char counter_index);
char* get_profile_counter_name(unsigned char counter_index);
#endif

void print_profile_summary(void);
#endif

