#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "beamer.h"
#include "image.h"
#include "slide.h"

int main(int argc, char *args[]) {
	slideshow slides;
	beamer_handle beamer;
	image_handle image;
	int files = 0;
	int i;
	char* data = malloc(FRAME_SIZE);

	if(argc < 2) {
		puts("no directory given!");
		return 1;
	}

	if(find_beamer(&beamer)) {
		puts("unable to find beamer");
		return 1;
	}

	init_slideshow(&slides);

	for(i = 1; i < argc; ++i) {
		files += fill_slideshow(args[i], &slides);
	}

	printf("Found %i files\n", files);

	create_image(&image);

	init_beamer(beamer);

	srand(time(NULL));

	while(1) {
		size_t index = rand() % slides.entries_len;
		char* choice = slides.entries[index];

		read_image(image, choice, data);
		send_raw_image(beamer, data);

		sleep(20);
	}

	free(data);

	clear_slideshow(&slides);

	finalize_beamer(beamer);
	finalize_image(image);

	return 0;
}

