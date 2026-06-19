/*! @file thread.h

*  @brief Threading tools
*
*  @version 1.0.0
*
*  (C) Copyright 2017 GoPro Inc (http://gopro.com/).
*
*  Licensed under either:
*  - Apache License, Version 2.0, http://www.apache.org/licenses/LICENSE-2.0  
*  - MIT license, http://opensource.org/licenses/MIT
*  at your option.
*
*  Unless required by applicable law or agreed to in writing, software
*  distributed under the License is distributed on an "AS IS" BASIS,
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*  See the License for the specific language governing permissions and
*  limitations under the License.
*
*/

#ifndef _THREAD_H
#define _THREAD_H

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef _WIN32
 #ifdef _DEBUG
  #include <tchar.h>				// For printing debug string in the console window
 #endif
#endif

// Maximum number of threads in a thread pool
#define THREAD_POOL_MAX		32

// Maximum of JOB depending of the completion of other job.
#define THREAD_JOB_LEVELS	8	//e.g. wavelet -> demosaic -> colorspace = 3 jobs

#define VERBOSE_DEBUG		0

typedef enum
{
	THREAD_ERROR_OKAY = 0,				// No error occurred
	THREAD_ERROR_CREATE_FAILED,			// Failed to create object
	THREAD_ERROR_JOIN_FAILED,			// Wait for thread failed
	THREAD_ERROR_INVALID_ARGUMENT,		// Bad argument passed to a thread routine
	THREAD_ERROR_WAIT_FAILED,			// Wait was abandoned or timed out
	THREAD_ERROR_BAD_STATE,				// Undefined event state
	THREAD_ERROR_DETACH_FAILED,			// Unable to detach a POSIX thread
	THREAD_ERROR_NOWORK,				// No more units of work available
	THREAD_ERROR_NOWORKYET,				// No units cuurently available
	THREAD_ERROR_WORKCOMPLETE,			// All units for all job are complete

} THREAD_ERROR;

// Events are either turned on (signalled) or off (cleared)
typedef enum
{
	EVENT_STATE_CLEARED = 0,
	EVENT_STATE_SIGNALLED = 1,

} EVENT_STATE;


#ifdef _WIN32

#include <windows.h>

// Macro for declaring routines in the threads API
#define THREAD_API(proc) \
	static __inline THREAD_ERROR proc

// Macro for declaring the thread procedure
#define THREAD_PROC(proc, data) \
	DWORD WINAPI proc(LPVOID data)

// Data type returned by the thread procedure
typedef DWORD THREAD_RETURN_TYPE;

// Declaration of the worker thread procedure
typedef DWORD (WINAPI * THREAD_PROC)(LPVOID data);

typedef DWORD TIMEOUT;

// The thread structs contain Windows handles
typedef struct
{
	HANDLE handle;
	DWORD id;

} THREAD;

typedef struct
{
	HANDLE handle;

} SEMAPHORE;

typedef struct
{
	HANDLE handle;

} EVENT;

typedef struct
{
	CRITICAL_SECTION mutex;

} LOCK;


THREAD_API(ThreadCreate)(THREAD *thread,
						 LPTHREAD_START_ROUTINE proc,
						 void *param)
{
	DWORD flags = 0;
	DWORD id;

	HANDLE handle = CreateThread(NULL, 128*1024, proc, param, flags, &id);

	if (handle != NULL)
	{
		thread->handle = handle;
		thread->id = id;
		return THREAD_ERROR_OKAY;
	}
	else
	{
		thread->handle = NULL;
		thread->id = 0;
		return THREAD_ERROR_CREATE_FAILED;
	}
}

THREAD_API(ThreadDelete)(THREAD *thread)
{
	CloseHandle(thread->handle);
	thread->handle = NULL;
	thread->id = 0;
	return THREAD_ERROR_OKAY;
}


