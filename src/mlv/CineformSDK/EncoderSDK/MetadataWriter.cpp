/*! @file MetadataWriter.cpp

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
//#include "Interface.h"

#ifndef _WIN32
#include <uuid/uuid.h>
#endif

// Include files from the codec library
#include "encoder.h"
//#include "thread.h"
#include "metadata.h"
#include "AVIExtendedHeader.h"

//TODO: Eliminate references to the codec library

// Include files from the encoder DLL
#include "Allocator.h"
#include "CFHDEncoder.h"
#ifdef __APPLE__
#include <CoreServices/CoreServices.h>
#endif

#include "Lock.h"

#include "MetadataWriter.h"
#include "VideoBuffers.h"
#include "SampleEncoder.h"
//#include "SampleMetadata.h"
//#include "Watermark.h"


/* Table of CRCs of all 8-bit messages. */
static unsigned long look_crc_table[256];

/* Flag: has the table been computed? Initially false. */
static int look_crc_table_computed = 0;

/* Make the table for a fast CRC. */
void look_make_crc_table(void)
{
 unsigned long c;
 int n, k;

 for (n = 0; n < 256; n++) {
   c = (unsigned long) n;
   for (k = 0; k < 8; k++) {
     if (c & 1)
       c = 0xedb88320L ^ (c >> 1);
     else
       c = c >> 1;
   }
   look_crc_table[n] = c;
 }
 look_crc_table_computed = 1;
}

/* Update a running CRC with the bytes buf[0..len-1]--the CRC
  should be initialized to all 1's, and the transmitted value
  is the 1's complement of the final running CRC (see the
  crc() routine below)). */

unsigned long look_update_crc(unsigned long crc, unsigned char *buf,
                        int len)
{
 unsigned long c = crc;
 int n;

 if (!look_crc_table_computed)
   look_make_crc_table();
 for (n = 0; n < len; n++) {
   c = look_crc_table[(c ^ buf[n]) & 0xff] ^ (c >> 8);
 }
 return c;
}

/* Return the CRC of the bytes buf[0..len-1]. */
unsigned long look_calc_crc(unsigned char *buf, int len)
{
 return look_update_crc(0xffffffffL, buf, len) ^ 0xffffffffL;
}


#define OUTPUT	0
#define BUFSIZE	1024

