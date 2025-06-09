// Author: Michael Oberling

#include <string.h>
#include <stdio.h>
#include <vxLib.h>

#include "profile.h"

// Set this to whatever the clock/timer object returns  
// Often not the same as TIME
typedef long long int CLOCK_TIME;

typedef enum {
	COUNTER_DISABLED,
	COUNTER_RUNNING,	
#ifdef PAUSABLE_PROFILER
	COUNTER_SUSPENDED,		// From context switch
	COUNTER_PAUSED,			// From call to pause function
#endif
	COUNTER_STOPPED			// From call to pause function
} COUNTER_STATE;

static double profile_clock_frequency;		// the profile clock tick frequency

static CLOCK_TIME start_time;		// set at init
static CLOCK_TIME current_time;		//
static TIME run_time;			// time since init

static char profiler_name[NUM_PROFILE_COUNTERS][MAX_NAME_LENGTH];

#ifndef NO_PROFILING
static CLOCK_TIME time_start[NUM_PROFILE_COUNTERS];		// the time a counter was started or resumed. 
static CLOCK_TIME time_stop[NUM_PROFILE_COUNTERS];		// the time a counter was stopped or paused.
#endif
static CLOCK_TIME cal_time_start[NUM_PROFILE_COUNTERS];		// used to compensate for function call time
static CLOCK_TIME cal_time_stop[NUM_PROFILE_COUNTERS];		// used to compensate for function call time
static TIME cycle_delta_time[NUM_PROFILE_COUNTERS];		// processing time used in the last start->stop period
static TIME total_time[NUM_PROFILE_COUNTERS];			// all time accumulated by a given profile counter
static TIME num_cycles[NUM_PROFILE_COUNTERS];			// number of start->stop cycles
static COUNT prescale[NUM_PROFILE_COUNTERS];			// profile counters only run 1 in n starts.  this stores n
static COUNT prescale_count[NUM_PROFILE_COUNTERS];		// used to tract when an profile counter actually runs (see above)
static COUNTER_STATE counter_state[NUM_PROFILE_COUNTERS];
#ifdef PAUSABLE_PROFILER
	static COUNT num_task_switches[NUM_PROFILE_COUNTERS];		// total number of time a given profiled block of code has been interrupted by a task switch.
	static COUNT cycle_num_task_switches[NUM_PROFILE_COUNTERS];	// number of time a given profiled block of code has been interrupted by a task switch on the current execution.
	#ifndef NO_PROFILING
	static CLOCK_TIME task_switch_time;
	#endif
	static CLOCK_TIME cal_task_switch_time;
	static WIND_TCB* counter_task[NUM_PROFILE_COUNTERS];
	static TIME cycle_time[NUM_PROFILE_COUNTERS];
	static TIME cycle_cal_time[NUM_PROFILE_COUNTERS];
//	static unsigned int suspend_on_task_switch_enabled[NUM_PROFILE_COUNTERS];
	
	static CLOCK_TIME time_start_real_time[NUM_PROFILE_COUNTERS];		// real time version of: the time a counter was started or resumed. 
	static CLOCK_TIME cal_time_start_real_time[NUM_PROFILE_COUNTERS];	// real time version of: used to compensate for function call time
	static TIME cycle_delta_time_real_time[NUM_PROFILE_COUNTERS];		// real time version of: processing time used in the last start->stop period
	static TIME total_time_real_time[NUM_PROFILE_COUNTERS];			// real time version of: all time accumulated by a given profile counter
#endif


inline CLOCK_TIME get_profile_clock_time (void);
inline TIME get_clock_delta (CLOCK_TIME start_time, CLOCK_TIME stop_time);
void start_profile_cal_counter(unsigned char counter_index);
void stop_profile_cal_counter(unsigned char counter_index);

inline CLOCK_TIME get_profile_clock_time (void) {
	unsigned int UL;
	unsigned int LL;
	vxTimeBaseGet(&UL, &LL);
	return ((CLOCK_TIME)(UL) << (CLOCK_TIME)(32)) | (CLOCK_TIME)(LL);
}

inline TIME get_clock_delta (CLOCK_TIME start_time, CLOCK_TIME stop_time) {
	return stop_time - start_time;
}

