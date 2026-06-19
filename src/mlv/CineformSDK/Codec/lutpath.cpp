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


#include "stdafx.h"
#include "config.h"
#include "encoder.h"
#include <string>

#ifdef _WIN32
// Must include the following file for Visual Studio 2005 (not required for Visual Studio 2003)
//#include <atlbase.h>
#include <tchar.h>
#else
#include <errno.h>
#endif

#include "../Common/AVIExtendedHeader.h"

#define MAX_PATH	260

#include "codec.h"
#include "lutpath.h"


#ifdef _WIN32

// Define the locations of registry keys for color processing
#define REG_COLORPROCESSING_PATH _T("SOFTWARE\\CineForm\\ColorProcessing") // HKCU Where default props are stored
#define REG_COLORPROCESSING_PREMIERE_KEY _T("Premiere") // The default Decoder resolution key
#define REG_COLORPROCESSING_DEFAULT_KEY _T("Default") // The default Decoder resolution key
#define REG_COLORPROCESSING_CS_OVERRIDE_KEY _T("ColorSpaceOverride")  //Use to tell the decode that the source was not what it think it is.
#define REG_COLORPROCESSING_LAST_GUID_KEY _T("LastGUID")  // Last GUID played
#define REG_COLORPROCESSING_FRAME_COUNT_KEY _T("LastFrameCount")  // Last frame count played
#define REG_COLORPROCESSING_LAST_TIMECODE_KEY _T("LastTimecode")  // Last timecode played


//TODO: Remove this routine as it does not appear to be used anywhere

static int
WINAPI
lstrlenWInternal(
    LPCWSTR lpString
    )
{
    int i = -1;
    while (*(lpString+(++i)))
        ;
    return i;
}

#elif __APPLE_REMOVE__

//TODO: define subroutines for parsing the preferences on Macintosh

#else

#include "scanner.h"

// Define the maximum length of a keyword including the terminating nul
#define KEYWORD_MAX 64

#ifndef PATH_MAX
#define PATH_MAX _MAX_PATH
#endif

#ifndef DEBUG
#define DEBUG (1 && _DEBUG)
#endif


// Define the token values
enum
{
	// The lookup routine assumes that the unknown opcode is zero
	OPCODE_UNKNOWN = 0,
	OPCODE_DBPATH,
	OPCODE_LUTPATH,
	OPCODE_OVERRIDE_PATH,

};

// Table of token keywords and values
TOKEN token_table[] =
{
	{ (char *)"DBPath", OPCODE_DBPATH},
	{ (char *)"LUTPath", OPCODE_LUTPATH},
	{ (char *)"OverridePath", OPCODE_OVERRIDE_PATH},
};
const int token_table_length = sizeof(token_table) / sizeof(token_table[0]);


#define CHECK_ERROR(err)	{ int _e = (err); if (_e != SCANNER_ERROR_OKAY) \
	return ((_e == SCANNER_ERROR_EOF) ? CODEC_ERROR_OKAY : CODEC_ERROR_PREFSFILE); }

#define BREAK_ERROR(err)	{ int _e = (err); if (_e != SCANNER_ERROR_OKAY) break; }

#define IGNORE_ERROR(err, e1, e2)	{ if (err == e1) err = e2; }

#define STRING_LENGTH(str)	(sizeof(str)/sizeof(str[0]))


// Safely copy a string after clearing the destination
void CopyString(char *target, const char *source, size_t size)
{
	int length = size / sizeof(target[0]);
	memset(target, 0, size);
	strncpy(target, source, length);
	target[length - 1] = '\0';
}

// Open the first user preferences file that exists
FILE *OpenUserPrefsFile(char *actual_pathname, size_t actual_size)
{
	// The system-wide preferences files are listed in search order
	static const char *system_preferences_path[] =
	{
		"/etc/cineform/dbsettings",
		"/usr/local/cineform/etc/dbsettings",
	};
	const int system_preferences_path_count =
			sizeof(system_preferences_path) / sizeof(system_preferences_path[0]);

	// Look for a preferences file in the user home directory
	const char *home_dir = getenv("HOME");
	if (home_dir)
	{
		// Initialize the preferences path with the user home directory
		std::string pathname(home_dir);
		pathname.append("/.cineform/dbsettings");

		// Does the user have a preferences file?
		FILE *file = fopen(pathname.c_str(), "r");
		if (file)
		{
			if (actual_pathname)
			{
				// Return the actual preferences pathname for error messages
				int actual_length = actual_size / sizeof(actual_pathname[0]);
				strncpy(actual_pathname, pathname.c_str(), actual_length);
				actual_pathname[actual_length - 1] = '\0';
			}
			return file;
		}
	}

	// Look for a system-wide preferences file
	for (int i = 0; i < system_preferences_path_count; i++)
	{
		// Try to open the preferences file
		FILE *file = fopen(system_preferences_path[i], "r");
		if (file)
		{
			if (actual_pathname)
			{
				// Return the actual preferences pathname for error messages
				int actual_length = actual_size / sizeof(actual_pathname[0]);
				strncpy(actual_pathname, system_preferences_path[i], actual_length);
				actual_pathname[actual_length - 1] = '\0';
			}
			return file;
		}
	}

	return NULL;
}

FILE *OpenLogFile()
{
	// Open the logfile for reporting errors
	FILE *logfile = fopen("/var/cineform/public/messages", "a");
	return logfile;
}

// Set decoder parameters from the preferences file
CODEC_ERROR ParseUserDecoderPrefs(FILE *file, SCANNER *scanner, DECODER *decoder)
{
	// Buffer for keywords
	char keyword[KEYWORD_MAX];
	//int keyword_count = 0;
	char *result_string = NULL;
	int result_length = 0;

	InitScanner(scanner, file);
	while (scanner->error == SCANNER_ERROR_OKAY)
	{
		BREAK_ERROR(SkipBlanks(scanner));

		// Found a comment?
		if (scanner->c == '#') {
			// Skip the rest of the line
			CHECK_ERROR(SkipLine(scanner));

			// Loop to process the next line
			continue;
		}

		// Found the start of a keyword?
		if (isalpha(scanner->c)) {
			CHECK_ERROR(ScanKeyword(scanner, keyword, KEYWORD_MAX));
		}

		// Lookup the keyword
		int opcode = Lookup(keyword, token_table, token_table_length);

		// Set the output location for the argument string
		switch (opcode)
		{
		case OPCODE_DBPATH:
			//result_string = result.dbpath;
			result_string = decoder->UserDBPathStr;
			result_length = STRING_LENGTH(decoder->UserDBPathStr);
			break;

		case OPCODE_LUTPATH:
			//result_string = result.lutpath;
			result_string = decoder->LUTsPathStr;
			result_length = STRING_LENGTH(decoder->LUTsPathStr);
			break;

		case OPCODE_OVERRIDE_PATH:
			//result_string = result.overridepath;
			result_string = decoder->OverridePathStr;
			result_length = STRING_LENGTH(decoder->OverridePathStr);
			break;

		case OPCODE_UNKNOWN:
		default:
			CHECK_ERROR(scanner->error = SCANNER_ERROR_KEYWORD);
			break;
		}

		// Skip whitespace between the keyword and the argument string
		BREAK_ERROR(SkipBlanks(scanner));

		//int result_count = 0;
		if (scanner->c == '\"')
		{
			// Copy the argument string into the result
			CHECK_ERROR(CopyQuotedString(scanner, result_string, result_length));
		}
		else
		{
			// Copy the argument string without trailing spaces
			CHECK_ERROR(CopyTrimmedString(scanner, result_string, result_length));
		}

#if (0 && DEBUG)
		printf("Keyword: \"%s\", argument string: \"%s\"\n",
			Keyword(opcode, token_table, token_table_length),
			result_string);
#endif
		// Skip the rest of the line
		CHECK_ERROR(SkipLine(scanner));
	}

	// Do not report end of file as an error
	IGNORE_ERROR(scanner->error, SCANNER_ERROR_EOF, SCANNER_ERROR_OKAY);

	return (scanner->error == SCANNER_ERROR_OKAY ? CODEC_ERROR_OKAY : CODEC_ERROR_PREFSFILE);
}

// Set metadata parameters from the preferences file
CODEC_ERROR ParseUserMetadataPrefs(FILE *file,
								   SCANNER *scanner,
								   char *lut_pathname_string,
								   size_t lut_pathname_size,
								   char *database_filename_string,
								   size_t database_filename_size)
{
	// Buffer for keywords
	char keyword[KEYWORD_MAX];
	//int keyword_count = 0;
	char *result_string = NULL;
	int result_length = 0;

	const int lut_pathname_length = lut_pathname_size / sizeof(lut_pathname_string[0]);
	const int database_filename_length = database_filename_size / sizeof(database_filename_string[0]);

	InitScanner(scanner, file);
	while (scanner->error == SCANNER_ERROR_OKAY)
	{
		BREAK_ERROR(SkipBlanks(scanner));

		// Found a comment?
		if (scanner->c == '#') {
			// Skip the rest of the line
			CHECK_ERROR(SkipLine(scanner));

			// Loop to process the next line
			continue;
		}

		// Found the start of a keyword?
		if (isalpha(scanner->c)) {
			CHECK_ERROR(ScanKeyword(scanner, keyword, KEYWORD_MAX));
		}

		// Lookup the keyword
		int opcode = Lookup(keyword, token_table, token_table_length);

		// Set the output location for the argument string
		switch (opcode)
		{
		case OPCODE_DBPATH:
			result_string = database_filename_string;
			result_length = database_filename_length;
			break;

		case OPCODE_LUTPATH:
			result_string = lut_pathname_string;
			result_length = lut_pathname_length;
			break;

		case OPCODE_OVERRIDE_PATH:
			//result_string = decoder->OverridePathStr;
			//result_length = STRING_LENGTH(decoder->OverridePathStr);
			CHECK_ERROR(SkipLine(scanner));
			continue;
			break;

		case OPCODE_UNKNOWN:
		default:
			CHECK_ERROR(scanner->error = SCANNER_ERROR_KEYWORD);
			break;
		}

		// Skip whitespace between the keyword and the argument string
		BREAK_ERROR(SkipBlanks(scanner));

		//int result_count = 0;
		if (scanner->c == '\"')
		{
			// Copy the argument string into the result
			CHECK_ERROR(CopyQuotedString(scanner, result_string, result_length));
		}
		else
		{
			// Copy the argument string without trailing spaces
			CHECK_ERROR(CopyTrimmedString(scanner, result_string, result_length));
		}

#if (0 && DEBUG)
		printf("Keyword: \"%s\", argument string: \"%s\"\n",
			Keyword(opcode, token_table, token_table_length),
			result_string);
#endif
		// Skip the rest of the line
		CHECK_ERROR(SkipLine(scanner));
	}

	// Do not report end of file as an error
	IGNORE_ERROR(scanner->error, SCANNER_ERROR_EOF, SCANNER_ERROR_OKAY);

	return (scanner->error == SCANNER_ERROR_OKAY ? CODEC_ERROR_OKAY : CODEC_ERROR_PREFSFILE);
}

