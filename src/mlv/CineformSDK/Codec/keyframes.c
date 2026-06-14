/*! @file keyframes.c

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
#include "config.h"
#include "timing.h"

#ifndef DEBUG
#define DEBUG  (1 && _DEBUG)
#endif
#define TIMING (1 && _TIMING)
#define XMMOPT (1 && _XMMOPT)

#include <stdlib.h>
#include <string.h>		// Use memcpy to copy runs of image pixels
#include <memory.h>
#include <assert.h>
#include <limits.h>
#ifdef __x86_64__
    #include <emmintrin.h>             // SSE2 intrinsics
#else
    #include "sse2neon/sse2neon.h"
#endif
#include <math.h>
#if __APPLE__
#include <dlfcn.h>
#endif

#include "image.h"
#include "codec.h"
#include "debug.h"
#include "color.h"
#include "convert.h"
#include "allocator.h"

#include "metadata.h"


KeyframePair *AddKeyframePair(DECODER *decoder, void *control_point_data, size_t payload_size,
		uint32_t control_point_type, uint32_t flags, uint32_t keypos, uint32_t currpos)
{
	int i;

	if (payload_size > sizeof(decoder->Keyframes.KeyframePairs[0].frame_in_payload))
	{
		return NULL; // can't use this keyframe
	}

	if(decoder->Keyframes.keyframetypecount == 0)
	{
		decoder->Keyframes.keyframetypecount++;

		decoder->Keyframes.KeyframePairs[0].control_point_type = control_point_type;
		decoder->Keyframes.KeyframePairs[0].control_point_flags = flags;
		decoder->Keyframes.KeyframePairs[0].trigger_frame_prev = keypos;
		decoder->Keyframes.KeyframePairs[0].trigger_frame_in = keypos;
		decoder->Keyframes.KeyframePairs[0].trigger_frame_out = 0;
		decoder->Keyframes.KeyframePairs[0].trigger_frame_next = 0;
		decoder->Keyframes.KeyframePairs[0].payload_size = (uint32_t)payload_size;
		memcpy(decoder->Keyframes.KeyframePairs[0].frame_prev_payload, control_point_data, payload_size);
		memcpy(decoder->Keyframes.KeyframePairs[0].frame_in_payload, control_point_data, payload_size);

		return &decoder->Keyframes.KeyframePairs[0];
	}
	
	
	for(i=0; i<decoder->Keyframes.keyframetypecount; i++)	
	{
		if(decoder->Keyframes.KeyframePairs[i].control_point_type == control_point_type)
		{
			if(keypos <= currpos && decoder->Keyframes.KeyframePairs[i].trigger_frame_in <= keypos)
			{
				decoder->Keyframes.KeyframePairs[i].trigger_frame_prev = decoder->Keyframes.KeyframePairs[i].trigger_frame_in;
				decoder->Keyframes.KeyframePairs[i].trigger_frame_in = keypos;
				decoder->Keyframes.KeyframePairs[i].trigger_frame_out = 0;
				decoder->Keyframes.KeyframePairs[i].trigger_frame_next = 0;
				//decoder->Keyframes.KeyframePairs[i].computed_fraction = -1.0;
				decoder->Keyframes.KeyframePairs[i].payload_size = (uint32_t)payload_size;
				memcpy(&decoder->Keyframes.KeyframePairs[i].frame_prev_payload, &decoder->Keyframes.KeyframePairs[i].frame_in_payload, payload_size);
				memcpy(&decoder->Keyframes.KeyframePairs[i].frame_in_payload, control_point_data, payload_size);
				return &decoder->Keyframes.KeyframePairs[i];
			}
			else if(keypos >= currpos)
			{
				if(decoder->Keyframes.KeyframePairs[i].payload_size == 0) // only store the fisrt one greater than currpos
				{
					decoder->Keyframes.KeyframePairs[i].trigger_frame_prev = decoder->Keyframes.KeyframePairs[i].trigger_frame_in;
					decoder->Keyframes.KeyframePairs[i].trigger_frame_in = keypos;
					decoder->Keyframes.KeyframePairs[i].trigger_frame_out = 0;
					decoder->Keyframes.KeyframePairs[i].trigger_frame_next = 0;
					//decoder->Keyframes.KeyframePairs[i].computed_fraction = -1.0;
					decoder->Keyframes.KeyframePairs[i].payload_size = (uint32_t)payload_size;
					memcpy(&decoder->Keyframes.KeyframePairs[i].frame_prev_payload, &decoder->Keyframes.KeyframePairs[i].frame_in_payload, payload_size);
					memcpy(&decoder->Keyframes.KeyframePairs[i].frame_in_payload, control_point_data, payload_size);
					return &decoder->Keyframes.KeyframePairs[i];
				}
				else if(decoder->Keyframes.KeyframePairs[i].trigger_frame_in < keypos && 
					decoder->Keyframes.KeyframePairs[i].trigger_frame_out == 0 &&
					decoder->Keyframes.KeyframePairs[i].payload_size == payload_size) // only store the first after currpos
				{
					decoder->Keyframes.KeyframePairs[i].trigger_frame_out = keypos;
					decoder->Keyframes.KeyframePairs[i].payload_size = (uint32_t)payload_size;
					memcpy(&decoder->Keyframes.KeyframePairs[i].frame_out_payload, control_point_data, payload_size);
					return &decoder->Keyframes.KeyframePairs[i];
				}
				else
				{
					if(decoder->Keyframes.KeyframePairs[i].trigger_frame_next == 0)
					{
						decoder->Keyframes.KeyframePairs[i].trigger_frame_next = keypos;
						decoder->Keyframes.KeyframePairs[i].payload_size = (uint32_t)payload_size;
						memcpy(&decoder->Keyframes.KeyframePairs[i].frame_next_payload, control_point_data, payload_size);
					}
					return &decoder->Keyframes.KeyframePairs[i];
				}
			}
			else
			{
				if(decoder->Keyframes.KeyframePairs[i].trigger_frame_out == 0 && // only store the next control point
					decoder->Keyframes.KeyframePairs[i].payload_size == payload_size)
				{
					decoder->Keyframes.KeyframePairs[i].trigger_frame_out = keypos;
					decoder->Keyframes.KeyframePairs[i].payload_size = (uint32_t)payload_size;
					memcpy(&decoder->Keyframes.KeyframePairs[i].frame_out_payload, control_point_data, payload_size);
					return &decoder->Keyframes.KeyframePairs[i];
				}
				else
				{
					return NULL;
				}
			}
		}
	}

	if(decoder->Keyframes.keyframetypecount+1<MAX_CONTROL_POINT_PAIRS)
	{
		int k = decoder->Keyframes.keyframetypecount++;

		decoder->Keyframes.KeyframePairs[k].control_point_type = control_point_type;
		decoder->Keyframes.KeyframePairs[k].control_point_flags = flags;
		decoder->Keyframes.KeyframePairs[k].trigger_frame_prev = keypos;
		decoder->Keyframes.KeyframePairs[k].trigger_frame_in = keypos;
		decoder->Keyframes.KeyframePairs[k].trigger_frame_out = 0;
		decoder->Keyframes.KeyframePairs[k].trigger_frame_next = 0;
		decoder->Keyframes.KeyframePairs[k].payload_size = (uint32_t)payload_size;
		memcpy(decoder->Keyframes.KeyframePairs[k].frame_prev_payload, control_point_data, payload_size);
		memcpy(decoder->Keyframes.KeyframePairs[k].frame_in_payload, control_point_data, payload_size);

		return &decoder->Keyframes.KeyframePairs[k];
	}
	
	return NULL;
}

void NewControlPoint(DECODER *decoder, unsigned char *ptr, unsigned int len, int delta, int priority)
{
	if(decoder && ptr && len) // overrides form database or external control
	{
		KeyframePair *retItem = NULL;
		//unsigned char *base = ptr;
		void *trigger_data;
		unsigned char type;
		unsigned int pos = 0;
		unsigned int size;
		unsigned int trigger_tag;
		void *control_point_data;
		unsigned int control_point_type;
		int keypos = 0;
		int currpos = 0;
		int flags;					

		if(pos+16 <= len)
		{
			control_point_type = MAKETAG(ptr[0],ptr[1],ptr[2],ptr[3]);
			flags = ptr[4] + (ptr[5]<<8) + (ptr[6]<<16) + (ptr[7]<<24);  //Future support for key frame type. syline, linear, hold, etc/
			trigger_tag = MAKETAG(ptr[8],ptr[9],ptr[10],ptr[11]); // like UFRM
			size = ptr[12] + (ptr[13]<<8) + (ptr[14]<<16);        // trigger size (4)
			type = ptr[15];
			
			ptr += (16 + 3) & 0xfffffc;
			pos += (16 + 3) & 0xfffffc;

			trigger_data = (void *)ptr;			//trigger data like UFRM value or TIMC string
			ptr += (size+3) & 0xfffffc;			// ptr to the first tag
			pos += (size+3) & 0xfffffc;

			switch(trigger_tag) 
			{
				case TAG_TIMECODE:
					currpos = Timecode2frames(decoder->cfhddata.FileTimecodeData.orgtime, decoder->cfhddata.timecode_base);
					keypos = Timecode2frames(trigger_data, decoder->cfhddata.timecode_base);
					break;	
				case TAG_UNIQUE_FRAMENUM:
					currpos = decoder->codec.unique_framenumber;
					keypos = *((uint32_t *)trigger_data);
					break;
				default:
					return; // invalid trigger
			}
			
			control_point_data = (void *)ptr;
			size = len - pos;
			
			retItem = AddKeyframePair(decoder, control_point_data, size, control_point_type, flags, keypos, currpos);
			if(retItem)
			{
				if(retItem->trigger_frame_out == 0)
				{
					UpdateCFHDDATA(decoder, retItem->frame_in_payload, retItem->payload_size, delta, priority);
				}
				else
				{
					unsigned char payload[KEYFRAME_PAYLOAD_MAX]; 
					unsigned char *ptrP = retItem->frame_prev_payload;
					unsigned char *ptrI = retItem->frame_in_payload;
					unsigned char *ptrO = retItem->frame_out_payload;
					unsigned char *ptrN = retItem->frame_next_payload;
					unsigned char *ptrL = payload;
					unsigned int tagI, tagO, sizeI, sizeO, typeI, typeO;
					void *dataI, *dataO;
					void *dataP, *dataN, *dataL;
					bool failed = false;
					float fraction = (currpos - retItem->trigger_frame_in) / (float)(retItem->trigger_frame_out - retItem->trigger_frame_in);


					if(fraction > 0.0 && fraction < 1.0)
					{
						//if(retItem->computed_fraction == fraction)
						//	failed = true; // repeated processing, skip work

						//retItem->computed_fraction = fraction;
						//fraction=(sinf(fraction*3.14159 - 1.570796)+1.0)*0.5; // basic ease-in, ease-out, it assumes starting/stopped from a none changing value. 

						if(!failed)
						{
							memcpy(payload, retItem->frame_in_payload, retItem->payload_size);

							pos = 0;
							len = retItem->payload_size;
							while(pos+12 <= len)
							{
								tagI = MAKETAG(ptrI[0],ptrI[1],ptrI[2],ptrI[3]);
								tagO = MAKETAG(ptrO[0],ptrO[1],ptrO[2],ptrO[3]);
								sizeI = ptrI[4] + (ptrI[5]<<8) + (ptrI[6]<<16);
								sizeO = ptrO[4] + (ptrO[5]<<8) + (ptrO[6]<<16);
								typeI = ptrI[7];
								typeO = ptrO[7];
								dataP = (void *)&ptrP[8];
								dataI = (void *)&ptrI[8];
								dataO = (void *)&ptrO[8];
								dataN = (void *)&ptrN[8];
								dataL = (void *)&ptrL[8];

								if(tagI != tagO || sizeI != sizeO || typeI != typeO)
								{
									failed = true;
									break;
								}

								switch(typeI)
								{
								case 'f':
									{
										float *valP = (float *)dataP;
										float *valI = (float *)dataI;
										float *valO = (float *)dataO;
										float *valN = (float *)dataN;
										float *valL = (float *)dataL;
										float alpha = fraction;

										if(retItem->trigger_frame_next == 0)
											valN = (float *)dataO;

										if(*valP == *valI && *valO == *valN) // sine Ease-in, Ease-out  
											alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;
										else if(*valP != *valI && *valO == *valN)
										{
											if(*valP > *valI && *valI < *valO)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else if(*valP < *valI && *valI > *valO)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else
											{	
												float slopePI = fabsf(*valP - *valI);
												float slopeIO = fabsf(*valI - *valO);
												float slopeON = 0.0;
												alpha = (sinf(fraction*1.570796f)) * (slopePI / (slopePI + slopeIO + slopeON)) +  (sinf(fraction*1.570796f - 1.570796f)+1.0f) * (slopeON / (slopePI + slopeIO + slopeON)) + (sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f*(slopeIO / (slopePI + slopeIO + slopeON));
											}
										}
										else if(*valP == *valI && *valO != *valN)
										{
											if(*valI > *valO && *valO < *valN)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else if(*valI < *valO && *valO > *valN)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else  //
											{
												float slopePI = 0.0;
												float slopeIO = fabsf(*valO - *valI);
												float slopeON = fabsf(*valN - *valO);
												//float ratio = 0;
												alpha = (sinf(fraction*1.570796f)) * (slopePI / (slopePI + slopeIO + slopeON)) +  (sinf(fraction*1.570796f - 1.570796f)+1.0f) * (slopeON / (slopePI + slopeIO + slopeON)) + (sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f*(slopeIO / (slopePI + slopeIO + slopeON));
											}
										}
										else // all different
										{
											if(*valP < *valI && *valI > *valO && *valO < *valN)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else if(*valP > *valI && *valI < *valO && *valO > *valN)
												alpha=(sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f;// direction change, sine Ease-in, Ease-out  
											else
											{
												float slopePI = fabsf(*valP - *valI);
												float slopeIO = fabsf(*valO - *valI);
												float slopeON = fabsf(*valN - *valO);
												alpha = (sinf(fraction*1.570796f)) * (slopePI / (slopePI + slopeIO + slopeON)) +  (sinf(fraction*1.570796f - 1.570796f)+1.0f) * (slopeON / (slopePI + slopeIO + slopeON)) + (sinf(fraction*3.14159f - 1.570796f)+1.0f)*0.5f*(slopeIO / (slopePI + slopeIO + slopeON));
											}
										}


										while(sizeI>0)
										{	
											
											float val = *valI * (1.0f - alpha);
											val += *valO * alpha;
											*valL++ = val; valI++; valO++; valP++, valN++;
											sizeI -= 4;
										}
									}
									break;
							/* old	case 'H':
								case 'L':
									{
										uint32_t *valI = (uint32_t *)dataI;
										uint32_t *valO = (uint32_t *)dataO;

										while(sizeI>0)
										{	
											//TODO: The GCC compiler warns that the operation on valI may be undefined
											*valI++ = (uint32_t)((float)(*valI) * (1.0 - fraction) + (float)*valO++ * fraction);
											sizeI -= 4;
										}
									}
									break; */
								}

								ptrI += (8 + sizeO + 3) & 0xfffffc;
								ptrO += (8 + sizeO + 3) & 0xfffffc;
								ptrP += (8 + sizeO + 3) & 0xfffffc;
								ptrN += (8 + sizeO + 3) & 0xfffffc;
								ptrL += (8 + sizeO + 3) & 0xfffffc;
								pos += (8 + sizeO + 3) & 0xfffffc;
							}
						}
					}
					else
					{
						memcpy(payload, retItem->frame_in_payload, retItem->payload_size);
					}

					if(!failed)
					{
						UpdateCFHDDATA(decoder, payload, retItem->payload_size, delta, priority);
					}
				}
			}
		}
	}

	return;
}
