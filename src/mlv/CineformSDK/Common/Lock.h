/*! @file Lock.h

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


/*!
	@brief Simple lock for controlling access to a critical section

	This class is called a simple lock as there can be other kinds of locks.
	All lock classes should provide lock and unlock methods and be derived
	from a common base class.
*/
#ifdef _WIN32

class CSimpleLock
{
	friend class ConditionVariable;

public:

	CSimpleLock()
	{
		InitializeCriticalSection(&lock);
	}

	~CSimpleLock()
	{
		DeleteCriticalSection(&lock);
	}

	void Lock()
	{
		EnterCriticalSection(&lock);
	}

	void Unlock()
	{
		LeaveCriticalSection(&lock);
	}

private:

	CRITICAL_SECTION lock;
};

#elif __APPLE__ || __GNUC__

#include "pthread.h"

class CSimpleLock
{
	friend class ConditionVariable;
	
public:
	
	CSimpleLock()
	{
		pthread_mutex_init(&lock, NULL);
	}
	
	~CSimpleLock()
	{
		pthread_mutex_destroy(&lock);
	}
	
	void Lock()
	{
		pthread_mutex_lock(&lock);
	}
	
	void Unlock()
	{
		pthread_mutex_unlock(&lock);
	}
	
private:
	
	pthread_mutex_t lock;
};

#endif


/*!
	@brief Lock access to a critical section with automatic unlocking
	
	Acquire access to a critical section and relinquish access when the
	class instance is destroyed.  The typical use case is to allocate an
	instance of this class on the stack so that the lock is automatically
	relinquished when enclosing lexical scope is released.

	Future versions may support automatic unlocking of other types of locks.
	In this case, the constructor and member variable should be changed to
	accept the base class for all locks as an argument.
*/
class CAutoLock
{
public:

	CAutoLock(CSimpleLock *lock) :
		m_lock(lock)
	{
		m_lock->Lock();
	}

	CAutoLock(CSimpleLock &lock) :
		m_lock(&lock)
	{
		m_lock->Lock();
	}

	~CAutoLock()
	{
		m_lock->Unlock();
	}

private:

	CSimpleLock *m_lock;
	
};
