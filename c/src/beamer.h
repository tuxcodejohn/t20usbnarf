#ifndef BEAMER_H
#define BEAMER_H 

#define FRAME_SIZE		(640*480*3)
#define FRAME_WIDTH		640
#define FRAME_HEIGHT		480

#include <libusb.h>

typedef libusb_device_handle* beamer_handle;

int find_beamer(beamer_handle* beamer);

int finalize_beamer(beamer_handle beamer);

int init_beamer(beamer_handle beamer);

int send_raw_image(beamer_handle beamer, char* data);

int send_white_image(beamer_handle beamer);

#endif /* BEAMER_H */