// Set encoder parameters from the preferences file
CODEC_ERROR ParseUserEncoderPrefs(FILE *file, SCANNER *scanner, ENCODER *encoder)
{
	// Buffer for keywords
	char keyword[KEYWORD_MAX];
	//int keyword_count = 0;
	char *result_string = NULL;
	int result_length = 0;

	InitScanner(scanner, file);
	while (scanner->error == SCANNER_ERROR_OKAY)
	{
		BREAK_ERROR(SkipBlanks(scanner));

		// Found a comment?
		if (scanner->c == '#') {
			// Skip the rest of the line
			CHECK_ERROR(SkipLine(scanner));

			// Loop to process the next line
			continue;
		}

		// Found the start of a keyword?
		if (isalpha(scanner->c)) {
			CHECK_ERROR(ScanKeyword(scanner, keyword, KEYWORD_MAX));
		}

		// Lookup the keyword
		int opcode = Lookup(keyword, token_table, token_table_length);

		// Set the output location for the argument string
		switch (opcode)
		{
		case OPCODE_DBPATH:
			//result_string = result.dbpath;
			result_string = encoder->UserDBPathStr;
			result_length = STRING_LENGTH(encoder->UserDBPathStr);
			break;

		case OPCODE_LUTPATH:
			//result_string = result.lutpath;
			result_string = encoder->LUTsPathStr;
			result_length = STRING_LENGTH(encoder->LUTsPathStr);
			break;

		case OPCODE_OVERRIDE_PATH:
			//result_string = result.overridepath;
			result_string = encoder->OverridePathStr;
			result_length = STRING_LENGTH(encoder->OverridePathStr);
			break;

		case OPCODE_UNKNOWN:
		default:
			CHECK_ERROR(scanner->error = SCANNER_ERROR_KEYWORD);
			break;
		}

		// Skip whitespace between the keyword and the argument string
		BREAK_ERROR(SkipBlanks(scanner));

		//int result_count = 0;
		if (scanner->c == '\"')
		{
			// Copy the argument string into the result
			CHECK_ERROR(CopyQuotedString(scanner, result_string, result_length));
		}
		else
		{
			// Copy the argument string without trailing spaces
			CHECK_ERROR(CopyTrimmedString(scanner, result_string, result_length));
		}

#if (0 && DEBUG)
		printf("Keyword: \"%s\", argument string: \"%s\"\n",
			Keyword(opcode, token_table, token_table_length),
			result_string);
#endif
		// Skip the rest of the line
		CHECK_ERROR(SkipLine(scanner));
	}

	// Do not report end of file as an error
	IGNORE_ERROR(scanner->error, SCANNER_ERROR_EOF, SCANNER_ERROR_OKAY);

	return (scanner->error == SCANNER_ERROR_OKAY ? CODEC_ERROR_OKAY : CODEC_ERROR_PREFSFILE);
}

#endif


// Newer name for the decoder LUT paths routine
void InitLUTPathsDec(struct decoder *decoder)
{
	InitLUTPaths(decoder);
}

void InitLUTPaths(DECODER *decoder)
{
	if(decoder)
	{
#ifdef _WIN32
		DWORD dwType = REG_SZ, length = 260;
		HKEY hKey = 0;
		const char* CPsubkey = "SOFTWARE\\CineForm\\ColorProcessing";

		char defaultLUTpath[260] = "NONE";
		char defaultOverridePath[260] = "";
		char DbNameStr[64] = "db";

		RegOpenKey(HKEY_CURRENT_USER, CPsubkey, &hKey);
		if (hKey != 0)
		{
			length = 260;
			RegQueryValueEx(hKey, "LUTPath", NULL, &dwType, (LPBYTE)defaultLUTpath, &length);
			length = 260;
			RegQueryValueEx(hKey, "OverridePath", NULL, &dwType, (LPBYTE)defaultOverridePath, &length);
			length = 64;
			RegQueryValueEx(hKey, "DBPath", NULL, &dwType, (LPBYTE)DbNameStr, &length);
		}

		if (0 == strcmp(defaultLUTpath, "NONE"))
		{
			int n;
			char PublicPath[80];

			if (n = GetEnvironmentVariable("PUBLIC", PublicPath, 79)) // Vista and Win7
			{
				_stprintf_s(defaultLUTpath, sizeof(defaultLUTpath), _T("%s\\%s"), PublicPath, _T("CineForm\\LUTs")); //Vista & 7 default
				_stprintf_s(defaultOverridePath, sizeof(defaultOverridePath), _T("%s\\%s"), PublicPath, _T("CineForm\\LUTs")); //Vista & 7 default
			}
			else
			{
				const char* CVsubkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion";
				RegOpenKey(HKEY_LOCAL_MACHINE, CVsubkey, &hKey);
				if (hKey != 0)
				{
					char commonpath[64] = "NONE";
					length = 64;
					RegQueryValueEx(hKey, "CommonFilesDir (x86)", NULL, &dwType, (LPBYTE)commonpath, &length);
					if (0 == strcmp(commonpath, "NONE"))
					{
						length = 64;
						RegQueryValueEx(hKey, "CommonFilesDir", NULL, &dwType, (LPBYTE)commonpath, &length);
					}
					_stprintf_s(defaultLUTpath, sizeof(defaultLUTpath), _T("%s\\%s"), commonpath, _T("CineForm\\LUTs"));
					_stprintf_s(defaultOverridePath, sizeof(defaultOverridePath), _T("%s\\%s"), commonpath, _T("CineForm\\LUTs"));
				}
			}
		}

		strncpy_s(decoder->OverridePathStr, sizeof(decoder->OverridePathStr), defaultOverridePath, sizeof(decoder->OverridePathStr));
		strncpy_s(decoder->LUTsPathStr, sizeof(decoder->LUTsPathStr), defaultLUTpath, sizeof(decoder->LUTsPathStr));
		strncpy_s(decoder->UserDBPathStr, sizeof(decoder->UserDBPathStr), DbNameStr, sizeof(decoder->UserDBPathStr));

#elif __APPLE_REMOVE__
		CFPropertyListRef	appValue;
		
		CFPreferencesAppSynchronize( CFSTR("com.cineform.codec") );
		
		appValue = CFPreferencesCopyAppValue( CFSTR("OverridePath"),  CFSTR("com.cineform.codec") );
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(decoder->OverridePathStr, pathStr);
			} else {
				strcpy(decoder->OverridePathStr, "/Library/Application Support/CineForm");
			}
		}
		else
		{
			strcpy(decoder->OverridePathStr, "/Library/Application Support/CineForm");
		}
		appValue = CFPreferencesCopyAppValue( CFSTR("LUTsPath"),  CFSTR("com.cineform.codec") );
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(decoder->LUTsPathStr, pathStr);
			}
			else
			{
				if( !CFStringGetCString( (CFStringRef)appValue, decoder->LUTsPathStr, 260, kCFStringEncodingASCII) ) {
					strcpy(decoder->LUTsPathStr, "/Library/Application Support/CineForm/LUTs");
				}
			}
		}
		else
		{
			strcpy(decoder->LUTsPathStr, "/Library/Application Support/CineForm/LUTs");
		}
		appValue = CFPreferencesCopyAppValue( CFSTR("CurrentDBPath"),  CFSTR("com.cineform.codec") );
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(decoder->UserDBPathStr, pathStr);
			}
			else
			{
				if( !CFStringGetCString( (CFStringRef)appValue, decoder->UserDBPathStr, 260, kCFStringEncodingASCII) ) {
					strcpy(decoder->UserDBPathStr, "db");
				}
			}
		}
		else
		{
			strcpy(decoder->UserDBPathStr, "db");
		}	
#else
		// Initialize the default locations on Linux
		CopyString(decoder->OverridePathStr, OVERRIDE_PATH_STRING, sizeof(decoder->OverridePathStr));
		CopyString(decoder->LUTsPathStr, LUT_PATH_STRING, sizeof(decoder->LUTsPathStr));
		CopyString(decoder->UserDBPathStr, DATABASE_PATH_STRING, sizeof(decoder->UserDBPathStr));

		// Open the first users preferences file that exists
		//FILE *file = fopen(SETTINGS_PATH_STRING, "r");
		char pathname[PATH_MAX];
		FILE *file = OpenUserPrefsFile(pathname, sizeof(pathname));
		if (file)
		{
			SCANNER scanner;

			// Parse the preferences file and set parameters in the decoder
			CODEC_ERROR error = ParseUserDecoderPrefs(file, &scanner, decoder);
			if (error != CODEC_ERROR_OKAY)
			{
				// Restore the default paths
				CopyString(decoder->OverridePathStr, OVERRIDE_PATH_STRING, sizeof(decoder->OverridePathStr));
				CopyString(decoder->LUTsPathStr, LUT_PATH_STRING, sizeof(decoder->LUTsPathStr));
				CopyString(decoder->UserDBPathStr, DATABASE_PATH_STRING, sizeof(decoder->UserDBPathStr));

				// Report the error code and line number from the scanner
				FILE *logfile = OpenLogFile();
				if (logfile)
				{
					int error = scanner.error;
					fprintf(logfile, "Error %s line %d: %s (%d)\n", pathname, scanner.line, Message(error), error);
					fclose(logfile);
				}
			}

			fclose(file);
		}
#endif
	}
}

