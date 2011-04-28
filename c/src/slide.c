#include "slide.h"

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

static int file_exists(const char* file) {
	struct stat buf;
	return stat(file, &buf) == 0;
}

static int is_dir(const char* path) {
	struct stat st;

	if(stat(path, &st) == 0) {
		return S_ISDIR(st.st_mode);
	} else {
		return 0;
	}
}

static int caseless_char(const char character) {
	if( ( character >= 'A' ) && ( character <= 'Z' ) )
		return character + 'a' - 'A';
	
	return character;
}

static int caseless_compare(const char *str1, const char *str2) {
	const char *cur1 = str1, *cur2 = str2;
	char char1, char2;
	
	while( *cur1 && *cur2 )
	{
		// caseless machen
		char1 = caseless_char( cur1[0] );
		char2 = caseless_char( cur2[0] );;
		
		// vergleich
		if( char1 > char2 )
			return 1;
		if( char1 < char2 )
			return -1;
		
		// go on
		cur1++;
		cur2++;
	}
	
	// gleich lang
	if( *cur1 == *cur2 )
		return 0;
	
	// wer ist laenger?
	return ( *cur2 ) ? 1 : -1;
}

static inline int has_extension(struct dirent* entry, const char* extension) {
	size_t entry_len = strlen(entry->d_name);
	size_t ext_len = strlen(extension);

	if(entry_len > ext_len) {
		const char* end = entry->d_name + entry_len - ext_len;
		return !caseless_compare(end, extension);
	} else {
		return 0;
	}
}

static int parse_dir(const char* path, slideshow* slides) {
	DIR* dir = opendir(path);
	struct dirent* entry;
	char abs_path[2048];
	int files = 0;

	while((entry = readdir(dir)) != NULL) {
		if(entry->d_name[0] != '.') {
			// paste the absolute path together
			snprintf(abs_path, sizeof(abs_path), "%s/%s", path, entry->d_name);
			abs_path[sizeof(abs_path)-1] = '\0';

			if(is_dir(abs_path)) {
				files += parse_dir(abs_path, slides);
			} else {
				// check for extensions
				if(has_extension(entry, ".jpg") || has_extension(entry, ".jpeg")) {
					// check for space
					if(slides->allocated <= slides->entries_len) {
						slides->allocated += 32;
						slides->entries = realloc(slides->entries, slides->allocated * sizeof(slides->entries[0]));
					}

					// get copy of name
					char* name = malloc(strlen(abs_path) + 1);
					strcpy(name, abs_path);

					// add to list
					slides->entries[slides->entries_len] = name;
					slides->entries_len++;

					// found one
					files++;
				}
			}
		}
	}

	closedir(dir);

	return files;
}

void init_slideshow(slideshow* slides) {
	slides->entries = malloc(32 * sizeof(slides->entries[0]));
	slides->allocated = 0;
	slides->entries_len = 0;
}

int fill_slideshow(const char* root, slideshow* slides) {
	if(file_exists(root)) {
		return parse_dir(root, slides);
	} else {
		return 0;
	}
}

void clear_slideshow(slideshow* slides) {
	for(int i = 0; i < slides->entries_len; ++i) {
		char* entry = slides->entries[i];
		free(entry);
	}

	slides->allocated = 0;
	slides->entries_len = 0;

	free(slides->entries);
}