THREAD_API(ThreadWait)(THREAD *thread)
{
	DWORD dwReturnValue = WaitForSingleObject(thread->handle, INFINITE);
	if (dwReturnValue != WAIT_OBJECT_0) {
		return THREAD_ERROR_WAIT_FAILED;
	}
	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaCreate)(SEMAPHORE *semaphore,
					   int32_t initial_count,
					   int32_t max_count)
{
	semaphore->handle = CreateSemaphore(NULL, initial_count, max_count, NULL);
	if (semaphore->handle == NULL) {
		return THREAD_ERROR_CREATE_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaDelete)(SEMAPHORE *semaphore)
{
	CloseHandle(semaphore->handle);
	semaphore->handle = NULL;
	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaWait)(SEMAPHORE *semaphore)
{
	// Wait for the semaphore indefinitely
	DWORD dwReturnValue = WaitForSingleObject(semaphore->handle, INFINITE);
	if (dwReturnValue != WAIT_OBJECT_0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaTimedWait)(SEMAPHORE *semaphore, TIMEOUT timeout)
{
	// Test the semaphore with a specific timeout
	DWORD dwReturnValue = WaitForSingleObject(semaphore->handle, timeout);
	if (dwReturnValue != WAIT_OBJECT_0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaTryWait)(SEMAPHORE *semaphore)
{
	// Test the semaphore without waiting
	DWORD dwReturnValue = WaitForSingleObject(semaphore->handle, 0);
	if (dwReturnValue != WAIT_OBJECT_0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaPost)(SEMAPHORE *semaphore)
{
	ReleaseSemaphore(semaphore->handle, 1, NULL);
	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaIncrement)(SEMAPHORE *semaphore, int count)
{
	assert(count > 0);
	ReleaseSemaphore(semaphore->handle, count, NULL);
	return THREAD_ERROR_OKAY;
}

THREAD_API(EventCreate)(EVENT *event)
{
	// Create a manual reset event that is not signalled
	event->handle = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(event->handle != NULL);
	return THREAD_ERROR_OKAY;
}

THREAD_API(EventDelete)(EVENT *event)
{
	CloseHandle(event->handle);
	event->handle = NULL;
	return THREAD_ERROR_OKAY;
}

THREAD_API(EventWait)(EVENT *event)
{
	// Wait for the event to be signalled
	DWORD dwReturnValue = WaitForSingleObject(event->handle, INFINITE);
	if (dwReturnValue != WAIT_OBJECT_0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SetEventState)(EVENT *event, EVENT_STATE state)
{
	switch (state)
	{
	case EVENT_STATE_SIGNALLED:
		// Signal the manual reset event
		SetEvent(event->handle);
		break;

	case EVENT_STATE_CLEARED:
		// Clear the manual reset event
		ResetEvent(event->handle);
		break;

	default:
		assert(0);
		return THREAD_ERROR_BAD_STATE;
		break;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(CreateLock)(LOCK *lock)
{
	InitializeCriticalSection(&lock->mutex);
	//InitializeCriticalSectionAndSpinCount(&lock->mutex, 100);
	return THREAD_ERROR_OKAY;
}

THREAD_API(DeleteLock)(LOCK *lock)
{
	DeleteCriticalSection(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(Lock)(LOCK *lock)
{
	EnterCriticalSection(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(Unlock)(LOCK *lock)
{
	LeaveCriticalSection(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

#else

#include "pthread.h"

#if __APPLE__
#include "../Common/macdefs.h"
#endif

//#include "semaphore.h"

// Macro for declaring routines in the threads API
#define THREAD_API(proc) \
	static inline THREAD_ERROR proc

// Macro for declaring the thread procedure
#define THREAD_PROC(proc, data) \
	void *proc(void *data)

// Data type returned by the thread procedure
typedef void * THREAD_RETURN_TYPE;

// Declaration of the worker thread procedure
typedef void *(* THREAD_PROC)(void *);

typedef uint32_t TIMEOUT;

typedef struct
{
	pthread_t pthread;

} THREAD;

// Dummy routines for thread affinity calls
#ifdef _WIN32
HANDLE GetCurrentThread(void);
DWORD SetThreadAffinityMask(HANDLE hThread, DWORD * dwThreadAffinityMask);
#else
#if __APPLE__
#else
pthread_t GetCurrentThread(void);
#endif
void SetThreadAffinityMask(pthread_t thread, uint32_t *thread_affinity_mask);
#endif

#if 0	// Unnamed semaphores are not supported on the Macintosh
typedef struct
{
	sem_t sema;
}
SEMAPHORE;
#endif

typedef struct
{
	// Use a pthreads condition variable to emulate a Windows event
	pthread_mutex_t mutex;
	pthread_cond_t cond;

	// True if the event has been signalled (turned on)
	EVENT_STATE state;

} EVENT;

typedef struct
{
	pthread_mutex_t mutex;

} LOCK;

/*
#include <mach/mach_init.h>
#include <mach/thread_policy.h>
//#include <mach/sched.h>

int set_realtime(pthread_t thread, int period, int computation, int constraint) {
    struct thread_time_constraint_policy ttcpolicy;
    int ret;

    ttcpolicy.period=period; // HZ/160
    ttcpolicy.computation=computation; // HZ/3300;
    ttcpolicy.constraint=constraint; // HZ/2200;
    ttcpolicy.preemptible=1;

    if ((ret=thread_policy_set(pthread_mach_thread_np(thread),
							   THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&ttcpolicy,
							   THREAD_TIME_CONSTRAINT_POLICY_COUNT)) != KERN_SUCCESS) {
		fprintf(stderr, "set_realtime() failed.\n");
		return 0;
    }
    return 1;
}
*/


THREAD_API(ThreadCreate)(THREAD *thread,
						 THREAD_PROC proc,
						 void *param)
{
	// Create a thread with default attributes

/*    pthread_attr_t attr;
    struct sched_param parm;
    pthread_attr_init(&attr);
    parm.sched_priority=96;
    pthread_attr_setscope(&attr,PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setschedpolicy(&attr,SCHED_FIFO);
    pthread_attr_setschedparam(&attr,&parm);

	int result = pthread_create(&thread->pthread, &attr, proc, param);
*/
	int result = pthread_create(&thread->pthread, 0, proc, param);
	if (result != 0) {
		return THREAD_ERROR_CREATE_FAILED;
	}

//	set_realtime(thread->pthread, 160, 3300, 2200);

	return THREAD_ERROR_OKAY;
}

THREAD_API(ThreadDelete)(THREAD * thread)
{
	//pthread_destroy(thread->pthread);
	(void) thread;
	return THREAD_ERROR_OKAY;
}

THREAD_API(ThreadWait)(THREAD *thread)
{
	void *value;
	int result = pthread_join(thread->pthread, &value);
	if (result != 0) {
		return THREAD_ERROR_DETACH_FAILED;
	}

	return THREAD_ERROR_OKAY;
}


#if 0	// Unnamed semaphores are not supported on the Macintosh

THREAD_API(SemaCreate)(SEMAPHORE *semaphore,
					   int32_t initial_count,
					   int32_t max_count)
{
	int result = sem_init(&semaphore->sema, 0, initial_count);
	if (result != 0) {
		return THREAD_ERROR_CREATE_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaDelete)(SEMAPHORE *semaphore)
{
	sem_destroy(&semaphore->sema);
	semaphore->sema = 0;
	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaWait)(SEMAPHORE *semaphore)
{
	// Wait for the semaphore indefinitely
	int result = sem_wait(&semaphore->sema);
	if (result != 0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaTimedWait)(SEMAPHORE *semaphore, TIMEOUT timeout)
{
}

THREAD_API(SemaTryWait)(SEMAPHORE *semaphore)
{
	// Test the semaphore without waiting
	int result = sem_trywait(&semaphore->sema);
	if (result != 0) {
		return THREAD_ERROR_WAIT_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaPost)(SEMAPHORE *semaphore)
{
	sem_post(&semaphore->sema);
	return THREAD_ERROR_OKAY;
}

THREAD_API(SemaIncrement)(SEMAPHORE *semaphore, int32_t count)
{
	for (; count > 0; count--) {
		sem_post(&semaphore->sema);
	}

	return THREAD_ERROR_OKAY;
}

#endif


// Use a condition variable to implement a manual reset event
THREAD_API(EventCreate)(EVENT *event)
{
	int result;

	// Clear the event state
	event->state = EVENT_STATE_CLEARED;

	// Initialize the mutex with default attributes
	result = pthread_mutex_init(&event->mutex, NULL);
	if (result != 0) {
		return THREAD_ERROR_CREATE_FAILED;
	}

	// Initialize the condition variable with default attributes
	result = pthread_cond_init(&event->cond, NULL);
	if (result != 0) {
		return THREAD_ERROR_CREATE_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(EventDelete)(EVENT *event)
{
	// Delete the condition variable and its mutex
	pthread_cond_destroy(&event->cond);
	pthread_mutex_destroy(&event->mutex);

	return THREAD_ERROR_OKAY;
}

THREAD_API(EventWait)(EVENT *event)
{
	// Lock the mutex associated with the condition variable
	pthread_mutex_lock(&event->mutex);

	// Wait the condition (event state) to change
	while (event->state != EVENT_STATE_SIGNALLED)
	{
		pthread_cond_wait(&event->cond, &event->mutex);
	}
	// The event has been signalled

	event->state = EVENT_STATE_CLEARED; // Mac recommended

	// Unlock the mutex that protects the condition variable
	pthread_mutex_unlock(&event->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(EventReady)(EVENT *event, bool *ready)
{
	pthread_mutex_lock(&event->mutex);
	*ready = (event->state == EVENT_STATE_SIGNALLED);
	pthread_mutex_unlock(&event->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(SetEventState)(EVENT *event, EVENT_STATE state)
{
	// Wait for exclusive access to the event state
	pthread_mutex_lock(&event->mutex);

	// Change the event state
	event->state = state;

	// Signal that the event state has changed
	pthread_cond_signal(&event->cond);

	// Release access to the event state
	pthread_mutex_unlock(&event->mutex);

	return THREAD_ERROR_OKAY;
}

THREAD_API(CreateLock)(LOCK *lock)
{
	// Initialize a mutex with default attributes
	int result = pthread_mutex_init(&lock->mutex, NULL);
	if (result != 0) {
		return THREAD_ERROR_CREATE_FAILED;
	}

	return THREAD_ERROR_OKAY;
}

THREAD_API(DeleteLock)(LOCK *lock)
{
	pthread_mutex_destroy(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(Lock)(LOCK *lock)
{
	pthread_mutex_lock(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

THREAD_API(Unlock)(LOCK *lock)
{
	pthread_mutex_unlock(&lock->mutex);
	return THREAD_ERROR_OKAY;
}

#endif


// Thread routines that are common to all platforms

THREAD_API(SignalEvent)(EVENT *event)
{
	return SetEventState(event, EVENT_STATE_SIGNALLED);
}

THREAD_API(ClearEvent)(EVENT *event)
{
	return SetEventState(event, EVENT_STATE_CLEARED);
}

// Use a manual reset event to emulate an event that is automatically reset
THREAD_API(EventWaitAndReset)(EVENT *event)
{
	THREAD_ERROR result;

	result = EventWait(event);
	if (result == THREAD_ERROR_OKAY) {
		result = ClearEvent(event);
	}

	return result;
}


// Messages that are passed to worker threads in a thread pool
typedef enum thread_message
{
	THREAD_MESSAGE_NONE = 0,
	THREAD_MESSAGE_START,			// Start processing (details passed in the thread data)
	THREAD_MESSAGE_STOP,			// Tell the worker thread terminate
	THREAD_MESSAGE_MORE_WORK,		// Wake threads are as more work has been added to a previously started pool.

	THREAD_MESSAGE_CUSTOM=100		// User messages

} THREAD_MESSAGE;


// Define the data structure for a pool of threads
typedef struct thread_pool
{
	THREAD thread[THREAD_POOL_MAX];			// Worker thread handles
	EVENT start_event[THREAD_POOL_MAX];		// Signal the worker threads to begin processing
	EVENT done_event[THREAD_POOL_MAX];		// Each thread signals when it is finished
	//HANDLE stop_event;					// Force all threads to terminate
	//SEMAPHORE sema;						// Semaphore that counts units of work that are available
	LOCK mutex;								// Exclusive access to the thread pool data

	int thread_count;						// Actual number of threads in the pool
	int thread_index;						// Count of worker threads that are active

	THREAD_MESSAGE message[THREAD_POOL_MAX];// Message that is passed with the start event

	int work_start_count;					// Number of units of work at initialization
	int work_count[THREAD_JOB_LEVELS];		// Number of units of work remaining
	int work_index[THREAD_JOB_LEVELS];		// Index of next unit of work to process
	int work_cmplt[THREAD_JOB_LEVELS];		// Index of highest continuous unit of work completed

	int work_unit_started[THREAD_JOB_LEVELS][THREAD_POOL_MAX];	 // optional usage for threads to determine the
	int work_unit_completed[THREAD_JOB_LEVELS][THREAD_POOL_MAX]; //status of other threads. -1 if not set

} THREAD_POOL;


// Create a pool of worker threads
THREAD_API(ThreadPoolCreate)(THREAD_POOL *pool, int count, THREAD_PROC proc, void *param)
{
	//THREAD_ERROR error = THREAD_ERROR_OKAY;
	int i,j;

	if(count >= THREAD_POOL_MAX)
		count = THREAD_POOL_MAX;
//        fprintf(stderr, "tp count %d\n",count);
	assert(0 < count && count <= THREAD_POOL_MAX);

	// Initialize the mutex that controls access to the thread pool data
	CreateLock(&pool->mutex);

	// Lock access to the thread pool data during initialization
	Lock(&pool->mutex);

	// Set the number of threads in the pool
	pool->thread_count = count;

	// Reset the number of active threads
	pool->thread_index = 0;


	// No units of work have been assigned to the worker threads
	pool->work_start_count = 0;
	for(j=0;j<THREAD_JOB_LEVELS; j++)
	{
		pool->work_count[j] = 0;
		pool->work_index[j] = 0;
		pool->work_cmplt[j] = -1;
	}

	// Create the semaphore for counting units of work
	//error = SemaCreate(&pool->sema, 0, LONG_MAX);
	//assert(error == THREAD_ERROR_OKAY);

	for (i = 0; i < count; i++)
	{
		// Clear the message variable
		pool->message[i] = THREAD_MESSAGE_NONE;

		// Create an event for signalling the thread to start
		EventCreate(&pool->start_event[i]);

		// Create an event for each thread to signal completion
		EventCreate(&pool->done_event[i]);

		for(j=0;j<THREAD_JOB_LEVELS; j++)
		{
			pool->work_unit_started[j][i] = -1;
			pool->work_unit_completed[j][i] = -1;
		}
		// Create each thread in the pool
		ThreadCreate(&pool->thread[i], proc, param);
	}

	// Unlock access to the thread pool data
	Unlock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}

// Signal all of the threads in the pool to start
THREAD_API(ThreadPoolSendMessage)(THREAD_POOL *pool, THREAD_MESSAGE message)
{
	int i;

	// Set the message to be received by the worker threads
	Lock(&pool->mutex);
	for (i = 0; i < pool->thread_count; i++)
		pool->message[i] = message;

	// Notify each thread that a new message is available
	for (i = 0; i < pool->thread_count; i++)
	{
		if(message == THREAD_MESSAGE_START)
			ClearEvent(&pool->done_event[i]);
		SignalEvent(&pool->start_event[i]);
	}
	Unlock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}


// Signal all of the threads in the pool to start
THREAD_API(ThreadSendMessage)(THREAD_POOL *pool, int thread_index, THREAD_MESSAGE message)
{
	// Set the message to be received by the worker threads
	Lock(&pool->mutex);
	pool->message[thread_index] = message;

	// Notify the thread that a new message is available
	if(message == THREAD_MESSAGE_START)
		ClearEvent(&pool->done_event[thread_index]);
	SignalEvent(&pool->start_event[thread_index]);
	Unlock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}

// Wait for all of the threads in the pool to finish
THREAD_API(ThreadPoolWaitAllDone)(THREAD_POOL *pool)
{
	int i;
	for (i = 0; i < pool->thread_count; i++)
	{
		// Wait for the worker thread to finish
		EventWait(&pool->done_event[i]);

		// Acknowledge the signal from the worker thread
		//ClearEvent(&pool->done_event[i]);
	}

	return THREAD_ERROR_OKAY;
}



// Wait for all of the threads in the pool to finish
THREAD_API(ThreadPoolWaitThreadDone)(THREAD_POOL *pool, int thread_index)
{
	if(thread_index < pool->thread_count)
	{
		// Wait for the worker thread to finish
		EventWait(&pool->done_event[thread_index]);

		// Acknowledge the signal from the worker thread
		//ClearEvent(&pool->done_event[thread_index]);
	}

	return THREAD_ERROR_OKAY;
}


// Set the number of units of work available to the worker threads
THREAD_API(ThreadPoolSetWorkCount)(THREAD_POOL *pool, int count)
{
	int i,j;
	//return SemaIncrement(&pool->sema, count);

	// Set the count of units of work and reset the index to the next work unit
	Lock(&pool->mutex);

	pool->work_start_count = count;
//	pool->work_count = count;
//	pool->work_index = 0;

	for(j=0;j<THREAD_JOB_LEVELS; j++)
	{
		pool->work_count[j] = count;
		pool->work_index[j] = 0;
		pool->work_cmplt[j] = -1;
	}

	for (j = 0; j < THREAD_JOB_LEVELS; j++)
	{
		for (i = 0; i < THREAD_POOL_MAX; i++)
		{
			pool->work_unit_started[j][i] = -1;
			pool->work_unit_completed[j][i] = -1;
		}
	}

	Unlock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}

THREAD_API(ThreadPoolAddWorkCount)(THREAD_POOL *pool, int count)
{
	//int i;
	int j;
	//return SemaIncrement(&pool->sema, count);

	// Set the count of units of work and reset the index to the next work unit
	Lock(&pool->mutex);

	pool->work_start_count += count;

	for(j=0;j<THREAD_JOB_LEVELS; j++)
	{
		pool->work_count[j] += count;
	}

	// Waking threads if they think they are done.
//	if(pool->work_count == 0)
/*	{
		for (i = 0; i < pool->thread_count; i++)
			pool->message[i] = THREAD_MESSAGE_MORE_WORK;

		// Notify each thread that a new message is available
		for (i = 0; i < pool->thread_count; i++)
		{
			SignalEvent(&pool->start_event[i]);
		}
	}*/

	Unlock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}


// Delete a pool of worker threads
THREAD_API(ThreadPoolDelete)(THREAD_POOL *pool)
{
	int i;

	// Tell all of the worker threads to stop
	ThreadPoolSendMessage(pool, THREAD_MESSAGE_STOP);

	// Wait for all of the worker threads to terminate
	for (i = 0; i < pool->thread_count; i++)
	{
		ThreadWait(&pool->thread[i]);
	}

	// Lock access to the thread pool data
	//Lock(&pool->mutex);

	for (i = 0; i < pool->thread_count; i++)
	{
		// Delete the worker thread
		ThreadDelete(&pool->thread[i]);

		// Delete the event for signalling completion
		EventDelete(&pool->done_event[i]);

		// Delete the event for starting each thread
		EventDelete(&pool->start_event[i]);
	}

	// Clear the thread count and the number of active threads
	pool->thread_count = 0;
	pool->thread_index = 0;

	// Need to unlock the mutex before deleting it?
	//Unlock(&pool->mutex);

	// Delete the mutex that controls access to the thread pool data
	DeleteLock(&pool->mutex);

	return THREAD_ERROR_OKAY;
}

THREAD_API(PoolThreadWaitForMessage)(THREAD_POOL *pool, int thread_index, THREAD_MESSAGE *message_out)
{
	THREAD_ERROR error = THREAD_ERROR_OKAY;

	// Wait for the signal for the worker thread to start processing
	error = EventWait(&pool->start_event[thread_index]);
	if (error != THREAD_ERROR_OKAY) {
		return error;
	}

	Lock(&pool->mutex);
	// Return the message that indicates what the thread should do
	*message_out = pool->message[thread_index];
	pool->message[thread_index] = THREAD_MESSAGE_NONE;
	error = ClearEvent(&pool->start_event[thread_index]);
	Unlock(&pool->mutex);

	// Acknowledge the signal to start processing
	return error;
}

THREAD_API(PoolThreadSignalDone)(THREAD_POOL *pool, int thread_index)
{
	THREAD_ERROR error = THREAD_ERROR_OKAY;

	Lock(&pool->mutex);
	// Return the message that indicates what the thread should do
	pool->message[thread_index] = THREAD_MESSAGE_NONE;
	ClearEvent(&pool->start_event[thread_index]);
	ClearEvent(&pool->done_event[thread_index]);
	error = SignalEvent(&pool->done_event[thread_index]);
	Unlock(&pool->mutex);

	return error;
}


static void UpdateJobsCompleted(THREAD_POOL *pool, int thread_index, int job_index)
{
	int i,mininprogress = 0x7fffffff,mincomplete = 0;
	if(pool->work_unit_started[job_index][thread_index] > pool->work_unit_completed[job_index][thread_index])
	{
		pool->work_unit_completed[job_index][thread_index] = pool->work_unit_started[job_index][thread_index];

		// make sure the status of other threads is reflected.
		// -- the minium of all threads completed jobs could be the next work_unit_completed
		for (i = 0; i < pool->thread_count; i++)
		{
			int workdone;
			if(pool->work_unit_started[job_index][i] > pool->work_unit_completed[job_index][i])
			{
				workdone = pool->work_unit_started[job_index][i]-1; //As the currect job is not complete

				if(workdone < mininprogress)
					mininprogress = workdone;
			}
			else if(pool->work_unit_started[job_index][i] == pool->work_unit_completed[job_index][i])
			{
				workdone = pool->work_unit_completed[job_index][i];

				if(workdone > mincomplete)
					mincomplete = workdone;
			}
		}

		if(mininprogress == 0x7fffffff)
			mininprogress = mincomplete;

		if(mininprogress > -1 && mininprogress < 0x7fffffff)
			if(mininprogress > pool->work_cmplt[job_index])
				pool->work_cmplt[job_index] = mininprogress;
	}
}



// Return the index to the next unit of work if any
THREAD_API(PoolThreadGetDependentJob)(THREAD_POOL *pool, int *work_index_out, int thread_index, int job_index, int delay)
{
	THREAD_ERROR error = THREAD_ERROR_OKAY;

	int work_count;
	int work_index;

	if (work_index_out == NULL) {
		return THREAD_ERROR_INVALID_ARGUMENT;
	}

	if(thread_index<0 && thread_index>THREAD_POOL_MAX) {
		return THREAD_ERROR_INVALID_ARGUMENT;
	}

	//return SemaTryWait(&pool->sema);
	Lock(&pool->mutex);

	// Asking for the next job also means the previous job on the same thread was finished
	if(job_index > 0)
		UpdateJobsCompleted(pool, thread_index, job_index-1);

	work_count = pool->work_count[job_index];
	work_index = pool->work_index[job_index];

	if (work_count > 0)
	{
		if(job_index > 0) // for jobs > 0 make sure early work is finished
		{
			//int i;
			if(pool->work_cmplt[job_index-1] <= work_index+delay  &&
				pool->work_cmplt[job_index-1] < pool->work_start_count-1)
			{
				error = THREAD_ERROR_NOWORKYET; // this work has been sent out.


				//DAN20090130 -- this code does nothing
				//if(pool->work_count[job_index-1] == 0)
				//{
				//	job_index++;
				//		job_index--;
				//}
			}
		}

		if(error == THREAD_ERROR_OKAY)
		{
			// Decrement the number of units of work remaining
			pool->work_count[job_index] = work_count - 1;

			// Increment the index to the next unit of work
			pool->work_index[job_index] = work_index + 1;

			// Record the status of the overal progress.
			UpdateJobsCompleted(pool, thread_index, job_index);

			// Set the current work unit.
			pool->work_unit_started[job_index][thread_index] = work_index;

			// Return the index to the next unit of work
			*work_index_out = work_index;
			
			// Indicate	that another unit of work is available
			error = THREAD_ERROR_OKAY;
		}
	}
	else
	{
		// No more work available
		error = THREAD_ERROR_NOWORK;
	}

	Unlock(&pool->mutex);

	return error;
}

THREAD_API(PoolThreadWaitForWork)(THREAD_POOL *pool, int *work_index_out, int thread_index)
{
	return PoolThreadGetDependentJob(pool, work_index_out, thread_index, 0, 0);
}

// Return the index of this thread in the thread pool
THREAD_API(PoolThreadGetIndex)(THREAD_POOL *pool, int *thread_index_out)
{
	int thread_index;

	if (thread_index_out == NULL) {
		return THREAD_ERROR_INVALID_ARGUMENT;
	}

	Lock(&pool->mutex);
	thread_index = pool->thread_index;
	pool->thread_index = thread_index + 1;
	Unlock(&pool->mutex);
	assert(0 <= thread_index && thread_index < pool->thread_count);

	*thread_index_out = thread_index;

	return THREAD_ERROR_OKAY;
}

#endif