void InitLUTPathsEnc(ENCODER *encoder)
{
	if(encoder && encoder->LUTsPathStr[0] == 0)
#ifdef _WIN32
	{
		DWORD dwType = REG_SZ, length = 260;
		HKEY hKey = 0;
		const char* CPsubkey = "SOFTWARE\\CineForm\\ColorProcessing";

		char defaultLUTpath[260] = "NONE";
		char defaultOverridePath[260] = "";
		char DbNameStr[64] = "db";

		RegOpenKey(HKEY_CURRENT_USER, CPsubkey, &hKey);
		if (hKey != 0)
		{
			length = 260;
			RegQueryValueEx(hKey, "LUTPath", NULL, &dwType, (LPBYTE)defaultLUTpath, &length);
			length = 260;
			RegQueryValueEx(hKey, "OverridePath", NULL, &dwType, (LPBYTE)defaultOverridePath, &length);
			length = 64;
			RegQueryValueEx(hKey, "DBPath", NULL, &dwType, (LPBYTE)DbNameStr, &length);
		}

		if (0 == strcmp(defaultLUTpath, "NONE"))
		{
			int n;
			char PublicPath[80];

			if (n = GetEnvironmentVariable("PUBLIC", PublicPath, 79)) // Vista and Win7
			{
				_stprintf_s(defaultLUTpath, sizeof(defaultLUTpath), _T("%s\\%s"), PublicPath, _T("CineForm\\LUTs")); //Vista & 7 default
				_stprintf_s(defaultOverridePath, sizeof(defaultOverridePath), _T("%s\\%s"), PublicPath, _T("CineForm\\LUTs")); //Vista & 7 default
			}
			else
			{
				const char* CVsubkey = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion";
				RegOpenKey(HKEY_LOCAL_MACHINE, CVsubkey, &hKey);
				if (hKey != 0)
				{
					char commonpath[64] = "NONE";
					length = 64;
					RegQueryValueEx(hKey, "CommonFilesDir (x86)", NULL, &dwType, (LPBYTE)commonpath, &length);
					if (0 == strcmp(commonpath, "NONE"))
					{
						length = 64;
						RegQueryValueEx(hKey, "CommonFilesDir", NULL, &dwType, (LPBYTE)commonpath, &length);
					}
					_stprintf_s(defaultLUTpath, sizeof(defaultLUTpath), _T("%s\\%s"), commonpath, _T("CineForm\\LUTs"));
					_stprintf_s(defaultOverridePath, sizeof(defaultOverridePath), _T("%s\\%s"), commonpath, _T("CineForm\\LUTs"));
				}
			}
		}

		strncpy_s(encoder->OverridePathStr, sizeof(encoder->OverridePathStr), defaultOverridePath, sizeof(encoder->OverridePathStr));
		strncpy_s(encoder->LUTsPathStr, sizeof(encoder->LUTsPathStr), defaultLUTpath, sizeof(encoder->LUTsPathStr));
		strncpy_s(encoder->UserDBPathStr, sizeof(encoder->UserDBPathStr), DbNameStr, sizeof(encoder->UserDBPathStr));
		
	}
#elif __APPLE_REMOVE__
	{
		CFPropertyListRef	appValue;
		
		CFPreferencesAppSynchronize( CFSTR("com.cineform.codec") );
		
		appValue = CFPreferencesCopyAppValue( CFSTR("OverridePath"),  CFSTR("com.cineform.codec") );
		//fprintf(stderr,"(Enc)AppValue for OverridePath = %08x\n",appValue);
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(encoder->OverridePathStr, pathStr);
			} else
			{
				strcpy(encoder->OverridePathStr, "/Library/Application Support/CineForm");
			}
		}  else
		{
			strcpy(encoder->OverridePathStr, "/Library/Application Support/CineForm");
		}
		appValue = CFPreferencesCopyAppValue( CFSTR("LUTsPath"),  CFSTR("com.cineform.codec") );
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(encoder->LUTsPathStr, pathStr);
			}
			else
			{
				strcpy(encoder->LUTsPathStr, "/Library/Application Support/CineForm/LUTs");
			}
		}
		else
		{
			strcpy(encoder->LUTsPathStr, "/Library/Application Support/CineForm/LUTs");
		}
		//strcpy(decoder->OverridePathStr, "/Library/Application Support/CineForm/LUTs");
		appValue = CFPreferencesCopyAppValue( CFSTR("CurrentDBPath"),  CFSTR("com.cineform.codec") );
		if ( appValue && CFGetTypeID(appValue) == CFStringGetTypeID()  ) {
			const char	*	pathStr = CFStringGetCStringPtr( (CFStringRef)appValue, kCFStringEncodingASCII);
			if(pathStr) {
				strcpy(encoder->UserDBPathStr, pathStr);
			}
			else
			{
				if( !CFStringGetCString( (CFStringRef)appValue, encoder->UserDBPathStr, 260, kCFStringEncodingASCII) ) {
					strcpy(encoder->UserDBPathStr, "db");
				}
			}
		}
		else
		{
			strcpy(encoder->UserDBPathStr, "db");
		}
	}
#else
	{
		// Initialize the default locations on Linux
		strncpy(encoder->OverridePathStr, OVERRIDE_PATH_STRING, STRING_LENGTH(encoder->OverridePathStr));
		strncpy(encoder->LUTsPathStr, LUT_PATH_STRING, STRING_LENGTH(encoder->LUTsPathStr));
		strncpy(encoder->UserDBPathStr, DATABASE_PATH_STRING, STRING_LENGTH(encoder->UserDBPathStr));

		//FILE *file = fopen(SETTINGS_PATH_STRING, "r");
		FILE *file = OpenUserPrefsFile();
		if(file)
		{
			//TODO: Parse the encoder preferences file
			assert(0);

			fclose(file);
		}
	}
#endif
}
void WriteLastGUIDAndFrame(DECODER *decoder, int checkdiskinfotime)
{
    //GetColorProcessingOverides()
#ifdef _WIN32
    {
		CFHDDATA *cfhddata = &decoder->cfhddata;
        HKEY key;
        //OutputDebugString("RegOpenKeyEx");

        if(RegOpenKeyEx(HKEY_CURRENT_USER,
                        REG_COLORPROCESSING_PATH,
                        0,
                        KEY_QUERY_VALUE | KEY_SET_VALUE,
                        &key) == ERROR_SUCCESS)
        {
            // Use a different registry value for preview in Premiere
            LPTSTR lpValueName;
            
            //if(IsDecoderEmbedded())
            if(decoder->premiere_embedded)
            {
                lpValueName = REG_COLORPROCESSING_PREMIERE_KEY;
                //OutputDebugString("IsDecoderEmbedded");
            }
            else
            {
                lpValueName = REG_COLORPROCESSING_DEFAULT_KEY;
                //OutputDebugString("not embedded");
            }
            
            int32_t data;
            DWORD size = sizeof(data);
            
            if((cfhddata->process_path_flags_mask == 0 || cfhddata->process_path_flags_mask == -1 || checkdiskinfotime == 1)) // Only read a new mask is not already set externally. //DAN20120104 -- added checkdiskinfotime as it stopped being checked in Vegas/Premiere
            {
                if (RegQueryValueEx(key,			// handle to key
                                    lpValueName,	//REG_RES_KEY,  // value name
                                    NULL,			// Reserved
                                    NULL,			// Value type buffer
                                    (BYTE *)&data,	// Data buffer
                                    &size			// Size of data buffer
                                    ) == ERROR_SUCCESS)
                {
                    cfhddata->process_path_flags_mask = data;
                    //OutputDebugString("Get Mask");
                }
                else
                {
                    //OutputDebugString("Mask not read");
                }
            }
            
            if (RegQueryValueEx(key,			// handle to key
                                REG_COLORPROCESSING_CS_OVERRIDE_KEY, // value name
                                NULL,			// Reserved
                                NULL,			// Value type buffer
                                (BYTE *)&data,	// Data buffer
                                &size			// Size of data buffer
                                ) == ERROR_SUCCESS)
            {
                if(data)
                    decoder->frame.colorspace = data;
                decoder->frame.colorspace_override = data;
            }
            
            
            if(cfhddata->update_last_used) // used by FightLight
            {
                myGUID lastGUID = cfhddata->clip_guid;
                
                //if(lastGUID.Data1 || lastGUID.Data2 || lastGUID.Data3)
                {
                    char TextGUID[64];
					sprintf_s(TextGUID, sizeof(TextGUID),
							"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                            lastGUID.Data1,
                            lastGUID.Data2,
                            lastGUID.Data3,
                            lastGUID.Data4[0],
                            lastGUID.Data4[1],
                            lastGUID.Data4[2],
                            lastGUID.Data4[3],
                            lastGUID.Data4[4],
                            lastGUID.Data4[5],
                            lastGUID.Data4[6],
                            lastGUID.Data4[7]);
                    RegSetValueEx(key, REG_COLORPROCESSING_LAST_GUID_KEY,
                                  0, REG_SZ, (BYTE*)TextGUID, lstrlen(TextGUID));
                    
                    DWORD value = decoder->codec.unique_framenumber;
                    RegSetValueEx(key, REG_COLORPROCESSING_FRAME_COUNT_KEY,
                                  0, REG_DWORD, (BYTE*)&value, sizeof(value));
                    
                    RegSetValueEx(key, REG_COLORPROCESSING_LAST_TIMECODE_KEY,
                                  0, REG_SZ, (BYTE*)cfhddata->FileTimecodeData.orgtime, lstrlen(cfhddata->FileTimecodeData.orgtime));
                }
            }
            
            
            RegCloseKey(key);
            
        } // RegOpenKey == success
    }
#elif __APPLE_REMOVE__		// Mac version
    {
		CFHDDATA *cfhddata = &decoder->cfhddata;
        FILE *fp;
#if MacUseOverrideFile
        if(fp = fopen("/Library/Application Support/CineForm/LUTs/override", "rb"))
        {
            char txt[8];
            if(fread(txt,1,8,fp))
            {
                int val = atoi(txt);
                process_path_flags_mask = val;
            }
            fclose(fp);
        }
#else
        //
        // CMD 2008.07.24 - change to default to global settings on if System Preferences not set
        //
        // CMD 2009.09.04 - change to set upper bits if the config program has not set them
        
        long				demosaic_type = 0xFF00 | PROCESSING_COLORMATRIX | PROCESSING_WHITEBALANCE | PROCESSING_LOOK_FILE | PROCESSING_GAMMA_TWEAKS;
        CFPropertyListRef	appValue;
        
        CFPreferencesAppSynchronize( CFSTR("com.cineform.codec") );
        appValue = CFPreferencesCopyAppValue( CFSTR("ColorProcessingPath"),  CFSTR("com.cineform.codec") );
        if ( appValue && CFGetTypeID(appValue) == CFNumberGetTypeID()  ) {
            if (!CFNumberGetValue((CFNumberRef)appValue, kCFNumberLongType, &demosaic_type) ) {
                demosaic_type = 0xFF00 | PROCESSING_COLORMATRIX | PROCESSING_WHITEBALANCE | PROCESSING_LOOK_FILE | PROCESSING_GAMMA_TWEAKS;
            } else {
                if( (demosaic_type & PROCESSING_ACTIVE2)==0)
                {
                    demosaic_type |= 0xff00;
                }
            }
        }
        decoder->cfhddata.process_path_flags_mask = demosaic_type;
        if(appValue) CFRelease(appValue);
        
#endif
        //
        //
        //	TODO: Need to review this logic so the 'last' file has ONLY the tags from the clip, and is created ONLY
        //			when the clip is first read.  It looked like that was below where it used to check for clip_guid==0
        //
        //
        
        //
        //  TODO: Do not do this when we are sandboxed
        //
        
        
        
        myGUID lastGUID = cfhddata->clip_guid;
        
        if(lastGUID.Data1 || lastGUID.Data2|| lastGUID.Data3)
        {
            char lastName[256];
            
            sprintf(lastName,"%s/last", decoder->OverridePathStr);
            if((fp = fopen(lastName, "wb")))
            {
                fwrite(&lastGUID,1,sizeof(lastGUID),fp);
                //
                //	Write out default values from Metadata for the clip
                //		WBAL, LOOK, COLM, PRCS, GAMAT (if there)
                unsigned char buffer[1024], *ptr = buffer;
                uint32_t *lptr = (uint32_t *)ptr;
                int len=0;
                
                *lptr++ = TAG_UNIQUE_FRAMENUM;
                *lptr++ = 'L'<<24|4;
                *lptr++ = decoder->codec.unique_framenumber;
                
                *lptr++ = TAG_PROCESS_PATH;
                *lptr++ = 'H'<<24|4;
                *lptr++ = cfhddata->process_path_flags;
                
                *lptr++ = TAG_LOOK_CRC;
                *lptr++ = 'H'<<24|4;
                *lptr++ =cfhddata->user_look_CRC;
                
                *lptr++ = TAG_WHITE_BALANCE;		// 4 floats
                *lptr++ = 'f'<<24|sizeof(cfhddata->user_white_balance);
                memcpy((char *)lptr, cfhddata->user_white_balance, sizeof(cfhddata->user_white_balance));
                lptr+=sizeof(cfhddata->user_white_balance)/4;
                
                *lptr++ = TAG_COLOR_MATRIX;			// 12 floats
                *lptr++ = 'f'<<24|sizeof(cfhddata->custom_colormatrix);
                memcpy((char *)lptr, cfhddata->custom_colormatrix, sizeof(cfhddata->custom_colormatrix)), lptr+=sizeof(cfhddata->custom_colormatrix)/4;
                
                if(cfhddata->process_path_flags & PROCESSING_GAMMA_TWEAKS)
                {
                    *lptr++ = TAG_GAMMA_TWEAKS;		// 3 floats
                    *lptr++ = 'f'<<24|12;
                    memcpy((char *)lptr, &cfhddata->channel[decoder->channel_current].user_rgb_gamma[0], 3*4), lptr+=3;
                }
                len = (ptrdiff_t)lptr - (ptrdiff_t)ptr;
                
                fwrite(buffer,1,len,fp);
                
                fclose(fp);
            }
            
        }
    }
#else //Linux
    //TODO: Add code to set the global overrides for Linux
#endif
}

