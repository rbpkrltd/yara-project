/*

Copyright(c) 2007. Victor M. Alvarez [plusvic@gmail.com].

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

*/

#include <string.h>
#include <ctype.h>
#include <pcre.h>

#include "filemap.h"
#include "yara.h"
#include "eval.h"
#include "ast.h"
#include "pefile.h"
#include "error.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif


/* Function implementations */


int compare(char* str1, char* str2, int len)
{
	char* s1 = str1;
	char* s2 = str2;
	int i = 0;
	
	while (*s1++ == *s2++ && i < len) i++;
	
	return ((i==len) ? i : 0);
}

int icompare(char* str1, char* str2, int len)
{
	char* s1 = str1;
	char* s2 = str2;
	int i = 0;
	
	while (tolower(*s1++) == tolower(*s2++) && i < len) i++;
	
	return ((i==len) ? i : 0);
}


int wcompare(char* str1, char* str2, int len)
{
	char* s1 = str1;
	char* s2 = str2;
	int i = 0;

	while (*s1 == *s2 && i < len) 
	{
		s1++;
		s2+=2;
		i++;
	}
	
	return ((i==len) ? i * 2 : 0);
}

int wicompare(char* str1, char* str2, int len)
{
	char* s1 = str1;
	char* s2 = str2;
	int i = 0;

	while (tolower(*s1) == tolower(*s2) && i < len) 
	{
		s1++;
		s2+=2;
		i++;
	}
	
	return ((i==len) ? i * 2 : 0);
}

 
int hex_match(unsigned char* buffer, unsigned int buffer_size, unsigned char* pattern, int pattern_length, unsigned char* mask)
{
	size_t b,p,m;
	unsigned char i;
	unsigned char distance;
	unsigned char delta;
	int matches;
	int tmp;
	
	b = 0;
	p = 0;
	m = 0;
	
	matches = 0;
	
	while (b < (size_t) buffer_size && p < (size_t) pattern_length)
	{
		if (mask[m] == MASK_EXACT_SKIP)
		{
			m++;
			distance = mask[m++];
			b += distance;
			matches += distance;
		}
		else if (mask[m] == MASK_RANGE_SKIP)
		{
			m++;
			distance = mask[m++];
			delta = mask[m++] - distance;
			b += distance;
			matches += distance;
			
			if (b < buffer_size)
			{		
				for (i = 0; i <= delta; i++)
				{
					tmp = hex_match(buffer + b + i, buffer_size - b - i,  pattern + p, pattern_length - p, mask + m);
					
				    if (tmp > 0) 
						return b + i + tmp;
				}
			}
			
			break;	
		}
		else if ((buffer[b] & mask[m]) == pattern[p])
		{
			b++;
			m++;
			p++;
			matches++;
		}
		else  /* do not match */
		{
			matches = 0;
			break;
		}
	}
	
	if (p < (size_t) pattern_length)  /* did not reach the end of pattern because buffer was too small */
	{
		matches = 0;
	}
	
	return matches;
}

int regexp_match(unsigned char* buffer, unsigned int buffer_size, unsigned char* pattern, int pattern_length, pcre* regexp, int negative_size)
{
	int ovector[3];
	unsigned int len;
	int rc;
	int result;
	char* s;
	
	result = 0;
	len = 0;
	
	while (len < buffer_size)
	{
		if (buffer[len] < 32 || buffer[len] > 126)  /* only printable characters */
		{
			break;
		}
		
		len++;
	}
	
	/* 
		negative_size > 0 indicates that it is safe to access buffer[-1], 
		if a previous printable char exists and pattern begins with ^ the 
		string doesn't match. 
	*/
	
	if (negative_size > 0 && buffer[-1] >= 32 && buffer[-1] <= 126 && pattern[0] == '^')
	{
		return 0;
	}

	rc = pcre_exec(
	  				regexp,               /* the compiled pattern */
	  				NULL,                 /* no extra data - we didn't study the pattern */
	  				(char*) buffer,  	  /* the subject string */
	  				len,       		  	  /* the length of the subject */
	  				0,                    /* start at offset 0 in the subject */
	  				0,                    /* default options */
	  				ovector,              /* output vector for substring information */
	  				3);                   /* number of elements in the output vector */
		
	if (rc >= 0)
	{	
		result = pcre_get_substring(	(char*) buffer, 
										ovector,
		            					1, 	
										0,
		            					(const char**) &s);
		            							
		if (result != PCRE_ERROR_NOMEMORY &&
		    result != PCRE_ERROR_NOSUBSTRING)	
		
		{
			pcre_free_substring(s);
			return result;
		}
	
	}
	
	return 0;
}

