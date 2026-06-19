/*! @file lutpath.h

*  @brief Active MetadataTools
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

// Define strings for the database locations on Windows
#define OVERRIDE_PATH_STRING	"C:/Users/Public/CineForm"
#define LUT_PATH_STRING			"C:/Users/Public/CineForm/LUTs"
#define DATABASE_PATH_STRING	"db"
#define SETTINGS_PATH_STRING	"C:/Users/Public/CineForm/dbsettings"

#elif __APPLE_REMOVE__

//TODO: Define strings for the database locations on Macintosh

#else

// Define strings for the database locations on Linux
#define OVERRIDE_PATH_STRING	"/var/cineform/public"
#define LUT_PATH_STRING			"/var/cineform/public/LUTs"
#define DATABASE_PATH_STRING	"db"
#define SETTINGS_PATH_STRING	"/etc/cineform/dbsettings"

#endif


#ifdef __cplusplus
extern "C" {
#endif

// Open the logfile used for reporting errors
FILE *OpenLogFile();

// Routine that initializes the LUT paths in the decoder
void InitLUTPaths(struct decoder *decoder);

// Newer name for the decoder LUT paths routine
void InitLUTPathsDec(struct decoder *decoder);

// Routine that initializes the LUT paths in the encoder
void InitLUTPathsEnc(struct encoder *encoder);
void WriteLastGUIDAndFrame(DECODER *decoder, int checkdiskinfotime);
void OverrideCFHDDATA(struct decoder *decoder, unsigned char *lpCurrentBuffer, int nWordsUsed);
void OverrideCFHDDATAUsingParent(struct decoder *decoder, struct decoder *parentDecoder, unsigned char *lpCurrentBuffer, int nWordsUsed);

#if !defined(_WIN32) && !defined(__APPLE_REMOVE__)

// Use the lexical scanner to parse the user preferences file on Linux
#include "scanner.h"

// Parse the LUT and database path strings from the user preferences file
CODEC_ERROR ParseUserMetadataPrefs(FILE *file,
								   SCANNER *scanner,
								   char *lut_pathname_string,
								   size_t lut_pathname_size,
								   char *database_filename_string,
								   size_t database_filename_size);

#endif

#ifdef __cplusplus
}
#endif


#ifdef __cplusplus

extern "C"
FILE *OpenUserPrefsFile(char *actual_pathname = NULL,	//!< Return the actual pathname
						size_t actual_size = 0			//!< Return the pathname size (in bytes)
						);
#else

FILE *OpenUserPrefsFile(char *actual_pathname,			//!< Return the actual pathname
						size_t actual_size				//!< Return the pathname size (in bytes)
						);
#endif