void InitializeCFHDDataToDefaults(CFHDDATA * cfhddata, uint32_t colorspace)
{
    float unity[4] = {1.0, 1.0, 1.0, 1.0};
    float cm[12] = {1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0};
    int channelNum;

    
    // Set all these fields to default values
    
    cfhddata->update_last_used = 1;
    cfhddata->bayer_format = 0;
    cfhddata->encode_curve = 0;
    cfhddata->encode_curve_preset = 0;
    cfhddata->decode_curve = 0;
    //	cfhddata->process_path_flags = 0; //DAN20090417
    cfhddata->user_look_CRC = 0;
    cfhddata->demosaic_type = 0;
    cfhddata->channel_flip = 0;		// CMD20100303 - not initializing
    cfhddata->calibration = 0;
    cfhddata->FramingFlags = 0;     // CMD20130424 - not initializing
    cfhddata->FrameOffsetX = 0;		//DAN20100903
    cfhddata->FrameOffsetY = 0;		//DAN20100903
	cfhddata->FrameOffsetR = 0;
	cfhddata->FrameOffsetF = 0;
    cfhddata->FrameHScale = 1.0;
    cfhddata->FrameHDynamic = 1.0;
    cfhddata->FrameHDynCenter = 0.5;
    cfhddata->FrameHDynWidth = 0.0;
    cfhddata->split_CC_position = 0.0;
    memcpy(cfhddata->orig_colormatrix,cm,48);
    memcpy(cfhddata->custom_colormatrix,cm,48);
    cfhddata->version = CFHDDATA_VERSION;
    cfhddata->MSChannel_type_value = 0;
    cfhddata->use_base_matrix = 2; //use user matrix
    cfhddata->ComputeFlags = 0;
	cfhddata->lensGoPro = 1;
	cfhddata->lensSphere = 0;
	cfhddata->lensFill = 0;
	cfhddata->doMesh = 0;


    // Set all these fields to default values for each channel
    
    //  float user_contrast;		// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
    //	float user_saturation;		// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
    //	float user_highlight_sat;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 1
    //	float user_highlight_point;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 11
    //	float user_vignette_start;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 1
    //	float user_vignette_end;	// -1.0 to 0.0+, 0.0 unity   real range 0 to 2
    //	float user_vignette_gain;	// 0.0 unity   real range 0 to 8
    //	float user_exposure;		// -1.0 to 7.0+, 0.0 unity   real range 0 to 8
    //	float user_rgb_lift[3];		// -1.0 to 1.0, 0.0 unity black offsets
    //	float user_rgb_gamma[3];		// if 0.0  then no gamma tweaks -- not a camera control used in post.
    //	float user_rgb_gain[3];		// -1.0 to 3.0+, 0.0 unity RGB gains (upon the current matrix)   real range 0 to 4
    //	float white_balance[3];
    //	float user_cdl_sat;			// -1.0 to 3.0+, 0.0 unity   real range 0 to 4
    //	float user_blur_sharpen;	// 0.0 to 1.0, 0.0 unity -- 1.0 sharp
    //
    //	float FrameZoom;
    //	float FrameDiffZoom;
    //	float FrameAutoZoom;
    //	float HorizontalOffset;			// 0.0 centre, -1.0 far left, 1.0 far right
    //	float VerticalOffset;			// 0.0 centre, -1.0 far up, 1.0 far down
    //	float RotationOffset;			// 0.0 centre, -0.1 anti-clockwize, 0.1 clockwize
    //	float FrameKeyStone;
    //	float FloatingWindowMaskL;
    //	float FloatingWindowMaskR;
    //	float FrameTilt;

    memset(&cfhddata->channel[0], 0, sizeof(ChannelData)*3);

    for (channelNum=0; channelNum<3; channelNum++) {
        // Default is 1.0 memcpy(cfhddata->channel[channelNum].user_rgb_lift,unity,12);
        memcpy(cfhddata->channel[channelNum].user_rgb_gamma,unity,12);
        memcpy(cfhddata->channel[channelNum].user_rgb_gain,unity,12);
        memcpy(cfhddata->channel[channelNum].white_balance,unity,12);
        cfhddata->channel[channelNum].FrameZoom = 1.0;
        cfhddata->channel[channelNum].FrameDiffZoom = 1.0;
        cfhddata->channel[channelNum].FrameAutoZoom = 1.0;
    }
        
    cfhddata->cpu_limit = 0;		// if non-zero limit to number of cores used to run.
    cfhddata->cpu_affinity = 0;		// if non-zero set the CPU affinity used to run each thread.
    cfhddata->colorspace = colorspace; //DAN20010916 -- fix for IP frames with the 422to444 filter
    cfhddata->ignore_disk_database = false;             // Not initialized anywhere obvious..
    cfhddata->force_metadata_refresh = true;            // first time through
}

void CopyMetadataChunks(DECODER *decoder, DECODER *parentDecoder)
{
    int i;
    for (i=0;i<decoder->metadatachunks; i++)
    {
#if _ALLOCATOR
        if(decoder->mdc[i])
            Free(decoder->allocator, decoder->mdc[i]);
#else
        if(decoder->mdc[i])
            MEMORY_FREE(decoder->mdc[i]);
#endif
        decoder->mdc_size[i]=0;
    }
    decoder->metadatachunks = 0;
    
    if (parentDecoder) {
        for (i=0; i<parentDecoder->metadatachunks; i++) {
#if _ALLOCATOR
            decoder->mdc[decoder->metadatachunks] = (unsigned char *)Alloc(decoder->allocator, parentDecoder->mdc_size[i]);
#else
            decoder->mdc[decoder->metadatachunks] = (unsigned char *)MEMORY_ALLOC(parentDecoder->mdc_size[i]);
#endif
            if(decoder->mdc[decoder->metadatachunks] && parentDecoder->mdc[i])
                memcpy(decoder->mdc[decoder->metadatachunks], parentDecoder->mdc[i], parentDecoder->mdc_size[i]);
            decoder->mdc_size[decoder->metadatachunks] = parentDecoder->mdc_size[i];
            
            decoder->metadatachunks++;
            
        }

    }
}


bool LoadDiskMetadata(DECODER *decoder, int priority, char *filename)
{
	bool ret = false;
	long int len;			// Must be large enough to hold file pointer from ftell()

    if(decoder->DataBases[priority])
    {
#if _ALLOCATOR
		Free(decoder->allocator, decoder->DataBases[priority]);
#else
		MEMORY_FREE(decoder->DataBases[priority]);
#endif
            
        decoder->DataBases[priority] = NULL;
        decoder->DataBasesSize[priority] = 0;
        decoder->DataBasesAllocSize[priority] = 0;
    }

	{
		unsigned int *size;
		unsigned int *allocSize;
		unsigned char **buffer = NULL;
		//char ext[8] = "";
		
		buffer = &decoder->DataBases[priority]; 
		if(buffer && strlen(filename) && decoder->hasFileDB[priority] <= 1)
		{
			FILE *fp;
			int first = 1;
			int retry = 0;
#ifdef _WIN32
			int openfail = 0;
#endif
			size = &decoder->DataBasesSize[priority];
			allocSize = &decoder->DataBasesAllocSize[priority];
			do
			{
				int err = 0;
				retry = 0;
#ifdef _WIN32
				openfail = 0;
				err = fopen_s(&fp, filename, "rb");
#else
				fp = fopen(filename, "rb");
#endif

				if (err == 0 && fp)
				{
//					if(0==first)
//                      fprintf(stderr,"OK\n");
					fseek (fp, 0, SEEK_END);
					len = ftell(fp);
                    
//                  fprintf(stderr, "Ok open %s len %d\n",filenameGUID,len);

					if(len > (long)*allocSize || *buffer == NULL)
					{
						if(*buffer)
						{		
							#if _ALLOCATOR
							Free(decoder->allocator, *buffer);
							#else
							MEMORY_FREE(*buffer);
							#endif
							*buffer = NULL;
						}

						*allocSize = (len + 511) & ~0xff;
						#if _ALLOCATOR
						*buffer = (unsigned char *)Alloc(decoder->allocator, *allocSize);
						#else
						*buffer = (unsigned char *)MEMORY_ALLOC(*allocSize);
						#endif

						if(*buffer == NULL)
							break;
					}

					if(len && len <= (long)*allocSize)
					{
						fseek (fp, 0, SEEK_SET);
						*size = (unsigned int)fread(*buffer,1,len,fp);
						if(*size != len)
                        {
                            // DAN20090529 -- Test for partially read files.
							retry = 1;
                            fprintf(stderr, "Length short\n");
                        }
						else
						{
							decoder->hasFileDB[priority] = 1;
							ret = true;
						}
					}
					else
					{
						if(len == 0)
							retry = 0;
						if(*size != 0)
							retry = 1;
						*size = 0;
					}


					fclose(fp);
//								break;
				}
				else
				{
                    int  theErr = errno;
                    
                    if (theErr==ENOENT) {
                        // file does not exist so just bail
                        *size = 0;
#ifdef _WIN32
                        openfail = 1;
#endif
                        retry = 0;
                    } else {
//                      fprintf(stderr,"no %d\n",theErr);
                        if(decoder->hasFileDB[priority] == 1)
                        {
#ifdef _WIN32
                            openfail = 1;
#endif
                            if(first)
                            {
//                              fprintf(stderr, "retryf\n");
                                first = 0;
                                retry = 1;
#ifdef _WIN32
                                Sleep(1);
#else
                                usleep(1000);
#endif
                            }
//                          if(*size != 0)
//                          {
//    //                       fprintf(stderr, "retrys\n");
//                             retry = 1;
//                          }
                            *size = 0;
                        }
                        else
                        {
                            *size = 0;
                        }
                    }
				}
			}
			while(retry);
		}
	}
	
	return ret;
}

