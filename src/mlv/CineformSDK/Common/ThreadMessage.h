/*! @file ThreadMessage.h

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


/*!
	@brief Thread-safe counter

	The thread-safe counter wraps an integer counter in a class
	with a lock that controls concurrent access to the counter.
*/
class ThreadSafeCounter
{
public:

	ThreadSafeCounter() :
		counter(0)
	{
	}

	~ThreadSafeCounter()
	{
	}

	int GetNextNumber()
	{
		CAutoLock lock(mutex);

		counter++;
		int number = counter;

		return number;
	}

private:

	int counter;			//!< Current value of the counter
	CSimpleLock mutex;		//!< Lock access to the counter
};


/*!
	@brief Base class for thread messages

	This base class provides the member variable for the thread command
	that is common to all thread messages and defines the enumeration
	for the thread commands.
*/
class ThreadMessage
{
public:

	enum ThreadCommand
	{
		THREAD_COMMAND_NULL = 0,
		THREAD_COMMAND_START,
		THREAD_COMMAND_STOP,
		THREAD_COMMAND_ENCODE,
	};

	ThreadMessage() :
		command(THREAD_COMMAND_NULL)
	{
#if _DEBUG
		number = counter.GetNextNumber();
#endif
	}

	ThreadMessage(ThreadCommand command) :
		command(command)
	{
#if _DEBUG
		number = counter.GetNextNumber();
#endif
	}

	// Must define a copy constructor
	ThreadMessage(const ThreadMessage& message) :
		command(message.command)
	{
#if _DEBUG
		number = message.number;
#endif	
	}

#if 0
	virtual ~ThreadMessage()
	{
	}
#endif

	const ThreadMessage& operator= (const ThreadMessage& message)
	{
		if (&message != this)
		{
			command = message.command;
#if _DEBUG
			number = message.number;
#endif
		}
		return *this;
	}

	ThreadCommand Command()
	{
		return command;
	}

private:

	ThreadCommand command;

#if _DEBUG
public:
	int MessageNumber()
	{
		return (int)number;
	}
private:
	size_t number;						//!< Each message is numbered for debugging
/*DAN	static */ ThreadSafeCounter counter;	//!< Thread-safe counter used to count messages
#endif
};

#if 0
/*!
	@brief Derived class for the thread process message

	This class sets the thread command member to the process command
	and provides the procedure that is called by the worker thread
	to process the message.  The parameter in the process message is
	passed to the process message procedure.

	This class should be subclassed to provide the specific message
	processing functionality.  The subclass must implement the virtual
	ProcessMessage method to process the message.
*/
class CProcessMessage : public CThreadMessage
{
protected:

	CProcessMessage(void *param) :
		 CThreadMessage(THREAD_COMMAND_PROCESS),
		 m_param(param),
		 m_result(0)
	{
	}

	virtual ~CProcessMessage()
	{
	}

public:

	//! Procedure for processing the message
	virtual int ProcessMessage(void *param = NULL) = 0;

private:

	//! Parameter passed to the process message procedure
	void *m_param;

	//! Result from processing this message (returned by ProcessMessage)
	int m_result;

};
#endif
