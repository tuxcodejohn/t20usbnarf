#include "image.h"

int create_image(image_handle* handle) {
	MagickWandGenesis();
	*handle = NewMagickWand();
	return 0;
}

int read_image(image_handle handle, const char* file_name, char* data) {
	MagickBooleanType status;

	puts(file_name);

	status = MagickReadImage(handle, file_name);

	if(status == MagickFalse) {
		return -1;
	}

	MagickResetIterator(handle);
	while(MagickNextImage(handle) != MagickFalse) {
		MagickResizeImage(handle, 640, 480, LanczosFilter, 1.0);
	}

	status = MagickExportImagePixels(handle, 0, 0, 640, 480, "BGR", CharPixel, data);

	if(status == MagickFalse) {
		return -1;
	}

	ClearMagickWand(handle);
}

int finalize_image(image_handle handle) {
	DestroyMagickWand(handle);
	MagickWandTerminus();
	return 0;
}

