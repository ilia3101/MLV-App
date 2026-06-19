/*! @file scanner.c

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
*/

//TODO: Move these includes to stdafx.h
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>

#if !defined(_WIN32) && !defined(__APPLE__)
// Different routines for case-insensitive string comparisons on Linux
#include <strings.h>
#endif

#include "scanner.h"

typedef struct message
{
	int error;			// Error code
	char *string;		// Error message

} MESSAGE;

static MESSAGE message_table[] = 
{
	{SCANNER_ERROR_OKAY, "Okay"},
	{SCANNER_ERROR_EOF, "End of file"},
	{SCANNER_ERROR_OVERFLOW, "Buffer overflow"},
	{SCANNER_ERROR_QUOTE, "Missing or unexpected quote"},
	{SCANNER_ERROR_KEYWORD, "Unknown keyword"},
};
const int message_table_length = sizeof(message_table) / sizeof(message_table[0]);


// Initialize the scanner
int InitScanner(SCANNER *scanner, FILE *file)
{
	assert(scanner != NULL && file != NULL);

	memset(scanner, 0, sizeof(SCANNER));
	scanner->file = file;
	scanner->error = SCANNER_ERROR_OKAY;
	scanner->line = 0;

	// Read the first character from the file
	scanner->c = getc(file);
	scanner->line++;

	return (scanner->error = ((scanner->c == EOF) ? SCANNER_ERROR_EOF : SCANNER_ERROR_OKAY));
}

// Skip leading whitespace
int SkipBlanks(SCANNER *scanner)
{
	char c;

	assert(scanner != NULL && scanner->file != NULL);

	c = scanner->c;
	while (isspace(c) && c != EOF) {
		c = getc(scanner->file);
		if (c == '\n') {
			// Found the next line
			scanner->line++;
		}
	}
	scanner->c = c;
	return (scanner->error = ((c == EOF) ? SCANNER_ERROR_EOF : SCANNER_ERROR_OKAY));
}

// Skip the rest of the line
int SkipLine(SCANNER *scanner)
{
	char c;

	assert(scanner != NULL && scanner->file != NULL);

	c = scanner->c;

	if (c == '\n')
	{
		// Already found the end of the line
		c = getc(scanner->file);
	}
	else
	{
		// Scan for the end of the line or end of file
		while (c != '\n' && c != EOF) {
			c = getc(scanner->file);
		}

		if (c == '\n') {
			// Found the next line
			scanner->line++;
		}
	}

	scanner->c = c;
	return (scanner->error = ((c == EOF) ? SCANNER_ERROR_EOF : SCANNER_ERROR_OKAY));
}

int ScanKeyword(SCANNER *scanner, char *keyword_string, int keyword_length)
{
	size_t keyword_size = 0;
	size_t remainder_size = 0;
	int keyword_count = 0;
	char c;

	assert(scanner != NULL && scanner->file != NULL);
	assert(keyword_string != NULL && keyword_length > 0);

	// Initialize the keyword buffer
	keyword_size = keyword_length * sizeof(keyword_string[0]);
	memset(keyword_string, 0, keyword_size);

	c = scanner->c;
	while (isalpha(c))
	{
		if (keyword_count == keyword_length) {
			// The keyword is too long
			return (scanner->error = SCANNER_ERROR_OVERFLOW);
		}

		keyword_string[keyword_count++] = c;

		c = getc(scanner->file);
	}

	// Terminate the keyword string
	remainder_size = (keyword_length - keyword_count) * sizeof(keyword_string[0]);
	memset(&keyword_string[keyword_count], 0, remainder_size);

	scanner->c = c;
	return (scanner->error = ((c == EOF) ? SCANNER_ERROR_EOF : SCANNER_ERROR_OKAY));
}

int CopyQuotedString(SCANNER *scanner, char *result_string, int result_length)
{
	int result_count = 0;
	char c = getc(scanner->file);

	assert(scanner != NULL && scanner->file != NULL);
	assert(result_string != NULL && result_length > 0);

	// Copy the argument string into the result
	while (c != EOF && c != '\n' && c != '\"')
	{
		if (result_count == result_length) {
			// The argument string is too long
			return (scanner->error = SCANNER_ERROR_OVERFLOW);
		}

		result_string[result_count++] = c;
		c = getc(scanner->file);
	}

	if (c == '\n') {
		// Found the next line
		scanner->line++;
	}

	scanner->c = c;

	// Last character should be the closing quote for the string
	return (scanner->error = ((c != '\"') ? SCANNER_ERROR_QUOTE : SCANNER_ERROR_OKAY));
}

int CopyTrimmedString(SCANNER *scanner, char *result_string, int result_length)
{
	int result_count = 0;
	int end_count = result_count;

	char c = scanner->c;

	assert(scanner != NULL && scanner->file != NULL);
	assert(result_string != NULL && result_length > 0);

	// Copy the argument string into the result and trim trailing spaces
	while (c != EOF && c != '\n')
	{
		if (result_count == result_length) {
			// The argument string is too long
			return (scanner->error = SCANNER_ERROR_OVERFLOW);
		}

		result_string[result_count++] = c;

		// Remember the location after the last non-space character
		if (!isspace(c)) {
			end_count = result_count;
		}

		c = getc(scanner->file);
	}

	// Trim trailing spaces
	if (end_count < result_count) {
		result_string[end_count] = '\0';
		result_count = end_count;
	}

	// Check for an unncesessary quote at the end of the argument string
	end_count = result_count - 1;
	if (end_count >= 0 && result_string[end_count] == '\"') {
		result_string[end_count] = '\0';
		//scanner->error = SCANNER_ERROR_QUOTE;
	}

	if (c == '\n') {
		// Found the next line
		scanner->line++;
	}

	scanner->c = c;
	return (scanner->error);
}

int Lookup(const char *keyword,
		   TOKEN *token_table,
		   int token_table_length)
{
	int i;

	assert(keyword != NULL && token_table != NULL);

	for (i = 0; i < token_table_length; i++)
	{
#ifdef _MSC_VER
		if (0 == _stricmp(keyword, token_table[i].string)) 
#else
		if (0 == strcasecmp(keyword, token_table[i].string))
#endif
		{
			return token_table[i].value;
		}
	}

	return 0;
}

const char *Keyword(int opcode,
					TOKEN *token_table,
					int token_table_length)
{
	const char *unknown_keyword = "unknown";
	int i;

	assert(token_table != NULL);

	for (i = 0; i < token_table_length; i++)
	{
		if (opcode == token_table[i].value) {
			return token_table[i].string;
		}
	}

	return unknown_keyword;
}

const char *Message(int error)
{
	const char *unknown_message = "Unknown";
	int i;

	for (i = 0; i < message_table_length; i++)
	{
		if (error == message_table[i].error) {
			return message_table[i].string;
		}
	}

	return unknown_message;
}
