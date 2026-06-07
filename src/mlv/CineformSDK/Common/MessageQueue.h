/*! @file MessageQueue.h

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

#pragma once


// Forward reference to the thread message
//class CThreadMessage;

#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif


class CMessageSemaphore
{
#ifdef _WIN32

public:

	CMessageSemaphore(long maximum_count) :
		handle(NULL)
	{
		handle = CreateSemaphore(NULL, 0, maximum_count, NULL);
		assert(handle != NULL);
	}

	~CMessageSemaphore()
	{
		if (handle)
		{
			CloseHandle(handle);
			handle = NULL;
		}
	}

	bool Wait()
	{		
		if (handle)
		{
			// Wait for the semaphore indefinitely
			DWORD result = WaitForSingleObject(handle, INFINITE);
			return result == WAIT_OBJECT_0;
		}
		//return WAIT_ABANDONED;
		return false;
	}

	bool Post()
	{
		if (handle)
		{
			long count;
			BOOL result = ReleaseSemaphore(handle, 1, &count);
			assert(result);
			return result != 0;
		}
		return false;
	}

private:

	HANDLE handle;
	
	
#elif defined (__APPLE__)
	

public:

	CMessageSemaphore(long maximum_count)
	{
		//int result = sem_init(&sema, 0, 0);
		semaphore = dispatch_semaphore_create(0);
		//printf("sem_init %d\n", result);
		//assert(result == 0);
	}

	~CMessageSemaphore()
	{
		dispatch_semaphore_signal(semaphore);
		//int result = sem_destroy(&sema);
		//assert(result == 0);
	}

	bool Wait()
	{
		dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
		//int result = sem_wait(&sema);
		//assert(result == 0);
		//return (result == 0);
		return true;
	}

	bool Post()
	{
		dispatch_semaphore_signal(semaphore);
		//int result = sem_post(&sema);
		//assert(result == 0);
		//return (result == 0);
		return true;
	}

private:

	//sem_t sema;
	dispatch_semaphore_t semaphore;

#else

public:

	CMessageSemaphore(long maximum_count)
	{
		int result = sem_init(&sema, 0, 0);
		assert(result == 0);
	}

	~CMessageSemaphore()
	{
		int result = sem_destroy(&sema);
		assert(result == 0);
	}

	bool Wait()
	{
		int result = sem_wait(&sema);
		assert(result == 0);
		return (result == 0);
	}

	bool Post()
	{
		int result = sem_post(&sema);
		assert(result == 0);
		return (result == 0);
	}

private:

	sem_t sema;

#endif
};

/*!
	@brief Thread-safe message queue

	Implements a message queue using the queue container from the
	standard template library by adding a mutex to control access
	to the critical section and a counting semaphore for the number
	of messages in the queue.
*/
template <typename MessageType> class MessageQueue
{
	//! Maximum number of messages in the queue
	static const long MESSAGE_QUEUE_MAX_COUNT = 1024;

public:

	MessageQueue();

	~MessageQueue();

	//! Add a message to the message queue
	CFHD_Error AddMessage(const MessageType &message);

	//! Wait for a message and return the next message
	CFHD_Error WaitForMessage(MessageType &message);

	//! Return the number of messages in the queue
	size_t Length()
	{
		return m_messageQueue.size();
	}

protected:

	std::queue<MessageType> m_messageQueue;		//<! Queue of messages for the worker threads
	CSimpleLock m_queueMutex;					//<! Exclusive access to the message queue
	CMessageSemaphore m_messageSema;			//<! Semaphore for messages in the queue

};