int init_hash_table(RULE_LIST* rule_list)
{
	RULE* rule;
	STRING* string;
	STRING_LIST_ENTRY* entry;
	unsigned char x,y;
	int next;
		
	rule = rule_list->head;
	
	while (rule != NULL)
	{
		string = rule->string_list_head;
		
		while (string != NULL)
		{	
			if (string->flags & STRING_FLAGS_REGEXP)
			{	
				/* take into account scaped characters and ^ at beginning of regular expressions */
							
				if (string->string[0] == '\\' || string->string[0] == '^')
				{
					x = string->string[1];
					next = 2;
				}
				else
				{
					x = string->string[0];
					next = 1;
				}
				
				if (string->string[next] == '\\')
				{
					y = string->string[next + 1];
				}
				else
				{
					y = string->string[next];
				}			
			}
			else
			{
				x = string->string[0];
				y = string->string[1];
			}
			
			if (string->flags & STRING_FLAGS_NO_CASE)
			{	
				x = tolower(x);
				y = tolower(y);
			}
		
			entry = (STRING_LIST_ENTRY*) malloc(sizeof(STRING_LIST_ENTRY));
				
			if (entry != NULL)
			{	
				entry->string = string;
				entry->next = rule_list->hash_table[x][y];  /* insert new entry at begining of list */
				rule_list->hash_table[x][y] = entry;
			}
			else
			{
				return ERROR_INSUFICIENT_MEMORY;
			}				
		
			string = string->next;
		}
		
		rule = rule->next;
	}
	
	return ERROR_SUCCESS;
}

void free_hash_table(RULE_LIST* rule_list)
{
	int i,j;
	STRING_LIST_ENTRY* next_entry;
	STRING_LIST_ENTRY* entry;

	for (i = 0; i < 256; i++)
	{
		for (j = 0; j < 256; j++)
		{
			entry = rule_list->hash_table[i][j];
				
			while (entry != NULL)
			{
				next_entry = entry->next;
				free(entry);
				entry = next_entry;
			}
			
			rule_list->hash_table[i][j] = NULL;
		}
	}
}

void clear_marks(RULE_LIST* rule_list)
{
	RULE* rule;
	STRING* string;
	MATCH* match;
	MATCH* next_match;
	
	rule = rule_list->head;
	
	while (rule != NULL)
	{	 
		string = rule->string_list_head;
		
		while (string != NULL)
		{
			string->flags &= ~STRING_FLAGS_FOUND;  /* clear found mark */
			
			match = string->matches;
			
			while (match != NULL)
			{
				next_match = match->next;
				free(match);
				match = next_match;
			}
			
			string->matches = NULL;
			string = string->next;
		}
		
		rule = rule->next;
	}
}

int string_match(unsigned char* buffer, unsigned int buffer_size, STRING* string, int negative_size)
{
	int match;
	int i, len;
	int is_wide_char;
	
	unsigned char* tmp;
	
	if (IS_HEX(string))
	{
		return hex_match(buffer, buffer_size, string->string, string->length, string->mask);
	}
	else if (IS_REGEXP(string)) 
	{
		if (IS_WIDE(string))
		{
			i = 0;
			
			while(i < buffer_size - 1 && isalnum(buffer[i]) && buffer[i + 1] == 0)
			{
				i += 2;
			}
			
			if (negative_size > 2 && buffer[-1] == 0 && isalnum(buffer[-2]))
			{
				len = i/2 + 1;
				tmp = malloc(len);
				i = -1;
			}
			else
			{
				len = i/2;
				tmp = malloc(len);
				i = 0;
			}
			
			if (tmp != NULL)
			{						
				while(i < len)
				{
					tmp[i] = buffer[i*2];
					i++;
				}
								
				match = regexp_match(tmp, len, string->string, string->length, string->regexp, (negative_size > 2) ? 1 : 0);
			
				free(tmp);			
				return match;
			}
			
		}
		else
		{
			return regexp_match(buffer, buffer_size, string->string, string->length, string->regexp, negative_size);
		}
	}
	else if (IS_WIDE(string) && string->length * 2 <= buffer_size)
	{	
		if(IS_NO_CASE(string))
		{
			match = wicompare((char*) string->string, (char*) buffer, string->length);			
		}
		else
		{
			match = wcompare((char*) string->string, (char*) buffer, string->length);		
		}
		
		if (match > 0 && IS_FULL_WORD(string))
		{
			if (negative_size >= 2)
			{
				is_wide_char = (buffer[-1] == 0 && isalnum((char) (buffer[-2])));
				
				if (is_wide_char)
				{
					match = 0;
				}
			}
			
			if (string->length * 2 < buffer_size - 1)
			{
				is_wide_char = (isalnum((char) (buffer[string->length * 2])) && buffer[string->length * 2 + 1] == 0);
				
				if (is_wide_char)
				{
					match = 0;
				}
			}
		}
		
		return match;		
	}
	else if (string->length <= buffer_size)
	{		
		if(IS_NO_CASE(string))
		{
			match = icompare((char*) string->string, (char*) buffer, string->length);			
		}
		else
		{
			match = compare((char*) string->string, (char*) buffer, string->length);		
		}
		
		if (match > 0 && IS_FULL_WORD(string))
		{
			if (negative_size >= 1 && isalnum((char) (buffer[-1])))
			{
				match = 0;
			}
			else if (string->length < buffer_size && isalnum((char) (buffer[string->length])))
			{
				match = 0;
			}
		}
		
		return match;
	}
	
	return 0;
}

