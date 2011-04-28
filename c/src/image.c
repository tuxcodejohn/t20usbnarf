#include "image.h"

#include "beamer.h"

int create_image(image_handle* handle) {
	MagickWandGenesis();
	*handle = NewMagickWand();
	return 0;
}

int read_image(image_handle handle, const char* file_name, char* data) {
	size_t scale_width, scale_height;
	size_t cur_width, cur_height;
	MagickBooleanType status;
	PixelWand* pixel = NewPixelWand();

	puts(file_name);

	status = MagickReadImage(handle, file_name);

	if(status == MagickFalse) {
		return -1;
	}

	cur_width = MagickGetImageWidth(handle);
	cur_height = MagickGetImageHeight(handle);

	if(cur_width * FRAME_HEIGHT / cur_height <= FRAME_WIDTH) {
		scale_width = cur_width * FRAME_HEIGHT / cur_height;
		scale_height = FRAME_HEIGHT;
	} else {
		scale_width = FRAME_WIDTH;
		scale_height = cur_height * FRAME_WIDTH / cur_width;
	}

	MagickResetIterator(handle);
	while(MagickNextImage(handle) != MagickFalse) {
		MagickResizeImage(handle, scale_width, scale_height, LanczosFilter, 1.0);
	}

	PixelSetColor(pixel, "black");
	MagickSetImageBackgroundColor(handle, pixel);

	status = MagickExtentImage(handle, FRAME_WIDTH, FRAME_HEIGHT, (FRAME_WIDTH - scale_width) / 2, (FRAME_HEIGHT - scale_height) / 2);

	if(status == MagickFalse) {
		return -1;
	}

	status = MagickExportImagePixels(handle, 0, 0, 640, 480, "BGR", CharPixel, data);

	if(status == MagickFalse) {
		return -1;
	}

	ClearMagickWand(handle);

	return 0;
}

int finalize_image(image_handle handle) {
	DestroyMagickWand(handle);
	MagickWandTerminus();
	return 0;
}