// Initialize the profiling system.
void init_profile_counters(double clock_frequency)
{
	unsigned char i;
	taskLock();
	for (i = 0; i < NUM_PROFILE_COUNTERS; i++)
	{
		cycle_delta_time[i] = 0;
		total_time[i] = 0;
		num_cycles[i] = 0;
		prescale[i] = 1;
		prescale_count[i] = 0;
		counter_state[i] = COUNTER_DISABLED;
		#ifdef PAUSABLE_PROFILER
			num_task_switches[i]  = 0;
			cycle_num_task_switches[i] = 0;
			counter_task[i] = (WIND_TCB*)(0); //NULL
			cycle_time[i] = 0;
			cycle_cal_time[i] = 0;

			time_start_real_time[i] = 0;
			cal_time_start_real_time[i] = 0;
			cycle_delta_time_real_time[i] = 0;
			total_time_real_time[i] = 0;
//			suspend_on_task_switch_enabled[i] = 1;
		#endif
	}
	profile_clock_frequency = clock_frequency;
	taskUnlock();
	return;
}

void run_profile_counters(void)
{
	unsigned char i;
	taskLock();
	for (i = 0; i < NUM_PROFILE_COUNTERS; i++)
	{
		counter_state[i] = COUNTER_STOPPED;
	}
	start_time = get_profile_clock_time();
	current_time = get_profile_clock_time();
	run_time = get_clock_delta(start_time, current_time);
	taskUnlock();
	return;
}

// Called at the start of a profiled block of code.
void start_profile_counter(unsigned char counter_index)
{
	#ifndef NO_PROFILING
	taskLock();
	if (counter_state[counter_index] != COUNTER_DISABLED)
	{
		if (prescale_count[counter_index] == 0)
		{
			start_profile_cal_counter(counter_index);
			time_start[counter_index] = get_profile_clock_time();
			#ifdef PAUSABLE_PROFILER
				time_start_real_time[counter_index] = time_start[counter_index];
			#endif
			counter_state[counter_index] = COUNTER_RUNNING;
        	}
	}
	taskUnlock();
	#endif
	return;
}

// Called at the end of a profiled block of code.
void stop_profile_counter(unsigned char counter_index)
{
	#ifndef NO_PROFILING
	taskLock();
	if (counter_state[counter_index] == COUNTER_RUNNING)
	{
		if (prescale_count[counter_index] == 0)
		{
			time_stop[counter_index] = get_profile_clock_time();
			stop_profile_cal_counter(counter_index);
			counter_state[counter_index] = COUNTER_STOPPED;
			#ifdef PAUSABLE_PROFILER
				cycle_time[counter_index] += get_clock_delta(time_start[counter_index], time_stop[counter_index]);
				cycle_cal_time[counter_index] += get_clock_delta(cal_time_start[counter_index], cal_time_stop[counter_index]);
				cycle_delta_time[counter_index] = cycle_time[counter_index] * (TIME)(prescale[counter_index]);
				cycle_delta_time[counter_index] -= (cycle_cal_time[counter_index] * (TIME)(prescale[counter_index]) - cycle_delta_time[counter_index]) / 2;
				num_task_switches[counter_index] += cycle_num_task_switches[counter_index];

				cycle_delta_time_real_time[counter_index] = get_clock_delta(time_start_real_time[counter_index], time_stop[counter_index]) * (TIME)(prescale[counter_index]);
				cycle_delta_time_real_time[counter_index] -= (get_clock_delta(cal_time_start_real_time[counter_index], cal_time_stop[counter_index])* (TIME)(prescale[counter_index]) - cycle_delta_time_real_time[counter_index]) / 2;
				
				cycle_time[counter_index] = 0;
				cycle_cal_time[counter_index] = 0;
				cycle_num_task_switches[counter_index] = 0;
			#else
				cycle_delta_time[counter_index] = get_clock_delta(time_start[counter_index], time_stop[counter_index]) * (TIME)(prescale[counter_index]);
				cycle_delta_time[counter_index] -= (get_clock_delta(cal_time_start[counter_index], cal_time_stop[counter_index])* (TIME)(prescale[counter_index]) - cycle_delta_time[counter_index]) / 2;
			#endif
			if (counter_state)
			{
				total_time[counter_index] += cycle_delta_time[counter_index];
				#ifdef PAUSABLE_PROFILER
					total_time_real_time[counter_index] += cycle_delta_time_real_time[counter_index];
				#endif
				num_cycles[counter_index] += prescale[counter_index];
			}
			prescale_count[counter_index] = prescale[counter_index];
		}
		prescale_count[counter_index]--;
	}
	taskUnlock();
	#endif
	return;
}

