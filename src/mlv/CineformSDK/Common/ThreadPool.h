/*! @file ThreadPool.h

*  @brief Thread Library Tools
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

#pragma once

#ifdef _WIN32

// The thread procedure must use the standard call interface
#ifndef STDCALL
#define STDCALL __stdcall
#endif

#else

#define STDCALL

#include "pthread.h"

#if __APPLE__
#include "macdefs.h"
#endif

#endif


// Opaque reference to the thread message
//class CThreadMessage;


class CThread
{
#ifdef _WIN32

	enum
	{
		DEFAULT_STACK_SIZE = 128*1024,
	};

public:

	// Data type returned by the thread procedure
	typedef DWORD ThreadReturnType;

	// Declaration of the thread procedure argument
	typedef ThreadReturnType (STDCALL * ThreadProcType)(void *param);

	CThread() :
		handle(NULL),
		id(0),
		running(false)
	{
	}

	~CThread()
	{
		if (handle != NULL)
		{
			assert(!running);
			CloseHandle(handle);
			handle = NULL;
			id = 0;
		}
	}

	CFHD_Error Start(ThreadProcType proc, void *param, DWORD stack = DEFAULT_STACK_SIZE)
	{
		DWORD flags = 0;
		handle = CreateThread(NULL, stack, proc, param, flags, &id);
		running = true;
		return CFHD_ERROR_OKAY;
	}

	CFHD_Error Wait()
	{
		// Wait for the thread to terminate
		WaitForSingleObject(handle, INFINITE);
		running = false;
		return CFHD_ERROR_OKAY;
	}

	bool IsRunning()
	{
		return running;
	}

private:

	HANDLE handle;		//!< Windows handle for the thread
	DWORD id;			//!< Windows thread ID
	bool running;		//!< True if the thread has been started

#else

	enum
	{
		DEFAULT_STACK_SIZE = 128*1024,
	};

public:

	// Data type returned by the thread procedure
	typedef void *ThreadReturnType;

	// Declaration of the thread procedure argument
	typedef ThreadReturnType (STDCALL * ThreadProcType)(void *param);

	CThread() :
		running(false)
	{
		//TODO: How to initialize (not create) the thread data structure?
	}

	~CThread()
	{
		assert(!running);
		//pthread_destroy(thread);
	}

	CFHD_Error Start(ThreadProcType proc, void *param, size_t stack_size = DEFAULT_STACK_SIZE)
	{
		// Set the stack size in the thread attributes
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, stack_size);

		// Create the thread and start the thread procedure
		int result = pthread_create(&thread, &attr, proc, param);
		if (result == 0)
		{
			running = true;
			return CFHD_ERROR_OKAY;
		}
		return CFHD_ERROR_THREAD_CREATE_FAILED;
	}

	CFHD_Error Wait()
	{
		// Wait for the thread to terminate
		void *value;
		int result = pthread_join(thread, &value);
		if (result != 0) {
			return CFHD_ERROR_THREAD_WAIT_FAILED;
		}
		return CFHD_ERROR_OKAY;
	}

	bool IsRunning()
	{
		return running;
	}

private:

	pthread_t thread;	//!< Posix thread structure
	bool running;		//!< True if the thread has been started

#endif
};


/*!
	@brief Pool of worker threads

	This class is intended to be inherited by another class that
	needs to use a pool of worker threads.  This class is designed
	so that the C language threads API is not exposed to clients of
	this class to avoid conflicts with the threads API.
*/
template <typename ThreadMessage>
class CThreadPool : public MessageQueue<ThreadMessage>
{
protected:

	//! Create a thread pool with the specified number of worker threads
	CThreadPool(size_t threadCount);

	~CThreadPool();

	// Declaration of the worker thread procedure
	typedef CThread::ThreadReturnType (STDCALL * WorkerThreadProc)(void *param);

#if 1
	size_t ThreadPoolCount()
	{
		//return m_threadCount;
		return m_threadPool.size();
	}
#endif

	//! Start the worker threads in the pool
	//CFHD_Error StartWorkerThreads(WorkerThreadProc threadProc, void *param);

	//! Start one of the worker threads
	CFHD_Error StartWorkerThread(size_t index, WorkerThreadProc threadProc, void *param);

	//! Stop the worker threads in the pool
	CFHD_Error StopWorkerThreads();

private:

	//! The thread pool is an array of worker threads
	//CThread m_threadPool[THREAD_POOL_MAX_THREADS];
	std::vector<CThread> m_threadPool;

	//! Actual number of worker threads
	//int m_threadCount;

	//! Lock access to the thread pool
	CSimpleLock m_poolMutex;

};

#if 0
//! Use a semaphore to limit the number of worker threads that execute concurrently
class CThreadLimiter
{
	enum
	{
		DEFAULT_THREAD_LIMIT = 8,
	};

public:

	CThreadLimiter(int threadLimit = 0) :
		m_handle(0)
	{
		if (threadLimit == 0) {
			threadLimit = GetProcessorCount();
		}

		if (threadLimit == 0) {
			threadLimit = DEFAULT_THREAD_LIMIT;
		}
		ASSERT(threadLimit > 0);

		if (threadLimit > 0)
		{
#if (1 && TRACE)
			char message[256];
			sprintf_s(message, "CThreadLimiter thread limit: %d\n", threadLimit);
			OutputDebugMesage(message);
#endif
			m_handle = CreateSemaphore(NULL, threadLimit, threadLimit, NULL);
			ASSERT(m_handle != 0);
		}
	}

	~CThreadLimiter()
	{
		CloseHandle(m_handle);
		m_handle = 0;
	}

	int WaitForThread()
	{
		if (m_handle != 0)
		{
			// Wait indefinitely for permission to run concurrently
			return WaitForSingleObject(m_handle, INFINITE);
		}
		return 0;
	}

	int ReleaseThread()
	{
		if (m_handle != 0)
		{
			// Allow another worker thread to run concurrently
			ReleaseSemaphore(m_handle, 1, NULL);
		}
		return 0;
	}

protected:

	int GetProcessorCount();

private:

	HANDLE m_handle;	//!< The semaphore limits the number of concurrent worker threads

};
#endif
