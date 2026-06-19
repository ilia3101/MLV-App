/*! @file MesssageQueue.pp

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


// Initialize the counting semaphore for the number of messages in the queue
template <typename MessageType>
MessageQueue<MessageType>::MessageQueue() :
	m_messageSema(MESSAGE_QUEUE_MAX_COUNT)
{
}

template <typename MessageType>
MessageQueue<MessageType>::~MessageQueue()
{
	// There should not be any messages remaining in the queue
	assert(Length() == 0);
}

template <typename MessageType>
CFHD_Error
MessageQueue<MessageType>::AddMessage(const MessageType &message)
{
	// Lock access to the message queue
	CAutoLock lock(&m_queueMutex);

	//fprintf(stderr, "Posting new message\n");

	// Add the message to the queue
	m_messageQueue.push(message);

	// Signal that a new message is available
	if (!m_messageSema.Post()) {
		fprintf(stderr, "Message queue semaphore post returned false\n");
	}

	return CFHD_ERROR_OKAY;
}

template <typename MessageType>
CFHD_Error
MessageQueue<MessageType>::WaitForMessage(MessageType &message)
{
	//CFHD_Error error = CFHD_ERROR_OKAY;

	// Wait for a message in the queue
	//ASSERT(m_messageSema.Wait());
	//while (Length() == 0)
	//	continue;

	if (!m_messageSema.Wait()) {
		ASSERT(0);
		fprintf(stderr, "Message queue semaphore wait returned false\n");
	}

	// Lock access to the message queue
	CAutoLock lock(&m_queueMutex);

	// There should be at least one message in the queue
	ASSERT(Length() > 0);
	if (! (Length() > 0)) {
		fprintf(stderr, "Message queue length not positive: %ld\n", Length());
	}

	// Remove the first message from the queue
	message = m_messageQueue.front();
	m_messageQueue.pop();

#if (0 && DEBUG && _WIN32)
	// Check the order in which messages are removed from the queue
	char string[256];
	sprintf_s(string, sizeof(string), "Message number: %d\n", message->MessageNumber());
	OutputDebugString(string);
#endif

	return CFHD_ERROR_OKAY;
}