uint32_t ValidateLookGenCRCEnc(char* path)
{
	int crc = 0;
	FILE *fp;
	int err = 0;
#ifdef _WIN32
	err = fopen_s(&fp, path, "r");
#else
	fp = fopen(path, "r");
#endif

	if(err || fp == NULL)
	{
#if (OUTPUT && _WIN32)
		OutputDebugString("ValidateLookGenCRCEnc : no file");
#endif
		return 0;
	}
	else
	{
		size_t len = 0;
		size_t lastlen = 0;
		size_t pos = 0;
		char buf[BUFSIZE];
		bool LUTfound = false;
		bool SIZEfound = false;
		bool DATAfound = false;
		bool finished = false;
		int size = 0;
		int rgbpos = 0;
		int entries = 0;
		float *LUT = NULL;
		unsigned char *iLUT = NULL;

		pos = 0;
		do
		{
			lastlen = len;
			if(pos)
				memcpy(buf, &buf[pos], lastlen-pos);
			
			len = fread(&buf[lastlen-pos],1,BUFSIZE-(lastlen-pos),fp) + (lastlen-pos);

			pos = 0;

			if(!LUTfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<LUT>",5) == 0)
					{
						pos+=5;
						LUTfound = true;
						break;

					}
					pos++;
				} while(pos < len-5);
			}
			else if(!SIZEfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<size>",6) == 0)
					{
						int j = 0;
						pos+=7;
						while(buf[pos+j] >= '0' && buf[pos+j] <= '9') j++;
						buf[pos+j] = 0;

						size = atoi(&buf[pos]);

						if(size > 65)
						{		
#if (OUTPUT && _WIN32)
							OutputDebugString("LUT too big");
#endif
							return 0;
						}
#if (OUTPUT && _WIN32)
						printf("size = %d\n",size);
#endif
						LUT = (float *)malloc(size*size*size*sizeof(float)*3);
						if(LUT == NULL)
						{
#if (OUTPUT && _WIN32)
							OutputDebugString("no memory\n");
#endif
							return 0;
						}
						iLUT = (unsigned char *)malloc(size*size*size*sizeof(unsigned char)*3);
						if(iLUT == NULL)
						{
#if (OUTPUT && _WIN32)
							OutputDebugString("no memory\n");
#endif
							free(LUT);
							return 0;
						}
					
						SIZEfound = true;
						break;

					}
					pos++;
				} while(pos < len-10);
			}
			else if(!DATAfound)
			{
				do
				{
					if(strncmp(&buf[pos],"<data>",6) == 0)
					{
						int j = 0;
						pos+=6;
						while(	!( (buf[pos+j] >= '0' && buf[pos+j] <= '9') || 
								(buf[pos+j] >= 'a' && buf[pos+j] <= 'f') ||
								(buf[pos+j] >= 'A' && buf[pos+j] <= 'F') ))
							pos++;

						//printf("%s\n",&buf[pos+j]);
						
						DATAfound = true;
						break;

					}
					pos++;
				} while(pos < len-256);
			}
			else if(DATAfound)
			{
				char hexstring[12] = "00000000";
				do
				{
					while(	!(  (buf[pos] >= '0' && buf[pos] <= '9') || 
								(buf[pos] >= 'a' && buf[pos] <= 'f') ||
								(buf[pos] >= 'A' && buf[pos] <= 'F') ))
					{
							if(buf[pos] == '"' || buf[pos] == '<')
							{
								finished = true;
#if (OUTPUT && _WIN32)
								OutputDebugString("finished\n");
#endif							
								break;
							}
							pos++;
					}

					if(!finished)
					{
						float val;
						hexstring[0] = buf[pos+6];
						hexstring[1] = buf[pos+7];
						hexstring[2] = buf[pos+4];
						hexstring[3] = buf[pos+5];
						hexstring[4] = buf[pos+2];
						hexstring[5] = buf[pos+3];
						hexstring[6] = buf[pos+0];
						hexstring[7] = buf[pos+1];

						//printf("%s",hexstring);
#ifdef _WIN32
						sscanf_s(hexstring, "%08x", (int *)&val);
#else
						sscanf(hexstring, "%08x", (int *)&val);
#endif
#if OUTPUT && 0
						printf("%6.3f",val);
#endif
						LUT[entries] = val;
						rgbpos++;
						entries++;
						if(rgbpos < 3)
						{
#if OUTPUT && 0
							printf(",");
#endif
						}
						else
						{
#if OUTPUT && 0
							printf("\n");
#endif
							rgbpos = 0;
						}
					}

					pos+=8;
				} while(pos < len-16 && !finished);
			}

			

		//	printf("len = %d\n", len);
		}
		while(len > 0 && !finished);

		fclose(fp);

		if(finished && (size*size*size*3 == entries))
		{
			// valid 3D LUT
			crc = look_calc_crc((unsigned char *)LUT, entries*4);
			//char fullpath[MAX_PATH];
			//if(0 == ::GetLongPathName(path, fullpath, MAX_PATH))
			//	strcat(fullpath, path);

			//GenerateLUTfile(crc, LUT, size, fullpath);
		}

        free(LUT);
        free(iLUT);
	}
	return crc;
}

CFHD_Error CSampleEncodeMetadata::AddGUID()
{
#ifdef _WIN32
	//UUID guid;
	//UuidCreate(&guid);
	GUID guid;
	CoCreateGuid(&guid);
#else
	uuid_t guid;
	uuid_generate(guid);
#endif

	return AddMetadata(&global[0], TAG_CLIP_GUID, (int)'G', 16, (uint32_t *)&guid) == true ? CFHD_ERROR_OKAY : CFHD_ERROR_UNEXPECTED;
}

