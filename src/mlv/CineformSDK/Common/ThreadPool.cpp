/*! @file ThreadPool.cpp

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
#include "StdAfx.h"

#undef	DEBUG
#define	DEBUG (1 && _DEBUG)

#ifndef ASSERT
#define ASSERT(x) assert(x)
#endif

#include "CFHDError.h"
#include "Lock.h"
#include "ThreadMessage.h"
#include "MessageQueue.h"
#include "ThreadPool.h"

/*!
	@brief Create the thread pool

	Note that the locks must be created and the message queue must be initialized
	before the threads are created since the threads will immediately begin execution
	and wait for a message in the queue.
*/
template <typename ThreadMessage>
CThreadPool<ThreadMessage>::CThreadPool(size_t threadCount) :
	m_threadPool(threadCount)
{
	CFHD_Error error = CFHD_ERROR_OKAY;

#if 0
	if (m_threadCount > THREAD_POOL_MAX_THREADS) {
		m_threadCount = THREAD_POOL_MAX_THREADS;
	}

	// Check that the thread count does not exceed the allocated size of the thread pool
	ASSERT(0 < m_threadCount && m_threadCount <= THREAD_POOL_MAX_THREADS);
#endif
}

template <typename ThreadMessage>
CThreadPool<ThreadMessage>::~CThreadPool()
{
	// Check that the thread count does not exceed the allocated size of the thread pool
	//ASSERT(0 < m_threadCount && m_threadCount <= THREAD_POOL_MAX_THREADS);

	// Stop all of the worker threads in the pool
	StopWorkerThreads();

	/*
		The deallocator for each thread will be called when the array of
		threads that is a member variable of this class is deallocated.
	*/
}

#if 0
CFHD_Error CThreadPool::StartWorkerThreads(WorkerThreadProc threadProc, void *param)
{
	//TODO: Need to check that this routine is only executed once

	// Check that the thread count does not exceed the allocated size of the thread pool
	//ASSERT(0 < m_threadCount && m_threadCount <= THREAD_POOL_MAX_THREADS);

	// Initialize the pool of worker threads
	for (int i = 0; i < m_threadPool.size(); i++)
	{
		// Create each thread in the pool
		m_threadPool[i].Start(threadProc, param);
	}

	// The threads will start running as soon as they are created

	return CFHD_ERROR_OKAY;
}
#endif

template <typename ThreadMessage>
CFHD_Error
CThreadPool<ThreadMessage>::StartWorkerThread(size_t index,
											  WorkerThreadProc threadProc,
											  void *param)
{
	// Check that the index is in range
	ASSERT(index < m_threadPool.size());
	if (! (index < m_threadPool.size())) {
		return CFHD_ERROR_INVALID_ARGUMENT;
	}

#if 0
	// Check that the thread has not already been started
	ASSERT(m_threadPool[index].HasStarted() == false);
	if (! (m_threadPool[index].HasStarted() == false))
#else
		// Check that the thread has not already been started
		ASSERT(m_threadPool[index].IsRunning() == false);
		if (! (m_threadPool[index].IsRunning() == false))
#endif
	{
		return CFHD_ERROR_UNEXPECTED;
	}

	// Create the worker thread
	m_threadPool[index].Start(threadProc, param);

	// The thread will start running as soon as it is created

	return CFHD_ERROR_OKAY;
}

template <typename ThreadMessage>
CFHD_Error
CThreadPool<ThreadMessage>::StopWorkerThreads()
{
	// Post stop messages for all of the worker threads
	for (int i = 0; i < m_threadPool.size(); i++)
	{
		ThreadMessage *message = new ThreadMessage(ThreadMessage::THREAD_COMMAND_STOP);
		AddMessage(message);
	}

	// Wait for the threads to terminate
	for (int i = 0; i < m_threadPool.size(); i++)
	{
		m_threadPool[i].Wait();
	}

	return CFHD_ERROR_OKAY;
}

#if 0
int CThreadLimiter::GetProcessorCount()
{
	SYSTEM_INFO cSystem_info;
	GetSystemInfo(&cSystem_info);
	return cSystem_info.dwNumberOfProcessors;
}
#endif
