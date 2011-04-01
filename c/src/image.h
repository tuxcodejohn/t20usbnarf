#ifndef IMAGE_H
#define IMAGE_H 

#include <wand/MagickWand.h>

typedef MagickWand* image_handle;

int create_image(image_handle* handle);

int read_image(image_handle handle, const char* file_name, char* data);

int finalize_image(image_handle handle);

#endif /* IMAGE_H */
