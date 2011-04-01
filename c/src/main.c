#include <stdio.h>

#include "beamer.h"
#include "image.h"

int main(int argc, char *args[]) {
	if(argc < 2) {
		puts("no image given!");
		return 1;
	}

	beamer_handle beamer;
	image_handle image;

	if(find_beamer(&beamer)) {
		puts("unable to find beamer");
		return 1;
	}

	init_beamer(beamer);

	create_image(&image);

	send_white_image(beamer);

	char* data = malloc(FRAME_SIZE);

	memset(data, 0xff, FRAME_SIZE);
	read_image(image, args[1], data);
	send_raw_image(beamer, data);

	free(data);

	finalize_beamer(beamer);
	finalize_image(image);

	return 0;
}

