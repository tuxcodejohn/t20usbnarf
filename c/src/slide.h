#ifndef SLIDE_H
#define SLIDE_H 

typedef struct slideshow {
	char** entries;
	int entries_len;
	int allocated;
} slideshow;

void init_slideshow(slideshow* slides);

int fill_slideshow(const char* root, slideshow* slides);

void clear_slideshow(slideshow* slides);

#endif /* SLIDE_H */