#ifdef PAUSABLE_PROFILER
// Called to pause the profile timer.  Use this when the profiled block of code is not continuous, or has periods of sleep.
// Do not use this before calling start_profile_counter();
// Do not use this at the end of a profiled block of code.  Use stop_profile_counter() instead.
void pause_profile_counter(unsigned char counter_index)
{
	#ifndef NO_PROFILING
	taskLock();
	if (counter_state[counter_index] == COUNTER_RUNNING)
	{
		if (prescale_count[counter_index] == 0)
		{
			time_stop[counter_index] = get_profile_clock_time();
			stop_profile_cal_counter(counter_index);
			counter_state[counter_index] = COUNTER_PAUSED;
			cycle_time[counter_index] += get_clock_delta(time_start[counter_index], time_stop[counter_index]);
			cycle_cal_time[counter_index] += get_clock_delta(cal_time_start[counter_index], cal_time_stop[counter_index]);
		}
	}
	taskUnlock();
	#endif
	return;
}

// Called to resume the profile timer.
// Do not use this before calling pause_profile_counter();
// Do not use this at the start of a profiled block of code.  Use start_profile_counter() instead.
void resume_profile_counter(unsigned char counter_index)
{
	#ifndef NO_PROFILING
	taskLock();
	if (counter_state[counter_index] == COUNTER_PAUSED)
		if (prescale_count[counter_index] == 0)
		{
			start_profile_cal_counter(counter_index);
			time_start[counter_index] = get_profile_clock_time();
			counter_state[counter_index] = COUNTER_PAUSED;
        	}
	taskUnlock();
	#endif
	return;
}
#endif


void start_profile_cal_counter(unsigned char counter_index)
{	
	taskLock();
	if (counter_state[counter_index] != COUNTER_DISABLED)
	{
		if (prescale_count[counter_index] == 0)
		{
			cal_time_start[counter_index] = get_profile_clock_time();
			#ifdef PAUSABLE_PROFILER
				cal_time_start_real_time[counter_index] = cal_time_start[counter_index];
			#endif
		}
	}
	taskUnlock();
	return;
}

void stop_profile_cal_counter(unsigned char counter_index)
{
	taskLock();
	if (counter_state[counter_index] != COUNTER_DISABLED)
	{
		if (prescale_count[counter_index] == 0)
		{
			cal_time_stop[counter_index] = get_profile_clock_time();
		}
	}
	taskUnlock();
	return;
}

#ifdef PAUSABLE_PROFILER
void update_cal_task_switch_time(void)
{
	cal_task_switch_time = get_profile_clock_time();
	return;
}


void profile_counter_task_switch_hook(WIND_TCB* pOldTcb, WIND_TCB* pNewTcb)
{
	#ifndef NO_PROFILING
	int i;
	update_cal_task_switch_time();
	task_switch_time = get_profile_clock_time();
	for (i = 0; i < NUM_PROFILE_COUNTERS; i++)
	{
		if (counter_state[i] == COUNTER_RUNNING)// && (suspend_on_task_switch_enabled[i]))
		{
			counter_state[i] = COUNTER_SUSPENDED;
			counter_task[i] = pOldTcb;
			time_stop[i] = task_switch_time;
			cycle_num_task_switches[i]++;
			cal_time_stop[i] = cal_task_switch_time;
			cycle_time[i] += get_clock_delta(time_start[i], time_stop[i]);
			cycle_cal_time[i] += get_clock_delta(cal_time_start[i], cal_time_stop[i]);
		}
	}
	update_cal_task_switch_time();
	task_switch_time = get_profile_clock_time();
	for (i = 0; i < NUM_PROFILE_COUNTERS; i++)
	{
		if ((counter_state[i] == COUNTER_SUSPENDED) && (pNewTcb == counter_task[i]))
		{
			counter_state[i] = COUNTER_RUNNING;
			// Unavoidable error.  No way to know how long the task switch process to finish.
			// This should be a small error in vxWorks, and insignificant over many measurements.
			time_start[i] = task_switch_time;
			cal_time_start[i] = cal_task_switch_time;
		}
	}
	#endif
	return;
}
#endif

