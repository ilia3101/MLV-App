/*! @file EncoderQueue.cpp

*  @brief
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
#include "Allocator.h"
#include "CFHDError.h"
#include "CFHDTypes.h"
#include "VideoBuffers.h"
#include "Lock.h"
#include "Condition.h"
#include "MetadataWriter.h"
#include "ThreadMessage.h"
#include "MessageQueue.h"
#include "EncoderQueue.h"


// Include the template class methods that are not defined in the header file
#include "../Common/MessageQueue.cpp"

// Force instantiation of the encoder message queue
template class MessageQueue<EncoderMessage>;