CFHD_Error CSampleEncodeMetadata::AddLookFile(METADATA_TYPE ctype,
											  METADATA_SIZE size,
											  uint32_t *data)
{
	//TODO: Replace uses of unsigned int with size_t
	assert(size <= METADATA_SIZE_MAX);
	METADATA_SIZE retsize = size;
	METADATA_TYPE rettype = ctype;
	void *retdata = (void *)data;
	uint32_t crc = 0;

	// Look in the global metadata for the look CRC
	if (!(retdata = MetadataFind(global[0].block, global[0].size,
		   TAG_LOOK_CRC, &retsize, &rettype)))
	{
		// if not LCRC generate a look CRC, assuming a full path is provided for the LUT (only the filename will be stored
		crc = ValidateLookGenCRCEnc((char *)data);
	}
	
	{
#if _WIN32
		char filename[260] = {0};
		size_t filenamelen = 0;

		char drive[_MAX_DRIVE];
		char dir[_MAX_DIR];
		char fname[_MAX_FNAME];
		char ext[_MAX_EXT];

		_splitpath_s((char *)data, drive, _MAX_DRIVE, dir, _MAX_DIR, fname, _MAX_FNAME, ext, _MAX_EXT);
		_makepath_s(filename, sizeof(filename), NULL, NULL, fname, ext);
		filenamelen = strlen(filename);

#elif __APPLE_REMOVE__

		char filename[260] = {0};
		size_t filenamelen = 0;

		CFStringRef cfPath;
		CFURLRef url;

		cfPath = CFStringCreateWithCString(kCFAllocatorDefault, (char *)data, kCFStringEncodingASCII);
		url = CFURLCreateWithString(NULL, cfPath, NULL);
		if(url)
		{
			CFStringRef lastPathComponent = CFURLCopyLastPathComponent(url);
			if(lastPathComponent)
			{
				filenamelen = CFStringGetLength(lastPathComponent);
				CFStringGetCString(lastPathComponent, filename, filenamelen+1, kCFStringEncodingASCII);
				CFRelease(lastPathComponent);
			}
			CFRelease(url);
		}

#else
		char *filename = basename((char *)data);
		size_t filenamelen = strlen(filename);
#endif
		if (filenamelen)
		{
			if(filenamelen < 40)
			{
				memset(&filename[filenamelen], 0, 40-filenamelen);
				filenamelen = 40;
			}

			AddMetadata(&global[0], TAG_LOOK_FILE, rettype, (uint32_t)filenamelen, (uint32_t *)filename);
			
			if(crc) // generated
				AddMetadata(&global[0], TAG_LOOK_CRC, 'H', 4, (uint32_t *)&crc);

			return CFHD_ERROR_OKAY;
		}
	}

	return CFHD_ERROR_UNEXPECTED;
}

CFHD_Error CSampleEncodeMetadata::AddTimeStamp(const char *date, const char *time)
{
	if (AddMetadata(&global[0], TAG_ENCODE_DATE, 'c', 10, (uint32_t *)date) &&
		AddMetadata(&global[0], TAG_ENCODE_TIME, 'c', 8, (uint32_t *)time))
	{
		return CFHD_ERROR_OKAY;
	}
	return CFHD_ERROR_UNEXPECTED;
}

CFHD_Error CSampleEncodeMetadata::AddTimeCode(const char *timecode, bool local_metadata)
{
	if (local_metadata)
	{
		AddMetadata(&local, TAG_TIMECODE, 'c', 11, (uint32_t *)timecode);
	}
	else
	{
		// Store the timecode in the metadata for all frames
		AddMetadata(&global[0], TAG_TIMECODE, 'c', 11, (uint32_t *)timecode);
	}

	return CFHD_ERROR_OKAY;
}

CFHD_Error CSampleEncodeMetadata::AddFrameNumber(uint32_t framenum, bool local_metadata)
{
	if (local_metadata)
	{
		AddMetadata(&local, TAG_UNIQUE_FRAMENUM, (int)'L', 4, (uint32_t *)&framenum);
	}
	else
	{
		AddMetadata(&global[0], TAG_UNIQUE_FRAMENUM, (int)'L', 4, (uint32_t *)&framenum);
	}

	return CFHD_ERROR_OKAY;
}




/*!
	@brief Free a metadata buffer (local or global)

	The low level API in the codec library takes pointers to the buffer
	pointer and size.  This routine intentionally allows the low level API
	to clear the local values passed as arguments.  The caller should set
	the buffer pointer to null and the buffer size to zero.
*/
void CSampleEncodeMetadata::ReleaseMetadata(METADATA *metadata)
{
	FreeMetadata(metadata);
}