TIME get_profile_counter_total_time(unsigned char counter_index)
{
	return total_time[counter_index];
}

TIME get_profile_counter_last_cycle_time(unsigned char counter_index)
{
	return cycle_delta_time[counter_index];
}

TIME get_profile_counter_num_cycles(unsigned char counter_index)
{
	return num_cycles[counter_index];
}

//#ifndef PAUSABLE_PROFILER
void init_profile_counter(unsigned char counter_index, const char* counter_name,  unsigned int profiler_prescale_value)
//#else
//void init_profile_counter(unsigned char counter_index, const char* counter_name,  unsigned int profiler_prescale_value, unsigned int can_be_suspended)
//#endif
{
	if(strlen(counter_name) < MAX_NAME_LENGTH)
		strcpy(profiler_name[counter_index], counter_name);
	else
	{
		strncpy(profiler_name[counter_index], counter_name, MAX_NAME_LENGTH - 2);
		profiler_name[counter_index][MAX_NAME_LENGTH - 1] = 0;
	}
	prescale[counter_index] = profiler_prescale_value;
//	#ifdef PAUSABLE_PROFILER
//		suspend_on_task_switch_enabled[counter_index] = can_be_suspended;
//	#endif
}

#ifndef NO_PROFILING
char* get_profile_counter_name(unsigned char counter_index)
{
	return profiler_name[counter_index];
}

TIME get_profile_counter_cycle_time(unsigned char counter_index,  unsigned int output_scale)
{
	return (num_cycles[counter_index] == 0) ? 0 : total_time[counter_index] * (TIME)(output_scale) /  num_cycles[counter_index];
}

void update_profile_counter_run_time(void)
{
	current_time = get_profile_clock_time();
	run_time = get_clock_delta(start_time, current_time);
	return;
}

TIME get_total_run_time(void)
{
	return run_time;
}

COUNT get_profile_counter_percent_time(unsigned char counter_index,  unsigned int scale)
{
	return (total_time[counter_index] * (TIME)(scale)) /  run_time;
}

#ifdef PAUSABLE_PROFILER
COUNT get_profile_counter_percent_real_time(unsigned char counter_index,  unsigned int scale)
{
	return (total_time_real_time[counter_index] * (TIME)(scale)) /  run_time;
}
#endif


COUNT get_profile_counter_task_switches_per_exec(unsigned char counter_index,  unsigned int scale)
{
	return (num_cycles[counter_index] == 0) ? 0 : ((TIME)(num_task_switches[counter_index]) * (TIME)(scale)) / num_cycles[counter_index];
}

double get_profile_counter_exec_second(unsigned char counter_index)
{
	return ((double)(num_cycles[counter_index]) * profile_clock_frequency) / (double)(run_time);
}
#endif

void print_profile_summary(void)
{
	#ifndef NO_PROFILING
	int i;
	update_profile_counter_run_time();

	printf(" %% CPU   %% REAL  TICKS  EXEC  T_SWP\n");
	printf(" TIME    TIME    \\EXEC  \\SEC  \\EXC\n");

	for (i = 0; i < NUM_PROFILE_COUNTERS; i++)
		printf("%06.2f%% %06.2f%% %07lu %05.1f %05.2f %s\n", (float)(get_profile_counter_percent_time(i,10000))/100.0, (float)(get_profile_counter_percent_real_time(i,10000))/100.0, (unsigned long int)(get_profile_counter_cycle_time(i,1)), get_profile_counter_exec_second(i), (float)(get_profile_counter_task_switches_per_exec(i,100))/100.0, get_profile_counter_name(i));
	#endif
}