void OverrideCFHDDATA(DECODER *decoder, unsigned char *lpCurrentBuffer, int nWordsUsed)
{
	CFHDDATA *cfhddata = &decoder->cfhddata;
	uint32_t *last_set_time = &decoder->last_set_time;
	int process_path_flags_mask = decoder->cfhddata.process_path_flags_mask;
	int checkdiskinfo = 0;	
	int checkdiskinfotime = 0;
	decoder->drawmetadataobjects = 0; // fix for metadata display on P frames.

	decoder->codec.PFrame = IsSampleKeyFrame(lpCurrentBuffer, nWordsUsed) ? 0 : (1-decoder->image_dev_only);
	//if(decoder->codec.PFrame && decoder->codec.unique_framenumber != -1 && (decoder->codec.unique_framenumber & 1) == 0)
	if(decoder->codec.PFrame && decoder->codec.unique_framenumber != UINT32_MAX && (decoder->codec.unique_framenumber & 1) == 0)
	{
		decoder->codec.unique_framenumber++;
	}
	
	clock_t process_time = clock();         // defined to return in CLOCKS_PER_SEC units
	time_t now;
	uint32_t diff = (uint32_t)process_time - (uint32_t)*last_set_time;
	now = time(NULL);                       // time is defined in POSIX.1 as seconds since 00:00:00 Jan 1, 1970

#define MS_DIFF	(CLOCKS_PER_SEC / 10)

    // Pre-processing
    //  See if the decoder has been initialized.  If not, initialize it and the cfhddata structures
    //  Read the first chunk of metadata, clear out the cfhddata structure and init from the metadata
    //
	// Read the metadata first to see if it is a new file
	{
		//unsigned char *ptr;
		//int len;

		myGUID lastGUID = cfhddata->clip_guid;

		// Read Clip GUID and other file data before checking the database.
		//if(lastGUID.Data1==0 && lastGUID.Data2==0 && lastGUID.Data3==0)
        size_t metadatasize = 0;
        void *metadatastart;
        bool cfhddataInitialized = false;
        
        //void *data;
        METADATA_TAG tag;
        METADATA_TYPE type;
        METADATA_SIZE size;
        unsigned char *buf = lpCurrentBuffer;
        unsigned int samplesize = nWordsUsed;	

        if(decoder->MDPdefault.initialized == 0)
        {
            decoder->MDPdefault.initialized = 1;
            
            InitializeCFHDDataToDefaults(cfhddata, decoder->frame.colorspace);
            cfhddataInitialized = true;
            
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;
            decoder->preformatted_3D_type = 0;
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;

            decoder->ActiveSafe[0] = 0.0375f/2.0f;
            decoder->ActiveSafe[1] = 0.05f/2.0f;
            decoder->TitleSafe[0] = 0.075f/2.0f;
            decoder->TitleSafe[1] = 0.1f/2.0f;
            decoder->OverlaySafe[0] = 0.075f/2.0f;
            decoder->OverlaySafe[1] = 0.1f/2.0f;

#ifdef _WIN32
			strcpy_s(decoder->MDPdefault.font, sizeof(decoder->MDPdefault.font), "Courier New Bold");
#else
			strcpy(decoder->MDPdefault.font, "Courier New Bold");
#endif
            decoder->MDPdefault.fontsize = 0.04f;

            decoder->MDPdefault.bcolor[0] = 0.0;
            decoder->MDPdefault.bcolor[1] = 0.0;
            decoder->MDPdefault.bcolor[2] = 0.0;
            decoder->MDPdefault.bcolor[3] = 1.0f;

            decoder->MDPdefault.scolor[0] = 0.0;
            decoder->MDPdefault.scolor[1] = 0.0;
            decoder->MDPdefault.scolor[2] = 0.0;
            decoder->MDPdefault.scolor[3] = 1.0f;

            decoder->MDPdefault.fcolor[0] = 1.0f;
            decoder->MDPdefault.fcolor[1] = 1.0f;
            decoder->MDPdefault.fcolor[2] = 1.0f;
            decoder->MDPdefault.fcolor[3] = 1.0f;

            {
                int j;
                for(j=0; j<16; j++)
                {
                    decoder->MDPdefault.xypos[j][0] = -1;
                    decoder->MDPdefault.xypos[j][1] = -1;
                }
            }

            memcpy(&decoder->MDPcurrent, &decoder->MDPdefault, sizeof(MDParams));

            //DAN20100114 -- do these reset once outside the MetaDataFindFirst() test, just in case the file is old and has no metadata.

            //DAN20080710 -- reset the value before loading them, as some RAW streams didn't have all the
            //value, which causes a database reset to fail (switch color database would not switch for
            //Premiere.)

            //decoder->codec.unique_framenumber = -1;
            decoder->codec.unique_framenumber = UINT32_MAX;
        }
		else
			cfhddataInitialized = true;

        if (decoder->image_dev_only || (metadatastart = MetaDataFindFirst(buf, samplesize,
                                             &metadatasize, &tag, &size, &type)))
        {
            int firstmetadatachunk = 1;
            //DAN20080710 -- reset the value before loading them, as some RAW streams didn't have all the
            //value, which causes a database reset to fail (switch color database would not switch for
            //Premiere.)
            if (false==cfhddataInitialized) {
                InitializeCFHDDataToDefaults(cfhddata, decoder->frame.colorspace);
                cfhddata->force_metadata_refresh = false;
            }
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;
            decoder->ghost_bust_left = 0;
            decoder->ghost_bust_right = 0;
            decoder->preformatted_3D_type = 0;
            memset(&decoder->Keyframes, 0, sizeof(decoder->Keyframes));
            //decoder->codec.unique_framenumber = -1;
            decoder->codec.unique_framenumber = UINT32_MAX;
            CopyMetadataChunks(decoder,NULL);

			if(!decoder->image_dev_only)
			{
				do
				{
					buf = (unsigned char *)metadatastart;
					buf -= 8; // Point to the tag not the data

					if(firstmetadatachunk)
					{
						unsigned char *data = buf;
						int size = (int)metadatasize;

						firstmetadatachunk = 0;

						if(size > (int)decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] || decoder->DataBases[METADATA_PRIORITY_FRAME] == NULL)
						{
							if(decoder->DataBases[METADATA_PRIORITY_FRAME])
							{		
								#if _ALLOCATOR
								Free(decoder->allocator, decoder->DataBases[METADATA_PRIORITY_FRAME]);
								#else
								MEMORY_FREE(decoder->DataBases[METADATA_PRIORITY_FRAME]);
								#endif
								decoder->DataBases[METADATA_PRIORITY_FRAME] = NULL;
							}
							decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] = (size + 511) & ~0xff;
							#if _ALLOCATOR
							decoder->DataBases[METADATA_PRIORITY_FRAME]= (unsigned char *)Alloc(decoder->allocator, decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME]);
							#else
							decoder->DataBases[METADATA_PRIORITY_FRAME] = (unsigned char *)MEMORY_ALLOC(decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME]);
							#endif

						}

						if(size && size <= (int) decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] && decoder->DataBases[METADATA_PRIORITY_FRAME])
						{
							memcpy(decoder->DataBases[METADATA_PRIORITY_FRAME], data, size);
							decoder->DataBasesSize[METADATA_PRIORITY_FRAME] = size;
						}
						else
						{
							decoder->DataBasesSize[METADATA_PRIORITY_FRAME] = 0;
						}
					}

	                
					UpdateCFHDDATA(decoder, buf, (int)metadatasize, 0, METADATA_PRIORITY_FRAME);
					buf += metadatasize;
					samplesize -= (unsigned int)metadatasize;
				}
				while ((metadatastart = MetaDataFindFirst(buf, samplesize, &metadatasize, &tag, &size, &type)));
			}

            if(decoder->image_dev_only || memcmp( &lastGUID, &cfhddata->clip_guid, sizeof(cfhddata->clip_guid) ) != 0)
            {
//                    fprintf(stderr, "newclip \n");
                int i;
                
                if(cfhddata->ignore_disk_database == false) {
                    checkdiskinfo = 1;	// CD See if it is a new clip  Need to set if not ignoring disk
                }

                memcpy(&lastGUID, &cfhddata->clip_guid, sizeof(cfhddata->clip_guid));

                //DAN20110114
                //OutputDebugString("GUID change");
                // Clear out databases related to the old GUID.
                for(i=METADATA_PRIORITY_DATABASE; i<METADATA_PRIORITY_OVERRIDE; i++)
                {
                    if(decoder->DataBases[i])
                    {
                        #if _ALLOCATOR
                        Free(decoder->allocator, decoder->DataBases[i]);
                        #else
                        MEMORY_FREE(decoder->DataBases[i]);
                        #endif

                        decoder->DataBases[i] = NULL;
                        decoder->DataBasesSize[i] = 0;
                        decoder->DataBasesAllocSize[i] = 0;
                    }
                }
            }
        }
	}


    // Need to move here since cfhddata may not be initialized yet...
    
	if(diff > MS_DIFF || *last_set_time==0 || now!=decoder->last_time_t) // only test every 1000ms
	{
		if(cfhddata->ignore_disk_database == false)
			checkdiskinfo = 1;
		checkdiskinfotime = 1;
	}

	
    // Do before and after -- before to set the force_disk_database flag, after to setup 3D display modes.
    // overrideData is used by the SDK only.
	if(decoder->overrideData && decoder->overrideSize)
	{
		unsigned char *ptr;
		int len;

		ptr = decoder->overrideData;
		len = decoder->overrideSize;

		UpdateCFHDDATA(decoder, ptr, len, 0, METADATA_PRIORITY_OVERRIDE);
	}

	if(checkdiskinfo || cfhddata->force_disk_database)	// only test every 1000ms - this is common to both conditions so
													// move it out here so the init is only done once
	{
		//InitLUTPaths(decoder);
        
        //  Lets note we just checked because we have a new clip and/or time expired.
        
		*last_set_time = (unsigned int)process_time;
		decoder->last_time_t = now;
		InitLUTPathsDec(decoder);
	}
		
	// CMD: add test for checkdiskinfo 20101012
	if((!(decoder->overrideData && decoder->overrideSize) && (process_path_flags_mask == 0 || process_path_flags_mask == -1))
		|| cfhddata->force_disk_database || checkdiskinfo)
	{
		//unsigned char *ptr;
		//int len;

		//myGUID lastGUID = cfhddata->clip_guid;
		if(checkdiskinfo || cfhddata->force_disk_database || checkdiskinfotime) // only test every 1000ms
		{
			WriteLastGUIDAndFrame(decoder,checkdiskinfotime);
			process_path_flags_mask = cfhddata->process_path_flags_mask; //DAN20130513 Support status viewer mask changes during playback.
		}
	}
	else if(cfhddata->update_last_used && checkdiskinfo) // used by FightLight
	{
#ifdef _WIN32
		HKEY key;
		//OutputDebugString("RegOpenKeyEx");

		if(RegOpenKeyEx(HKEY_CURRENT_USER,
						REG_COLORPROCESSING_PATH,
						0,
						KEY_QUERY_VALUE | KEY_SET_VALUE,
						&key) == ERROR_SUCCESS)

		{
			myGUID lastGUID = cfhddata->clip_guid;
			//if(lastGUID.Data1 || lastGUID.Data2 || lastGUID.Data3)
			{
				char TextGUID[64];
#ifdef _WIN32
				sprintf_s(TextGUID, sizeof(TextGUID), 
#else
				sprintf(TextGUID, 
#endif
						"%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
							lastGUID.Data1,
							lastGUID.Data2,
							lastGUID.Data3,
							lastGUID.Data4[0],
							lastGUID.Data4[1],
							lastGUID.Data4[2],
							lastGUID.Data4[3],
							lastGUID.Data4[4],
							lastGUID.Data4[5],
							lastGUID.Data4[6],
							lastGUID.Data4[7]);
				RegSetValueEx(key, REG_COLORPROCESSING_LAST_GUID_KEY,
					0, REG_SZ, (BYTE*)TextGUID, lstrlen(TextGUID));

				DWORD value = decoder->codec.unique_framenumber;
				RegSetValueEx(key, REG_COLORPROCESSING_FRAME_COUNT_KEY,
					0, REG_DWORD, (BYTE*)&value, sizeof(value));

				RegSetValueEx(key, REG_COLORPROCESSING_LAST_TIMECODE_KEY,
					0, REG_SZ, (BYTE*)cfhddata->FileTimecodeData.orgtime, lstrlen(cfhddata->FileTimecodeData.orgtime));
			}
		}

		RegCloseKey(key);
#endif
	}

	{

		unsigned char *ptr;
		long int len;			// Must be large enough to hold file pointer from ftell()

		// Do before and after -- before to set the force_disk_database flag, after to setup 3D display modes.
		// overrideData is used by the SDK only.
		if(decoder->overrideData && decoder->overrideSize)
		{
			ptr = decoder->overrideData;
			len = decoder->overrideSize;

			UpdateCFHDDATA(decoder, ptr, len, 0, METADATA_PRIORITY_OVERRIDE);
		}

		if(checkdiskinfotime) // Test defaults.colr peroidically
		{
			char filename[MAX_PATH+1] = "";
			//DAN20130529 -- Reading the override.colr at the begining and the end database priority, instead of defaults.colr which as never used.
			//               This fix split and tools not working on the storyboard within Studio.
			/* 
			
			sprintf(filename, "%s/defaults.colr",
								decoder->OverridePathStr);
			if(LoadDiskMetadata(decoder, METADATA_PRIORITY_BASE, filename))
				checkdiskinfo = 1;

			sprintf(filename, "%s/%s/defaults.colr",
					decoder->LUTsPathStr, decoder->UserDBPathStr);
			if(LoadDiskMetadata(decoder, METADATA_PRIORITY_BASE_DBDIR, filename))
				checkdiskinfo = 1;
			*/			
#ifdef _WIN32
			sprintf_s(filename, sizeof(filename), "%s/override.colr", decoder->OverridePathStr);
#else
			sprintf(filename, "%s/override.colr", decoder->OverridePathStr);
#endif
			if(LoadDiskMetadata(decoder, METADATA_PRIORITY_BASE, filename))
				checkdiskinfo = 1;
		}

		if(!(decoder->overrideData && decoder->overrideSize) ||
           cfhddata->force_disk_database ||
           cfhddata->force_metadata_refresh ||
           checkdiskinfo) // DAN20090625 -- so METADATATYPE_ORIGINAL work again //DAN20110114 added checkdiskinfo
		{
			if((checkdiskinfo || cfhddata->force_disk_database || cfhddata->force_metadata_refresh) && cfhddata->ignore_disk_database == false) // only test every 1000ms)
			{
				//InitLUTPaths(decoder);
				//InitLUTPathsDec(decoder);

                cfhddata->force_metadata_refresh = false;
                /*
                // Clear out databases related to the old GUID.
                for(int i=METADATA_PRIORITY_DATABASE; i<METADATA_PRIORITY_OVERRIDE; i++)
                {
                    if(decoder->DataBases[i])
                    {
                        if (METADATA_PRIORITY_BASE==i ||
                            METADATA_PRIORITY_DATABASE==i || METADATA_PRIORITY_DATABASE_1==i || METADATA_PRIORITY_DATABASE_2==i ||
                            METADATA_PRIORITY_OVERRIDE==i || METADATA_PRIORITY_OVERRIDE_1==i || METADATA_PRIORITY_OVERRIDE_2==i) {
#if _ALLOCATOR
                            Free(decoder->allocator, decoder->DataBases[i]);
#else
                            MEMORY_FREE(decoder->DataBases[i]);
#endif
                            
                            decoder->DataBases[i] = NULL;
                            decoder->DataBasesSize[i] = 0;
                            decoder->DataBasesAllocSize[i] = 0;
                        }
                    }
                }*/
                
				myGUID lastGUID = cfhddata->clip_guid;
				for(int type=METADATA_PRIORITY_DATABASE; type<=METADATA_PRIORITY_MAX; type++)
				{
					unsigned char **buffer = NULL;
					char ext[8] = "";
					char filenameGUID[MAX_PATH+1] = "";

					switch(type)
					{
				/*	case METADATA_PRIORITY_BASE: // preset_default an colr file for all clips.
						sprintf(filenameGUID, "%s/%s/defaults.colr",
							decoder->LUTsPathStr, decoder->UserDBPathStr);
						buffer = &decoder->DataBases[type]; 
						break;*/

					case METADATA_PRIORITY_DATABASE: //file database
                    case METADATA_PRIORITY_DATABASE_1: //file database channel 2 (stereo Right - delta)
                    case METADATA_PRIORITY_DATABASE_2: //file database channel 2 (stereo Right - delta)
#ifdef _WIN32
						strcpy_s(ext, sizeof(ext), "colr");
						if (type == METADATA_PRIORITY_DATABASE_1)
							strcpy_s(ext, sizeof(ext), "col1");
						if (type == METADATA_PRIORITY_DATABASE_2)
							strcpy_s(ext, sizeof(ext), "col2");
#else
						strcpy(ext, "colr");
						if (type == METADATA_PRIORITY_DATABASE_1)
							strcpy(ext, "col1");
						if (type == METADATA_PRIORITY_DATABASE_2)
							strcpy(ext, "col2");
#endif

						if(lastGUID.Data1 || lastGUID.Data2 || lastGUID.Data3)
						{
#ifdef _WIN32
							sprintf_s(filenameGUID, sizeof(filenameGUID), 
#else
							sprintf(filenameGUID, 
#endif
										"%s/%s/%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X.%s",
										decoder->LUTsPathStr,
										decoder->UserDBPathStr,
										lastGUID.Data1,
										lastGUID.Data2,
										lastGUID.Data3,
										lastGUID.Data4[0],
										lastGUID.Data4[1],
										lastGUID.Data4[2],
										lastGUID.Data4[3],
										lastGUID.Data4[4],
										lastGUID.Data4[5],
										lastGUID.Data4[6],
										lastGUID.Data4[7],
										ext);
						}
                        buffer = &decoder->DataBases[type];
						break;

					case METADATA_PRIORITY_OVERRIDE: // preset_override an colr file for all clips.
                    case METADATA_PRIORITY_OVERRIDE_1: // preset_override an col1 file for all clips.
                    case METADATA_PRIORITY_OVERRIDE_2: // preset_override an col2 file for all clips.
#ifdef _WIN32
						strcpy_s(ext, sizeof(ext), "colr");
						if(type == METADATA_PRIORITY_OVERRIDE_1)
							strcpy_s(ext, sizeof(ext), "col1");
						if(type == METADATA_PRIORITY_OVERRIDE_2)
							strcpy_s(ext, sizeof(ext), "col2");
#else
						strcpy(ext, "colr");
						if (type == METADATA_PRIORITY_OVERRIDE_1)
							strcpy(ext, "col1");
						if (type == METADATA_PRIORITY_OVERRIDE_2)
							strcpy(ext, "col2");
#endif

                        buffer = &decoder->DataBases[type];

#ifdef _WIN32
						sprintf_s(filenameGUID, sizeof(filenameGUID), "%s/override.%s", decoder->OverridePathStr, ext);
#else
						sprintf(filenameGUID, "%s/override.%s", decoder->OverridePathStr, ext);
#endif
						break;
					}

					
					LoadDiskMetadata(decoder, type, filenameGUID);

	/*				if(buffer && strlen(filenameGUID) && decoder->hasFileDB[type] <= 1)
					{
						FILE *fp;
						int first = 1;
						int retry = 0;
#ifdef _WIN32
						int openfail = 0;
#endif
						size = &decoder->DataBasesSize[type];
						allocSize = &decoder->DataBasesAllocSize[type];
						do
						{
							retry = 0;
#ifdef _WIN32
							openfail = 0;
#endif
							if ((fp = fopen(filenameGUID,"rb")))
							{
//								if(0==first)
//                                    fprintf(stderr,"OK\n");
								fseek (fp, 0, SEEK_END);
								len = ftell(fp);
                                
//                                fprintf(stderr, "Ok open %s len %d\n",filenameGUID,len);

								if(len > *allocSize || *buffer == NULL)
								{
									if(*buffer)
									{		
										#if _ALLOCATOR
										Free(decoder->allocator, *buffer);
										#else
										MEMORY_FREE(*buffer);
										#endif
										*buffer = NULL;
									}

									*allocSize = (len + 511) & ~0xff;
									#if _ALLOCATOR
									*buffer = (unsigned char *)Alloc(decoder->allocator, *allocSize);
									#else
									*buffer = (unsigned char *)MEMORY_ALLOC(*allocSize);
									#endif

									if(*buffer == NULL)
										break;
								}

								if(len && len <= *allocSize)
								{
									fseek (fp, 0, SEEK_SET);
									*size = fread(*buffer,1,len,fp);
									if(*size != len)
                                    {
                                        // DAN20090529 -- Test for partially read files.
										retry = 1;
                                        fprintf(stderr, "Length short\n");
                                    }
									else
										decoder->hasFileDB[type] = 1;
								}
								else
								{
									if(len == 0)
										retry = 0;
									if(*size != 0)
										retry = 1;
									*size = 0;
								}


								fclose(fp);
//								break;
							}
							else
							{
                                int  theErr = errno;
                                
                                if (theErr==ENOENT) {
                                    // file does not exist so just bail
                                    *size = 0;
#ifdef _WIN32
                                    openfail = 1;
#endif
                                    retry = 0;
                                } else {
//                                    fprintf(stderr,"no %d\n",theErr);
                                    if(decoder->hasFileDB[type] == 1)
                                    {
#ifdef _WIN32
                                        openfail = 1;
#endif
                                        if(first)
                                        {
    //                                        fprintf(stderr, "retryf\n");
                                            first = 0;
                                            retry = 1;
    #ifdef _WIN32
                                            Sleep(1);
    #else
                                            usleep(1000);
    #endif
                                        }
//                                        if(*size != 0)
//                                        {
//    //                                        fprintf(stderr, "retrys\n");
//                                            retry = 1;
//                                        }
                                        *size = 0;
                                    }
                                    else
                                    {
                                        *size = 0;
                                    }
                                }
							}
						}
						while(retry);
#if (0 && _WIN32)
						{
							char t[100];
							if(openfail)
								sprintf(t, "%s, open fail", filenameGUID, *size);
							else
								sprintf(t, "%s, size = %d", filenameGUID, *size);
							OutputDebugString(t);
						}
#endif
					}
					*/
				}
			}

			for(int type=0; type<=METADATA_PRIORITY_MAX; type++)
			{
				unsigned char *buffer = NULL;
				int len = 0;
				int delta = 0;

				switch(type)
				{
				case METADATA_PRIORITY_BASE:
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
					
				case METADATA_PRIORITY_FRAME:
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_FRAME_1:
					memcpy(&decoder->cfhddata.channel[1], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 1;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_FRAME_2:
					memcpy(&decoder->cfhddata.channel[2], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 2;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE:
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE_1:
					// Clone right eye from both
					//DAN20101104 memcpy(&decoder->cfhddata.channel[1], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 1;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE_2:
					// Clone right eye from both
					//DAN20101104 memcpy(&decoder->cfhddata.channel[2], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 2;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE:
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE_1:
					delta = 1;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE_2:
					delta = 2;
					buffer = decoder->DataBases[type]; 
					len = decoder->DataBasesSize[type];
					break; 
				}

				if(buffer && len)
					UpdateCFHDDATA(decoder, buffer, len, delta, type);
			}
		}

		// Do before and after -- before to set the force_disk_database flag, after to setup 3D display modes.
		//DAN20090903 -- This was screwing up the Right only view with correct color.
		if(decoder->overrideData && decoder->overrideSize)
		{
			int delta;
			ptr = decoder->overrideData;
			len = decoder->overrideSize;

			UpdateCFHDDATA(decoder, ptr, len, 0, METADATA_PRIORITY_OVERRIDE);
			
			delta = 1;
			ptr = decoder->DataBases[METADATA_PRIORITY_OVERRIDE_1]; 
			len = decoder->DataBasesSize[METADATA_PRIORITY_OVERRIDE_1];
			if(ptr && len)
				UpdateCFHDDATA(decoder, ptr, len, delta, METADATA_PRIORITY_OVERRIDE_1);

			delta = 2;
			ptr = decoder->DataBases[METADATA_PRIORITY_OVERRIDE_2]; 
			len = decoder->DataBasesSize[METADATA_PRIORITY_OVERRIDE_2];
			if(ptr && len)
				UpdateCFHDDATA(decoder, ptr, len, delta, METADATA_PRIORITY_OVERRIDE_2);
		}

		if(process_path_flags_mask > 0)
		{
			cfhddata->process_path_flags_mask = process_path_flags_mask | 1;
		}
	}

	if ((uint32_t)decoder->frame.colorspace != cfhddata->colorspace && cfhddata->colorspace) {
		if(cfhddata->colorspace & COLORSPACE_MASK)
			decoder->frame.colorspace = cfhddata->colorspace;		// colorspace and 422->444
		else
			decoder->frame.colorspace |= (cfhddata->colorspace & ~COLORSPACE_MASK); // 422->444 only
	}

	if (decoder->thread_cntrl.limit == 0 && cfhddata->cpu_limit) {
		decoder->thread_cntrl.limit = cfhddata->cpu_limit;
		decoder->thread_cntrl.set_thread_params = 1;
	}

	if (decoder->thread_cntrl.affinity == 0 && cfhddata->cpu_affinity) {
		decoder->thread_cntrl.affinity = cfhddata->cpu_affinity;
		decoder->thread_cntrl.set_thread_params = 1;
	}

	
		
#if WARPSTUFF
	{
		bool doMesh = false;

		if(cfhddata->lensGoPro == 0 && cfhddata->lensSphere == 1)  
		{
			doMesh = true; // rectilinear
		}	
		if (cfhddata->lensFill == 1 && (cfhddata->FrameOffsetX != 0.0 || cfhddata->FrameOffsetY != 0.0 || cfhddata->FrameOffsetR != 0.0 || cfhddata->FrameOffsetF != 0.0 || decoder->cfhddata.channel[0].FrameZoom < 1.0))
		{
			doMesh = true; // fill background
		}
		if(cfhddata->lensSphere == 1) 
		{
			doMesh = true; //zoom 
		}
		if(cfhddata->lensSphere == 1 && (cfhddata->FrameOffsetX != 0.0 || cfhddata->FrameOffsetY != 0.0))
		{
			doMesh = true; //repoint 
		}
		if((cfhddata->lensSphere == 1 && cfhddata->FrameOffsetR != 0.0) || fabs(cfhddata->FrameOffsetR) > 0.01) 
		{
			doMesh = true; //rotate 
		}
		if(cfhddata->lensGoPro >= 2)  
		{
			doMesh = true; // equi-rect or defish
		}

		if(doMesh)
		{
			decoder->cfhddata.LensZoom = decoder->cfhddata.channel[0].FrameZoom;
			decoder->cfhddata.channel[0].FrameZoom = 1.0;
			decoder->cfhddata.channel[1].FrameZoom = 1.0;
			decoder->cfhddata.channel[2].FrameZoom = 1.0;

			decoder->cfhddata.LensOffsetX = decoder->cfhddata.FrameOffsetX;
			decoder->cfhddata.FrameOffsetX = 0.0;

			decoder->cfhddata.LensOffsetY = decoder->cfhddata.FrameOffsetY;
			decoder->cfhddata.FrameOffsetY = 0.0;

			decoder->cfhddata.LensOffsetR = decoder->cfhddata.FrameOffsetR;
			decoder->cfhddata.FrameOffsetR = 0.0;

			decoder->cfhddata.LensFishFOV = decoder->cfhddata.FrameOffsetF;
			decoder->cfhddata.LensOffsetZ = decoder->cfhddata.FrameHScale;
			decoder->cfhddata.FrameHScale = 1.0;
			decoder->cfhddata.FrameHDynamic = 1.0;
			decoder->cfhddata.channel[0].user_vignette_start = 0.0;

			decoder->cfhddata.LensXmin = decoder->cfhddata.channel[0].FrameMask.topLftX;
			decoder->cfhddata.LensXmax = decoder->cfhddata.channel[0].FrameMask.topRgtX;
			decoder->cfhddata.LensYmin = decoder->cfhddata.channel[0].FrameMask.topLftY;
			decoder->cfhddata.LensYmax = decoder->cfhddata.channel[0].FrameMask.botLftY;

			FRAME_REGION emptyFrameMask = FRAME_REGION_INITIALIZER;
			memcpy(&decoder->cfhddata.channel[0].FrameMask, &emptyFrameMask, 32);
		}
		
		decoder->cfhddata.doMesh = doMesh;
	}
#endif
}

void OverrideCFHDDATAUsingParent(struct decoder *decoder, struct decoder *parentDecoder, unsigned char *lpCurrentBuffer, int nWordsUsed)
{
    // Copy the databases and buffers from the parent instead of doing a read from disk
//    fprintf(stderr, "Usepar\n");
    
    CFHDDATA *cfhddata = &decoder->cfhddata;
	int process_path_flags_mask = decoder->cfhddata.process_path_flags_mask;
    myGUID lastGUID = cfhddata->clip_guid;
    int i;

    decoder->codec.PFrame = IsSampleKeyFrame(lpCurrentBuffer, nWordsUsed) ? 0 : 1;
	if(decoder->codec.PFrame && decoder->codec.unique_framenumber != UINT32_MAX && (decoder->codec.unique_framenumber & 1) == 0)
	{
		decoder->codec.unique_framenumber++;
	}
    // Pre-processing
    //  See if the decoder has been initialized.  If not, initialize it and the cfhddata structures
    //  Read the first chunk of metadata, clear out the cfhddata structure and init from the metadata
    //
	// Read the metadata first to see if it is a new file
	{
		//unsigned char *ptr;
		//int len;
        
        
		// Read Clip GUID and other file data before checking the database.
		//if(lastGUID.Data1==0 && lastGUID.Data2==0 && lastGUID.Data3==0)
        size_t metadatasize = 0;
        void *metadatastart;
        bool cfhddataInitialized = false;
        
        //void *data;
        METADATA_TAG tag;
        METADATA_TYPE type;
        METADATA_SIZE size;
        unsigned char *buf = lpCurrentBuffer;
        unsigned int samplesize = nWordsUsed;
        
        if(decoder->MDPdefault.initialized == 0)
        {
            decoder->MDPdefault.initialized = 1;
            
            InitializeCFHDDataToDefaults(cfhddata, decoder->frame.colorspace);
            cfhddataInitialized = true;
            
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;
            decoder->preformatted_3D_type = 0;
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;
            
            decoder->ActiveSafe[0] = 0.0375f/2.0f;
            decoder->ActiveSafe[1] = 0.05f/2.0f;
            decoder->TitleSafe[0] = 0.075f/2.0f;
            decoder->TitleSafe[1] = 0.1f/2.0f;
            decoder->OverlaySafe[0] = 0.075f/2.0f;
            decoder->OverlaySafe[1] = 0.1f/2.0f;

#ifdef _WIN32
			strcpy_s(decoder->MDPdefault.font, sizeof(decoder->MDPdefault.font), "Courier New Bold");
#else
			strcpy(decoder->MDPdefault.font, "Courier New Bold");
#endif
            decoder->MDPdefault.fontsize = 0.04f;
            
            decoder->MDPdefault.bcolor[0] = 0.0;
            decoder->MDPdefault.bcolor[1] = 0.0;
            decoder->MDPdefault.bcolor[2] = 0.0;
            decoder->MDPdefault.bcolor[3] = 1.0f;
            
            decoder->MDPdefault.scolor[0] = 0.0;
            decoder->MDPdefault.scolor[1] = 0.0;
            decoder->MDPdefault.scolor[2] = 0.0;
            decoder->MDPdefault.scolor[3] = 1.0f;
            
            decoder->MDPdefault.fcolor[0] = 1.0f;
            decoder->MDPdefault.fcolor[1] = 1.0f;
            decoder->MDPdefault.fcolor[2] = 1.0f;
            decoder->MDPdefault.fcolor[3] = 1.0f;
            
            {
                int j;
                for(j=0; j<16; j++)
                {
                    decoder->MDPdefault.xypos[j][0] = -1;
                    decoder->MDPdefault.xypos[j][1] = -1;
                }
            }
            
            memcpy(&decoder->MDPcurrent, &decoder->MDPdefault, sizeof(MDParams));
            
            //DAN20100114 -- do these reset once outside the MetaDataFindFirst() test, just in case the file is old and has no metadata.
            
            //DAN20080710 -- reset the value before loading them, as some RAW streams didn't have all the
            //value, which causes a database reset to fail (switch color database would not switch for
            //Premiere.)
            
            //decoder->codec.unique_framenumber = -1;
            decoder->codec.unique_framenumber = UINT32_MAX;
        }
        
        //  The FRAME metadata cannot be copied from the parent, rebuild it from the sample.
        InitializeCFHDDataToDefaults(cfhddata, decoder->frame.colorspace);

        if ((metadatastart = MetaDataFindFirst(buf, samplesize,
                                               &metadatasize, &tag, &size, &type)))
        {
            int firstmetadatachunk = 1;
            //DAN20080710 -- reset the value before loading them, as some RAW streams didn't have all the
            //value, which causes a database reset to fail (switch color database would not switch for
            //Premiere.)
            if (false==cfhddataInitialized) {
                InitializeCFHDDataToDefaults(cfhddata, decoder->frame.colorspace);
                cfhddata->force_metadata_refresh = false;
            }
            decoder->metadatachunks = 0;
            decoder->drawmetadataobjects = 0;
            decoder->ghost_bust_left = 0;
            decoder->ghost_bust_right = 0;
            decoder->preformatted_3D_type = 0;
            decoder->cdl_sat = 0;
            memset(&decoder->Keyframes, 0, sizeof(decoder->Keyframes));
            //decoder->codec.unique_framenumber = -1;
            decoder->codec.unique_framenumber = UINT32_MAX;
            
            do
            {
                buf = (unsigned char *)metadatastart;
                buf -= 8; // Point to the tag not the data
                
                if(firstmetadatachunk)
                {
                    unsigned char *data = buf;
                    int size = (int)metadatasize;
                    
                    firstmetadatachunk = 0;
                    
                    if(size > (int)decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] || decoder->DataBases[METADATA_PRIORITY_FRAME] == NULL)
                    {
                        if(decoder->DataBases[METADATA_PRIORITY_FRAME])
                        {
#if _ALLOCATOR
                            Free(decoder->allocator, decoder->DataBases[METADATA_PRIORITY_FRAME]);
#else
                            MEMORY_FREE(decoder->DataBases[METADATA_PRIORITY_FRAME]);
#endif
                            decoder->DataBases[METADATA_PRIORITY_FRAME] = NULL;
                        }
                        decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] = (size + 511) & ~0xff;
#if _ALLOCATOR
                        decoder->DataBases[METADATA_PRIORITY_FRAME]= (unsigned char *)Alloc(decoder->allocator, decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME]);
#else
                        decoder->DataBases[METADATA_PRIORITY_FRAME] = (unsigned char *)MEMORY_ALLOC(decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME]);
#endif
                        
                    }
                    
                    if(size && size <= (int)decoder->DataBasesAllocSize[METADATA_PRIORITY_FRAME] && decoder->DataBases[METADATA_PRIORITY_FRAME])
                    {
                        memcpy(decoder->DataBases[METADATA_PRIORITY_FRAME], data, size);
                        decoder->DataBasesSize[METADATA_PRIORITY_FRAME] = size;
                    }
                    else
                    {
                        decoder->DataBasesSize[METADATA_PRIORITY_FRAME] = 0;
                    }
                }
                
                
                UpdateCFHDDATA(decoder, buf, (int)metadatasize, 0, METADATA_PRIORITY_FRAME);
                buf += metadatasize;
                samplesize -= (unsigned int)metadatasize;
            }
            while ((metadatastart = MetaDataFindFirst(buf, samplesize, &metadatasize, &tag, &size, &type)));
            
        }
    }
    if(memcmp( &lastGUID, &cfhddata->clip_guid, sizeof(cfhddata->clip_guid) ) != 0)
    {
        CopyMetadataChunks(decoder,parentDecoder);
        memcpy(&lastGUID, &cfhddata->clip_guid, sizeof(cfhddata->clip_guid));
        
        decoder->Cube_format = 0;
        decoder->Cube_output_colorspace = 0;
        
        //DAN20110114
        //OutputDebugString("GUID change");
        // Clear out databases related to the old GUID.
        for(i=METADATA_PRIORITY_DATABASE; i<METADATA_PRIORITY_OVERRIDE; i++)
        {
            if(decoder->DataBases[i])
            {
                if (METADATA_PRIORITY_BASE==i ||
                    METADATA_PRIORITY_DATABASE==i || METADATA_PRIORITY_DATABASE_1==i || METADATA_PRIORITY_DATABASE_2==i ||
                    METADATA_PRIORITY_OVERRIDE==i || METADATA_PRIORITY_OVERRIDE_1==i || METADATA_PRIORITY_OVERRIDE_2==i) {
#if _ALLOCATOR
                    Free(decoder->allocator, decoder->DataBases[i]);
#else
                    MEMORY_FREE(decoder->DataBases[i]);
#endif
                    
                    decoder->DataBases[i] = NULL;
                    decoder->DataBasesSize[i] = 0;
                    decoder->DataBasesAllocSize[i] = 0;
                }
            }
        }
 
    }
    // Copy the metadata from the parent.  Process the metadata in the correct priority order
    // NOTE: This is done in 2 passes for a good reason.  First clear out all the metadata databases and copy
    //       from the parent then process the data .  It must be done this way since processing the main
    //       colr database (METADATA_PRIORITY_DATABASE) changes the metadata in the METADATA_PRIORITY_DATABASE_1 and
    //       METADATA_PRIORITY_DATABASE_2 databases if single eye adjustments are made.
    
    // TODO: Should see if we can just reference it..
    
    // First clear everything out and copy from parent
    
    {
        for(int type=0; type<=METADATA_PRIORITY_MAX; type++)
        {
            unsigned int *size;
            unsigned int *allocSize;
            long int len;
            unsigned char **buffer = NULL;
            len = parentDecoder->DataBasesSize[type];
            
            size = &decoder->DataBasesSize[type];
            allocSize = &decoder->DataBasesAllocSize[type];
            buffer = &decoder->DataBases[type];
            if (METADATA_PRIORITY_BASE==type ||
                METADATA_PRIORITY_DATABASE==type || METADATA_PRIORITY_DATABASE_1==type || METADATA_PRIORITY_DATABASE_2==type ||
                METADATA_PRIORITY_OVERRIDE==type || METADATA_PRIORITY_OVERRIDE_1==type || METADATA_PRIORITY_OVERRIDE_2==type) {
                // Copy the metadata and process it.
                if (len>(long)*allocSize || *buffer==NULL) {
                    if (*buffer) {
#if _ALLOCATOR
                        Free(decoder->allocator, *buffer);
#else
                        MEMORY_FREE(*buffer);
#endif
                        *buffer = NULL;

                    }
                    *allocSize = parentDecoder->DataBasesAllocSize[type];
#if _ALLOCATOR
                    *buffer = (unsigned char *)Alloc(decoder->allocator, *allocSize);
#else
                    *buffer = (unsigned char *)MEMORY_ALLOC(*allocSize);
#endif
                    if (NULL == *buffer) {
                        *allocSize = 0;
                    }
                }
                
                *size = 0;
                
                if(len && len <= (long)*allocSize  && parentDecoder->hasFileDB[type] <= 1) {
                    *size = len;
                    memcpy(*buffer, parentDecoder->DataBases[type], len);
                }
            }
        }
        // Process the databases in priority order
        
        for(int type=0; type<=METADATA_PRIORITY_MAX; type++)
        {
            long int len=0;
            unsigned char **buffer = NULL;
            int delta = 0;
            switch(type)
            {
				case METADATA_PRIORITY_BASE:
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
					
				case METADATA_PRIORITY_FRAME:
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_FRAME_1:
					memcpy(&decoder->cfhddata.channel[1], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 1;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_FRAME_2:
					memcpy(&decoder->cfhddata.channel[2], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 2;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE:
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE_1:
					// Clone right eye from both
					//DAN20101104 memcpy(&decoder->cfhddata.channel[1], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 1;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_DATABASE_2:
					// Clone right eye from both
					//DAN20101104 memcpy(&decoder->cfhddata.channel[2], &decoder->cfhddata.channel[0], sizeof(ChannelData));
					delta = 2;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE:
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE_1:
					delta = 1;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
				case METADATA_PRIORITY_OVERRIDE_2:
					delta = 2;
					buffer = &decoder->DataBases[type];
					len = decoder->DataBasesSize[type];
					break;
            }
            
            if(buffer && *buffer && len) {
                UpdateCFHDDATA(decoder, *buffer, len, delta, type);
            }
        }
    }
    
    // Setup 3D display modes.
    if(decoder->overrideData && decoder->overrideSize)
    {
        int delta;
		unsigned char *ptr;
		int len;
        ptr = decoder->overrideData;
        len = decoder->overrideSize;
        
        UpdateCFHDDATA(decoder, ptr, len, 0, METADATA_PRIORITY_OVERRIDE);
        
        delta = 1;
        ptr = decoder->DataBases[METADATA_PRIORITY_OVERRIDE_1];
        len = decoder->DataBasesSize[METADATA_PRIORITY_OVERRIDE_1];
        if(ptr && len)
            UpdateCFHDDATA(decoder, ptr, len, delta, METADATA_PRIORITY_OVERRIDE_1);
        
        delta = 2;
        ptr = decoder->DataBases[METADATA_PRIORITY_OVERRIDE_2];
        len = decoder->DataBasesSize[METADATA_PRIORITY_OVERRIDE_2];
        if(ptr && len)
            UpdateCFHDDATA(decoder, ptr, len, delta, METADATA_PRIORITY_OVERRIDE_2);
    }
    
    if(process_path_flags_mask > 0)
    {
        cfhddata->process_path_flags_mask = process_path_flags_mask | 1;
    }
    // Set the colorspace, cpulimit and thread affinity
    if ((uint32_t)decoder->frame.colorspace != cfhddata->colorspace && cfhddata->colorspace) {
		if(cfhddata->colorspace & COLORSPACE_MASK)
			decoder->frame.colorspace = cfhddata->colorspace;		// colorspace and 422->444
		else
			decoder->frame.colorspace |= (cfhddata->colorspace & ~COLORSPACE_MASK); // 422->444 only
	}
    
	if (decoder->thread_cntrl.limit == 0 && cfhddata->cpu_limit) {
		decoder->thread_cntrl.limit = cfhddata->cpu_limit;
		decoder->thread_cntrl.set_thread_params = 1;
	}
    
	if (decoder->thread_cntrl.affinity == 0 && cfhddata->cpu_affinity) {
		decoder->thread_cntrl.affinity = cfhddata->cpu_affinity;
		decoder->thread_cntrl.set_thread_params = 1;
	}
}
