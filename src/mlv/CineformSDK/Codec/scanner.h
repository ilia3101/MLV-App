/*! @file scanner.h

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

#pragma once

typedef enum error
{
	SCANNER_ERROR_OKAY = 0,
	SCANNER_ERROR_EOF,
	SCANNER_ERROR_OVERFLOW,
	SCANNER_ERROR_QUOTE,
	SCANNER_ERROR_KEYWORD,

} SCANNER_ERROR;

// Scanner state
typedef struct scanner
{
	FILE *file;
	char c;				// Last character read from the file
	int error;			// Error code from the last operation
	int line;			// Line number where the error occurred

} SCANNER;

// Token table entry
typedef struct token
{
	char *string;		// The token keyword
	int value;			// The opcode for the token

} TOKEN;


#ifdef __cplusplus
extern "C" {
#endif

int InitScanner(SCANNER *scanner, FILE *file);

int SkipBlanks(SCANNER *scanner);

int SkipLine(SCANNER *scanner);

int ScanKeyword(SCANNER *scanner, char *keyword, int keyword_length);

int CopyQuotedString(SCANNER *scanner, char *result_string, int result_length);

int CopyTrimmedString(SCANNER *scanner, char *result_string, int result_length);

int Lookup(const char *keyword,
		   TOKEN *token_table,
		   int token_table_length);

const char *Keyword(int opcode,
					TOKEN *token_table,
					int token_table_length);

const char *Message(int error);

#ifdef __cplusplus
}
#endif