int find_matches(	unsigned char first_char, 
					unsigned char second_char, 
					unsigned char* buffer, 
					unsigned int buffer_size, 
					unsigned int current_file_offset,
					int wide,
					int negative_size, 
					RULE_LIST* rule_list)
{
	unsigned char first_char_lower;
	unsigned char second_char_lower;
	
	int len;
	
	STRING* string;
	MATCH* match;
	STRING_LIST_ENTRY* entry;
	
	entry = rule_list->hash_table[first_char][second_char];

	while (entry != NULL)
	{	
		string = entry->string;

		if ((!wide || IS_WIDE(string)) && (len = string_match(buffer, buffer_size, string, negative_size)))
		{
			string->flags |= STRING_FLAGS_FOUND;
			match = (MATCH*) malloc(sizeof(MATCH));

			if (match != NULL)
			{
				match->offset = current_file_offset;
				match->length = len;
				match->next = string->matches;
				string->matches = match;
			}
			else
			{
				return ERROR_INSUFICIENT_MEMORY;
			}
		}
		
		entry = entry->next;
	}	
	
	/* case insensitive */
	
	first_char_lower = tolower(first_char);
	second_char_lower = tolower(second_char);
	
	if (first_char_lower != first_char || second_char_lower != second_char)
	{
		entry = rule_list->hash_table[first_char_lower][second_char_lower];

		while (entry != NULL)
		{
			string = entry->string;
		
			if ((!wide || IS_WIDE(string)) &&
			    (string->flags & STRING_FLAGS_NO_CASE) &&
			    (len = string_match(buffer, buffer_size, string, negative_size)))
			{
				string->flags |= STRING_FLAGS_FOUND;
				match = (MATCH*) malloc(sizeof(MATCH));

				if (match != NULL)
				{
					match->offset = current_file_offset;
					match->length = len;
					match->next = string->matches;
					string->matches = match;
				}
				else
				{
					return ERROR_INSUFICIENT_MEMORY;
				}
			}
		
			entry = entry->next;
		}
	}
	
	return ERROR_SUCCESS;
}

int scan_mem(unsigned char* buffer, unsigned int buffer_size, RULE_LIST* rule_list, YARACALLBACK callback, void* user_data)
{
    int error;
	unsigned int i;	
	int file_is_pe;
	
	RULE* rule;
	EVALUATION_CONTEXT context;
	
	context.file_size = buffer_size;
	
	file_is_pe = is_pe(buffer, buffer_size);
	
	if (file_is_pe)
	{
		context.entry_point = get_entry_point_offset(buffer, buffer_size);
	}
	
	clear_marks(rule_list);
	
	for (i = 0; i < buffer_size - 1; i++)
	{		    
		/* search for normal strings */	
        error = find_matches(buffer[i], buffer[i + 1], buffer + i, buffer_size - i, i, FALSE, i, rule_list);
		
		if (error != ERROR_SUCCESS)
		    return error;
		
		/* search for wide strings */
		if (i < buffer_size - 3 && buffer[i + 1] == 0 && buffer[i + 3] == 0)
		{
			error = find_matches(buffer[i], buffer[i + 2], buffer + i, buffer_size - i, i, TRUE, i, rule_list);
			
			if (error != ERROR_SUCCESS)
    		    return error;
		}	
	}

	rule = rule_list->head;
	
	while (rule != NULL)
	{
		/* skip privates rules, or rules expecting PE files if the file is not a PE */
		
		if ((rule->flags & RULE_FLAGS_PRIVATE) || 
			((rule->flags & RULE_FLAGS_REQUIRE_PE_FILE) && !file_is_pe))  
		{
			rule = rule->next;
			continue;
		}
	 
		if (evaluate(rule->condition, &context))
		{
			if (callback(rule, buffer, buffer_size, user_data) != 0)
			{
                return ERROR_CALLBACK_ERROR;
			}
		}
		
		rule = rule->next;
	}
	
	return ERROR_SUCCESS;
}

int scan_file(const char* file_path, RULE_LIST* rule_list, YARACALLBACK callback, void* user_data)
{
	MAPPED_FILE mfile;
	int result = ERROR_SUCCESS;
	
	if (map_file(file_path, &mfile))
	{
		result = scan_mem(mfile.data, (unsigned int) mfile.size, rule_list, callback, user_data);		
		unmap_file(&mfile);
	}
	else
	{
		result = ERROR_COULD_NOT_OPEN_FILE;
	}
		
	return result;
}


